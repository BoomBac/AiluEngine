#include "RHI/DX12/D3DCommandBuffer.h"
#include "Ext/pix/Include/WinPixEventRuntime/pix3.h"
#include "Framework/Common/RenderDebugConfig.h"
#include "Framework/Common/ResourceMgr.h"
#include "RHI/DX12/D3DBuffer.h"
#include "RHI/DX12/D3DContext.h"
#include "RHI/DX12/D3DTexture.h"
#include "RHI/DX12/dxhelper.h"
#include "Render/Gizmo.h"
#include "Render/GraphicsPipelineStateObject.h"
#include "Render/RenderQueue.h"
#include "Render/RenderingData.h"
#include "pch.h"

namespace Ailu::RHI::DX12
{
    D3DCommandBuffer::D3DCommandBuffer(String name,ECommandBufferType type) : RHICommandBuffer(name,type)
    {
        _dx_cmd_type = (D3D12_COMMAND_LIST_TYPE)type;
        auto dev = dynamic_cast<D3DContext*>(g_pGfxContext)->GetDevice();
        ThrowIfFailed(dev->CreateCommandAllocator(_dx_cmd_type, IID_PPV_ARGS(_p_alloc.GetAddressOf())));
        ThrowIfFailed(dev->CreateCommandList(0, _dx_cmd_type, _p_alloc.Get(), nullptr,
                                             IID_PPV_ARGS(_p_cmd.GetAddressOf())));
        ThrowIfFailed(_p_cmd->Close());
        _is_cmd_closed = true;
        _is_submitted = false;
        _upload_buf = MakeScope<UploadBuffer>(std::format("CmdUploadBuffer_{}", _id));
        _cur_cbv_heap_id = -1;
        _fence_value = 0u;
        _used_resources.reserve(64u);
        _barrier_cache.reserve(64u);
        _is_executed = false;
        _p_cmd->SetName(std::format(L"CmdList_{}", _id).c_str());
        _p_alloc->SetName(std::format(L"CmdAllocator_{}", _id).c_str());
    }

    void D3DCommandBuffer::Clear()
    {
        AL_ASSERT(IsReady());
        RHICommandBuffer::Clear();
        ThrowIfFailed(_p_alloc->Reset());
        ThrowIfFailed(_p_cmd->Reset(_p_alloc.Get(), nullptr));
        _cur_cbv_heap_id = -1;
        _is_cmd_closed = false;
        _allocations.clear();
        _temp_allocs.clear();
        _upload_buf->Reset();
        _used_resource_set.clear();
        _used_resources.clear();
        _state_tracker.Clear();
        _first_recording_group_name.clear();
        _recording_group_name.clear();
        _first_group_submission_index = 0u;
        _last_group_submission_index = 0u;
        _has_recorded_group = false;
        _fence_value = 0u;
        _is_submitted = false;
        _is_executed = false;
        _barrier_cache.clear();
        _is_batching_barriers = false;
#if AILU_ENABLE_FRAME_DEBUGGER
        SetCaptureWriter(nullptr);
#endif
        ResetGraphicsStateCache();
        _recording_context.Clear();
        _statistics.Reset();
        _profiler_stack.clear();
        _post_submit_callbacks.clear();
    }

    void D3DCommandBuffer::Close()
    {
        if (!_is_cmd_closed)
        {
            ThrowIfFailed(_p_cmd->Close());
            _is_cmd_closed = true;
        }
    }
    void D3DCommandBuffer::AllocConstBuffer(const String& name,u32 size, u8 *data)
    {
//        if (_allocations.contains(name) && _allocations[name]._size >= size)
//        {
//            _allocations[name].SetData(data, size);
//        }
//        else
//        {
//            auto alloc = _upload_buf->Allocate(size,256);
//            alloc.SetData(data,size);
//            _allocations[name] = alloc;
//        }
        auto alloc = _upload_buf->Allocate(size,256);
        alloc.SetData(data,size);
        _allocations[name] = alloc;
    }

    UploadBuffer::Allocation D3DCommandBuffer::AllocConstBuffer(const u8* data, u32 size)
    {
        auto alloc = _upload_buf->Allocate(size,256);
        alloc.SetData(data,size);
        _temp_allocs.emplace_back(alloc);
        return _temp_allocs.back();
    }

    void D3DCommandBuffer::PostExecute()
    {
        for(auto& it : _used_resources)
        {
            it->Track(_fence_value);
        }
        _recording_context.RenderingStatesData().MergeTo(Render::RenderingStates::RenderData());
        _statistics.MergeTo(Render::RenderingStates::RenderData());
        _is_executed = true;
    }

    void D3DCommandBuffer::RunPostSubmitCallbacks(u64 fence_value)
    {
        for (auto &callback: _post_submit_callbacks)
            callback(fence_value);
        _post_submit_callbacks.clear();
    }

    void D3DCommandBuffer::MarkSubmitted(u64 fence_value)
    {
        _fence_value = fence_value;
        _is_submitted = true;
    }

    void D3DCommandBuffer::UavBarrier()
    {
        const auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(nullptr);
        _p_cmd->ResourceBarrier(1u, &barrier);
    }

    void D3DCommandBuffer::UavBarrier(const D3DResource &resource)
    {
        if (!resource)
            return;
        _barrier_cache.emplace_back(CD3DX12_RESOURCE_BARRIER::UAV(resource.Get()));
        if (!_is_batching_barriers)
            FlushResourceBarriers();
    }

    void D3DCommandBuffer::FlushResourceBarriers()
    {
        if (_barrier_cache.empty())
            return;

        // A batch can legitimately hold several transitions for the same subresource, e.g. a pass that reads a
        // resource and then writes it.  Nothing is recorded between them, so the intermediate state is never
        // observable and the chain collapses to its endpoints.  D3D12 additionally flags a repeated subresource
        // inside one ResourceBarrier call, so collapsing both satisfies the debug layer and removes driver work.
        u32 write_index = 0u;
        for (u32 read_index = 0u; read_index < _barrier_cache.size(); ++read_index)
        {
            const D3D12_RESOURCE_BARRIER incoming = _barrier_cache[read_index];
            if (incoming.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)
            {
                bool merged = false;
                for (u32 probe = write_index; probe-- > 0u;)
                {
                    D3D12_RESOURCE_BARRIER &existing = _barrier_cache[probe];
                    if (existing.Type != D3D12_RESOURCE_BARRIER_TYPE_TRANSITION ||
                        existing.Transition.pResource != incoming.Transition.pResource ||
                        existing.Transition.Subresource != incoming.Transition.Subresource)
                        continue;
                    // The chain has to be contiguous, otherwise the two transitions describe different timelines.
                    if (existing.Transition.StateAfter != incoming.Transition.StateBefore)
                        break;
                    existing.Transition.StateAfter = incoming.Transition.StateAfter;
                    merged = true;
                    break;
                }
                if (merged)
                    continue;
            }
            _barrier_cache[write_index++] = incoming;
        }
        _barrier_cache.resize(write_index);

        // A chain that folds back onto its own source state carries no information at all.
        _barrier_cache.erase(std::remove_if(_barrier_cache.begin(), _barrier_cache.end(),
                                            [](const D3D12_RESOURCE_BARRIER &barrier)
                                            {
                                                return barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION &&
                                                       barrier.Transition.StateBefore == barrier.Transition.StateAfter;
                                            }),
                              _barrier_cache.end());
        if (_barrier_cache.empty())
            return;

        // Surviving entries keep their recording order; D3D12 executes the array in order.
        _p_cmd->ResourceBarrier(static_cast<UINT>(_barrier_cache.size()), _barrier_cache.data());
        _barrier_cache.clear();
    }

    void D3DCommandBuffer::AliasingBarrier(const D3DResource &before, const D3DResource &after)
    {
        _barrier_cache.emplace_back(CD3DX12_RESOURCE_BARRIER::Aliasing(before.Get(), after.Get()));
        if (!_is_batching_barriers)
            FlushResourceBarriers();
    }

    void D3DCommandBuffer::RecordQueueTransition(const D3DResource &resource, Render::EResourceState before_state,
                                                  Render::EResourceState after_state, u32 sub_res)
    {
        if (!resource)
            return;
        if (before_state == after_state)
            return;
        _barrier_cache.emplace_back(CD3DX12_RESOURCE_BARRIER::Transition(
            resource.Get(), D3DConvertUtils::FromALResState(before_state),
            D3DConvertUtils::FromALResState(after_state), sub_res));
        if (!_is_batching_barriers)
            FlushResourceBarriers();
    }

    void D3DCommandBuffer::RequireState(const D3DResource &resource, Render::EResourceState state, u32 sub_res)
    {
        if (!resource)
            return;
        _state_tracker.RequireState(resource, state, sub_res);
        BeginResourceBarrierBatch();
        for (const auto &transition: _state_tracker.Transitions())
            RecordQueueTransition(resource, transition._before, transition._after, transition._subresource);
        EndResourceBarrierBatch();
    }

    void D3DCommandBuffer::UploadDataToBuffer(void *src, u64 src_size, const D3DResource &resource)
    {
        if (src == nullptr || src_size == 0u || !resource)
            return;
        auto alloc = _upload_buf->Allocate(src_size,256);
        alloc.SetData(src,src_size);
        RequireState(resource, Render::EResourceState::kCopyDest);
        _p_cmd->CopyBufferRegion(resource.Get(), 0u, alloc._page_res, alloc._offset, src_size);
    }

    void D3DCommandBuffer::ResetRenderTarget()
    {
        _recording_context.ResetRenderTargetState();
        _active_render_targets.clear();
        for(u16 i = 0; i < RenderConstants::kMaxMRTNum; i++)
        {
            _colors[i] = nullptr;
        }
        _depth = nullptr;
        _color_count = 0u;
    }
    bool D3DCommandBuffer::IsReady() const
    {
        if (!_is_submitted)
            return true;
        return GraphicsContext::Get().GetFenceValueGPU() >= _fence_value;
    }
}// namespace Ailu::RHI::DX12
