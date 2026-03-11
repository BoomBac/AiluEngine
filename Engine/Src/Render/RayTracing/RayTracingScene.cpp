#include "Render/RayTracing/RayTracingScene.h"
#include "Render/Buffer.h"
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
                return MakeRef<RHI::DX12::D3DRayTracingScene>();
        }
        AL_ASSERT_MSG(false, "Unsupported render api!");
        return nullptr;
    }
    RayTracingScene::~RayTracingScene()
    {
    }
    NativeHandle RayTracingScene::NativeResource()
    {
        return _tlas_buffer ? _tlas_buffer->NativeResource() : NativeHandle{};
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
        _instances[instance_handle] = instance;
        if (!_is_need_rebuild)
            _is_need_refit = true;
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
            _tlas_buffer.reset();
            _is_need_rebuild = false;
            _is_need_refit = false;
            return;
        }

        auto cmd = CommandBufferPool::Get();
        cmd->BuildAS(this, !_is_need_rebuild);
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
