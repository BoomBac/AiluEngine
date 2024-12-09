#include "Render/GpuResource.h"
#include "Render/GraphicsContext.h"
#include "pch.h"

namespace Ailu
{
    bool GpuResourceState::IsReferenceByGpu() const
    {
        u64 fence_value = g_pGfxContext->GetFenceValueGPU();
        return fence_value < _fence_value;
    }
    void GpuResourceState::Track()
    {
        _fence_value = g_pGfxContext->GetFenceValueCPU() + 1;
    }
    void GpuResourceState::SetSize(u64 size)
    {
        if (_mem_size != 0)
            s_total_mem_size -= _mem_size;
        _mem_size = size;
        s_total_mem_size += size;
    }
    GpuResource::GpuResource()
    {
    }

    void GpuResource::Bind()
    {
        _state.Track();
    }
    GpuResource::~GpuResource()
    {
        GpuResourceState::s_total_mem_size -= _state._mem_size;
    }
}// namespace Ailu
