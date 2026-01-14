#include "Render/GpuResource.h"
#include "Render/GraphicsContext.h"
#include "pch.h"
#include "Framework/Common/Log.h"

namespace Ailu::Render
{
    void GpuResource::Track(u64 fence)
    {
        _fence_value = fence == 0u? GraphicsContext::Get().GetFenceValueCPU() + 1 : fence;
    }
    GpuResource::GpuResource()
    {
    }
    GpuResource::~GpuResource()
    {
        s_total_mem_size -= _mem_size;
        LOG_INFO("GpuResource::~GpuResource: {} released,mem size {}", _name, _mem_size);
    }
    void GpuResource::Apply()
    {
        GraphicsContext::Get().CreateResource(this);
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
        if (_is_ready_for_rendering)
            BindImpl(rhi_cmd, params);
        else
        {
            LOG_WARNING("GpuResource::Bind: {} not ready yet,forget call `GraphicsContext::CreateResource`?", _name);
        }
    }
    void GpuResource::Upload(GraphicsContext* ctx,RHICommandBuffer* rhi_cmd,UploadParams* params)
    {
        UploadImpl(ctx,rhi_cmd,params);
        _is_ready_for_rendering = true;
    }
}// namespace Ailu
