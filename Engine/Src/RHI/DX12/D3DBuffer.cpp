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
    D3DGPUBuffer::D3DGPUBuffer(GPUBufferDesc desc) : _desc(desc)
    {
        auto d3d_conetxt = D3DContext::Get();
        _desc._size = Math::AlignTo(_desc._size, 256);
        auto res_desc = CD3DX12_RESOURCE_DESC::Buffer(_desc._size);
        res_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        res_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
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
        auto [cpu_handle, gpu_handle] = _alloc.At(0);
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
        uav_desc.Format = ConvertToDXGIFormat(desc._format);
        uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uav_desc.Buffer.FirstElement = 0;
        uav_desc.Buffer.NumElements = 1;
        p_device->CreateUnorderedAccessView(_p_d3d_res.Get(), nullptr, &uav_desc, cpu_handle);
    }
    D3DGPUBuffer::~D3DGPUBuffer()
    {
        g_pGPUDescriptorAllocator->Free(std::move(_alloc));
    }
    void D3DGPUBuffer::Bind(CommandBuffer *cmd, u8 bind_slot, bool is_compute_pipeline) const
    {
        auto [ch, gh] = _alloc.At(0);
        if (is_compute_pipeline)
            static_cast<D3DCommandBuffer *>(cmd)->GetCmdList()->SetComputeRootDescriptorTable(bind_slot, gh);
        else
            static_cast<D3DCommandBuffer *>(cmd)->GetCmdList()->SetGraphicsRootDescriptorTable(bind_slot, gh);
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
            LOG_WARNING("D3DGPUBuffer::ReadBack size not equal!");
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
			    LOG_WARNING("D3DGPUBuffer::ReadBack size not equal!");

		    // Copy the data from GPU to the destination buffer
		    memcpy(dst, data, std::min<u32>(_desc._size, size));

		    // Unmap the buffer
		    D3D12_RANGE emptyRange{0, 0};
		    _p_d3d_res_readback->Unmap(0, &emptyRange);

		    if (on_complete)
		    {
			    on_complete();
		    } 
        });
        CommandBufferPool::Release(cmd);
    }
    void D3DGPUBuffer::SetName(const String &name)
    {
        _name = name;
        _p_d3d_res->SetName(ToWChar(name));
        if (_p_d3d_res_readback)
            _p_d3d_res_readback->SetName(std::format(L"{}_readback", ToWChar(name)).c_str());
    }
    //----------------------------------------------------------------D3DGPUBuffer------------------------------------------------------------------------
    D3DVertexBuffer::D3DVertexBuffer(VertexBufferLayout layout)
    {
        _buffer_layout = std::move(layout);
        u16 stream_count = _buffer_layout.GetStreamCount();
        _buffer_views.resize(stream_count);
        _vertex_buffers.resize(stream_count);
        _mapped_data.resize(RenderConstants::kMaxVertexAttrNum);
    }
    D3DVertexBuffer::D3DVertexBuffer(float *vertices, u32 size)//: _buf_num(1)
    {
        _vertex_buffers.emplace_back(ComPtr<ID3D12Resource>());
        _buffer_views.emplace_back(D3D12_VERTEX_BUFFER_VIEW{});
        SetStream(vertices, size, 0);
    }
    D3DVertexBuffer::~D3DVertexBuffer()
    {
    }

    void D3DVertexBuffer::Bind(CommandBuffer *cmd, const VertexBufferLayout &pipeline_input_layout) const
    {
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
        //s_vertex_upload_bufs.emplace_back(upload_heap);
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
            _mapped_data[stream_index] = data;
            u32 cur_buffer_index = stream_index;
            _vertex_buffers[cur_buffer_index].Reset();
            ThrowIfFailed(d3d_conetxt->GetDevice()->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &res_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(_vertex_buffers[cur_buffer_index].GetAddressOf())));
            _vertex_buffers[cur_buffer_index]->Map(0, nullptr, reinterpret_cast<void **>(&_mapped_data[stream_index]));
            memcpy(_mapped_data[stream_index], data, size);
        }

        // Initialize the vertex buffer view.
        _buffer_views[cur_buffer_index].BufferLocation = _vertex_buffers[cur_buffer_index]->GetGPUVirtualAddress();
        _buffer_views[cur_buffer_index].StrideInBytes = _buffer_layout.GetStride(stream_index);
        _buffer_views[cur_buffer_index].SizeInBytes = size;
        _buffer_layout_indexer.emplace(std::make_pair(_buffer_layout[stream_index].Name, stream_index));
    }

    void D3DVertexBuffer::SetName(const String &name)
    {
        for (auto &it: _buffer_layout_indexer)
        {
            _vertex_buffers[it.second]->SetName(ToWChar(std::format("vb_{}_{}", name, it.second).c_str()));
        }
    }

    u32 D3DVertexBuffer::GetVertexCount() const
    {
        return _vertices_count;
    }

    //-----------------------------------------------------------------D3DDynamicVertexBuffer----------------------------------------------------------
    D3DDynamicVertexBuffer::D3DDynamicVertexBuffer(VertexBufferLayout layout)
    {
        _size_pos_buf = RenderConstants::KMaxDynamicVertexNum * sizeof(Vector3f);
        _size_color_buf = RenderConstants::KMaxDynamicVertexNum * sizeof(Vector4f);
        _ime_vertex_data_offset = _ime_color_data_offset = 0;
        _p_ime_vertex_data = new u8[12 * RenderConstants::KMaxDynamicVertexNum];
        _p_ime_color_data = new u8[16 * RenderConstants::KMaxDynamicVertexNum];
        auto device = D3DContext::Get()->GetDevice();
        D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(_size_pos_buf);
        ThrowIfFailed(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&_p_vertex_buf)));
        bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(_size_color_buf);
        ThrowIfFailed(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&_p_color_buf)));
        _p_vertex_buf->Map(0, nullptr, reinterpret_cast<void **>(&_p_vertex_data));
        _p_color_buf->Map(0, nullptr, reinterpret_cast<void **>(&_p_color_data));
        D3D12_VERTEX_BUFFER_VIEW buf_view{};
        buf_view.BufferLocation = _p_vertex_buf->GetGPUVirtualAddress();
        buf_view.StrideInBytes = sizeof(Vector3f);
        buf_view.SizeInBytes = _size_pos_buf;
        _buf_views[0] = buf_view;
        buf_view.BufferLocation = _p_color_buf->GetGPUVirtualAddress();
        buf_view.StrideInBytes = sizeof(Vector4f);
        buf_view.SizeInBytes = _size_color_buf;
        _buf_views[1] = buf_view;
    }
    D3DDynamicVertexBuffer::~D3DDynamicVertexBuffer()
    {
        delete[] _p_ime_vertex_data;
        delete[] _p_ime_color_data;
    }

    void D3DDynamicVertexBuffer::Bind(CommandBuffer *cmd) const
    {
        RenderingStates::s_vertex_num += _vertex_num;
        static_cast<D3DCommandBuffer *>(cmd)->GetCmdList()->IASetVertexBuffers(0, 2, _buf_views);
    }
    void D3DDynamicVertexBuffer::UploadData()
    {
        _vertex_num = _ime_vertex_data_offset / 12;
        //memcpy(_p_vertex_data, _p_ime_vertex_data, _size_pos_buf);
        //memcpy(_p_color_data, _p_ime_color_data, _size_color_buf);
        _ime_vertex_data_offset = 0;
        _ime_color_data_offset = 0;
    }
    void D3DDynamicVertexBuffer::AppendData(float *data0, u32 num0, float *data1, u32 num1)
    {
        memcpy(_p_vertex_data + _ime_vertex_data_offset, data0, num0 * 4);
        memcpy(_p_color_data + _ime_color_data_offset, data1, num1 * 4);
        _ime_vertex_data_offset += num0 * 4;
        _ime_color_data_offset += num1 * 4;
    }
    //-----------------------------------------------------------------D3DDynamicVertexBuffer----------------------------------------------------------

    //-----------------------------------------------------------------IndexBuffer---------------------------------------------------------------------
    D3DIndexBuffer::D3DIndexBuffer(u32 *indices, u32 count) : _index_count(count)
    {
        auto d3d_conetxt = D3DContext::Get();
        auto size = sizeof(u32) * count;
        auto heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto res_desc = CD3DX12_RESOURCE_DESC::Buffer(size);
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
        memcpy(pVertexDataBegin, indices, size);
        _index_buf->Unmap(0, nullptr);
        // Initialize the vertex buffer view.
        _index_buf_view.BufferLocation = _index_buf->GetGPUVirtualAddress();
        _index_buf_view.Format = DXGI_FORMAT_R32_UINT;
        _index_buf_view.SizeInBytes = static_cast<u32>(size);
    }
    D3DIndexBuffer::~D3DIndexBuffer()
    {
    }

    void D3DIndexBuffer::Bind(CommandBuffer *cmd) const
    {
        RenderingStates::s_triangle_num += _index_count / 3;
        static_cast<D3DCommandBuffer *>(cmd)->GetCmdList()->IASetIndexBuffer(&_index_buf_view);
    }
    u32 D3DIndexBuffer::GetCount() const
    {
        return _index_count;
    }
    void D3DIndexBuffer::SetName(const String &name)
    {
        _index_buf->SetName(ToWChar(std::format("ib_{}", name).c_str()));
    }
    //-----------------------------------------------------------------IndexBuffer---------------------------------------------------------------------


    //-----------------------------------------------------------------ConstBuffer---------------------------------------------------------------------
    static u32 CalcConstantBufferByteSize(u32 byte_size)
    {
        return (byte_size + 255) & ~255;
    }

    D3DConstantBuffer::D3DConstantBuffer(u32 size, bool compute_buffer) : _is_compute_buffer(compute_buffer)
    {
        static bool b_init = false;
        if (!b_init)
        {
            s_global_index = 0u;
            s_global_offset = 0u;
            auto device = D3DContext::Get()->GetDevice();
            //s_p_d3d_heap = D3DContext::Get()->GetDescriptorHeap();
            //constbuffer
            auto heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            s_total_size = RenderConstants::kPerFrameTotalSize * RenderConstants::kFrameCount + RenderConstants::kMaxMaterialDataCount * 256;
            s_desc_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            auto res_desc = CD3DX12_RESOURCE_DESC::Buffer(s_total_size);
            ThrowIfFailed(device->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &res_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&s_p_d3d_res)));
            s_p_d3d_res->SetName(L"GlobalConstBuffer");
            // Map and initialize the constant buffer. We don't unmap this until the
            // app closes. Keeping things mapped for the lifetime of the resource is okay.
            CD3DX12_RANGE readRange(0, 0);// We do not intend to read from this resource on the CPU.
            ThrowIfFailed(s_p_d3d_res->Map(0, &readRange, reinterpret_cast<void **>(&s_p_data)));
            b_init = true;
        }
        _size = CalculateConstantBufferByteSize(size);
        try
        {
            AL_ASSERT_MSG(s_global_offset + _size < s_total_size, "Constant buffer overflow");
        }
        catch (const std::runtime_error &e)
        {
            std::cerr << "Exception caught: " << e.what() << std::endl;
            // 这里可以进行适当的处理，如日志记录、清理资源等
            // 如果不希望程序继续执行，可以直接退出程序
            // exit(EXIT_FAILURE);
        }

        _offset = s_global_offset;
        _index = s_global_index;
        _gpu_ptr = s_p_d3d_res->GetGPUVirtualAddress() + _offset;
        // {
        // 	_allocation = g_pGPUDescriptorAllocator->Allocate(1);
        // 	//D3D12_CPU_DESCRIPTOR_HANDLE cbvHandle;
        // 	//cbvHandle.ptr = s_p_d3d_heap->GetCPUDescriptorHandleForHeapStart().ptr + _index * s_desc_size;
        // 	D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
        // 	cbv_desc.BufferLocation = s_p_d3d_res->GetGPUVirtualAddress() + _offset;
        // 	cbv_desc.SizeInBytes = _size;
        // 	D3DContext::Get()->GetDevice()->CreateConstantBufferView(&cbv_desc, std::get<0>(_allocation.At(0)));
        // 	s_cbuf_views.emplace_back(cbv_desc);
        // }
        s_global_offset += _size;
        s_global_index++;
    }
    D3DConstantBuffer::~D3DConstantBuffer()
    {
        //好像会报错 unmaped，不知道为啥
        //if (s_p_d3d_res)
        //    s_p_d3d_res->Unmap(0, nullptr);
    }

    void D3DConstantBuffer::Bind(CommandBuffer *cmd, u8 bind_slot, bool is_compute_pipeline) const
    {
        if (is_compute_pipeline)
            static_cast<D3DCommandBuffer *>(cmd)->GetCmdList()->SetComputeRootConstantBufferView(bind_slot, _gpu_ptr);
        else
            static_cast<D3DCommandBuffer *>(cmd)->GetCmdList()->SetGraphicsRootConstantBufferView(bind_slot, _gpu_ptr);
    }
    //-----------------------------------------------------------------ConstBuffer---------------------------------------------------------------------

}// namespace Ailu
