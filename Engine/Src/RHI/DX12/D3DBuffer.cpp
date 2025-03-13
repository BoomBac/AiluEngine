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
        auto d3d_conetxt = D3DContext::Get();
        _desc._size = Math::AlignTo(_desc._size, 256);
        auto res_desc = CD3DX12_RESOURCE_DESC::Buffer(_desc._size);
        res_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        if (desc._is_random_write)
            res_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        auto heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        _state_guard = D3DResourceStateGuard(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ThrowIfFailed(d3d_conetxt->GetDevice()->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &res_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                                        nullptr, IID_PPV_ARGS(_p_d3d_res.GetAddressOf())));
        if (desc._is_readable)
        {
            heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
            _state_guard_readback = D3DResourceStateGuard(D3D12_RESOURCE_STATE_COPY_DEST);
            res_desc.Flags &= ~D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            ThrowIfFailed(d3d_conetxt->GetDevice()->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &res_desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                                                            nullptr, IID_PPV_ARGS(_p_d3d_res_readback.GetAddressOf())));
        }
        auto p_device = dynamic_cast<D3DContext *>(g_pGfxContext)->GetDevice();
        _alloc = g_pGPUDescriptorAllocator->Allocate(1);
        if (desc._is_random_write)
        {
            auto [cpu_handle, gpu_handle] = _alloc.At(0);
            D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
            uav_desc.Format = ConvertToDXGIFormat(desc._format);
            uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uav_desc.Buffer.FirstElement = 0;
            uav_desc.Buffer.NumElements = desc._element_num;
            p_device->CreateUnorderedAccessView(_p_d3d_res.Get(), nullptr, &uav_desc, cpu_handle);
        }
        _state.SetSize(desc._size);
    }
    D3DGPUBuffer::~D3DGPUBuffer()
    {
        g_pGPUDescriptorAllocator->Free(std::move(_alloc));
        g_pGfxContext->WaitForFence(_state.GetFenceValue());
    }
    void D3DGPUBuffer::Bind(CommandBuffer *cmd, u8 bind_slot, bool is_compute_pipeline)
    {
        GpuResource::Bind();
        auto [ch, gh] = _alloc.At(0);
        if (is_compute_pipeline)
        {
            _alloc.CommitDescriptorsForDispatch(static_cast<D3DCommandBuffer *>(cmd), bind_slot);
            //static_cast<D3DCommandBuffer *>(cmd)->GetCmdList()->SetComputeRootDescriptorTable(bind_slot, gh);
            //_alloc.CommitDescriptorsForDraw(static_cast<D3DCommandBuffer *>(cmd),bind_slot);
        }
        else
            _alloc.CommitDescriptorsForDraw(static_cast<D3DCommandBuffer *>(cmd), bind_slot);
    }
    u8 *D3DGPUBuffer::ReadBack()
    {
        if (!_desc._is_readable)
        {
            g_pLogMgr->LogErrorFormat(std::source_location::current(), "{} not a readable resource", _name);
            return nullptr;
        }
        auto cmd = CommandBufferPool::Get();
        auto dxcmd = static_cast<D3DCommandBuffer *>(cmd.get())->GetCmdList();
        _state_guard.MakesureResourceState(dxcmd, _p_d3d_res.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
        dxcmd->CopyResource(_p_d3d_res_readback.Get(), _p_d3d_res.Get());
        _state_guard.MakesureResourceState(dxcmd, _p_d3d_res.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        g_pGfxContext->ExecuteAndWaitCommandBuffer(cmd);
        D3D12_RANGE readbackBufferRange{0, _desc._size};
        u8 *data;
        _p_d3d_res_readback->Map(0, &readbackBufferRange, reinterpret_cast<void **>(&data));
        //use data here
        u8 *aligned_out_data = new u8[_desc._size];
        memcpy(aligned_out_data, data, _desc._size);
        //end use
        D3D12_RANGE emptyRange{0, 0};
        _p_d3d_res_readback->Unmap(0, &emptyRange);
        return aligned_out_data;
    }
    void D3DGPUBuffer::ReadBack(u8 *dst, u32 size)
    {
        if (!_desc._is_readable)
        {
            g_pLogMgr->LogErrorFormat(std::source_location::current(), "{} not a readable resource", _name);
            return;
        }
        auto cmd = CommandBufferPool::Get();
        auto dxcmd = static_cast<D3DCommandBuffer *>(cmd.get())->GetCmdList();
        _state_guard.MakesureResourceState(dxcmd, _p_d3d_res.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
        dxcmd->CopyResource(_p_d3d_res_readback.Get(), _p_d3d_res.Get());
        _state_guard.MakesureResourceState(dxcmd, _p_d3d_res.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        g_pGfxContext->ExecuteAndWaitCommandBuffer(cmd);
        D3D12_RANGE readbackBufferRange{0, _desc._size};
        u8 *data;
        _p_d3d_res_readback->Map(0, &readbackBufferRange, reinterpret_cast<void **>(&data));
        //use data here
        if (size != _desc._size)
            LOG_WARNING("D3DGPUBuffer::ReadBack byte_size not equal!");
        memcpy(dst, data, std::min<u32>(_desc._size, size));
        //end use
        D3D12_RANGE emptyRange{0, 0};
        _p_d3d_res_readback->Unmap(0, &emptyRange);
        CommandBufferPool::Release(cmd);
    }
    void D3DGPUBuffer::ReadBackAsync(u8 *dst, u32 size, std::function<void()> on_complete)
    {
        if (!_desc._is_readable)
        {
            g_pLogMgr->LogErrorFormat(std::source_location::current(), "{} not a readable resource", _name);
            return;
        }
        auto cmd = CommandBufferPool::Get();
        auto dxcmd = static_cast<D3DCommandBuffer *>(cmd.get())->GetCmdList();
        _state_guard.MakesureResourceState(dxcmd, _p_d3d_res.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
        dxcmd->CopyResource(_p_d3d_res_readback.Get(), _p_d3d_res.Get());
        _state_guard.MakesureResourceState(dxcmd, _p_d3d_res.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        g_pGfxContext->ExecuteCommandBuffer(cmd);

        std::async(std::launch::async, [=]()
                   {
		    g_pGfxContext->WaitForGpu();
		    D3D12_RANGE readbackBufferRange{0, _desc._size};
		    u8* data;
		    _p_d3d_res_readback->Map(0, &readbackBufferRange, reinterpret_cast<void**>(&data));
		
		    // Ensure the buffer size matches
		    if (size != _desc._size)
			    LOG_WARNING("D3DGPUBuffer::ReadBack byte_size not equal!");

		    // Copy the data from GPU to the destination buffer
		    memcpy(dst, data, std::min<u32>(_desc._size, size));

		    // Unmap the buffer
		    D3D12_RANGE emptyRange{0, 0};
		    _p_d3d_res_readback->Unmap(0, &emptyRange);

		    if (on_complete)
		    {
			    on_complete();
		    } });
        CommandBufferPool::Release(cmd);
    }

    void D3DGPUBuffer::Name(const String &name)
    {
        _name = name;
        _p_d3d_res->SetName(ToWStr(name).c_str());
        if (_p_d3d_res_readback)
            _p_d3d_res_readback->SetName(std::format(L"{}_readback", ToWStr(name)).c_str());
    }
#pragma endregion
    //----------------------------------------------------------------D3DGPUBuffer------------------------------------------------------------------------
    #pragma region D3DVertexBuffer
    D3DVertexBuffer::D3DVertexBuffer(VertexBufferLayout layout)
    {
        _buffer_layout = std::move(layout);
        u16 stream_count = _buffer_layout.GetStreamCount();
        _buffer_views.resize(stream_count);
        _vertex_buffers.resize(stream_count);
        _mapped_data.resize(RenderConstants::kMaxVertexAttrNum);
        _stream_size.resize(RenderConstants::kMaxVertexAttrNum);
        _size_all_stream = 0u;
    }
    D3DVertexBuffer::~D3DVertexBuffer()
    {
        _size_all_stream = 0u;
        if (g_pGfxContext)
            g_pGfxContext->WaitForFence(_state.GetFenceValue());
    }

    void D3DVertexBuffer::Bind(CommandBuffer *cmd, const VertexBufferLayout &pipeline_input_layout)
    {
        GpuResource::Bind();
        auto cmdlist = static_cast<D3DCommandBuffer *>(cmd)->GetCmdList();
        RenderingStates::s_vertex_num += _vertices_count;
        for (auto &layout_ele: pipeline_input_layout)
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

    void D3DVertexBuffer::SetLayout(VertexBufferLayout layout)
    {
        _buffer_layout = std::move(layout);
    }

    const VertexBufferLayout &D3DVertexBuffer::GetLayout() const
    {
        return _buffer_layout;
    }

    void D3DVertexBuffer::SetStream(float *vertices, u32 size, u8 stream_index)
    {
        auto d3d_conetxt = D3DContext::Get();
        if (_buffer_layout.GetStride(stream_index) == 0)
        {
            AL_ASSERT_MSG(false, "Try to set a null stream!");
            return;
        }
        _vertices_count = size / _buffer_layout.GetStride(stream_index);
        ComPtr<ID3D12Resource> upload_heap;
        auto heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto res_desc = CD3DX12_RESOURCE_DESC::Buffer(size);
        ThrowIfFailed(d3d_conetxt->GetDevice()->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &res_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(upload_heap.GetAddressOf())));
        // Copy the triangle data to update heap
        u8 *pVertexDataBegin;
        CD3DX12_RANGE readRange(0, 0);// We do not intend to read from this resource on the CPU.
        ThrowIfFailed(upload_heap->Map(0, &readRange, reinterpret_cast<void **>(&pVertexDataBegin)));
        memcpy(pVertexDataBegin, vertices, size);
        upload_heap->Unmap(0, nullptr);

        // Create a Default Heap for the vertex buffer
        //u32 cur_buffer_index = _buf_start + stream_index;
        //s_vertex_bufs[cur_buffer_index].Reset();
        _vertex_buffers[stream_index].Reset();
        heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        ThrowIfFailed(d3d_conetxt->GetDevice()->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &res_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(_vertex_buffers[stream_index].GetAddressOf())));
        auto cmd = CommandBufferPool::Get();
        auto cmdlist = static_cast<D3DCommandBuffer *>(cmd.get())->GetCmdList();
        cmdlist->CopyBufferRegion(_vertex_buffers[stream_index].Get(), 0, upload_heap.Get(), 0, size);
        auto buf_state = CD3DX12_RESOURCE_BARRIER::Transition(_vertex_buffers[stream_index].Get(),
                                                              D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        cmdlist->ResourceBarrier(1, &buf_state);
        d3d_conetxt->ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
        // Initialize the vertex buffer view.
        _buffer_views[stream_index].BufferLocation = _vertex_buffers[stream_index]->GetGPUVirtualAddress();
        _buffer_views[stream_index].StrideInBytes = _buffer_layout.GetStride(stream_index);
        _buffer_views[stream_index].SizeInBytes = size;
        _buffer_layout_indexer.emplace(std::make_pair(_buffer_layout[stream_index].Name, stream_index));
        D3DContext::Get()->TrackResource(upload_heap);
        _stream_size[stream_index] = size;
        //s_vertex_upload_bufs.emplace_back(upload_heap);
        _size_all_stream += size;
        _state.SetSize(_size_all_stream);
    }

    void D3DVertexBuffer::SetStream(u8 *data, u32 size, u8 stream_index, bool dynamic)
    {
        auto d3d_conetxt = D3DContext::Get();
        if (_buffer_layout.GetStride(stream_index) == 0)
        {
            AL_ASSERT_MSG(false, "Try to set a null stream!");
            return;
        }
        _vertices_count = size / _buffer_layout.GetStride(stream_index);
        auto heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto res_desc = CD3DX12_RESOURCE_DESC::Buffer(size);
        //u32 cur_buffer_index = _buf_start + stream_index;
        u32 cur_buffer_index = stream_index;
        if (!dynamic)
        {
            ComPtr<ID3D12Resource> upload_heap;
            ThrowIfFailed(d3d_conetxt->GetDevice()->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &res_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(upload_heap.GetAddressOf())));
            // Copy the triangle data to update heap
            u8 *pVertexDataBegin;
            CD3DX12_RANGE readRange(0, 0);// We do not intend to read from this resource on the CPU.
            ThrowIfFailed(upload_heap->Map(0, &readRange, reinterpret_cast<void **>(&pVertexDataBegin)));
            memcpy(pVertexDataBegin, data, size);
            upload_heap->Unmap(0, nullptr);
            // Create a Default Heap for the vertex buffer
            _vertex_buffers[cur_buffer_index].Reset();
            heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
            ThrowIfFailed(d3d_conetxt->GetDevice()->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &res_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(_vertex_buffers[cur_buffer_index].GetAddressOf())));
            auto cmd = CommandBufferPool::Get();
            auto cmdlist = static_cast<D3DCommandBuffer *>(cmd.get())->GetCmdList();
            cmdlist->CopyBufferRegion(_vertex_buffers[cur_buffer_index].Get(), 0, upload_heap.Get(), 0, size);
            auto buf_state = CD3DX12_RESOURCE_BARRIER::Transition(_vertex_buffers[cur_buffer_index].Get(),
                                                                  D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
            cmdlist->ResourceBarrier(1, &buf_state);
            d3d_conetxt->ExecuteCommandBuffer(cmd);
            CommandBufferPool::Release(cmd);
            D3DContext::Get()->TrackResource(upload_heap);
        }
        else
        {
            _vertex_buffers[cur_buffer_index].Reset();
            ThrowIfFailed(d3d_conetxt->GetDevice()->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &res_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(_vertex_buffers[cur_buffer_index].GetAddressOf())));
            _vertex_buffers[cur_buffer_index]->Map(0, nullptr, reinterpret_cast<void **>(&_mapped_data[stream_index]));
            if (data)
                memcpy(_mapped_data[stream_index], data, size);
        }
        _stream_size[stream_index] = size;
        // Initialize the vertex buffer view.
        _buffer_views[cur_buffer_index].BufferLocation = _vertex_buffers[cur_buffer_index]->GetGPUVirtualAddress();
        _buffer_views[cur_buffer_index].StrideInBytes = _buffer_layout.GetStride(stream_index);
        _buffer_views[cur_buffer_index].SizeInBytes = size;
        _buffer_layout_indexer.emplace(std::make_pair(_buffer_layout[stream_index].Name, stream_index));
        _size_all_stream += size;
        _state.SetSize(_size_all_stream);
    }

    void D3DVertexBuffer::SetName(const String &name)
    {
        for (auto &it: _buffer_layout_indexer)
        {
            _vertex_buffers[it.second]->SetName(ToWStr(std::format("vb_{}_{}", name, it.second).c_str()).c_str());
        }
    }

    u32 D3DVertexBuffer::GetVertexCount() const
    {
        return _vertices_count;
    }
    void D3DVertexBuffer::SetData(u8 *data, u32 size, u8 stream_index, u32 offset)
    {
        AL_ASSERT(stream_index < _mapped_data.size());
        AL_ASSERT(offset + size <= _stream_size[stream_index]);
        memcpy(_mapped_data[stream_index], data + offset, size);
        _vertices_count = size / _buffer_layout[stream_index].Size;
    }
    #pragma endregion

    #pragma region D3DIndexBuffer
    //-----------------------------------------------------------------IndexBuffer---------------------------------------------------------------------
    D3DIndexBuffer::D3DIndexBuffer(u32 *indices, u32 count, bool is_dynamic) : _count(count), _capacity(count), _is_dynamic(is_dynamic)
    {
        auto d3d_conetxt = D3DContext::Get();
        auto byte_size = sizeof(u32) * _count;
        auto heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto res_desc = CD3DX12_RESOURCE_DESC::Buffer(byte_size);
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
            memcpy(pVertexDataBegin, indices, byte_size);
            _index_buf->Unmap(0, nullptr);
        }
        else
        {
            ThrowIfFailed(d3d_conetxt->GetDevice()->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &res_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(_index_buf.GetAddressOf())));
            _index_buf->Map(0, nullptr, reinterpret_cast<void **>(&_p_data));
        }
        // Initialize the vertex buffer view.
        _index_buf_view.BufferLocation = _index_buf->GetGPUVirtualAddress();
        _index_buf_view.Format = DXGI_FORMAT_R32_UINT;
        _index_buf_view.SizeInBytes = static_cast<u32>(byte_size);
        _state.SetSize(byte_size);
    }
    D3DIndexBuffer::~D3DIndexBuffer()
    {
        g_pGfxContext->WaitForFence(_state.GetFenceValue());
    }

    void D3DIndexBuffer::Bind(CommandBuffer *cmd)
    {
        GpuResource::Bind();
        RenderingStates::s_triangle_num += _count / 3;
        static_cast<D3DCommandBuffer *>(cmd)->GetCmdList()->IASetIndexBuffer(&_index_buf_view);
    }
    u32 D3DIndexBuffer::GetCount() const
    {
        return _count;
    }
    void D3DIndexBuffer::SetName(const String &name)
    {
        _index_buf->SetName(ToWStr(std::format("ib_{}", name).c_str()).c_str());
    }
    u8 *D3DIndexBuffer::GetData()
    {
        return _p_data;
    }
    void D3DIndexBuffer::SetData(u8 *data, u32 size)
    {
        if (_p_data == nullptr || data == nullptr)
        {
            LOG_WARNING("Try to set a null data to index buffer!");
            return;
        }
        u32 max_size = _capacity * sizeof(u32);
        if (size > max_size)
        {
            size = max_size;
            LOG_WARNING("Try to set a larger data to index buffer! ({} > {})", size, max_size);
        }
        memcpy(_p_data, data, size * sizeof(u32));
        _count = size / sizeof(u32);
    }
    void D3DIndexBuffer::Resize(u32 new_size)
    {
        if (_capacity == new_size || !_is_dynamic)
            return;
        _capacity = new_size;
        _count = std::min<u32>(new_size, _count);
        auto d3d_conetxt = D3DContext::Get();
        auto byte_size = sizeof(u32) * _capacity;
        auto heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto res_desc = CD3DX12_RESOURCE_DESC::Buffer(byte_size);
        ThrowIfFailed(d3d_conetxt->GetDevice()->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &res_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                                        nullptr, IID_PPV_ARGS(_index_buf.ReleaseAndGetAddressOf())));
        _index_buf->Map(0, nullptr, reinterpret_cast<void **>(&_p_data));
        // Initialize the vertex buffer view.
        _index_buf_view.BufferLocation = _index_buf->GetGPUVirtualAddress();
        _index_buf_view.Format = DXGI_FORMAT_R32_UINT;
        _index_buf_view.SizeInBytes = static_cast<u32>(byte_size);
        _state.SetSize(new_size);
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
        _alloc = GpuResourceManager::Get()->Allocate(size);
        _state.SetSize(size);
        _data = reinterpret_cast<u8*>(_alloc._cpu_ptr);
        _size = size;
    }
    D3DConstantBuffer::~D3DConstantBuffer()
    {
        GpuResourceManager::Get()->Free(std::move(_alloc));
    }

    void D3DConstantBuffer::Bind(CommandBuffer *cmd, u8 bind_slot, bool is_compute_pipeline)
    {
        GpuResource::Bind();
        if (is_compute_pipeline)
            static_cast<D3DCommandBuffer *>(cmd)->GetCmdList()->SetComputeRootConstantBufferView(bind_slot, _alloc._gpu_ptr);
        else
            static_cast<D3DCommandBuffer *>(cmd)->GetCmdList()->SetGraphicsRootConstantBufferView(bind_slot, _alloc._gpu_ptr);
    }
    void D3DConstantBuffer::Reset()
    {
        memset(_alloc._cpu_ptr, 0, _alloc._size);
    }
    #pragma endregion
    //-----------------------------------------------------------------ConstBuffer---------------------------------------------------------------------

}// namespace Ailu
