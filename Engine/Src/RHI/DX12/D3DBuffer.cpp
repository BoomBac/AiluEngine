#include "RHI/DX12/D3DBuffer.h"
#include "Framework/Math/ALMath.hpp"
#include "RHI/DX12/D3DCommandBuffer.h"
#include "RHI/DX12/D3DContext.h"
#include "RHI/DX12/dxhelper.h"
#include "Render/RenderConstants.h"
#include "Render/RenderingData.h"

#include "pch.h"
using namespace Ailu::Render;

namespace Ailu::RHI::DX12
{
    namespace
    {
        inline String DebugResourceName(const char* prefix, const String& name)
        {
            return name.empty() ? std::format("{}_unnamed", prefix) : std::format("{}_{}", prefix, name);
        }

        inline void NameAndLogResource(ID3D12Resource* resource, const String& name)
        {
            if (resource == nullptr)
                return;
            SetName(resource, ToWChar(name).c_str());
            //LOG_INFO("D3D12 resource created: name={}, ptr={}", name, static_cast<const void*>(resource));
        }

        inline bool IsUploadHeapResource(ID3D12Resource *resource)
        {
            if (resource == nullptr)
                return false;

            D3D12_HEAP_PROPERTIES heap_properties{};
            D3D12_HEAP_FLAGS heap_flags{};
            return SUCCEEDED(resource->GetHeapProperties(&heap_properties, &heap_flags)) &&
                   heap_properties.Type == D3D12_HEAP_TYPE_UPLOAD;
        }

        inline D3D12_SHADER_RESOURCE_VIEW_DESC CreateRawBufferSrvDesc(u64 byte_size)
        {
            AL_ASSERT_MSG((byte_size % sizeof(u32)) == 0u, "Bindless ByteAddressBuffer requires 4-byte aligned size");

            D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
            srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv_desc.Format = DXGI_FORMAT_R32_TYPELESS;
            srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srv_desc.Buffer.FirstElement = 0;
            srv_desc.Buffer.NumElements = static_cast<UINT>(byte_size / sizeof(u32));
            srv_desc.Buffer.StructureByteStride = 0u;
            srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
            return srv_desc;
        }

        inline D3D12_UNORDERED_ACCESS_VIEW_DESC CreateRawBufferUavDesc(u64 byte_size)
        {
            AL_ASSERT_MSG((byte_size % sizeof(u32)) == 0u, "Bindless RWByteAddressBuffer requires 4-byte aligned size");

            D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
            uav_desc.Format = DXGI_FORMAT_R32_TYPELESS;
            uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uav_desc.Buffer.FirstElement = 0;
            uav_desc.Buffer.NumElements = static_cast<UINT>(byte_size / sizeof(u32));
            uav_desc.Buffer.StructureByteStride = 0u;
            uav_desc.Buffer.CounterOffsetInBytes = 0u;
            uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
            return uav_desc;
        }

        inline void CreateBindlessBufferSrv(ID3D12Device* device, ID3D12Resource* resource, u64 byte_size, i32& bindless_srv_index)
        {
            if (device == nullptr || resource == nullptr)
                return;

            auto& desc_mgr = D3DDescriptorMgr::Get();
            bindless_srv_index = static_cast<i32>(desc_mgr.AllocBindlessSRVIndex());
            auto srv_desc = CreateRawBufferSrvDesc(byte_size);
            device->CreateShaderResourceView(resource, &srv_desc, desc_mgr.GetBindlessSRVCpuHandle(bindless_srv_index));
        }

        inline void CreateBindlessBufferUav(ID3D12Device* device, ID3D12Resource* resource, u64 byte_size, i32& bindless_uav_index)
        {
            if (device == nullptr || resource == nullptr)
                return;

            auto& desc_mgr = D3DDescriptorMgr::Get();
            bindless_uav_index = static_cast<i32>(desc_mgr.AllocBindlessUAVIndex());
            auto uav_desc = CreateRawBufferUavDesc(byte_size);
            device->CreateUnorderedAccessView(resource, nullptr, &uav_desc, desc_mgr.GetBindlessUAVCpuHandle(bindless_uav_index));
        }

        inline void ReleaseBindlessSrvIndex(i32& bindless_srv_index)
        {
            if (bindless_srv_index < 0)
                return;

            D3DDescriptorMgr::Get().ReleaseBindlessSRVIndex(static_cast<u32>(bindless_srv_index));
            bindless_srv_index = -1;
        }

        inline void ReleaseBindlessUavIndex(i32& bindless_uav_index)
        {
            if (bindless_uav_index < 0)
                return;

            D3DDescriptorMgr::Get().ReleaseBindlessUAVIndex(static_cast<u32>(bindless_uav_index));
            bindless_uav_index = -1;
        }
    }

//----------------------------------------------------------------D3DGPUBuffer------------------------------------------------------------------------
#pragma region D3DGPUBuffer
    D3DGPUBuffer::D3DGPUBuffer(BufferDesc desc) : GPUBuffer(desc)
    {
        _mem_size = (u32)Math::AlignTo(_desc._size, 16);
    }
    D3DGPUBuffer::~D3DGPUBuffer()
    {
        D3DDescriptorMgr::Get().Free(std::move(_srv_alloc));
        D3DDescriptorMgr::Get().Free(std::move(_uav_alloc));
        ReleaseBindlessSrvIndex(_bindless_srv_index);
        ReleaseBindlessUavIndex(_bindless_uav_index);
    }
    void D3DGPUBuffer::UploadImpl(GraphicsContext *ctx, RHICommandBuffer *rhi_cmd, UploadParams *params)
    {
        GpuResource::UploadImpl(ctx, rhi_cmd, params);
        auto d3d_conetxt = dynamic_cast<D3DContext *>(ctx);
        auto d3dcmd = dynamic_cast<D3DCommandBuffer *>(rhi_cmd);
        auto res_desc = CD3DX12_RESOURCE_DESC::Buffer(_mem_size);
        res_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        if (_desc._is_random_write)
            res_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        if (_desc._target & EGPUBufferTarget::kAppend || _desc._target & EGPUBufferTarget::kIndirectArguments)
            _desc._target = (EGPUBufferTarget)(_desc._target | EGPUBufferTarget::kCounter);
        bool is_with_counter = _desc._target & EGPUBufferTarget::kCounter;
        bool is_structured = _desc._target & EGPUBufferTarget::kStructured || is_with_counter;
        auto p_device = d3d_conetxt->GetDevice();
        if (_desc._target & EGPUBufferTarget::kConstant)
        {
            u64 unaligned_size = _mem_size;
            _mem_size = AlignTo(_mem_size,256);
            auto heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            auto res_desc = CD3DX12_RESOURCE_DESC::Buffer(_mem_size);
            ThrowIfFailed(p_device->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &res_desc,
                                                            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(_p_d3d_res.GetAddressOf())));
            NameAndLogResource(_p_d3d_res.Get(), DebugResourceName("cb", _name));
            _p_d3d_res._subresource_count = 1u;
            _p_d3d_res.ResetStateId();
            _p_d3d_res->Map(0, nullptr, reinterpret_cast<void **>(&_mapped_data));
            _gpu_ptr = _p_d3d_res->GetGPUVirtualAddress();
            if (_data)
                memcpy(_mapped_data, _data, unaligned_size);
        }
        else
        {
            auto heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
            // D3D12 buffers are created in COMMON.  Keep this physical state equal to the RenderGraph
            // lease boundary; all upload and usage transitions are emitted explicitly by the command stream.
            D3D12_RESOURCE_STATES init_state = D3D12_RESOURCE_STATE_COMMON;
            ThrowIfFailed(d3d_conetxt->GetDevice()->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &res_desc, init_state, nullptr, IID_PPV_ARGS(_p_d3d_res.GetAddressOf())));
            NameAndLogResource(_p_d3d_res.Get(), DebugResourceName("buffer", _name));
            _p_d3d_res._subresource_count = 1u;
            _p_d3d_res.ResetStateId();
            if (_data)
            {
                d3dcmd->UploadDataToBuffer(_data, _fill_data_size, _p_d3d_res);
            }
            if (_desc._is_random_write)
            {
                _uav_alloc = D3DDescriptorMgr::Get().AllocGPU(1u);
                if (is_with_counter)
                {
                    // Counter buffer (必须是 4 字节大小的 buffer)
                    D3D12_RESOURCE_DESC counterDesc = res_desc;
                    counterDesc.Width = sizeof(UINT);// 4 bytes
                    counterDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
                    auto default_heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
                    p_device->CreateCommittedResource(
                            &default_heap,
                            D3D12_HEAP_FLAG_NONE,
                            &counterDesc,
                            D3D12_RESOURCE_STATE_COMMON,
                            nullptr,
                            IID_PPV_ARGS(_counter_buffer.GetAddressOf()));
                        NameAndLogResource(_counter_buffer.Get(), DebugResourceName("counter", _name));
                    _counter_buffer._subresource_count = 1u;
                    _counter_buffer.ResetStateId();
                    d3dcmd->UploadDataToBuffer(&_counter, sizeof(u32), _counter_buffer);
                    if (_desc._target & EGPUBufferTarget::kIndirectArguments)
                        d3dcmd->RequireState(_counter_buffer, EResourceState::kIndirectArgument);
                }
                if (_desc._is_create_uav)
                {
                    auto [cpu_handle, gpu_handle] = _uav_alloc.At(0);
                    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
                    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
                    uav_desc.Buffer.FirstElement = 0;
                    if (_desc._target & EGPUBufferTarget::kRaw)
                    {
                        uav_desc.Format = DXGI_FORMAT_R32_TYPELESS;
                        uav_desc.Buffer.NumElements = static_cast<UINT>(_mem_size / sizeof(u32));
                        uav_desc.Buffer.StructureByteStride = 0u;
                        uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
                        uav_desc.Buffer.CounterOffsetInBytes = 0u;
                    }
                    else
                    {
                        uav_desc.Format = is_structured ? DXGI_FORMAT_UNKNOWN : ConvertToDXGIFormat(_desc._format);
                        uav_desc.Buffer.NumElements = _desc._element_num;
                        uav_desc.Buffer.StructureByteStride = is_structured || is_with_counter ? _desc._element_size : 0u;
                        if (is_with_counter)
                            AL_ASSERT(uav_desc.Buffer.StructureByteStride > 0);
                    }
                    p_device->CreateUnorderedAccessView(_p_d3d_res.Get(), is_with_counter ? _counter_buffer.Get() : nullptr, &uav_desc, cpu_handle);
                    _uav_alloc.MarkWritten();
                    ReleaseBindlessUavIndex(_bindless_uav_index);
                    CreateBindlessBufferUav(p_device, _p_d3d_res.Get(), _mem_size, _bindless_uav_index);
                }
            }
        }
        if (_desc._is_create_srv)
        {
            _srv_alloc = D3DDescriptorMgr::Get().AllocGPU(1u);
            auto [cpu_handle, gpu_handle] = _srv_alloc.At(0);
            D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
            srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            const bool is_raytracing_as = _desc._target & EGPUBufferTarget::kRaytraceAS;
            if (is_raytracing_as)
            {
                srv_desc.Format = DXGI_FORMAT_UNKNOWN;
                srv_desc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
                srv_desc.RaytracingAccelerationStructure.Location = _p_d3d_res->GetGPUVirtualAddress();
            }
            else if (_desc._target & EGPUBufferTarget::kRaw)
            {
                srv_desc.Format = DXGI_FORMAT_R32_TYPELESS;
                srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                srv_desc.Buffer.FirstElement = 0;
                srv_desc.Buffer.NumElements = static_cast<UINT>(_mem_size / sizeof(u32));
                srv_desc.Buffer.StructureByteStride = 0u;
                srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
            }
            else
            {
                srv_desc.Format = is_structured ? DXGI_FORMAT_UNKNOWN : ConvertToDXGIFormat(_desc._format);
                srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                srv_desc.Buffer.FirstElement = 0;
                srv_desc.Buffer.NumElements = _desc._element_num;
                srv_desc.Buffer.StructureByteStride = is_structured || is_with_counter ? _desc._element_size : 0u;
                if (is_with_counter)
                    AL_ASSERT(srv_desc.Buffer.StructureByteStride > 0);
            }
            p_device->CreateShaderResourceView(is_raytracing_as? nullptr : _p_d3d_res.Get(), &srv_desc, cpu_handle);
            _srv_alloc.MarkWritten();

            if (!is_raytracing_as)
            {
                ReleaseBindlessSrvIndex(_bindless_srv_index);
                CreateBindlessBufferSrv(p_device, _p_d3d_res.Get(), _mem_size, _bindless_srv_index);
            }
        }
    }
    void D3DGPUBuffer::BindImpl(RHICommandBuffer *rhi_cmd, const BindParams& params)
    {
        GpuResource::BindImpl(rhi_cmd, params);
        auto d3dcmd = dynamic_cast<D3DCommandBuffer *>(rhi_cmd);
        RequireState(rhi_cmd, params._is_random_access ? EResourceState::kUnorderedAccess :
                                                          EResourceState::kAllShaderResource);
        GPUVisibleDescriptorAllocation *alloc = params._is_random_access ? &_uav_alloc : &_srv_alloc;
        auto [ch, gh] = alloc->At(0);
        if (params._is_compute_pipeline)
            alloc->CommitDescriptorsForDispatch(d3dcmd, params._slot);
        else
            alloc->CommitDescriptorsForDraw(d3dcmd, params._slot);
    }
    void D3DGPUBuffer::RequireState(RHICommandBuffer *rhi_cmd, EResourceState state, u32 sub_res)
    {
        auto d3dcmd = dynamic_cast<D3DCommandBuffer *>(rhi_cmd);
        if (d3dcmd == nullptr)
            return;
        if (_p_d3d_res && !IsUploadHeapResource(_p_d3d_res.Get()))
            d3dcmd->RequireState(_p_d3d_res, state, sub_res);
        if (_counter_buffer && (state == EResourceState::kUnorderedAccess ||
                                state == EResourceState::kIndirectArgument))
            d3dcmd->RequireState(_counter_buffer, state, sub_res);
    }

    void D3DGPUBuffer::UavBarrier(RHICommandBuffer *rhi_cmd)
    {
        auto d3dcmd = dynamic_cast<D3DCommandBuffer *>(rhi_cmd);
        d3dcmd->UavBarrier(_p_d3d_res);
        if (_counter_buffer)
            d3dcmd->UavBarrier(_counter_buffer);
    }

    void D3DGPUBuffer::OnDataChanged()
    {
        if (_mapped_data)
        {
            memcpy(_mapped_data, _data, _fill_data_size);
            return;
        }

        // The initial SetData can happen before the asynchronous resource-create command is recorded.
        // UploadImpl will consume the CPU copy once the physical resource exists.
        if (!_p_d3d_res)
            return;
        if (!IsReady())
        {
            LOG_WARNING("D3DGPUBuffer::OnDataChanged: buffer({}) not ready for rendering!",_name);
            return;
        }

        auto cmd = RHICommandBufferPool::Get("ChangeData");
        auto d3dcmd = dynamic_cast<D3DCommandBuffer *>(cmd.get());
        d3dcmd->UploadDataToBuffer(_data, _fill_data_size, _p_d3d_res);
        if (_counter_buffer)
            d3dcmd->UploadDataToBuffer(&_counter, sizeof(u32), _counter_buffer);
        GraphicsContext::Get().ExecuteRHICommandBuffer(cmd.get());
        RHICommandBufferPool::Release(cmd);
    }

    void D3DGPUBuffer::ReadBack(u8 *dst, u32 size)
    {
        GraphicsContext::Get().ReadBack(this, dst, size);
        // if (!_desc._is_readable)
        // {
        //     return;
        // }
        // auto cmd = CommandBufferPool::Get();
        // auto dxcmd = static_cast<D3DCommandBuffer *>(cmd.get())->NativeCmdList();
        // dxcmd->CopyResource(_p_d3d_res_readback.Get(), _p_d3d_res.Get());
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
        //     LogMgr::Get().LogErrorFormat(std::source_location::current(), "{} not a readable resource", _name);
        //     return;
        // }
        // auto cmd = CommandBufferPool::Get();
        // auto dxcmd = static_cast<D3DCommandBuffer *>(cmd.get())->NativeCmdList();
        // dxcmd->CopyResource(_p_d3d_res_readback.Get(), _p_d3d_res.Get());
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
            _p_d3d_res->SetName(ToWChar(name).c_str());
        if (_counter_buffer)
            _counter_buffer->SetName(ToWChar(std::format("{}_counter",_name)).c_str());
    }
    void D3DGPUBuffer::GetCounter(std::function<void(u32)> callback)
    {
        if (!_is_ready_for_rendering)
            return;
        static_cast<D3DContext &>(GraphicsContext::Get()).ReadBackAsync(_counter_buffer, 4u, [=](const u8* data){
            callback(*reinterpret_cast<const u32*>(data));
        });
    }
    void D3DGPUBuffer::SetCounter(u32 counter)
    {
        if (!_is_ready_for_rendering)
            return;
        GPUBuffer::SetCounter(counter);
        auto cmd = RHICommandBufferPool::Get();
        auto d3dcmd = static_cast<D3DCommandBuffer *>(cmd.get());
        d3dcmd->UploadDataToBuffer(&_counter, sizeof(u32), _counter_buffer);
        GraphicsContext::Get().ExecuteRHICommandBuffer(cmd.get());
        RHICommandBufferPool::Release(cmd);
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
        for (auto& bindless_srv_index : _bindless_srv_indices)
            ReleaseBindlessSrvIndex(bindless_srv_index);
    }
    void D3DVertexBuffer::RequireState(RHICommandBuffer *rhi_cmd, EResourceState state, u32 sub_res)
    {
        auto *d3d_cmd = dynamic_cast<D3DCommandBuffer *>(rhi_cmd);
        if (d3d_cmd == nullptr)
            return;
        for (u32 stream = 0u; stream < _vertex_buffers.size(); ++stream)
        {
            if (_vertex_buffers[stream] && !IsUploadHeapResource(_vertex_buffers[stream].Get()))
                d3d_cmd->RequireState(_vertex_buffers[stream], state, sub_res);
        }
    }

    void D3DVertexBuffer::BindImpl(RHICommandBuffer *rhi_cmd, const BindParams &params)
    {
        auto *d3d_cmd = static_cast<D3DCommandBuffer *>(rhi_cmd);
        RequireState(rhi_cmd, EResourceState::kVertexAndConstantBuffer);

        const auto &layout = *params._params._vb_binder._layout;
        const auto &resolved = ResolveLayout(layout);

        if (resolved._is_slot_contiguous)
        {
            std::array<D3D12_VERTEX_BUFFER_VIEW, 30> views;
            for (u8 i = 0u; i < resolved._binding_count; ++i)
                views[i] = _buffer_views[resolved._bindings[i]._stream_index];
            d3d_cmd->SetVertexBuffers(resolved._first_slot, resolved._binding_count, views.data());
            return;
        }

        for (u8 i = 0u; i < resolved._binding_count; ++i)
        {
            const auto &binding = resolved._bindings[i];
            d3d_cmd->SetVertexBuffers(binding._slot, 1u, &_buffer_views[binding._stream_index]);
        }
    }
    
    void D3DVertexBuffer::UploadImpl(GraphicsContext *ctx, RHICommandBuffer *rhi_cmd, UploadParams *params)
    {
        GpuResource::UploadImpl(ctx, rhi_cmd, params);
        auto d3d_ctx = dynamic_cast<D3DContext *>(ctx);
        auto d3d_dev = d3d_ctx->GetDevice();
        for (u16 i = 0; i < _stream_data.size(); i++)
        {
            if (auto *gpu_stream = GetGpuStream(static_cast<u8>(i)); gpu_stream != nullptr)
            {
                auto *d3d_gpu_stream = dynamic_cast<D3DGPUBuffer *>(gpu_stream);
                AL_ASSERT(d3d_gpu_stream != nullptr);
                _vertex_buffers[i] = d3d_gpu_stream->Resource();
                _buffer_views[i].BufferLocation = d3d_gpu_stream->GetGPUVirtualAddress();
                _buffer_views[i].StrideInBytes = _buffer_layout.GetStride(i);
                _buffer_views[i].SizeInBytes = static_cast<u32>(_stream_data[i]._size);
                _buffer_layout_indexer.emplace(_buffer_layout[i]._semantic, static_cast<u8>(i));
                continue;
            }
            auto heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            auto res_desc = CD3DX12_RESOURCE_DESC::Buffer(_stream_data[i]._size);
            u32 stream_index = i;
            if (!_stream_data[i]._is_dynamic)
            {
                if (_stream_data[i]._data == nullptr)
                    continue;
                ComPtr<ID3D12Resource> upload_heap;
                ThrowIfFailed(d3d_dev->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &res_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(upload_heap.GetAddressOf())));
                NameAndLogResource(upload_heap.Get(), std::format("vb_upload_{}_{}", _name.empty() ? "unnamed" : _name, stream_index));
                // Copy the triangle data to update heap
                u8 *pVertexDataBegin;
                CD3DX12_RANGE readRange(0, 0);// We do not intend to read from this resource on the CPU.
                ThrowIfFailed(upload_heap->Map(0, &readRange, reinterpret_cast<void **>(&pVertexDataBegin)));
                memcpy(pVertexDataBegin, _stream_data[i]._data, _stream_data[i]._size);
                upload_heap->Unmap(0, nullptr);
                // Create a Default Heap for the vertex buffer
                _vertex_buffers[stream_index].Reset();
                heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
                ThrowIfFailed(d3d_dev->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &res_desc,
                                                                D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                                IID_PPV_ARGS(_vertex_buffers[stream_index].GetAddressOf())));
                NameAndLogResource(_vertex_buffers[stream_index].Get(), std::format("vb_{}_{}", _name.empty() ? "unnamed" : _name, stream_index));
                auto d3dcmd = dynamic_cast<D3DCommandBuffer *>(rhi_cmd);
                auto cmdlist = d3dcmd->NativeCmdList();
                _vertex_buffers[stream_index].ResetStateId();
                d3dcmd->RequireState(_vertex_buffers[stream_index], EResourceState::kCopyDest);
                cmdlist->CopyBufferRegion(_vertex_buffers[stream_index].Get(), 0, upload_heap.Get(), 0, _stream_data[i]._size);
                d3dcmd->RequireState(_vertex_buffers[stream_index], EResourceState::kGenericRead);
                d3d_ctx->TrackResource(upload_heap);
            }
            else
            {
                // Keep the old resource alive until the initial CPU data has been copied.  Skinning
                // meshes use dynamic position/normal streams, but those streams still need their
                // bind-pose data before the first animation update.
                u8 *initial_data = _stream_data[stream_index]._data;
                ComPtr<ID3D12Resource> dynamic_buffer;
                ThrowIfFailed(d3d_dev->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &res_desc,
                                                                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                                IID_PPV_ARGS(dynamic_buffer.GetAddressOf())));
                NameAndLogResource(dynamic_buffer.Get(), std::format("vb_dynamic_{}_{}",
                                                                       _name.empty() ? "unnamed" : _name,
                                                                       stream_index));
                u8 *mapped_data = nullptr;
                ThrowIfFailed(dynamic_buffer->Map(0, nullptr, reinterpret_cast<void **>(&mapped_data)));
                if (initial_data != nullptr)
                    memcpy(mapped_data, initial_data, _stream_data[stream_index]._size);
                _vertex_buffers[stream_index] = D3DResource(std::move(dynamic_buffer), 1u);
                _stream_data[stream_index]._data = mapped_data;
            }
            // Initialize the vertex buffer view.
            _buffer_views[stream_index].BufferLocation = _vertex_buffers[stream_index]->GetGPUVirtualAddress();
            _buffer_views[stream_index].StrideInBytes = _buffer_layout.GetStride(stream_index);
            _buffer_views[stream_index].SizeInBytes = (u32) _stream_data[i]._size;
            _buffer_layout_indexer.emplace(_buffer_layout[stream_index]._semantic, static_cast<u8>(stream_index));

            if (_bindless_srv_enabled)
            {
                ReleaseBindlessSrvIndex(_bindless_srv_indices[stream_index]);
                CreateBindlessBufferSrv(d3d_dev, _vertex_buffers[stream_index].Get(), _stream_data[i]._size,
                                        _bindless_srv_indices[stream_index]);
            }
        }
        ++_view_version;
    }

    void D3DVertexBuffer::Name(const String &name)
    {
        Object::Name(name);
        for (auto &it: _buffer_layout_indexer)
        {
            if (_vertex_buffers[it.second])
                _vertex_buffers[it.second]->SetName(ToWChar(std::format("vb_{}_{}", name, it.second).c_str()).c_str());
        }
    }

#pragma endregion

#pragma region D3DIndexBuffer
    //-----------------------------------------------------------------IndexBuffer---------------------------------------------------------------------
    D3DIndexBuffer::D3DIndexBuffer(u32 *indices, u32 count, bool is_dynamic)
        : IndexBuffer(indices, count, is_dynamic)
    {
    }
    D3DIndexBuffer::~D3DIndexBuffer()
    {
        ReleaseBindlessSrvIndex(_bindless_srv_index);
    }

    void D3DIndexBuffer::UploadImpl(GraphicsContext *ctx, RHICommandBuffer *rhi_cmd, UploadParams *params)
    {
        GpuResource::UploadImpl(ctx, rhi_cmd, params);
        auto d3d_conetxt = dynamic_cast<D3DContext *>(ctx);
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
                    IID_PPV_ARGS(_index_buf.GetAddressOf())));
                NameAndLogResource(_index_buf.Get(), DebugResourceName("ib", _name));

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
            NameAndLogResource(_index_buf.Get(), DebugResourceName("ib_dynamic", _name));
            _index_buf->Map(0, nullptr, reinterpret_cast<void **>(&_data));
        }
        _index_buf._subresource_count = 1u;
        _index_buf.ResetStateId();
        // Initialize the vertex buffer view.
        _index_buf_view.BufferLocation = _index_buf->GetGPUVirtualAddress();
        _index_buf_view.Format = DXGI_FORMAT_R32_UINT;
        _index_buf_view.SizeInBytes = static_cast<u32>(_mem_size);

        if (_bindless_srv_enabled)
        {
            ReleaseBindlessSrvIndex(_bindless_srv_index);
            CreateBindlessBufferSrv(d3d_conetxt->GetDevice(), _index_buf.Get(), _mem_size, _bindless_srv_index);
        }
        ++_view_version;
    }
    void D3DIndexBuffer::BindImpl(RHICommandBuffer *rhi_cmd, const BindParams& params)
    {
        GpuResource::BindImpl(rhi_cmd, params);
        auto d3dcmd = dynamic_cast<D3DCommandBuffer *>(rhi_cmd);
        RequireState(rhi_cmd, EResourceState::kIndexBuffer);
        d3dcmd->SetIndexBuffer(_index_buf_view);
    }

    void D3DIndexBuffer::RequireState(RHICommandBuffer *rhi_cmd, EResourceState state, u32 sub_res)
    {
        if (_index_buf && !IsUploadHeapResource(_index_buf.Get()))
            static_cast<D3DCommandBuffer *>(rhi_cmd)->RequireState(_index_buf, state, sub_res);
    }

    void D3DIndexBuffer::Name(const String &name)
    {
        if (_index_buf)
            _index_buf->SetName(ToWChar(std::format("ib_{}", name).c_str()).c_str());
    }

    void D3DIndexBuffer::Resize(u32 new_size)
    {
        if (_capacity == new_size || !_is_dynamic)
            return;
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

    D3DConstantBuffer::D3DConstantBuffer(u32 size)
    {
        size = (u32) AlignTo(size, 256);
        _alloc = GpuResourceManager::Get()->Allocate(size);
        _mem_size = size;
        _data = reinterpret_cast<u8 *>(_alloc._cpu_ptr);
        _is_ready_for_rendering = true;
    }
    D3DConstantBuffer::~D3DConstantBuffer()
    {
        GpuResourceManager::Get()->Free(std::move(_alloc));
    }
    void D3DConstantBuffer::BindImpl(RHICommandBuffer *rhi_cmd, const BindParams& params)
    {
        auto d3dcmd = dynamic_cast<D3DCommandBuffer *>(rhi_cmd);
        auto cmd = d3dcmd->NativeCmdList();
        if (params._is_compute_pipeline)
            cmd->SetComputeRootConstantBufferView(params._slot, _alloc._gpu_ptr);
        else
            d3dcmd->SetGraphicsRootConstantBufferView(params._slot, _alloc._gpu_ptr);
    }
    void D3DConstantBuffer::Reset()
    {
        memset(_alloc._cpu_ptr, 0, _alloc._size);
    }
#pragma endregion
    //-----------------------------------------------------------------ConstBuffer---------------------------------------------------------------------

}// namespace Ailu
