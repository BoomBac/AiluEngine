#pragma once
#ifndef __D3D_COMMAND_BUF_H__
#define __D3D_COMMAND_BUF_H__

#include "D3DResourceBase.h"
#include "Render/CommandBuffer.h"
#include "Render/GpuResource.h"
#include "Render/RenderingStates.h"
#include "Render/RenderConstants.h"
#include "UploadBuffer.h"
#include <array>
#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <unordered_map>
#include <d3dx12.h>
#include <wrl/client.h>

namespace Ailu
{
    namespace Render
    {
        struct CommandProfiler;
    }

    using Microsoft::WRL::ComPtr;
    using Render::RHICommandBuffer;
    using Render::ECommandBufferType;
    using Render::GpuResource;

    namespace RHI::DX12
    {
        class D3DCommandBuffer : public RHICommandBuffer
        {
            friend class D3DContext;

            struct CommandBufferStatistics
            {
                u32 _vertex_num = 0u;
                u32 _triangle_num = 0u;
                u32 _draw_call = 0u;
                u32 _dispatch_call = 0u;
                u64 _draw_command_count = 0u;
                u64 _resource_mark_request_count = 0u;
                u64 _unique_resource_mark_count = 0u;
                u64 _vb_bind_cache_hit_count = 0u;
                u64 _vb_bind_cache_miss_count = 0u;

                void Reset()
                {
                    _vertex_num = 0u;
                    _triangle_num = 0u;
                    _draw_call = 0u;
                    _dispatch_call = 0u;
                    _draw_command_count = 0u;
                    _resource_mark_request_count = 0u;
                    _unique_resource_mark_count = 0u;
                    _vb_bind_cache_hit_count = 0u;
                    _vb_bind_cache_miss_count = 0u;
                }

                void MergeTo(Render::RenderingStatesData &data) const
                {
                    data.VertexNum += _vertex_num;
                    data.TriangleNum += _triangle_num;
                    data.DrawCall += _draw_call;
                    data.DispatchCall += _dispatch_call;
                    data.DrawCommandCount += _draw_command_count;
                    data.ResourceMarkRequestCount += _resource_mark_request_count;
                    data.UniqueResourceMarkCount += _unique_resource_mark_count;
                    data.VbBindCacheHitCount += _vb_bind_cache_hit_count;
                    data.VbBindCacheMissCount += _vb_bind_cache_miss_count;
                }
            };

            struct GraphicsStateCache
            {
                struct VertexBufferState
                {
                    D3D12_GPU_VIRTUAL_ADDRESS _location = 0u;
                    u32 _size = 0u;
                    u32 _stride = 0u;
                };

                struct IndexBufferState
                {
                    D3D12_GPU_VIRTUAL_ADDRESS _location = 0u;
                    u32 _size = 0u;
                    DXGI_FORMAT _format = DXGI_FORMAT_UNKNOWN;
                    bool _valid = false;
                };

                struct GraphicsRootSlotState
                {
                    const void *_resource = nullptr;
                    const void *_native_resource = nullptr;
                    u64 _gpu_handle = 0u;
                    u32 _resource_type = 0u;
                    u16 _view_index = 0u;
                    u32 _sub_resource = UINT32_MAX;
                    bool _is_compute = false;
                };

                struct RenderTargetState
                {
                    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, Render::RenderConstants::kMaxMRTNum> _rtvs{};
                    D3D12_CPU_DESCRIPTOR_HANDLE _dsv{};
                    u8 _rt_count = 0u;
                    bool _has_dsv = false;
                    bool _valid = false;
                };

                struct ViewportState
                {
                    std::array<D3D12_VIEWPORT, Render::RenderConstants::kMaxMRTNum> _viewports{};
                    u8 _count = 0u;
                    bool _valid = false;
                };

                struct ScissorState
                {
                    std::array<D3D12_RECT, Render::RenderConstants::kMaxMRTNum> _scissors{};
                    u8 _count = 0u;
                    bool _valid = false;
                };

                const void *_pso = nullptr;
                ID3D12RootSignature *_root_signature = nullptr;
                D3D12_PRIMITIVE_TOPOLOGY _primitive_topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
                IndexBufferState _index_buffer;
                std::array<VertexBufferState, 32> _vertex_buffers{};
                u32 _vertex_buffer_valid_mask = 0u;
                std::array<GraphicsRootSlotState, 32> _root_slots{};
                u64 _graphics_root_valid_mask = 0u;
                u64 _graphics_descriptor_table_mask = 0u;
                ID3D12DescriptorHeap *_cbv_srv_uav_heap = nullptr;
                std::array<u8, 32> _native_root_slot_types{};
                std::array<u64, 32> _native_root_slot_values{};
                u64 _native_root_valid_mask = 0u;
                u64 _native_descriptor_table_mask = 0u;
                RenderTargetState _render_targets;
                ViewportState _viewport;
                ScissorState _scissor;

                void Reset()
                {
                    *this = GraphicsStateCache{};
                }
            };

        public:
            D3DCommandBuffer(String name, ECommandBufferType type);
            bool IsReady() const final;
            void InsertUAVBarrier() final;
            void InsertUAVBarrier(ID3D12Resource* resource);
            void EnsureResourceState(D3DResourceStateGuard& state_guard, D3D12_RESOURCE_STATES target_state,
                                     u32 sub_res = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
            void ApplyResourceBarrier(D3DResourceStateGuard &state_guard, D3D12_RESOURCE_STATES before_state,
                                      D3D12_RESOURCE_STATES after_state,
                                      u32 sub_res = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
            void RegisterRenderGraphResource(Render::GpuResource *resource);
            void BeginRenderGraphGroup(const Vector<Render::GpuResource *> &resources);
            bool IsRenderGraphResource(ID3D12Resource *resource) const;
            void RecordResourceBarrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before_state,
                                        D3D12_RESOURCE_STATES after_state, u32 sub_res);
            /// @brief Submit a whole block of engine barriers as a single native ResourceBarrier call.
            /// Resource state tracking is identical to the per-barrier path; only the native submission is batched.
            void RecordResourceBarriers(const Render::ResourceBarrierDesc *barriers, u32 count);
            /// @brief While a batch is open, barrier recording only appends to the internal cache.
            void BeginResourceBarrierBatch() { _is_batching_barriers = true; }
            void EndResourceBarrierBatch()
            {
                _is_batching_barriers = false;
                FlushResourceBarriers();
            }
            struct ResourceStateSnapshot
            {
                u64 _resource_instance_id = 0u;
                ID3D12Resource* _resource = nullptr;
                D3DResourceStateGuard* _global_state = nullptr;
                String _recording_group_name;
                String _last_recording_group_name;
                bool _is_render_graph_resource = false;
                u32 _first_group_submission_index = 0u;
                u32 _last_group_submission_index = 0u;
                Vector<D3D12_RESOURCE_STATES> _initial_states;
                Vector<D3D12_RESOURCE_STATES> _final_states;
            };
            void GetResourceStateSnapshots(Vector<ResourceStateSnapshot>& out_snapshots) const;
            void CommitResourceStates();
            ID3D12GraphicsCommandList4 *NativeCmdList() { return _p_cmd.Get(); };
            void BeginRecordingGroup(const String& name, u32 submission_index)
            {
                if (!_has_recorded_group)
                {
                    _first_recording_group_name = name;
                    _first_group_submission_index = submission_index;
                    _has_recorded_group = true;
                }
                _recording_group_name = name;
                _last_group_submission_index = submission_index;
            }
            const String &RecordingGroupName() const
            {
                return _recording_group_name.empty() ? Name() : _recording_group_name;
            }
            u32 FirstGroupSubmissionIndex() const { return _first_group_submission_index; }
            u32 LastGroupSubmissionIndex() const { return _last_group_submission_index; }
            void AllocConstBuffer(const String &name, u32 size, u8 *data);
            UploadBuffer::Allocation AllocConstBuffer(const u8* data, u32 size);
            void Clear() final;
            void ResetRenderTarget();
            void SetActiveRenderTarget(GpuResource *resource) { if (resource != nullptr) _active_render_targets.insert(resource); }
            bool IsActiveRenderTarget(GpuResource *resource) const { return _active_render_targets.contains(resource); }
            /// @brief 标记当前cmd使用的资源，将当前cmd直接完毕的围栏值写入
            /// @param res
            void MarkUsedResource(GpuResource *resource)
            {
                ++_statistics._resource_mark_request_count;

                if (resource == nullptr)
                    return;

                if (_used_resource_set.insert(resource).second)
                {
                    _used_resources.emplace_back(resource);
                    ++_statistics._unique_resource_mark_count;
                }
            }
            u16 GetDescriptorHeapId() const { return _cur_cbv_heap_id; }
            void SetDescriptorHeapId(u16 id) { _cur_cbv_heap_id = id; };
            void PostExecute();
            void AddPostSubmitCallback(std::function<void(u64)> callback) { _post_submit_callbacks.emplace_back(std::move(callback)); }
            void RunPostSubmitCallbacks(u64 fence_value);
            /// @brief old -> COPY_DEST -> Copy -> old.  Passing restore_state=false stops after the copy and
            /// leaves the resource in COPY_DEST for the caller to transition, which removes one barrier.
            ///
            /// Only valid for callers that own the declared resource state afterwards.  Resources leased from
            /// FrameResourceManager and resources that the RenderGraph imports must keep the restoring default:
            /// the compiled graph declares their initial state up front, so leaving them in COPY_DEST would make
            /// the first compiled transition disagree with the real state.
            void UploadDataToBuffer(void *src, u64 src_size, ID3D12Resource *dst, D3DResourceStateGuard &state_guard,
                                    bool restore_state = true);
            bool IsGraphicsPSOActive(const void *pso) const { return _graphics_state_cache._pso == pso; }
            void SetGraphicsPSOActive(const void *pso)
            {
                _graphics_state_cache._pso = pso;
            }
            bool SetGraphicsPipelineState(const void *pso, ID3D12PipelineState *native_pso)
            {
                if (_graphics_state_cache._pso == pso)
                    return false;
                _graphics_state_cache._pso = pso;
                _p_cmd->SetPipelineState(native_pso);
                return true;
            }
            bool SetGraphicsRootSignature(ID3D12RootSignature *root_signature)
            {
                if (_graphics_state_cache._root_signature == root_signature)
                    return false;
                _graphics_state_cache._root_signature = root_signature;
                _graphics_state_cache._graphics_root_valid_mask = 0u;
                _graphics_state_cache._graphics_descriptor_table_mask = 0u;
                _graphics_state_cache._native_root_valid_mask = 0u;
                _graphics_state_cache._native_descriptor_table_mask = 0u;
                _p_cmd->SetGraphicsRootSignature(root_signature);
                return true;
            }
            bool SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY topology)
            {
                if (_graphics_state_cache._primitive_topology == topology)
                    return false;
                _graphics_state_cache._primitive_topology = topology;
                _p_cmd->IASetPrimitiveTopology(topology);
                return true;
            }
            bool SetDescriptorHeap(ID3D12DescriptorHeap *heap, i16 heap_id)
            {
                if (_graphics_state_cache._cbv_srv_uav_heap == heap)
                {
                    _cur_cbv_heap_id = heap_id;
                    return false;
                }
                _graphics_state_cache._cbv_srv_uav_heap = heap;
                _graphics_state_cache._graphics_root_valid_mask &=
                    ~_graphics_state_cache._graphics_descriptor_table_mask;
                _graphics_state_cache._native_root_valid_mask &=
                    ~_graphics_state_cache._native_descriptor_table_mask;
                _cur_cbv_heap_id = heap_id;
                _p_cmd->SetDescriptorHeaps(1u, &heap);
                return true;
            }
            bool SetGraphicsRootConstantBufferView(u16 slot, D3D12_GPU_VIRTUAL_ADDRESS address)
            {
                if (slot >= 32u)
                    return false;
                const u64 slot_bit = 1ull << slot;
                if ((_graphics_state_cache._native_root_valid_mask & slot_bit) != 0u &&
                    _graphics_state_cache._native_root_slot_types[slot] == 1u &&
                    _graphics_state_cache._native_root_slot_values[slot] == address)
                {
                    return false;
                }
                _graphics_state_cache._native_root_slot_types[slot] = 1u;
                _graphics_state_cache._native_root_slot_values[slot] = address;
                _graphics_state_cache._native_root_valid_mask |= slot_bit;
                _graphics_state_cache._native_descriptor_table_mask &= ~slot_bit;
                _p_cmd->SetGraphicsRootConstantBufferView(slot, address);
                return true;
            }
            bool SetGraphicsRootDescriptorTable(u16 slot, D3D12_GPU_DESCRIPTOR_HANDLE handle)
            {
                if (slot >= 32u)
                    return false;
                const u64 slot_bit = 1ull << slot;
                if ((_graphics_state_cache._native_root_valid_mask & slot_bit) != 0u &&
                    _graphics_state_cache._native_root_slot_types[slot] == 2u &&
                    _graphics_state_cache._native_root_slot_values[slot] == handle.ptr)
                {
                    return false;
                }
                _graphics_state_cache._native_root_slot_types[slot] = 2u;
                _graphics_state_cache._native_root_slot_values[slot] = handle.ptr;
                _graphics_state_cache._native_root_valid_mask |= slot_bit;
                _graphics_state_cache._native_descriptor_table_mask |= slot_bit;
                _p_cmd->SetGraphicsRootDescriptorTable(slot, handle);
                return true;
            }
            bool IsVertexBufferActive(const void *vb, const void *layout, u64 view_version) const
            {
                return _active_vb == vb && _active_vb_layout == layout && _active_vb_view_version == view_version;
            }
            void SetVertexBufferActive(const void *vb, const void *layout, u64 view_version)
            {
                _active_vb = vb;
                _active_vb_layout = layout;
                _active_vb_view_version = view_version;
            }
            bool IsIndexBufferActive(const void *ib, u64 view_version) const
            {
                return _active_ib == ib && _active_ib_view_version == view_version;
            }
            void SetIndexBufferActive(const void *ib, u64 view_version)
            {
                _active_ib = ib;
                _active_ib_view_version = view_version;
            }
            u64 GraphicsSlotMask() const { return _graphics_state_cache._graphics_root_valid_mask; }
            bool IsGraphicsSlotUpToDate(u16 slot, const Render::PipelineResource &resource) const
            {
                if (slot >= 32u)
                    return false;
                const auto &state = _graphics_state_cache._root_slots[slot];
                const u64 slot_bit = 1ull << slot;
                return (_graphics_state_cache._graphics_root_valid_mask & slot_bit) != 0u &&
                       state._resource == resource._p_resource &&
                       state._native_resource == resource._addi_info._native_res_ptr &&
                       state._gpu_handle == resource._addi_info._gpu_handle &&
                       state._resource_type == static_cast<u32>(resource._res_type) &&
                       state._view_index == resource._addi_info._view_index &&
                       state._sub_resource == resource._addi_info._sub_res &&
                       state._is_compute == resource._is_compute;
            }
            void UpdateGraphicsSlot(u16 slot, const Render::PipelineResource &resource, bool is_descriptor_table)
            {
                if (slot >= 32u)
                    return;
                auto &state = _graphics_state_cache._root_slots[slot];
                state._resource = resource._p_resource;
                state._native_resource = resource._addi_info._native_res_ptr;
                state._gpu_handle = resource._addi_info._gpu_handle;
                state._resource_type = static_cast<u32>(resource._res_type);
                state._view_index = resource._addi_info._view_index;
                state._sub_resource = resource._addi_info._sub_res;
                state._is_compute = resource._is_compute;
                const u64 slot_bit = 1ull << slot;
                _graphics_state_cache._graphics_root_valid_mask |= slot_bit;
                if (is_descriptor_table)
                    _graphics_state_cache._graphics_descriptor_table_mask |= slot_bit;
                else
                    _graphics_state_cache._graphics_descriptor_table_mask &= ~slot_bit;
            }
            u32 SetVertexBuffers(u32 first_slot, u32 count, const D3D12_VERTEX_BUFFER_VIEW *views)
            {
                if (count == 0u || views == nullptr || first_slot >= 32u || first_slot + count > 32u)
                    return 0u;
                u32 dirty_mask = 0u;
                for (u32 i = 0u; i < count; ++i)
                {
                    const u32 slot = first_slot + i;
                    const auto &view = views[i];
                    const auto &cached = _graphics_state_cache._vertex_buffers[slot];
                    if ((_graphics_state_cache._vertex_buffer_valid_mask & (1u << slot)) == 0u ||
                        cached._location != view.BufferLocation || cached._size != view.SizeInBytes ||
                        cached._stride != view.StrideInBytes)
                    {
                        dirty_mask |= 1u << slot;
                    }
                }
                if (dirty_mask == 0u)
                    return 0u;
                u32 native_call_count = 0u;
                u32 i = 0u;
                while (i < count)
                {
                    const u32 slot = first_slot + i;
                    if ((dirty_mask & (1u << slot)) == 0u)
                    {
                        ++i;
                        continue;
                    }
                    const u32 run_start = i++;
                    while (i < count && (dirty_mask & (1u << (first_slot + i))) != 0u)
                        ++i;
                    _p_cmd->IASetVertexBuffers(first_slot + run_start, i - run_start, views + run_start);
                    ++native_call_count;
                }
                for (u32 j = 0u; j < count; ++j)
                {
                    const u32 slot = first_slot + j;
                    if ((dirty_mask & (1u << slot)) == 0u)
                        continue;
                    const auto &view = views[j];
                    auto &cached = _graphics_state_cache._vertex_buffers[slot];
                    cached._location = view.BufferLocation;
                    cached._size = view.SizeInBytes;
                    cached._stride = view.StrideInBytes;
                    _graphics_state_cache._vertex_buffer_valid_mask |= 1u << slot;
                }
                return native_call_count;
            }
            bool SetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW &view)
            {
                const auto &cached = _graphics_state_cache._index_buffer;
                if (cached._location == view.BufferLocation && cached._size == view.SizeInBytes &&
                    cached._format == view.Format && cached._valid)
                {
                    return false;
                }
                _graphics_state_cache._index_buffer._location = view.BufferLocation;
                _graphics_state_cache._index_buffer._size = view.SizeInBytes;
                _graphics_state_cache._index_buffer._format = view.Format;
                _graphics_state_cache._index_buffer._valid = true;
                _p_cmd->IASetIndexBuffer(&view);
                return true;
            }
            bool SetRenderTargets(u32 color_count, const D3D12_CPU_DESCRIPTOR_HANDLE *rtvs,
                                  const D3D12_CPU_DESCRIPTOR_HANDLE *dsv)
            {
                if (color_count > Render::RenderConstants::kMaxMRTNum)
                    return false;
                if (color_count != 0u && rtvs == nullptr)
                    return false;
                auto &state = _graphics_state_cache._render_targets;
                bool is_same = state._valid && state._rt_count == color_count && state._has_dsv == (dsv != nullptr);
                if (is_same)
                {
                    for (u32 i = 0u; i < color_count; ++i)
                        is_same = is_same && state._rtvs[i].ptr == rtvs[i].ptr;
                    if (is_same && dsv != nullptr)
                        is_same = state._dsv.ptr == dsv->ptr;
                }
                if (is_same)
                    return false;
                state._valid = true;
                state._rt_count = static_cast<u8>(color_count);
                state._has_dsv = dsv != nullptr;
                state._dsv = dsv != nullptr ? *dsv : D3D12_CPU_DESCRIPTOR_HANDLE{};
                for (u32 i = 0u; i < color_count; ++i)
                    state._rtvs[i] = rtvs[i];
                _p_cmd->OMSetRenderTargets(color_count, color_count == 0u ? nullptr : state._rtvs.data(), FALSE,
                                            state._has_dsv ? &state._dsv : nullptr);
                return true;
            }
            bool SetViewports(u32 count, const D3D12_VIEWPORT *viewports)
            {
                if (count > Render::RenderConstants::kMaxMRTNum || (count != 0u && viewports == nullptr))
                    return false;
                auto &state = _graphics_state_cache._viewport;
                bool is_same = state._valid && state._count == count;
                if (is_same)
                {
                    for (u32 i = 0u; i < count; ++i)
                    {
                        const auto &a = state._viewports[i];
                        const auto &b = viewports[i];
                        is_same = is_same && a.TopLeftX == b.TopLeftX && a.TopLeftY == b.TopLeftY &&
                                  a.Width == b.Width && a.Height == b.Height && a.MinDepth == b.MinDepth &&
                                  a.MaxDepth == b.MaxDepth;
                    }
                }
                if (is_same)
                    return false;
                state._valid = true;
                state._count = static_cast<u8>(count);
                for (u32 i = 0u; i < count; ++i)
                    state._viewports[i] = viewports[i];
                _p_cmd->RSSetViewports(count, count == 0u ? nullptr : state._viewports.data());
                return true;
            }
            bool SetScissors(u32 count, const D3D12_RECT *scissors)
            {
                if (count > Render::RenderConstants::kMaxMRTNum || (count != 0u && scissors == nullptr))
                    return false;
                auto &state = _graphics_state_cache._scissor;
                bool is_same = state._valid && state._count == count;
                if (is_same)
                {
                    for (u32 i = 0u; i < count; ++i)
                    {
                        const auto &a = state._scissors[i];
                        const auto &b = scissors[i];
                        is_same = is_same && a.left == b.left && a.top == b.top && a.right == b.right &&
                                  a.bottom == b.bottom;
                    }
                }
                if (is_same)
                    return false;
                state._valid = true;
                state._count = static_cast<u8>(count);
                for (u32 i = 0u; i < count; ++i)
                    state._scissors[i] = scissors[i];
                _p_cmd->RSSetScissorRects(count, count == 0u ? nullptr : state._scissors.data());
                return true;
            }
            void ResetGraphicsStateCache()
            {
                _graphics_state_cache.Reset();
                _active_vb = nullptr;
                _active_vb_layout = nullptr;
                _active_vb_view_version = 0u;
                _active_ib = nullptr;
                _active_ib_view_version = 0u;
            }
            CommandBufferStatistics &Statistics() { return _statistics; }
            void PushProfiler(Render::CommandProfiler *profiler) { _profiler_stack.emplace_back(profiler); }
            Render::CommandProfiler *TopProfiler()
            {
                return _profiler_stack.empty() ? nullptr : _profiler_stack.back();
            }
            void PopProfiler()
            {
                if (!_profiler_stack.empty())
                    _profiler_stack.pop_back();
            }
            bool ProfilerStackEmpty() const { return _profiler_stack.empty(); }

        private:
            void Close();
            void MarkSubmitted(u64 fence_value);
            void FlushResourceBarriers();

        private:
            struct LocalResourceState
            {
                ID3D12Resource* _resource = nullptr;
                D3DResourceStateGuard* _global_state = nullptr;
                String _recording_group_name;
                String _last_recording_group_name;
                bool _is_render_graph_resource = false;
                Vector<D3D12_RESOURCE_STATES> _initial_states;
                Vector<D3D12_RESOURCE_STATES> _states;
                Vector<u8> _initialized_subresources;
            };

            D3D12_COMMAND_LIST_TYPE _dx_cmd_type;
            ComPtr<ID3D12GraphicsCommandList4> _p_cmd;
            ComPtr<ID3D12CommandAllocator> _p_alloc;
            Scope<UploadBuffer> _upload_buf;
            //存储用于管线资源的uploadbuffer，需要名字来绑定
            HashMap<String, UploadBuffer::Allocation> _allocations;
            //存储临时buffer，不需要名称，即刻返回
            Vector<UploadBuffer::Allocation> _temp_allocs;
            Array<D3D12_CPU_DESCRIPTOR_HANDLE *, Render::RenderConstants::kMaxMRTNum> _colors;
            u16 _color_count;
            D3D12_CPU_DESCRIPTOR_HANDLE *_depth;
            Array<D3D12_VIEWPORT, Render::RenderConstants::kMaxMRTNum> _viewports;
            Array<D3D12_RECT, Render::RenderConstants::kMaxMRTNum> _scissors;
            Vector<GpuResource *> _used_resources;
            String _first_recording_group_name;
            String _recording_group_name;
            u32 _first_group_submission_index = 0u;
            u32 _last_group_submission_index = 0u;
            bool _has_recorded_group = false;
            std::unordered_set<ID3D12Resource *> _render_graph_resources;
            std::unordered_set<ID3D12Resource *> _active_render_graph_resources;
            std::unordered_set<GpuResource *> _used_resource_set;
            bool _is_cmd_closed;
            bool _is_submitted;
            i16 _cur_cbv_heap_id;
            u64 _fence_value;
            GraphicsStateCache _graphics_state_cache;
            const void *_active_vb = nullptr;
            const void *_active_vb_layout = nullptr;
            u64 _active_vb_view_version = 0u;
            const void *_active_ib = nullptr;
            u64 _active_ib_view_version = 0u;
            CommandBufferStatistics _statistics;
            Vector<Render::CommandProfiler *> _profiler_stack;
            std::unordered_set<GpuResource *> _active_render_targets;
            std::unordered_map<u64, LocalResourceState> _local_resource_states;
            Vector<std::function<void(u64)>> _post_submit_callbacks;
            // Reusable native barrier scratch buffer. Reserved up front so a batched submission never allocates.
            Vector<D3D12_RESOURCE_BARRIER> _barrier_cache;
            bool _is_batching_barriers = false;
        };
    }// namespace ::RHI::DX12
}// namespace Ailu


#endif// !D3D_COMMAND_BUF_H__
