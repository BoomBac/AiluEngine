#pragma once
#ifndef __D3D_COMMAND_BUF_H__
#define __D3D_COMMAND_BUF_H__

#include "Render/CommandBuffer.h"
#include "UploadBuffer.h"
#include <d3dx12.h>
#include <wrl/client.h>
#include "Render/RenderConstants.h"

namespace Ailu
{
    using Microsoft::WRL::ComPtr;
    class D3DCommandBuffer : public RHICommandBuffer
    {
        friend class D3DContext;
    public:
        D3DCommandBuffer(String name,ECommandBufferType type);
        bool IsReady() const final;
        ID3D12GraphicsCommandList* NativeCmdList() {return _p_cmd.Get();};
        void AllocConstBuffer(const String& name,u32 size, u8 *data);
        void Name(const String &name) final;
        void Clear() final;
        void ResetRenderTarget();
        /// @brief 标记当前cmd使用的资源，将当前cmd直接完毕的围栏值写入
        /// @param res 
        void MarkUsedResource(GpuResource* res) {_used_res.insert(res);}
        u16 GetDescriptorHeapId() const { return _cur_cbv_heap_id; }
        void SetDescriptorHeapId(u16 id) { _cur_cbv_heap_id = id; };
        void PostExecute();
    private:
        void Close();
    private:
        D3D12_COMMAND_LIST_TYPE _dx_cmd_type;
        ComPtr<ID3D12GraphicsCommandList> _p_cmd;
        ComPtr<ID3D12CommandAllocator> _p_alloc;
        Scope<UploadBuffer> _upload_buf;
        HashMap<String ,UploadBuffer::Allocation> _allocations;
        Array<D3D12_CPU_DESCRIPTOR_HANDLE*,RenderConstants::kMaxMRTNum> _colors;
        u16 _color_count;
        D3D12_CPU_DESCRIPTOR_HANDLE* _depth;
        Array<D3D12_VIEWPORT,RenderConstants::kMaxMRTNum> _viewports;
        Array<D3D12_RECT,RenderConstants::kMaxMRTNum> _scissors;
        std::set<GpuResource*,ObjectPtrCompare> _used_res;
        bool _is_cmd_closed;
        i16 _cur_cbv_heap_id;
        u64 _fence_value;
    };
}// namespace Ailu


#endif// !D3D_COMMAND_BUF_H__
