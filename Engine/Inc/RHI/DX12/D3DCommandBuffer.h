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

                void Reset()
                {
                    _vertex_num = 0u;
                    _triangle_num = 0u;
                    _draw_call = 0u;
                    _dispatch_call = 0u;
                    _draw_command_count = 0u;
                    _resource_mark_request_count = 0u;
                    _unique_resource_mark_count = 0u;
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
                }
            };

            struct GraphicsStateCache
            {
                const void *_pso = nullptr;
                const void *_vb = nullptr;
                const void *_ib = nullptr;
                const void *_vb_layout = nullptr;
                u64 _vb_view_version = 0u;
                u64 _ib_view_version = 0u;
                std::array<u64, 32> _slot_hashes{};
                u32 _slot_mask = 0u;

                void Reset()
                {
                    _pso = nullptr;
                    _vb = nullptr;
                    _ib = nullptr;
                    _vb_layout = nullptr;
                    _vb_view_version = 0u;
                    _ib_view_version = 0u;
                    _slot_hashes.fill(0u);
                    _slot_mask = 0u;
                }
            };

        public:
            D3DCommandBuffer(String name, ECommandBufferType type);
            bool IsReady() const final;
            void InsertUAVBarrier() final;
            void InsertUAVBarrier(ID3D12Resource* resource);
            void EnsureResourceState(D3DResourceStateGuard& state_guard, D3D12_RESOURCE_STATES target_state,
                                     u32 sub_res = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
            void RecordResourceBarrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before_state,
                                       D3D12_RESOURCE_STATES after_state, u32 sub_res);
            struct ResourceStateSnapshot
            {
                u64 _resource_instance_id = 0u;
                ID3D12Resource* _resource = nullptr;
                D3DResourceStateGuard* _global_state = nullptr;
                Vector<D3D12_RESOURCE_STATES> _initial_states;
                Vector<D3D12_RESOURCE_STATES> _final_states;
            };
            void GetResourceStateSnapshots(Vector<ResourceStateSnapshot>& out_snapshots) const;
            void CommitResourceStates();
            ID3D12GraphicsCommandList4 *NativeCmdList() { return _p_cmd.Get(); };
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
            void UploadDataToBuffer(void *src, u64 src_size, ID3D12Resource *dst, D3DResourceStateGuard &state_guard);
            bool IsGraphicsPSOActive(const void *pso) const { return _graphics_state_cache._pso == pso; }
            void SetGraphicsPSOActive(const void *pso)
            {
                if (_graphics_state_cache._pso != pso)
                {
                    _graphics_state_cache._pso = pso;
                    // The input layout belongs to the PSO.  A vertex buffer can therefore
                    // require a different set of IA slots after a PSO switch.
                    _graphics_state_cache._vb = nullptr;
                    _graphics_state_cache._ib = nullptr;
                    _graphics_state_cache._vb_layout = nullptr;
                    _graphics_state_cache._vb_view_version = 0u;
                    _graphics_state_cache._ib_view_version = 0u;
                    _graphics_state_cache._slot_hashes.fill(0u);
                    _graphics_state_cache._slot_mask = 0u;
                }
            }
            bool IsVertexBufferActive(const void *vb, const void *layout, u64 view_version) const
            {
                return _graphics_state_cache._vb == vb && _graphics_state_cache._vb_layout == layout &&
                       _graphics_state_cache._vb_view_version == view_version;
            }
            void SetVertexBufferActive(const void *vb, const void *layout, u64 view_version)
            {
                _graphics_state_cache._vb = vb;
                _graphics_state_cache._vb_layout = layout;
                _graphics_state_cache._vb_view_version = view_version;
            }
            bool IsIndexBufferActive(const void *ib, u64 view_version) const
            {
                return _graphics_state_cache._ib == ib && _graphics_state_cache._ib_view_version == view_version;
            }
            void SetIndexBufferActive(const void *ib, u64 view_version)
            {
                _graphics_state_cache._ib = ib;
                _graphics_state_cache._ib_view_version = view_version;
            }
            u32 GraphicsSlotMask() const { return _graphics_state_cache._slot_mask; }
            bool IsGraphicsSlotUpToDate(u16 slot, u64 binding_hash) const
            {
                return (_graphics_state_cache._slot_mask & (1u << slot)) != 0u && _graphics_state_cache._slot_hashes[slot] == binding_hash;
            }
            void UpdateGraphicsSlot(u16 slot, u64 binding_hash)
            {
                _graphics_state_cache._slot_mask |= (1u << slot);
                _graphics_state_cache._slot_hashes[slot] = binding_hash;
            }
            void ResetGraphicsStateCache() { _graphics_state_cache.Reset(); }
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

        private:
            struct LocalResourceState
            {
                ID3D12Resource* _resource = nullptr;
                D3DResourceStateGuard* _global_state = nullptr;
                Vector<D3D12_RESOURCE_STATES> _initial_states;
                Vector<D3D12_RESOURCE_STATES> _states;
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
            std::unordered_set<GpuResource *> _used_resource_set;
            bool _is_cmd_closed;
            bool _is_submitted;
            i16 _cur_cbv_heap_id;
            u64 _fence_value;
            GraphicsStateCache _graphics_state_cache;
            CommandBufferStatistics _statistics;
            Vector<Render::CommandProfiler *> _profiler_stack;
            std::unordered_set<GpuResource *> _active_render_targets;
            std::unordered_map<u64, LocalResourceState> _local_resource_states;
            Vector<std::function<void(u64)>> _post_submit_callbacks;
        };
    }// namespace ::RHI::DX12
}// namespace Ailu


#endif// !D3D_COMMAND_BUF_H__
