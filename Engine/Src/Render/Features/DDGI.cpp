#include "Render/Features/DDGI.h"

#include "Framework/Math/Transform.h"
#include "Render/DDGI/DDGIProbeLayout.h"
#include "Render/GraphicsContext.h"
#include "Scene/Component.h"
#include "Scene/Scene.h"

namespace Ailu::Render
{
    namespace
    {
        Matrix4x4f BuildDDGIVolumeTransform(const ECS::TransformComponent &transform)
        {
            const Transform world_transform(transform.GetRenderPosition(), transform.GetRenderRotation(), Vector3f::kOne);
            return Transform::ToMatrix(world_transform);
        }

        bool IsRuntimeCompatible(const DDGIVolumeRenderData &old_data, const DDGIVolumeRenderData &new_data)
        {
            return old_data._probe_count == new_data._probe_count;
        }
    }

    DDGIFeature::DDGIFeature() : RenderFeature("DDGI")
    {
        _is_active = true;
    }

    void DDGIFeature::AddRenderPasses(Renderer &, const RenderingData &rendering_data)
    {
        if (!_is_active || rendering_data._scene == nullptr)
            return;

        const u64 frame = g_pGfxContext != nullptr ? g_pGfxContext->GetFrameCount() : 0u;
        if (_last_scene != rendering_data._scene || _last_sync_frame != frame)
        {
            SyncSceneVolumes(*rendering_data._scene);
            _last_scene = rendering_data._scene;
            _last_sync_frame = frame;
        }
    }

    void DDGIFeature::SyncSceneVolumes(const SceneManagement::Scene &scene)
    {
        _volumes.clear();

        const auto &registry = scene.GetRegister();
        u32 volume_index = 0u;
        for (const auto &volume : registry.View<ECS::DDGIVolumeComponent>())
        {
            const ECS::Entity entity = registry.GetEntity<ECS::DDGIVolumeComponent>(volume_index++);
            if (!scene.IsEntityEnabled(entity) || !registry.IsComponentEnabled<ECS::DDGIVolumeComponent>(entity) ||
                !DDGI::IsValidProbeGrid(volume._probe_count, volume._probe_spacing))
            {
                continue;
            }

            const auto *transform = registry.GetComponent<ECS::TransformComponent>(entity);
            if (transform == nullptr)
                continue;

            DDGIVolumeRenderData data;
            data._entity = entity;
            data._local_to_world = BuildDDGIVolumeTransform(*transform);
            data._probe_count = volume._probe_count;
            data._probe_spacing = volume._probe_spacing;
            _volumes.emplace_back(data);
        }

        SyncVolumeRuntimes();
    }

    void DDGIFeature::SyncVolumeRuntimes()
    {
        for (auto &[entity, runtime] : _volume_runtimes)
        {
            (void) entity;
            runtime._seen_this_frame = false;
        }

        for (const DDGIVolumeRenderData &volume : _volumes)
        {
            auto [it, inserted] = _volume_runtimes.try_emplace(volume._entity);
            DDGIVolumeRuntime &runtime = it->second;
            runtime._seen_this_frame = true;

            if (inserted || !IsRuntimeCompatible(runtime._desc, volume))
                runtime._entity = volume._entity;
            runtime._desc = volume;
        }

        std::erase_if(_volume_runtimes, [](const auto &item)
        {
            return !item.second._seen_this_frame;
        });
    }
}
