#include "RHI/DX12/D3DBuffer.h"
#include "Framework/Math/ALMath.hpp"
#include "RHI/DX12/D3DCommandBuffer.h"
#include "RHI/DX12/D3DContext.h"
#include "RHI/DX12/dxhelper.h"
#include "Render/RenderConstants.h"
#include "Render/RenderingData.h"

#include "pch.h"

namespace Ailu
{
//----------------------------------------------------------------D3DGPUBuffer------------------------------------------------------------------------
#pragma region D3DGPUBuffer
    D3DGPUBuffer::D3DGPUBuffer(GPUBufferDesc desc) : GPUBuffer(desc)
    {
        _mem_size = Math::AlignTo(_desc._size, 256);
    }
    D3DGPUBuffer::~D3DGPUBuffer()
    {
        D3DDescriptorMgr::Get().Free(std::move(_alloc));
        g_pGfxContext->WaitForFence(_fence_value);
    }
    void D3DGPUBuffer::UploadImpl(GraphicsContext* ctx,RHICommandBuffer* rhi_cmd,UploadParams* params)
    {
        GpuResource::UploadImpl(ctx,rhi_cmd,params);
        auto d3d_conetxt = dynamic_cast<D3DContext*>(ctx);
        auto res_desc = CD3DX12_RESOURCE_DESC::Buffer(_mem_size);
        res_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        if (_desc._is_random_write)
            res_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        auto heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        _state_guard = D3DResourceStateGuard(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ThrowIfFailed(d3d_conetxt->GetDevice()->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &res_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                                        nullptr, IID_PPV_ARGS(_p_d3d_res.GetAddressOf())));
        // if (_desc._is_readable)
        // {
        //     heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
        //     _state_guard_readback = D3DResourceStateGuard(D3D12_RESOURCE_STATE_COPY_DEST);
        //     res_desc.Flags &= ~D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        //     ThrowIfFailed(d3d_conetxt->GetDevice()->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &res_desc, D3D12_RESOURCE_STATE_COPY_DEST,
        //                                                                     nullptr, IID_PPV_ARGS(_p_d3d_res_readback.GetAddressOf())));
        // }
        auto p_device = d3d_conetxt->GetDevice();
        _alloc = D3DDescriptorMgr::Get().AllocGPU(1u);
        if (_desc._is_random_write)
        {
            auto [cpu_handle, gpu_handle] = _alloc.At(0);
            D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
            uav_desc.Format = ConvertToDXGIFormat(_desc._format);
            uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uav_desc.Buffer.FirstElement = 0;
            uav_desc.Buffer.NumElements = _desc._element_num;
            p_device->CreateUnorderedAccessView(_p_d3d_res.Get(), nullptr, &uav_desc, cpu_handle);
        }
    }
    void D3DGPUBuffer::BindImpl(RHICommandBuffer* rhi_cmd,BindParams* params)
    {
        GpuResource::BindImpl(rhi_cmd, params);
        auto d3dcmd = dynamic_cast<D3DCommandBuffer*>(rhi_cmd);
        auto [ch, gh] = _alloc.At(0);
        if (params->_is_compute_pipeline)
            _alloc.CommitDescriptorsForDispatch(d3dcmd, params->_slot);
        else
            _alloc.CommitDescriptorsForDraw(d3dcmd, params->_slot);
    }
    void D3DGPUBuffer::StateTranslation(RHICommandBuffer* rhi_cmd,EResourceState new_state,u32 sub_res)
    {
        auto cmd = dynamic_cast<D3DCommandBuffer*>(rhi_cmd)->NativeCmdList();
        _state_guard.MakesureResourceState(cmd,_p_d3d_res.Get(),D3DConvertUtils::FromALResState(new_state),sub_res);
        _state = new_state;
    }
    void D3DGPUBuffer::ReadBack(u8 *dst, u32 size)
    {
        GraphicsContext::Get().ReadBack(this, dst, size);
        // if (!_desc._is_readable)
        // {
        //     g_pLogMgr->LogErrorFormat(std::source_location::current(), "{} not a readable resource", _name);
        //     return;
        // }
        // auto cmd = CommandBufferPool::Get();
        // auto dxcmd = static_cast<D3DCommandBuffer *>(cmd.get())->NativeCmdList();
        // _state_guard.MakesureResourceState(dxcmd, _p_d3d_res.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
        // dxcmd->CopyResource(_p_d3d_res_readback.Get(), _p_d3d_res.Get());
        // _state_guard.MakesureResourceState(dxcmd, _p_d3d_res.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        // g_pGfxContext->ExecuteAndWaitCommandBuffer(cmd);
        // D3D12_RANGE readbackBufferRange{0, _desc._size};
        // u8 *data;
        // _p_d3d_res_readback->Map(0, &readbackBufferRange, reinterpret_cast<void **>(&data));
        // //use data here
        // if (size != _desc._size)
        //     LOG_WARNING("D3DGPUBuffer::ReadBack byte_size not equal!");
        // memcpy(dst, data, std::min<u32>(_desc._size, size));
        // //end use
        // D3D12_RANGE emptyRange{0, 0};
        // _p_d3d_res_readback->Unmap(0, &emptyRange);
        // CommandBufferPool::Release(cmd);
    }
    void D3DGPUBuffer::ReadBackAsync(u8 *dst, u32 size, std::function<void()> on_complete)
    {
        AL_ASSERT(true);
        // if (!_desc._is_readable)
        // {
        //     g_pLogMgr->LogErrorFormat(std::source_location::current(), "{} not a readable resource", _name);
        //     return;
        // }
        // auto cmd = CommandBufferPool::Get();
        // auto dxcmd = static_cast<D3DCommandBuffer *>(cmd.get())->NativeCmdList();
        // _state_guard.MakesureResourceState(dxcmd, _p_d3d_res.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
        // dxcmd->CopyResource(_p_d3d_res_readback.Get(), _p_d3d_res.Get());
        // _state_guard.MakesureResourceState(dxcmd, _p_d3d_res.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        // g_pGfxContext->ExecuteCommandBuffer(cmd);

        // std::async(std::launch::async, [=]()
        //            {
		//     g_pGfxContext->WaitForGpu();
		//     D3D12_RANGE readbackBufferRange{0, _desc._size};
		//     u8* data;
		//     _p_d3d_res_readback->Map(0, &readbackBufferRange, reinterpret_cast<void**>(&data));
		
		//     // Ensure the buffer size matches
		//     if (size != _desc._size)
		// 	    LOG_WARNING("D3DGPUBuffer::ReadBack byte_size not equal!");

		//     // Copy the data from GPU to the destination buffer
		//     memcpy(dst, data, std::min<u32>(_desc._size, size));

		//     // Unmap the buffer
		//     D3D12_RANGE emptyRange{0, 0};
		//     _p_d3d_res_readback->Unmap(0, &emptyRange);

		//     if (on_complete)
		//     {
		// 	    on_complete();
		//     } });
        // CommandBufferPool::Release(cmd);
    }

    void D3DGPUBuffer::Name(const String &name)
    {
        _name = name;
        if (_p_d3d_res)
            _p_d3d_res->SetName(ToWStr(name).c_str());
    }
#pragma endregion
//----------------------------------------------------------------D3DGPUBuffer------------------------------------------------------------------------
#pragma region D3DVertexBuffer
    D3DVertexBuffer::D3DVertexBuffer(VertexBufferLayout layout) : VertexBuffer(layout)
    {
        u16 stream_count = _buffer_layout.GetStreamCount();
        _buffer_views.resize(stream_count);
        _vertex_buffers.resize(stream_count);
    }
    D3DVertexBuffer::~D3DVertexBuffer()
    {
        if (g_pGfxContext)
            g_pGfxContext->WaitForFence(_fence_value);
    }

    void D3DVertexBuffer::BindImpl(RHICommandBuffer *rhi_cmd, BindParams *params)
    {
        if (auto param = dynamic_cast<BindParamsVB *>(params); param != nullptr)
        {
            auto d3dcmd = dynamic_cast<D3DCommandBuffer*>(rhi_cmd);
            auto cmdlist = d3dcmd->NativeCmdList();
            GpuResource::BindImpl(rhi_cmd, params);
            for (auto &layout_ele: *param->_layout)
            {
                auto it = _buffer_layout_indexer.find(layout_ele.Name);
                if (it != _buffer_layout_indexer.end())
                {
                    const u8 stream_index = it->second;
                    //cmdlist->IASetVertexBuffers(layout_ele.Stream, 1, &s_vertex_buf_views[_buf_start + stream_index]);
                    cmdlist->IASetVertexBuffers(layout_ele.Stream, 1, &_buffer_views[stream_index]);
                }
                else
                {
                    LOG_WARNING("Try to bind a vertex buffer with an invalid layout element name {}", layout_ele.Name);
                }
            }
        }
    }
    void D3DVertexBuffer::UploadImpl(GraphicsContext *ctx, RHICommandBuffer *rhi_cmd,UploadParams* params)
    {
        GpuResource::UploadImpl(ctx,rhi_cmd,params);
        auto d3d_ctx = dynamic_cast<D3DContext*>(ctx);
        auto d3d_dev = d3d_ctx->GetDevice();
        for (u16 i = 0; i < _stream_data.size(); i++)
        {
            auto heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            auto res_desc = CD3DX12_RESOURCE_DESC::Buffer(_stream_data[i]._size);
            u32 stream_index = i;
            if (!_stream_data[i]._is_dynamic)
            {
                if (_stream_data[i]._data == nullptr)
                    continue;
                ComPtr<ID3D12Resource> upload_heap;
                ThrowIfFailed(d3d_dev->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &res_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(upload_heap.GetAddressOf())));
                // Copy the triangle data to update heap
                u8 *pVertexDataBegin;
                CD3DX12_RANGE readRange(0, 0);// We do not intend to read from this resource on the CPU.
                ThrowIfFailed(upload_heap->Map(0, &readRange, reinterpret_cast<void **>(&pVertexDataBegin)));
                memcpy(pVertexDataBegin, _stream_data[i]._data, _stream_data[i]._size);
                upload_heap->Unmap(0, nullptr);
                // Create a Default Heap for the vertex buffer
                _vertex_buffers[stream_index].Reset();
                heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
                ThrowIfFailed(d3d_dev->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &res_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(_vertex_buffers[stream_index].GetAddressOf())));
                auto d3dcmd = dynamic_cast<D3DCommandBuffer*>(rhi_cmd);
                auto cmdlist = d3dcmd->NativeCmdList();
                cmdlist->CopyBufferRegion(_vertex_buffers[stream_index].Get(), 0, upload_heap.Get(), 0, _stream_data[i]._size);
                auto buf_state = CD3DX12_RESOURCE_BARRIER::Transition(_vertex_buffers[stream_index].Get(),
                                                                      D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
                cmdlist->ResourceBarrier(1, &buf_state);
                d3d_ctx->TrackResource(upload_heap);
            }
            else
            {
                _vertex_buffers[stream_index].Reset();
                ThrowIfFailed(d3d_dev->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &res_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(_vertex_buffers[stream_index].GetAddressOf())));
                _vertex_buffers[stream_index]->Map(0, nullptr, reinterpret_cast<void **>(&_mapped_data[stream_index]));
                if (_stream_data[i]._data)
                    memcpy(_mapped_data[stream_index], _stream_data[i]._data, _stream_data[i]._size);
            }
            // Initialize the vertex buffer view.
            _buffer_views[stream_index].BufferLocation = _vertex_buffers[stream_index]->GetGPUVirtualAddress();
            _buffer_views[stream_index].StrideInBytes = _buffer_layout.GetStride(stream_index);
            _buffer_views[stream_index].SizeInBytes = (u32)_stream_data[i]._size;
            _buffer_layout_indexer.emplace(std::make_pair(_buffer_layout[stream_index].Name, stream_index));
        }
    }

    void D3DVertexBuffer::Name(const String &name)
    {
        Object::Name(name);
        for (auto &it: _buffer_layout_indexer)
        {
            if (_vertex_buffers[it.second])
                _vertex_buffers[it.second]->SetName(ToWStr(std::format("vb_{}_{}", name, it.second).c_str()).c_str());
        }
    }

#pragma endregion

#pragma region D3DIndexBuffer
    //-----------------------------------------------------------------IndexBuffer---------------------------------------------------------------------
    D3DIndexBuffer::D3DIndexBuffer(u32 *indices, u32 count, bool is_dynamic)
     : IndexBuffer(indices,count,is_dynamic)
    {

    }
    D3DIndexBuffer::~D3DIndexBuffer()
    {
        g_pGfxContext->WaitForFence(_fence_value);
    }

    void D3DIndexBuffer::UploadImpl(GraphicsContext* ctx,RHICommandBuffer* rhi_cmd,UploadParams* params)
    {
        GpuResource::UploadImpl(ctx,rhi_cmd,params);
        auto d3d_conetxt = dynamic_cast<D3DContext*>(ctx);
        auto heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto res_desc = CD3DX12_RESOURCE_DESC::Buffer(_mem_size);
        if (!_is_dynamic)
        {
            ComPtr<ID3D12Resource> temp_buffer = nullptr;
            ThrowIfFailed(d3d_conetxt->GetDevice()->CreateCommittedResource(
                    &heap_prop,
                    D3D12_HEAP_FLAG_NONE,
                    &res_desc,
                    D3D12_RESOURCE_STATE_GENERIC_READ,
                    nullptr,
                    IID_PPV_ARGS(&_index_buf)));

            // Copy the triangle data to the vertex buffer.
            u8 *pVertexDataBegin;
            CD3DX12_RANGE readRange(0, 0);// We do not intend to read from this resource on the CPU.
            ThrowIfFailed(_index_buf->Map(0, &readRange, reinterpret_cast<void **>(&pVertexDataBegin)));
            memcpy(pVertexDataBegin, _data, _mem_size);
            _index_buf->Unmap(0, nullptr);
        }
        else
        {
            ThrowIfFailed(d3d_conetxt->GetDevice()->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &res_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(_index_buf.GetAddressOf())));
            _index_buf->Map(0, nullptr, reinterpret_cast<void **>(&_data));
        }
        // Initialize the vertex buffer view.
        _index_buf_view.BufferLocation = _index_buf->GetGPUVirtualAddress();
        _index_buf_view.Format = DXGI_FORMAT_R32_UINT;
        _index_buf_view.SizeInBytes = static_cast<u32>(_mem_size);
    }
    void D3DIndexBuffer::BindImpl(RHICommandBuffer* rhi_cmd,BindParams* params)
    {
        GpuResource::BindImpl(rhi_cmd, params);
        auto d3dcmd = dynamic_cast<D3DCommandBuffer*>(rhi_cmd);
        d3dcmd->NativeCmdList()->IASetIndexBuffer(&_index_buf_view);
    }

    void D3DIndexBuffer::Name(const String &name)
    {
        if (_index_buf)
            _index_buf->SetName(ToWStr(std::format("ib_{}", name).c_str()).c_str());
    }

    void D3DIndexBuffer::Resize(u32 new_size)
    {
        if (_capacity == new_size || !_is_dynamic)
            return;
        g_pGfxContext->WaitForFence(_fence_value);
        _capacity = new_size;
        _count = std::min<u32>(new_size, _count);
        _mem_size = sizeof(u32) * new_size;
        _index_buf.Reset();
        Apply();
    }
//-----------------------------------------------------------------IndexBuffer---------------------------------------------------------------------
#pragma endregion

//-----------------------------------------------------------------ConstBuffer---------------------------------------------------------------------
//https://maraneshi.github.io/HLSL-ConstantBufferLayoutVisualizer/
#pragma region D3DConstantBuffer
    static u32 CalcConstantBufferByteSize(u32 byte_size)
    {
        return (byte_size + 255) & ~255;
    }

    D3DConstantBuffer::D3DConstantBuffer(u32 size, bool compute_buffer) : _is_compute_buffer(compute_buffer)
    {
        size = (u32)AlignTo(size, 256);
        _alloc = GpuResourceManager::Get()->Allocate(size);
        _mem_size = size;
        _data = reinterpret_cast<u8 *>(_alloc._cpu_ptr);
        _is_ready_for_rendering = true;
    }
    D3DConstantBuffer::~D3DConstantBuffer()
    {
        GpuResourceManager::Get()->Free(std::move(_alloc));
    }
    void D3DConstantBuffer::BindImpl(RHICommandBuffer* rhi_cmd,BindParams* params)
    {
        auto d3dcmd = dynamic_cast<D3DCommandBuffer*>(rhi_cmd);
        auto cmd = d3dcmd->NativeCmdList();
        if (params->_is_compute_pipeline)
            cmd->SetComputeRootConstantBufferView(params->_slot, _alloc._gpu_ptr);
        else
            cmd->SetGraphicsRootConstantBufferView(params->_slot, _alloc._gpu_ptr);
    }
    void D3DConstantBuffer::Reset()
    {
        memset(_alloc._cpu_ptr, 0, _alloc._size);
    }
#pragma endregion
    //-----------------------------------------------------------------ConstBuffer---------------------------------------------------------------------

}// namespace Ailu
