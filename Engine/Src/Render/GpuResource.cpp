#include "Render/GpuResource.h"
#include "Render/GraphicsContext.h"
#include "pch.h"
#include "Framework/Common/Log.h"

namespace Ailu::Render
{
    #pragma region GpuResource
    void GpuResource::Track(u64 fence)
    {
        _fence_value = fence == 0u? GraphicsContext::Get().GetFenceValueCPU() + 1 : fence;
    }
    bool GpuResource::MarkUsedByCommand(u64 command_epoch)
    {
        if (_last_marked_command_epoch == command_epoch)
            return false;
        _last_marked_command_epoch = command_epoch;
        return true;
    }
    GpuResource::GpuResource()
    {
    }
    GpuResource::~GpuResource()
    {
        s_total_mem_size -= _mem_size;
        //LOG_INFO("GpuResource::~GpuResource: {} released,mem size {}", _name, _mem_size);
    }
    void GpuResource::Apply()
    {
        // Async resource creation may be checked by UI/render code before the upload command runs.
        // Keep readiness independent from the per-queue resource state tracker.
        SetCreatedFence(0xFFFFFFFFFFFFFFFFu);
        GraphicsContext::Get().CreateResource(this);
    }
    void GpuResource::ApplySync()
    {
        GraphicsContext::Get().CreateResourceSync(this);
    }
    void GpuResource::UploadImpl(GraphicsContext* ctx,RHICommandBuffer* rhi_cmd,UploadParams* params)
    {
        s_total_mem_size += _mem_size;
    }
    bool GpuResource::IsReferenceByGpu() const
    {
        u64 fence_value = GraphicsContext::Get().GetFenceValueGPU();
        return fence_value < _fence_value;
    }
    void GpuResource::Bind(RHICommandBuffer *rhi_cmd, const BindParams& params)
    {
        if (IsReady())
            BindImpl(rhi_cmd, params);
        else
        {
            LOG_WARNING("GpuResource::Bind: {} not ready yet,forget call `GraphicsContext::CreateResource`?", _name);
        }
    }
    void GpuResource::Upload(GraphicsContext* ctx,RHICommandBuffer* rhi_cmd,UploadParams* params)
    {
        UploadImpl(ctx,rhi_cmd,params);
    }

    bool GpuResource::IsReady()
    {
        if (!_is_ready_for_rendering)
        {
            const u64 gpu_fence_value = GraphicsContext::Get().GetFenceValueGPU();
            _is_ready_for_rendering = gpu_fence_value >= _created_fence;
        }
        return _is_ready_for_rendering;
    }
}// namespace Ailu
