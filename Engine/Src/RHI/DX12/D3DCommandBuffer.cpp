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
        ThrowIfFailed(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(_p_alloc.GetAddressOf())));
        ThrowIfFailed(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _p_alloc.Get(), nullptr, IID_PPV_ARGS(_p_cmd.GetAddressOf())));
        _is_cmd_closed = false;
        _upload_buf = MakeScope<UploadBuffer>(std::format("CmdUploadBuffer_{}", _id));
        _cur_cbv_heap_id = -1;
        _fence_value = 0u;
        _is_executed = false;
    }

    void D3DCommandBuffer::Clear()
    {
        if (_is_cmd_closed)
        {
            ThrowIfFailed(_p_alloc->Reset());
            ThrowIfFailed(_p_cmd->Reset(_p_alloc.Get(), nullptr));
            _is_cmd_closed = false;
        }
        _cur_cbv_heap_id = -1;
        _is_cmd_closed = false;
        _allocations.clear();
        _upload_buf->Reset();
        _used_res.clear();
        _is_executed = false;
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
    void D3DCommandBuffer::PostExecute()
    {
        for(auto& it : _used_res)
        {
            it->Track(_fence_value);
        }
        _is_executed = true;
    }

    void D3DCommandBuffer::UploadDataToBuffer(void* src,u64 src_size,ID3D12Resource* dst,D3DResourceStateGuard& state_guard)
    {
        auto alloc = _upload_buf->Allocate(src_size,256);
        alloc.SetData(src,src_size);
        auto old_state = state_guard.CurState();
        state_guard.MakesureResourceState(_p_cmd.Get(),D3D12_RESOURCE_STATE_COPY_DEST);
        _p_cmd->CopyBufferRegion(dst, 0u, alloc._page_res, alloc._offset, src_size);
        state_guard.MakesureResourceState(_p_cmd.Get(),old_state);
    }
    void D3DCommandBuffer::Name(const String &name)
    {
        auto wname = ToWChar(name);
        _p_cmd->SetName(wname.c_str());
        _p_alloc->SetName(wname.c_str());
        _name = name;
    }
    void D3DCommandBuffer::ResetRenderTarget()
    {
        GraphicsPipelineStateMgr::ResetRenderTargetState();
        for(u16 i = 0; i < RenderConstants::kMaxMRTNum; i++)
        {
            _colors[i] = nullptr;
        }
        _depth = nullptr;
        _color_count = 0u;
    }
    bool D3DCommandBuffer::IsReady() const
    {
        return GraphicsContext::Get().GetFenceValueGPU() > _fence_value;
    }
}// namespace Ailu
