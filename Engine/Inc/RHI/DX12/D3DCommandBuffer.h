#pragma once
#ifndef __D3D_COMMAND_BUF_H__
#define __D3D_COMMAND_BUF_H__

#include "D3DResourceBase.h"
#include "Render/CommandBuffer.h"
#include "Render/RenderConstants.h"
#include "UploadBuffer.h"
#include <d3dx12.h>
#include <wrl/client.h>

namespace Ailu
{
    using Microsoft::WRL::ComPtr;
    using Render::RHICommandBuffer;
    using Render::ECommandBufferType;
    using Render::GpuResource;

    namespace RHI::DX12
    {
        class D3DCommandBuffer : public RHICommandBuffer
        {
            friend class D3DContext;

        public:
            D3DCommandBuffer(String name, ECommandBufferType type);
            bool IsReady() const final;
            ID3D12GraphicsCommandList *NativeCmdList() { return _p_cmd.Get(); };
            void AllocConstBuffer(const String &name, u32 size, u8 *data);
            void Name(const String &name) final;
            void Clear() final;
            void ResetRenderTarget();
            /// @brief 标记当前cmd使用的资源，将当前cmd直接完毕的围栏值写入
            /// @param res
            void MarkUsedResource(GpuResource *res) { _used_res.insert(res); }
            u16 GetDescriptorHeapId() const { return _cur_cbv_heap_id; }
            void SetDescriptorHeapId(u16 id) { _cur_cbv_heap_id = id; };
            void PostExecute();
            void UploadDataToBuffer(void *src, u64 src_size, ID3D12Resource *dst, D3DResourceStateGuard &state_guard);

        private:
            void Close();

        private:
            D3D12_COMMAND_LIST_TYPE _dx_cmd_type;
            ComPtr<ID3D12GraphicsCommandList> _p_cmd;
            ComPtr<ID3D12CommandAllocator> _p_alloc;
            Scope<UploadBuffer> _upload_buf;
            //存储用于管线资源的uploadbuffer，需要名字来绑定
            HashMap<String, UploadBuffer::Allocation> _allocations;
            Array<D3D12_CPU_DESCRIPTOR_HANDLE *, Render::RenderConstants::kMaxMRTNum> _colors;
            u16 _color_count;
            D3D12_CPU_DESCRIPTOR_HANDLE *_depth;
            Array<D3D12_VIEWPORT, Render::RenderConstants::kMaxMRTNum> _viewports;
            Array<D3D12_RECT, Render::RenderConstants::kMaxMRTNum> _scissors;
            std::set<GpuResource *, ObjectPtrCompare> _used_res;
            bool _is_cmd_closed;
            i16 _cur_cbv_heap_id;
            u64 _fence_value;
        };
    }// namespace ::RHI::DX12
}// namespace Ailu


#endif// !D3D_COMMAND_BUF_H__
