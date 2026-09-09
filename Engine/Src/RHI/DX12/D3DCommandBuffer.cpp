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
        _local_resource_states.reserve(64u);
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
        _local_resource_states.clear();
        _render_graph_resources.clear();
        _active_render_graph_resources.clear();
        _first_recording_group_name.clear();
        _recording_group_name.clear();
        _first_group_submission_index = 0u;
        _last_group_submission_index = 0u;
        _has_recorded_group = false;
        _fence_value = 0u;
        _is_submitted = false;
        _is_executed = false;
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

    void D3DCommandBuffer::RegisterRenderGraphResource(Render::GpuResource *resource)
    {
        if (resource == nullptr)
            return;
        // Keep the command-wide set for lifetime/diagnostic ownership.  State tracking uses the active group set,
        // because one RHI command can contain several groups.
        const auto native_resource = resource->NativeResource();
        if (native_resource._res != nullptr)
        {
            auto *native = static_cast<ID3D12Resource *>(native_resource._res);
            _render_graph_resources.insert(native);
            _active_render_graph_resources.insert(native);
        }
        if (auto *buffer = dynamic_cast<D3DGPUBuffer *>(resource); buffer != nullptr)
        {
            if (auto *counter = buffer->GetCounterBuffer(); counter != nullptr)
            {
                _render_graph_resources.insert(counter);
                _active_render_graph_resources.insert(counter);
            }
        }
    }

    void D3DCommandBuffer::BeginRenderGraphGroup(const Vector<Render::GpuResource *> &resources)
    {
        // Do not let resources from a later group affect barrier recording for the current group.
        _active_render_graph_resources.clear();
        for (auto *resource: resources)
            RegisterRenderGraphResource(resource);
    }

    bool D3DCommandBuffer::IsRenderGraphResource(ID3D12Resource *resource) const
    {
        return resource != nullptr && _active_render_graph_resources.contains(resource);
    }

    void D3DCommandBuffer::RecordResourceBarrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before_state,
                                                  D3D12_RESOURCE_STATES after_state, u32 sub_res)
    {
        AL_ASSERT(resource != nullptr);
        if (before_state == after_state) return;
#if AILU_ENABLE_RESOURCE_STATE_TRACE
        const String resource_name = D3DResourceStateGuard::DebugObjectName(resource);
        if (resource_name.find("_MainLightShadowMap") != String::npos ||
            resource_name.find("_AddLightShadowMaps") != String::npos ||
            resource_name.find("VolumetricFogAccumTexture") != String::npos)
        {
            LOG_WARNING("D3DCommandBuffer native barrier: cmd_ptr={}, cmd={}, group={}, group_submission_index={}, "
                        "resource={}, ptr={}, sub_res={}, before={}, after={}",
                        static_cast<const void *>(this),
                        Name(),
                        RecordingGroupName(),
                        LastGroupSubmissionIndex(),
                        resource_name,
                        static_cast<const void *>(resource),
                        sub_res,
                        static_cast<u32>(before_state),
                        static_cast<u32>(after_state));
        }
#endif
        const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(resource, before_state, after_state, sub_res);
        _p_cmd->ResourceBarrier(1u, &barrier);
    }

    void D3DCommandBuffer::ApplyResourceBarrier(D3DResourceStateGuard &state_guard, D3D12_RESOURCE_STATES before_state,
                                                D3D12_RESOURCE_STATES after_state, u32 sub_res)
    {
        ID3D12Resource *resource = state_guard.NativeResource();
        if (resource == nullptr)
            return;

        const u64 resource_instance_id = state_guard.InstanceId();
        const bool is_render_graph_resource = IsRenderGraphResource(resource);
#if AILU_ENABLE_RESOURCE_STATE_TRACE
        const String resource_name = D3DResourceStateGuard::DebugObjectName(resource);
        const bool is_trace_resource = resource_name.find("GBuffer0") != String::npos ||
                                       resource_name.find("light probe") != String::npos ||
                                       resource_name.find("_MainLightShadowMap") != String::npos ||
                                       resource_name.find("_AddLightShadowMaps") != String::npos ||
                                       resource_name.find("VolumetricFogAccumTexture") != String::npos;
#endif
#if AILU_ENABLE_RESOURCE_STATE_TRACE
        if (is_trace_resource)
        {
            LOG_WARNING("D3DCommandBuffer barrier record: cmd_ptr={}, cmd={}, group={}, group_submission_index={}, "
                        "rg={}, resource={}, ptr={}, instance={}, sub_res={}, before={}, after={}",
                        static_cast<const void *>(this),
                        Name(),
                        RecordingGroupName(),
                        LastGroupSubmissionIndex(),
                        is_render_graph_resource,
                        resource_name,
                        static_cast<const void *>(resource),
                        resource_instance_id,
                        sub_res,
                        static_cast<u32>(before_state),
                        static_cast<u32>(after_state));
        }
#endif
        auto it = _local_resource_states.find(resource_instance_id);
        const bool has_local_state = it != _local_resource_states.end();
#if AILU_ENABLE_RESOURCE_STATE_TRACE
        if (is_trace_resource && has_local_state)
        {
            const auto &local_states = it->second._states;
            const u32 previous_state = sub_res == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES ||
                                               sub_res >= local_states.size() ?
                                           static_cast<u32>(local_states.front()) :
                                           static_cast<u32>(local_states[sub_res]);
            LOG_WARNING("D3DCommandBuffer resource barrier local state: cmd_ptr={}, group={}, group_submission_index={}, "
                        "resource={}, sub_res={}, local_before={}, requested_before={}, requested_after={}",
                        static_cast<const void *>(this),
                        RecordingGroupName(),
                        LastGroupSubmissionIndex(),
                        resource_name,
                        sub_res,
                        previous_state,
                        static_cast<u32>(before_state),
                        static_cast<u32>(after_state));
        }
#endif
        if (it == _local_resource_states.end())
        {
            LocalResourceState local_state;
            local_state._resource = resource;
            local_state._global_state = &state_guard;
            local_state._recording_group_name = RecordingGroupName();
            local_state._last_recording_group_name = RecordingGroupName();
            local_state._is_render_graph_resource = is_render_graph_resource;
            state_guard.SnapshotStates(local_state._states);
            local_state._initialized_subresources.resize(local_state._states.size(), 1u);
            local_state._initial_states = local_state._states;
            it = _local_resource_states.emplace(resource_instance_id, std::move(local_state)).first;
        }

        auto &local_state = it->second;
        if (is_render_graph_resource && !local_state._is_render_graph_resource)
        {
            local_state._is_render_graph_resource = true;
            local_state._recording_group_name = RecordingGroupName();
        }
        local_state._last_recording_group_name = RecordingGroupName();
        const u32 subresource_count = static_cast<u32>(local_state._states.size());
        AL_ASSERT(subresource_count > 0u);
        // A graph command list can begin in the middle of the compiled graph state timeline.  Seed that timeline
        // only when this command list has not recorded the resource yet.  If an earlier non-graph command already
        // touched it, the local state is the only valid Before state for the same native command list.
        if (is_render_graph_resource && !has_local_state)
        {
            if (sub_res == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
            {
                std::fill(local_state._initial_states.begin(), local_state._initial_states.end(), before_state);
                std::fill(local_state._states.begin(), local_state._states.end(), before_state);
            }
            else
            {
                AL_ASSERT(sub_res < subresource_count);
                local_state._initial_states[sub_res] = before_state;
                local_state._states[sub_res] = before_state;
            }
        }
        if (sub_res == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
        {
            if (std::all_of(local_state._states.begin(), local_state._states.end(),
                            [&](const auto state) { return state == local_state._states.front(); }))
            {
                RecordResourceBarrier(resource, local_state._states.front(), after_state, sub_res);
            }
            else
            {
                for (u32 index = 0u; index < subresource_count; ++index)
                    RecordResourceBarrier(resource, local_state._states[index], after_state, index);
            }
            std::fill(local_state._states.begin(), local_state._states.end(), after_state);
            return;
        }

        AL_ASSERT(sub_res < subresource_count);
        const D3D12_RESOURCE_STATES recorded_before = local_state._states[sub_res];
        RecordResourceBarrier(resource, recorded_before, after_state, sub_res);
        local_state._states[sub_res] = after_state;
    }

    void D3DCommandBuffer::EnsureResourceState(D3DResourceStateGuard& state_guard,
                                                D3D12_RESOURCE_STATES target_state, u32 sub_res)
    {
        ID3D12Resource* resource = state_guard.NativeResource();
        if (resource == nullptr)
            return;
#if AILU_ENABLE_RESOURCE_STATE_TRACE
        const String resource_name = D3DResourceStateGuard::DebugObjectName(resource);
        const bool is_trace_resource = resource_name.find("GBuffer0") != String::npos ||
                                       resource_name.find("light probe") != String::npos ||
                                       resource_name.find("_MainLightShadowMap") != String::npos ||
                                       resource_name.find("_AddLightShadowMaps") != String::npos ||
                                       resource_name.find("VolumetricFogAccumTexture") != String::npos;
#endif
#if AILU_ENABLE_RESOURCE_STATE_TRACE
        if (is_trace_resource)
        {
            LOG_WARNING("D3DCommandBuffer EnsureResourceState: cmd_ptr={}, cmd={}, group={}, group_submission_index={}, "
                        "rg={}, resource={}, ptr={}, instance={}, sub_res={}, target={}",
                        static_cast<const void *>(this),
                        Name(),
                        RecordingGroupName(),
                        LastGroupSubmissionIndex(),
                        IsRenderGraphResource(resource),
                        resource_name,
                        static_cast<const void *>(resource),
                        state_guard.InstanceId(),
                        sub_res,
                        static_cast<u32>(target_state));
        }
#endif
        if (IsRenderGraphResource(resource))
        {
            return;
        }

        const u64 resource_instance_id = state_guard.InstanceId();
        auto it = _local_resource_states.find(resource_instance_id);
        if (it == _local_resource_states.end())
        {
            LocalResourceState local_state;
            local_state._resource = resource;
            local_state._global_state = &state_guard;
            local_state._recording_group_name = RecordingGroupName();
            local_state._last_recording_group_name = RecordingGroupName();
            local_state._is_render_graph_resource = false;
            state_guard.SnapshotStates(local_state._states);
            local_state._initial_states = local_state._states;
            local_state._initialized_subresources.resize(local_state._states.size(), 1u);
            it = _local_resource_states.emplace(resource_instance_id, std::move(local_state)).first;
        }

        auto &local_state = it->second;
        local_state._last_recording_group_name = RecordingGroupName();
        auto& states = local_state._states;
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
                    RecordResourceBarrier(resource, states.front(), target_state, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
                }
                else
                {
                    for (u32 i = 0u; i < subresource_count; ++i)
                    {
                        if (states[i] != target_state)
                            RecordResourceBarrier(resource, states[i], target_state, i);
                    }
                }
                std::fill(states.begin(), states.end(), target_state);
            }
            return;
        }

        AL_ASSERT(sub_res < subresource_count);
        if (states[sub_res] == target_state) return;

        RecordResourceBarrier(resource, states[sub_res], target_state, sub_res);
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
            snapshot._recording_group_name = local_state._recording_group_name;
            snapshot._last_recording_group_name = local_state._last_recording_group_name;
            snapshot._is_render_graph_resource = local_state._is_render_graph_resource;
            snapshot._first_group_submission_index = FirstGroupSubmissionIndex();
            snapshot._last_group_submission_index = LastGroupSubmissionIndex();
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
