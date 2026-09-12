#include "Render/RayTracing/RayTracingScene.h"
#include "Render/CommandBuffer.h"
#include "Render/GraphicsContext.h"
#include "RHI/DX12/RayTracing/D3DRayTracingScene.h"
#include "pch.h"

namespace Ailu::Render
{
    Ref<RayTracingScene> RayTracingScene::Create()
    {
        switch (RendererAPI::GetAPI())
        {
            case RendererAPI::ERenderAPI::kNone:
                AL_ASSERT_MSG(false, "None render api used!");
                return nullptr;
            case RendererAPI::ERenderAPI::kDirectX12:
                return AdoptGpuResource(new RHI::DX12::D3DRayTracingScene());
        }
        AL_ASSERT_MSG(false, "Unsupported render api!");
        return nullptr;
    }
    RayTracingScene::RayTracingScene()
    {
        _res_type = Render::EGpuResType::kTopAS;
    }

    RayTracingScene::~RayTracingScene()
    {
    }
    NativeHandle RayTracingScene::NativeResource()
    {
        return _native_resource;
    }

    u64 RayTracingScene::AddInstance(const RayTracingInstance &instance)
    {
        _instances.emplace_back(instance);
        _is_need_rebuild = true;
        _is_need_refit = false;
        return _instances.size() - 1u;
    }

    bool RayTracingScene::RemoveInstance(u64 instance_handle)
    {
        if (!HasInstance(instance_handle))
            return false;

        _instances.erase(_instances.begin() + static_cast<ptrdiff_t>(instance_handle));
        _is_need_rebuild = true;
        _is_need_refit = false;
        return true;
    }

    void RayTracingScene::UpdateInstance(u64 instance_handle, const RayTracingInstance &instance)
    {
        AL_ASSERT(HasInstance(instance_handle));
        const RayTracingInstance old_instance = _instances[instance_handle];
        _instances[instance_handle] = instance;

        const bool is_transform_changed = !(old_instance._transform == instance._transform);
        const bool is_instance_desc_changed =
                old_instance._geometry != instance._geometry ||
                old_instance._instance_id != instance._instance_id ||
                old_instance._hit_group_offset != instance._hit_group_offset ||
                old_instance._mask != instance._mask;

        if (!_is_need_rebuild && (is_transform_changed || is_instance_desc_changed))
            _is_need_refit = true;

        if (is_instance_desc_changed)
            OnInstanceChanged(static_cast<u32>(instance_handle));
        else if (is_transform_changed)
            OnInstanceTransformChanged(static_cast<u32>(instance_handle));
    }

    void RayTracingScene::UpdateInstance(u64 instance_handle, const Matrix4x4f &transform)
    {
        AL_ASSERT(HasInstance(instance_handle));
        _instances[instance_handle]._transform = transform;
        if (!_is_need_rebuild)
            _is_need_refit = true;
        OnInstanceTransformChanged(static_cast<u32>(instance_handle));
    }

    const RayTracingInstance *RayTracingScene::GetInstance(u64 instance_handle) const
    {
        if (!HasInstance(instance_handle))
            return nullptr;
        return &_instances[instance_handle];
    }

    bool RayTracingScene::HasInstance(u64 instance_handle) const
    {
        return instance_handle < _instances.size();
    }

    void RayTracingScene::ClearInstances()
    {
        if (_instances.empty())
            return;

        _instances.clear();
        _is_need_rebuild = true;
        _is_need_refit = false;
    }

    void RayTracingScene::Build()
    {
        if (_instances.empty())
        {
            _native_resource = {};
            _is_need_rebuild = false;
            _is_need_refit = false;
            return;
        }

        const bool need_recreate_resource = _is_need_rebuild && NeedRecreateBuildResource();
        if (need_recreate_resource)
        {
            _is_ready_for_rendering = false;
            ApplySync();
        }

        PrepareBuild(_is_need_rebuild);

        auto cmd = CommandBufferPool::Get();
        cmd->BuildAS(this, !_is_need_rebuild);
        if (need_recreate_resource)
            GraphicsContext::Get().ExecuteCommandBufferSync(cmd);
        else
            GraphicsContext::Get().ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
        _is_need_rebuild = false;
        _is_need_refit = false;
    }

    void RayTracingScene::Update()
    {
        if (_is_need_rebuild)
        {
            Build();
            return;
        }
        if (!_is_need_refit)
            return;

        Build();
    }
}
