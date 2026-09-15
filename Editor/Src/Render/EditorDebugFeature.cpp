#include "Render/EditorDebugFeature.h"

#include "Framework/Common/ResourceMgr.h"
#include "Render/CommandBuffer.h"
#include "Render/DDGI/DDGIProbeLayout.h"
#include "Render/Gizmo.h"
#include "Render/GraphicsContext.h"
#include "Render/Material.h"
#include "Render/Mesh.h"
#include "Render/RenderGraph/RenderGraph.h"
#include "Render/Renderer.h"
#include "Render/StaticDiffuseProbeData.h"
#include "Render/Texture.h"
#include "Common/Selection.h"
#include "Scene/Component.h"
#include "Scene/RenderSystem.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <cmath>

namespace Ailu::Render
{
    using Math::Vector3UInt;

    namespace
    {
        constexpr char kProbeCubemapKeyword[] = "DEBUG_CUBEMAP";
        constexpr char kDDGIProbePreviewKeyword[] = "DDGI_PROBE_PREVIEW";
        constexpr u64 kMaxPreviewProbeCount = 65536u;

        Matrix4x4f BuildProbePreviewMatrix(const Mesh *mesh, const Vector3f &position, f32 radius)
        {
            const Vector3f diameter(radius * 2.0f);
            if (mesh == nullptr || mesh->BoundBox().empty())
                return MatrixScale(diameter) * MatrixTranslation(position);

            const AABB bounds = mesh->GetBoundBox();
            const Vector3f size = bounds.Size();
            if (size.x <= 0.0f || size.y <= 0.0f || size.z <= 0.0f)
                return MatrixScale(diameter) * MatrixTranslation(position);

            const Vector3f scale(diameter.x / size.x, diameter.y / size.y, diameter.z / size.z);
            const Vector3f center = bounds.Center();
            const Vector3f offset(-center.x * scale.x, -center.y * scale.y, -center.z * scale.z);
            return MatrixScale(scale) * MatrixTranslation(offset) * MatrixTranslation(position);
        }

        void SetProbePreviewSH(Material *material, const SphericalHarmonicsL2 *sh)
        {
            if (material == nullptr)
                return;
            for (u32 coefficient_index = 0u; coefficient_index < SphericalHarmonicsL2::kCoefficientCount;
                 ++coefficient_index)
            {
                const Vector4f coefficient = sh == nullptr ? Vector4f(0.0f) : sh->_coefficients[coefficient_index];
                material->SetVector(std::format("_SH{}", coefficient_index), coefficient);
            }
        }

        bool HasProbePreviewSH(const SphericalHarmonicsL2 &sh)
        {
            for (const Vector4f &coefficient : sh._coefficients)
            {
                if (coefficient.x != 0.0f || coefficient.y != 0.0f || coefficient.z != 0.0f || coefficient.w != 0.0f)
                    return true;
            }
            return false;
        }

        Matrix4x4f BuildDDGIVolumeTransform(const ECS::TransformComponent &transform)
        {
            const Quaternion rotation = transform.GetRenderRotation();
            const f32 x2 = rotation.x + rotation.x;
            const f32 y2 = rotation.y + rotation.y;
            const f32 z2 = rotation.z + rotation.z;
            const f32 xx2 = rotation.x * x2;
            const f32 yy2 = rotation.y * y2;
            const f32 zz2 = rotation.z * z2;
            const f32 yz2 = rotation.y * z2;
            const f32 wx2 = rotation.w * x2;
            const f32 xy2 = rotation.x * y2;
            const f32 wz2 = rotation.w * z2;
            const f32 xz2 = rotation.x * z2;
            const f32 wy2 = rotation.w * y2;

            Matrix4x4f matrix{};
            matrix[0][0] = 1.0f - (yy2 + zz2);
            matrix[1][1] = 1.0f - (xx2 + zz2);
            matrix[2][2] = 1.0f - (xx2 + yy2);
            matrix[2][1] = yz2 - wx2;
            matrix[1][2] = yz2 + wx2;
            matrix[1][0] = xy2 - wz2;
            matrix[0][1] = xy2 + wz2;
            matrix[2][0] = xz2 + wy2;
            matrix[0][2] = xz2 - wy2;
            matrix[3][0] = transform.GetRenderPosition().x;
            matrix[3][1] = transform.GetRenderPosition().y;
            matrix[3][2] = transform.GetRenderPosition().z;
            matrix[3][3] = 1.0f;
            return matrix;
        }

        OBB BuildDDGIVolumeBounds(const Matrix4x4f &local_to_world, const Vector3f &volume_size)
        {
            OBB bounds;
            bounds._center = TransformCoord(local_to_world, Vector3f::kZero);
            bounds._half_axis_length = volume_size * 0.5f;
            bounds._local_axis[0] = Vector3f(local_to_world[0][0], local_to_world[0][1], local_to_world[0][2]);
            bounds._local_axis[1] = Vector3f(local_to_world[1][0], local_to_world[1][1], local_to_world[1][2]);
            bounds._local_axis[2] = Vector3f(local_to_world[2][0], local_to_world[2][1], local_to_world[2][2]);
            return bounds;
        }
    }

    EditorDebugPass::EditorDebugPass() : RenderPass("EditorDebugPass")
    {
        _event = ERenderPassEvent::kAfterPostprocess;
        _light_probe_preview_shader = ResourceMgr::Get().LoadAssetRef<Shader>(
            L"Shaders/hlsl/light_probe_preview.alasset");
    }

    void EditorDebugPass::DrawLightProbeGroups(const RenderingData &rendering_data)
    {
        if (rendering_data._scene == nullptr)
            return;

        auto *scene = SceneManagement::SceneMgr::Get().ActiveScene();
        if (scene == nullptr || scene != rendering_data._scene)
            return;

        auto &registry = scene->GetRegister();
        auto *lighting_system = registry.GetSystem<ECS::LightingSystem>();
        const auto &selected_entities = Ailu::Editor::Selection::SelectedEntities();
        u32 group_index = 0u;
        u32 probe_preview_material_index = 0u;
        u32 cubemap_preview_material_index = 0u;
        for (const auto &group : registry.View<ECS::CLightProbeGroup>())
        {
            const ECS::Entity entity = registry.GetEntity<ECS::CLightProbeGroup>(group_index++);
            if (!scene->IsEntityEnabled(entity) || !registry.IsComponentEnabled<ECS::CLightProbeGroup>(entity) ||
                !group._draw_probes ||
                std::find(selected_entities.begin(), selected_entities.end(), entity) == selected_entities.end())
                continue;

            const auto *transform = registry.GetComponent<ECS::TransformComponent>(entity);
            if (transform == nullptr)
                continue;

            const Vector3f center = transform->GetRenderPosition();
            Gizmo::DrawAABB(center - group._size * 0.5f, center + group._size * 0.5f, Colors::kCyan);

            Ref<StaticDiffuseProbeData> probe_data;
            if (!group._preview_cubemap && !group._baked_data.IsEmpty())
            {
                ResourceMgr::Get().Load<StaticDiffuseProbeData>(group._baked_data);
                probe_data = ResourceMgr::Get().GetRef<StaticDiffuseProbeData>(group._baked_data);
            }

            Mesh *sphere = Mesh::s_sphere.lock().get();
            if (sphere == nullptr)
                continue;

            if (_light_probe_preview_shader == nullptr)
                _light_probe_preview_shader = ResourceMgr::Get().LoadAssetRef<Shader>(
                    L"Shaders/hlsl/light_probe_preview.alasset");
            if (_light_probe_preview_shader == nullptr)
                continue;
            if (_light_probe_preview_empty_material == nullptr && _light_probe_preview_shader != nullptr)
            {
                _light_probe_preview_empty_material = MakeRef<Material>(
                    _light_probe_preview_shader.get(), "light_probe_preview_empty");
                _light_probe_preview_empty_material->DisableKeyword(kProbeCubemapKeyword);
                SetProbePreviewSH(_light_probe_preview_empty_material.get(), nullptr);
            }

            const Vector3f spacing(
                group._size.x / static_cast<f32>(std::max(1, group._probe_count.x - 1)),
                group._size.y / static_cast<f32>(std::max(1, group._probe_count.y - 1)),
                group._size.z / static_cast<f32>(std::max(1, group._probe_count.z - 1)));
            for (i32 z = 0; z < group._probe_count.z; ++z)
                for (i32 y = 0; y < group._probe_count.y; ++y)
                    for (i32 x = 0; x < group._probe_count.x; ++x)
                    {
                        const Vector3f probe = center - group._size * 0.5f +
                                               Vector3f(x * spacing.x, y * spacing.y, z * spacing.z);
                        const u32 probe_index = static_cast<u32>(
                            (z * group._probe_count.y + y) * group._probe_count.x + x);
                        Material *material = nullptr;
                        if (group._preview_cubemap)
                        {
                            RenderTexture *cubemap = lighting_system != nullptr
                                ? lighting_system->GetStaticProbePreviewCubemap(entity, probe_index) : nullptr;
                            if (cubemap == nullptr)
                            {
                                if (_light_probe_preview_empty_material == nullptr)
                                    continue;
                                material = _light_probe_preview_empty_material.get();
                            }
                            else
                            {
                                if (cubemap_preview_material_index >= _light_probe_cubemap_preview_materials.size())
                                {
                                    _light_probe_cubemap_preview_materials.emplace_back(MakeRef<Material>(
                                        _light_probe_preview_shader.get(),
                                        std::format("light_probe_cubemap_preview_{}", cubemap_preview_material_index)));
                                    _light_probe_cubemap_preview_materials.back()->EnableKeyword(kProbeCubemapKeyword);
                                }
                                material = _light_probe_cubemap_preview_materials[
                                    cubemap_preview_material_index++].get();
                                material->SetTexture("_ProbeCubemap", cubemap);
                            }
                        }
                        else
                        {
                            const SphericalHarmonicsL2 *sh = nullptr;
                            if (probe_data != nullptr && probe_index < probe_data->_probes.size())
                                sh = &probe_data->_probes[probe_index];
                            material = _light_probe_preview_empty_material.get();
                            if (sh != nullptr && HasProbePreviewSH(*sh))
                            {
                                if (probe_preview_material_index >= _light_probe_preview_materials.size())
                                {
                                    _light_probe_preview_materials.emplace_back(MakeRef<Material>(
                                        _light_probe_preview_shader.get(),
                                        std::format("light_probe_preview_{}", probe_preview_material_index)));
                                    _light_probe_preview_materials.back()->DisableKeyword(kProbeCubemapKeyword);
                                }
                                material = _light_probe_preview_materials[probe_preview_material_index++].get();
                                SetProbePreviewSH(material, sh);
                            }
                        }
                        Gizmo::DrawMesh(sphere, BuildProbePreviewMatrix(sphere, probe, 0.04f), material);
                    }
        }
    }

    void EditorDebugPass::DrawDDGIVolumes(const RenderingData &rendering_data)
    {
        if (rendering_data._scene == nullptr)
            return;

        auto *scene = SceneManagement::SceneMgr::Get().ActiveScene();
        if (scene == nullptr || scene != rendering_data._scene)
            return;

        auto &registry = scene->GetRegister();
        const auto &selected_entities = Ailu::Editor::Selection::SelectedEntities();
        u32 volume_index = 0u;
        for (const auto &volume : registry.View<ECS::DDGIVolumeComponent>())
        {
            const ECS::Entity entity = registry.GetEntity<ECS::DDGIVolumeComponent>(volume_index++);
            if (!scene->IsEntityEnabled(entity) || !registry.IsComponentEnabled<ECS::DDGIVolumeComponent>(entity))
                continue;

            const auto *transform = registry.GetComponent<ECS::TransformComponent>(entity);
            if (transform == nullptr || !DDGI::IsValidProbeGrid(volume._probe_count, volume._probe_spacing))
                continue;

            const Matrix4x4f local_to_world = BuildDDGIVolumeTransform(*transform);
            const bool is_selected = std::find(selected_entities.begin(), selected_entities.end(), entity) !=
                                     selected_entities.end();
            if (!is_selected)
                continue;

            const Vector3f volume_size = DDGI::GridSize(volume._probe_count, volume._probe_spacing) + volume._probe_spacing;
            Gizmo::DrawOBB(BuildDDGIVolumeBounds(local_to_world, volume_size), Colors::kYellow);

            if (!volume._draw_probes || DDGI::ProbeCount(volume._probe_count) > kMaxPreviewProbeCount)
                continue;

            Mesh *sphere = Mesh::s_sphere.lock().get();
            if (sphere == nullptr)
                continue;
            if (_light_probe_preview_shader == nullptr)
                _light_probe_preview_shader = ResourceMgr::Get().LoadAssetRef<Shader>(
                    L"Shaders/hlsl/light_probe_preview.alasset");
            if (_light_probe_preview_shader == nullptr)
                continue;
            if (_ddgi_probe_preview_material == nullptr)
            {
                _ddgi_probe_preview_material = MakeRef<Material>(_light_probe_preview_shader.get(), "ddgi_probe_preview");
                _ddgi_probe_preview_material->DisableKeyword(kProbeCubemapKeyword);
                _ddgi_probe_preview_material->EnableKeyword(kDDGIProbePreviewKeyword);
            }

            const f32 min_spacing = std::min(volume._probe_spacing.x,
                                             std::min(volume._probe_spacing.y, volume._probe_spacing.z));
            const f32 probe_radius = std::min(min_spacing * 0.08f, 0.25f);
            Vector<Matrix4x4f> probe_matrices;
            probe_matrices.reserve(static_cast<size_t>(DDGI::ProbeCount(volume._probe_count)));
            for (u32 z = 0u; z < volume._probe_count.z; ++z)
                for (u32 y = 0u; y < volume._probe_count.y; ++y)
                    for (u32 x = 0u; x < volume._probe_count.x; ++x)
                    {
                        const Vector3UInt coord(x, y, z);
                        const Vector3f local_position = DDGI::ProbeLocalPosition(
                            coord, volume._probe_count, volume._probe_spacing);
                        const Vector3f world_position = TransformCoord(local_to_world, local_position);
                        probe_matrices.emplace_back(BuildProbePreviewMatrix(sphere, world_position, probe_radius));
                    }
            Gizmo::DrawMeshInstanced(sphere, probe_matrices, _ddgi_probe_preview_material.get());
        }
    }

    void EditorDebugPass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
    {
        graph.AddPass(_name, RDG::PassDesc(), [&](RDG::RenderGraphBuilder &builder)
        {
            builder.Read(rendering_data._rg_handles._color_target, EResourceUsage::kWriteRTV);
            builder.Read(rendering_data._rg_handles._depth_target, EResourceUsage::kDSV);
            rendering_data._rg_handles._color_target = builder.Write(rendering_data._rg_handles._color_target);
            rendering_data._rg_handles._depth_target = builder.Write(rendering_data._rg_handles._depth_target,
                                                                       EResourceUsage::kDSV);
        }, [this](RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &pass_rendering_data)
        {
            cmd->SetRenderTarget(pass_rendering_data._rg_handles._color_target,
                                 pass_rendering_data._rg_handles._depth_target);
            cmd->SetViewport(pass_rendering_data._viewport);
            cmd->SetScissorRect(pass_rendering_data._scissor_rect);
            DrawLightProbeGroups(pass_rendering_data);
            DrawDDGIVolumes(pass_rendering_data);
            Gizmo::Submit(cmd, &pass_rendering_data);
        });
    }

    void EditorDebugPass::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        auto cmd = CommandBufferPool::Get("EditorDebugPass");
        cmd->Clear();
        cmd->SetRenderTarget(rendering_data._camera_color_target_handle, rendering_data._camera_depth_target_handle);
        cmd->SetViewport(rendering_data._viewport);
        cmd->SetScissorRect(rendering_data._scissor_rect);
        DrawLightProbeGroups(rendering_data);
        DrawDDGIVolumes(rendering_data);
        Gizmo::Submit(cmd.get(), &rendering_data);
        context->ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
    }

    EditorDebugFeature::EditorDebugFeature() : RenderFeature("EditorDebug")
    {
        _is_active = true;
    }

    void EditorDebugFeature::AddRenderPasses(Renderer &renderer, const RenderingData &rendering_data)
    {
        if (!_is_active || rendering_data._camera == nullptr || !rendering_data._camera->_is_scene_camera)
            return;
        renderer.EnqueuePass(&_debug_pass);
    }
}
