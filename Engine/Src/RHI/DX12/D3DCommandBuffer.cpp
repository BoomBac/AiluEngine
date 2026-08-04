#include "RHI/DX12/D3DCommandBuffer.h"
#include "Ext/pix/Include/WinPixEventRuntime/pix3.h"
#include "Framework/Common/ResourceMgr.h"
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
        _local_resource_states.reserve(64u);
        _is_executed = false;
        _p_cmd->SetName(std::format(L"CmdList_{}", _id).c_str());
        _p_alloc->SetName(std::format(L"CmdAllocator_{}", _id).c_str());
    }

    void D3DCommandBuffer::Clear()
    {
        AL_ASSERT(IsReady());
        ThrowIfFailed(_p_alloc->Reset());
        ThrowIfFailed(_p_cmd->Reset(_p_alloc.Get(), nullptr));
        _cur_cbv_heap_id = -1;
        _is_cmd_closed = false;
        _allocations.clear();
        _temp_allocs.clear();
        _upload_buf->Reset();
        _used_resource_set.clear();
        _used_resources.clear();
        _local_resource_states.clear();
        _fence_value = 0u;
        _is_submitted = false;
        _is_executed = false;
#if AILU_ENABLE_FRAME_DEBUGGER
        SetCaptureWriter(nullptr);
#endif
        _graphics_state_cache.Reset();
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

    void D3DCommandBuffer::InsertUAVBarrier()
    {
        const auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(nullptr);
        _p_cmd->ResourceBarrier(1u, &barrier);
    }

    void D3DCommandBuffer::InsertUAVBarrier(ID3D12Resource* resource)
    {
        const auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(resource);
        _p_cmd->ResourceBarrier(1u, &barrier);
    }

    void D3DCommandBuffer::RecordResourceBarrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before_state,
                                                 D3D12_RESOURCE_STATES after_state, u32 sub_res)
    {
        AL_ASSERT(resource != nullptr);
        if (before_state == after_state) return;
        const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(resource, before_state, after_state, sub_res);
        _p_cmd->ResourceBarrier(1u, &barrier);
    }

    void D3DCommandBuffer::EnsureResourceState(D3DResourceStateGuard& state_guard,
                                               D3D12_RESOURCE_STATES target_state, u32 sub_res)
    {
        ID3D12Resource* resource = state_guard.NativeResource();
        if (resource == nullptr)
            return;

        const u64 resource_instance_id = state_guard.InstanceId();
        auto it = _local_resource_states.find(resource_instance_id);
        if (it == _local_resource_states.end())
        {
            LocalResourceState local_state;
            local_state._resource = resource;
            local_state._global_state = &state_guard;
            state_guard.SnapshotStates(local_state._states);
            local_state._initial_states = local_state._states;
            it = _local_resource_states.emplace(resource_instance_id, std::move(local_state)).first;
        }

        auto& states = it->second._states;
        const u32 subresource_count = static_cast<u32>(states.size());
        AL_ASSERT(subresource_count > 0u);

        if (sub_res == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
        {
            bool needs_transition = false;
            for (const auto state : states)
            {
                if (state != target_state)
                {
                    needs_transition = true;
                    break;
                }
            }

            if (needs_transition)
            {
                if (std::all_of(states.begin(), states.end(), [&](const auto state) { return state == states.front(); }))
                {
                    const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(resource, states.front(), target_state,
                                                                                D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
                    _p_cmd->ResourceBarrier(1u, &barrier);
                }
                else
                {
                    Vector<D3D12_RESOURCE_BARRIER> barriers;
                    barriers.reserve(subresource_count);
                    for (u32 i = 0u; i < subresource_count; ++i)
                    {
                        if (states[i] != target_state)
                            barriers.emplace_back(CD3DX12_RESOURCE_BARRIER::Transition(resource, states[i], target_state, i));
                    }
                    if (!barriers.empty()) _p_cmd->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
                }
                std::fill(states.begin(), states.end(), target_state);
            }
            return;
        }

        AL_ASSERT(sub_res < subresource_count);
        if (states[sub_res] == target_state) return;

        const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(resource, states[sub_res], target_state, sub_res);
        _p_cmd->ResourceBarrier(1u, &barrier);
        states[sub_res] = target_state;
    }

    void D3DCommandBuffer::GetResourceStateSnapshots(Vector<ResourceStateSnapshot>& out_snapshots) const
    {
        out_snapshots.clear();
        out_snapshots.reserve(_local_resource_states.size());
        for (const auto& [resource_instance_id, local_state] : _local_resource_states)
        {
            ResourceStateSnapshot snapshot;
            snapshot._resource_instance_id = resource_instance_id;
            snapshot._resource = local_state._resource;
            snapshot._global_state = local_state._global_state;
            snapshot._initial_states = local_state._initial_states;
            snapshot._final_states = local_state._states;
            out_snapshots.emplace_back(std::move(snapshot));
        }
    }

    void D3DCommandBuffer::CommitResourceStates()
    {
        for (auto& [resource_instance_id, local_state] : _local_resource_states)
        {
            (void) resource_instance_id;
            if (local_state._global_state != nullptr)
                local_state._global_state->SetStateFromSnapshot(local_state._states);
        }
    }

    void D3DCommandBuffer::UploadDataToBuffer(void* src,u64 src_size,ID3D12Resource* dst,D3DResourceStateGuard& state_guard)
    {
        auto alloc = _upload_buf->Allocate(src_size,256);
        alloc.SetData(src,src_size);
        auto old_state = state_guard.CurState();
        EnsureResourceState(state_guard, D3D12_RESOURCE_STATE_COPY_DEST);
        _p_cmd->CopyBufferRegion(dst, 0u, alloc._page_res, alloc._offset, src_size);
        EnsureResourceState(state_guard, old_state);
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
