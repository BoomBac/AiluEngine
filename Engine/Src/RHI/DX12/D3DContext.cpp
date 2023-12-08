#include "pch.h"
#include "Ext/imgui/backends/imgui_impl_dx12.h"
#include "Framework/Common/Log.h"
#include "Render/RenderingData.h"
#include "Render/Gizmo.h"
#include "RHI/DX12/D3DContext.h"
#include "RHI/DX12/dxhelper.h"




namespace Ailu
{
    static void GetHardwareAdapter(IDXGIFactory6* pFactory, IDXGIAdapter4** ppAdapter)
    {
        IDXGIAdapter* pAdapter = nullptr;
        IDXGIAdapter4* pAdapter4 = nullptr;
        *ppAdapter = nullptr;
        for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != pFactory->EnumAdapters(adapterIndex, &pAdapter); adapterIndex++)
        {
            DXGI_ADAPTER_DESC3 desc{};
            if (SUCCEEDED(pAdapter->QueryInterface(IID_PPV_ARGS(&pAdapter4))))
            {
                pAdapter4->GetDesc3(&desc);
                LOG_INFO(desc.Description);
            }
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                // Don't select the Basic Render Driver adapter.
                continue;
            }
            // Check to see if the adapter supports Direct3D 12, but don't create the
            // actual device yet.
            if (SUCCEEDED(D3D12CreateDevice(pAdapter4, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)))
            {
                break;
            }
        }
        *ppAdapter = pAdapter4;
    }

    static uint8_t* GetPerRenderObjectCbufferPtr(uint8_t* begin, uint32_t object_index)
    {
        return begin + RenderConstants::kPerFrameDataSize + RenderConstants::kPerMaterialDataSize * RenderConstants::kMaxMaterialDataCount + object_index * RenderConstants::kPeObjectDataSize;
    }


    D3DContext::D3DContext(WinWindow* window) : _window(window),_width(window->GetWidth()),_height(window->GetHeight())
    {
        m_aspectRatio = (float)_width / (float)_height;
    }

    D3DContext::~D3DContext()
    {
        Destroy();
    }

    void D3DContext::Init()
    {
        s_p_d3dcontext = this;
        LoadPipeline();
        LoadAssets();
        //init imgui
        {
            D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle{};
            gpu_handle.ptr = m_cbvHeap->GetGPUDescriptorHandleForHeapStart().ptr + _cbv_desc_size * _cbv_desc_num;
            D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle{};
            cpu_handle.ptr = m_cbvHeap->GetCPUDescriptorHandleForHeapStart().ptr + _cbv_desc_size * _cbv_desc_num;
            auto ret = ImGui_ImplDX12_Init(m_device.Get(), RenderConstants::kFrameCount,
                DXGI_FORMAT_R8G8B8A8_UNORM, m_cbvHeap.Get(), cpu_handle, gpu_handle);
        }
        Gizmo::Init();
        m_commandList->Close();
        ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
        m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
        WaitForGpu();
    }

    ID3D12Device* D3DContext::GetDevice()
    {
        AL_ASSERT(m_device == nullptr,"Global D3D device hasn't been init!")
        return m_device.Get();
    }

    ID3D12GraphicsCommandList* D3DContext::GetCmdList()
    {
        AL_ASSERT(m_commandList == nullptr, "Global D3D cmdlist hasn't been init!")
        return m_commandList.Get();
    }

    ID3D12GraphicsCommandList* D3DContext::GetTaskCmdList()
    {
        _b_has_task = true;
        return _task_cmd.Get();
    }

    ComPtr<ID3D12DescriptorHeap> D3DContext::GetDescriptorHeap()
    {
        return m_cbvHeap;
    }

    std::tuple<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> D3DContext::GetSRVDescriptorHandle()
    {
        static u32 global_texture_offset = 0u;
        static uint32_t base = RenderConstants::kFrameCount + RenderConstants::kMaxMaterialDataCount * RenderConstants::kFrameCount +
            RenderConstants::kMaxRenderObjectCount * RenderConstants::kFrameCount;
        auto gpu_handle = m_cbvHeap->GetGPUDescriptorHandleForHeapStart();
        gpu_handle.ptr += _cbv_desc_size * (base + global_texture_offset);
        auto cpu_handle = m_cbvHeap->GetCPUDescriptorHandleForHeapStart();
        cpu_handle.ptr += _cbv_desc_size * (base + global_texture_offset);
        ++global_texture_offset;
        return std::make_tuple(cpu_handle, gpu_handle);
    }

    std::tuple<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> D3DContext::GetRTVDescriptorHandle()
    {
        static u32 global_texture_offset = 0u;
        static uint32_t base = RenderConstants::kFrameCount;
        auto gpu_handle = m_rtvHeap->GetGPUDescriptorHandleForHeapStart();
        gpu_handle.ptr += _rtv_desc_size * (base + global_texture_offset);
        auto cpu_handle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        cpu_handle.ptr += _rtv_desc_size * (base + global_texture_offset);
        ++global_texture_offset;
        return std::make_tuple(cpu_handle, gpu_handle);
    }

    std::tuple<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> D3DContext::GetDSVDescriptorHandle()
    {
        static u32 global_texture_offset = 0u;
        static uint32_t base = RenderConstants::kFrameCount;
        auto gpu_handle = m_dsvHeap->GetGPUDescriptorHandleForHeapStart();
        gpu_handle.ptr += _dsv_desc_size * (base + global_texture_offset);
        auto cpu_handle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        cpu_handle.ptr += _dsv_desc_size * (base + global_texture_offset);
        ++global_texture_offset;
        return std::make_tuple(cpu_handle, gpu_handle);
    }

    uint8_t* D3DContext::GetCBufferPtr()
    {
        return _p_cbuffer;
    }

    uint8_t* D3DContext::GetPerFrameCbufData()
    {
        return _p_cbuffer;
    }

    uint8_t* D3DContext::GetPerMaterialCbufData(uint32_t mat_index)
    {
        return _p_cbuffer;
    }

    D3D12_CONSTANT_BUFFER_VIEW_DESC* D3DContext::GetPerFrameCbufGPURes()
    {
        return &_cbuf_views[0];
    }

    ScenePerFrameData* D3DContext::GetPerFrameCbufDataStruct()
    {
        return &_perframe_scene_data;
    }

    void D3DContext::ExecuteCommandBuffer(Ref<D3DCommandBuffer>& cmd)
    {    
        for (auto& call : cmd->_commands)
        {
            _all_commands.emplace_back(std::move(call));
        }
    }

    void D3DContext::DrawIndexedInstanced(uint32_t index_count, uint32_t instance_count, const Matrix4x4f& transform)
    {
        //TODO:默认将每个物体的cbuf视作绑定在 0 槽位上，当着色器没有PerObjectBuf时，PerFrameBuf会在0槽位上，此时这里会把PSO绑定的结果覆盖
        ++RenderingStates::s_draw_call;
        m_commandList->SetGraphicsRootConstantBufferView(0, GetCBufferViewDesc(1 + RenderConstants::kMaxMaterialDataCount + _render_object_index).BufferLocation);
        //m_commandList->SetGraphicsRootDescriptorTable(0, GetCBVGPUDescHandle(1 + D3DConstants::kMaxMaterialDataCount + _render_object_index));
        memcpy(_p_cbuffer + RenderConstants::kPerFrameDataSize + RenderConstants::kPerMaterialDataSize * RenderConstants::kMaxMaterialDataCount + RenderConstants::kPeObjectDataSize * (_render_object_index++),
            &transform, sizeof(transform));
        m_commandList->DrawIndexedInstanced(index_count, instance_count, 0, 0, 0);
    }

    void D3DContext::DrawInstanced(uint32_t vertex_count, uint32_t instance_count, const Matrix4x4f& transform)
    {
        ++RenderingStates::s_draw_call;
        m_commandList->SetGraphicsRootConstantBufferView(0, GetCBufferViewDesc(1 + RenderConstants::kMaxMaterialDataCount + _render_object_index).BufferLocation);
        //m_commandList->SetGraphicsRootDescriptorTable(0, GetCBVGPUDescHandle(1 + D3DConstants::kMaxMaterialDataCount + _render_object_index));
        memcpy(_p_cbuffer + RenderConstants::kPerFrameDataSize + RenderConstants::kPerMaterialDataSize * RenderConstants::kMaxMaterialDataCount + RenderConstants::kPeObjectDataSize * (_render_object_index++),
            &transform, sizeof(transform));
        m_commandList->DrawInstanced(vertex_count, instance_count, 0, 0);
    }

    void D3DContext::DrawInstanced(uint32_t vertex_count, uint32_t instance_count)
    {
        ++RenderingStates::s_draw_call;
        m_commandList->DrawInstanced(vertex_count, instance_count, 0, 0);
    }

    void D3DContext::Clear(Vector4f color, float depth, bool clear_color, bool clear_depth)
    {
        if (clear_color)
        {
            CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, _rtv_desc_size);
            m_commandList->ClearRenderTargetView(rtvHandle, color, 0, nullptr);
        }
        if (clear_depth)
        {
            CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, _dsv_desc_size);
            m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, depth, 0, 0, nullptr);
        }
    }

    void D3DContext::Destroy()
    {
        // Ensure that the GPU is no longer referencing resources that are about to be
// cleaned up by the destructor.
        WaitForGpu();

        CloseHandle(m_fenceEvent);
    }

    void D3DContext::LoadPipeline()
    {
        UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
        // Enable the debug layer (requires the Graphics Tools "optional feature").
        // NOTE: Enabling the debug layer after device creation will invalidate the active device.
        {
            ComPtr<ID3D12Debug> debugController;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
            {
                debugController->EnableDebugLayer();

                // Enable additional debug layers.
                dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
            }
        }
#endif
        ComPtr<IDXGIFactory6> factory;
        ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));
        ComPtr<IDXGIAdapter4> hardwareAdapter;
        GetHardwareAdapter(factory.Get(), &hardwareAdapter);
        ThrowIfFailed(D3D12CreateDevice(hardwareAdapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&m_device)));
        // Describe and create the command queue.
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));
        // Describe and create the swap chain.
        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
        swapChainDesc.BufferCount = RenderConstants::kFrameCount;
        swapChainDesc.Width = _width;
        swapChainDesc.Height = _height;
        swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        auto hwnd = static_cast<HWND>(_window->GetNativeWindowPtr());
        ComPtr<IDXGISwapChain1> swapChain;
        ThrowIfFailed(factory->CreateSwapChainForHwnd(m_commandQueue.Get(),hwnd,&swapChainDesc,nullptr,nullptr,&swapChain));
        // This sample does not support fullscreen transitions.
        ThrowIfFailed(factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER));
        ThrowIfFailed(swapChain->SetFullscreenState(FALSE, nullptr));
        ThrowIfFailed(swapChain.As(&m_swapChain));

        m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

        CreateDescriptorHeap();

        // Create frame resources.
        {
            CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

            // Create a RTV for each frame.
            for (UINT n = 0; n < RenderConstants::kFrameCount; n++)
            {
                ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&_color_buffer[n])));
                m_device->CreateRenderTargetView(_color_buffer[n].Get(), nullptr, rtvHandle);
                rtvHandle.Offset(1, _rtv_desc_size);
                ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[n])));
            }
        }
        CreateDepthStencilTarget();
        // Create the command list.
        ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[m_frameIndex].Get(), nullptr, IID_PPV_ARGS(&m_commandList)));
        //Create task cmd
        {
            ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(_task_cmd_alloc.GetAddressOf())));
            ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _task_cmd_alloc.Get(), nullptr, IID_PPV_ARGS(&_task_cmd)));
        }
        // Command lists are created in the recording state, but there is nothing
        // to record yet. The main loop expects it to be closed, so close it now.
        //ThrowIfFailed(m_commandList->Close());
    }


    void D3DContext::LoadAssets()
    {
        GraphicsPipelineStateMgr::BuildPSOCache();
 
        // Create the vertex buffer.
        {
            float size = 0.5f;
            Vector3f origin_pos = { 0.f,0.f,0.f };
            Vector3f testMesh[8]{ 
                origin_pos + Vector3f{size, size, size},
                origin_pos + Vector3f{size, size, -size},
                origin_pos + Vector3f{-size, size, -size},
                origin_pos + Vector3f{-size, size, size},
                origin_pos + Vector3f{size, -size, size},
                origin_pos + Vector3f{size, -size, -size},
                origin_pos + Vector3f{-size, -size, -size},
                origin_pos + Vector3f{-size, -size, size} 
            };
            origin_pos.y = 5.0f;
            Vector3f testMesh0[8]{
                origin_pos + Vector3f{size, size, size},
                origin_pos + Vector3f{size, size, -size},
                origin_pos + Vector3f{-size, size, -size},
                origin_pos + Vector3f{-size, size, size},
                origin_pos + Vector3f{size, -size, size},
                origin_pos + Vector3f{size, -size, -size},
                origin_pos + Vector3f{-size, -size, -size},
                origin_pos + Vector3f{-size, -size, size}
            };
            //SimpleVertex testMesh[8]{
            //    {{size, size, size}   ,{1,1,0,1}},
            //    {{size, size, -size}  ,{1,1,0,1}},
            //    {{-size, size, -size} ,{1,1,0,1}},
            //    {{-size, size, size}  ,{1,1,0,1}},
            //    {{size, -size, size}  ,{1,0,0,1}},
            //    {{size, -size, -size} ,{1,0,0,1}},
            //    {{-size, -size, -size},{1,0,0,1}},
            //    {{-size, -size, size} ,{1,0,0,1}}
            //};
            Vector4f color[8]{
            {1,1,0,1},
            {1,1,0,1},
            {1,1,0,1},
            {1,1,0,1},
            {1,0,0,1},
            {1,0,0,1},
            {1,0,0,1},
            {1,0,0,1}
            };

            uint32_t indices[36]{
                3,0,1,
                3,1,2,//top
                1,0,4,
                1,4,5,//right
                2,1,5,
                2,5,6,//front
                6,5,4,
                6,4,7,//bot
                3,2,6,
                3,6,7,//left
                0,3,7,
                7,4,0//back
            };
            //_p_vertex_buf.reset(VertexBuffer::Create({
            //    {"POSITION",EShaderDateType::kFloat3,0},
            //    }));
            //_p_vertex_buf0.reset(VertexBuffer::Create({
            //    {"POSITION",EShaderDateType::kFloat3,0},
            //    }));
            //_p_vertex_buf->SetStream(reinterpret_cast<float*>(testMesh), sizeof(testMesh), 0);
            ////_p_vertex_buf->SetStream(reinterpret_cast<float*>(color), sizeof(color), 1);

            //_p_vertex_buf0->SetStream(reinterpret_cast<float*>(testMesh0), sizeof(testMesh0), 0);
            ////_p_vertex_buf0->SetStream(reinterpret_cast<float*>(color), sizeof(color), 1);
            //_p_index_buf.reset(IndexBuffer::Create(indices, 36));
        }

        // Create synchronization objects.
        {
            ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
            m_fenceValues[m_frameIndex]++;
            m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            if (m_fenceEvent == nullptr)
            {
                ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
            }
            WaitForGpu();
        }
    }

    void D3DContext::Present()
    {
        PopulateCommandList();
        _task_cmd->Close();
        if (_b_has_task)
        {
            ID3D12CommandList* ppCommandLists[] = { _task_cmd.Get(),m_commandList.Get() };
            m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
        }
        else
        {
            ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
            m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
        }
        ImGuiIO& io = ImGui::GetIO();
        // Update and Render additional Platform Windows
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault(nullptr, (void*)m_commandList.Get());
        }
        // Present the frame.
        ThrowIfFailed(m_swapChain->Present(1, 0));
        WaitForGpu();
        MoveToNextFrame();

        _b_has_task = false;
        ThrowIfFailed(_task_cmd_alloc->Reset());
        ThrowIfFailed(_task_cmd->Reset(_task_cmd_alloc.Get(), nullptr));
    }

    void D3DContext::PopulateCommandList()
    {
        // Command list allocators can only be reset when the associated 
        // command lists have finished execution on the GPU; apps should use 
        // fences to determine GPU execution progress.
        ThrowIfFailed(m_commandAllocators[m_frameIndex]->Reset());
        // However, when ExecuteCommandList() is called on a particular command 
        // list, that command list can then be reset at any time and must be before 
        // re-recording.
        ThrowIfFailed(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), nullptr));
        ID3D12DescriptorHeap* ppHeaps[] = { m_cbvHeap.Get() };
        m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
        auto bar_before = CD3DX12_RESOURCE_BARRIER::Transition(_color_buffer[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_commandList->ResourceBarrier(1, &bar_before);

        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, _rtv_desc_size);
        CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, _dsv_desc_size);
        m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
        _render_object_index = 0;

        //执行所有命令
        for (auto& cmd : _all_commands) cmd();
        _all_commands.clear();

        //DrawGizmo
        GraphicsPipelineStateMgr::s_gizmo_pso->Bind();
        GraphicsPipelineStateMgr::s_gizmo_pso->SubmitBindResource(&_cbuf_views[0], EBindResDescType::kConstBuffer);
        Gizmo::Submit();

        //imgui draw
        {
            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_commandList.Get());
        }
        // Indicate that the back buffer will now be used to present.
        auto bar_after = CD3DX12_RESOURCE_BARRIER::Transition(_color_buffer[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        m_commandList->ResourceBarrier(1, &bar_after);

        ThrowIfFailed(m_commandList->Close());
    }

    void D3DContext::WaitForGpu()
    {
        // Schedule a Signal command in the queue.
        ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), m_fenceValues[m_frameIndex]));

        // Wait until the fence has been processed.
        ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
        WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);

        // Increment the fence value for the current frame.
        m_fenceValues[m_frameIndex]++;
    }

    void D3DContext::WaitForTask()
    {
    }

    void D3DContext::MoveToNextFrame()
    {
        // Schedule a Signal command in the queue.
        const UINT64 currentFenceValue = m_fenceValues[m_frameIndex];
        ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), currentFenceValue));

        // Update the frame index.
        m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

        // If the next frame is not ready to be rendered yet, wait until it is ready.
        if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex])
        {
            ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
            WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
        }

        // Set the fence value for the next frame.
        m_fenceValues[m_frameIndex] = currentFenceValue + 1;
    }

    void D3DContext::InitCBVSRVUAVDescHeap()
    {
        auto device = D3DContext::GetInstance()->GetDevice();
        //constbuffer desc heap
        D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc{};
        _cbv_desc_num = (1u + RenderConstants::kMaxMaterialDataCount + RenderConstants::kMaxRenderObjectCount) * RenderConstants::kFrameCount +
            RenderConstants::kMaxTextureCount;
        cbvHeapDesc.NumDescriptors = _cbv_desc_num + 1;
        cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        ThrowIfFailed(device->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&m_cbvHeap)));
        _cbv_desc_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        _cbuf_views.reserve(cbvHeapDesc.NumDescriptors);
        //constbuffer
        auto heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto res_desc = CD3DX12_RESOURCE_DESC::Buffer(RenderConstants::kPerFrameTotalSize * RenderConstants::kFrameCount);
        ThrowIfFailed(device->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &res_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_constantBuffer)));
        // Describe and create a constant buffer view.
        for (uint32_t i = 0; i < RenderConstants::kFrameCount; i++)
        {
            D3D12_CPU_DESCRIPTOR_HANDLE cbvHandle;
            cbvHandle.ptr = m_cbvHeap->GetCPUDescriptorHandleForHeapStart().ptr + i * (1 + RenderConstants::kMaxMaterialDataCount + RenderConstants::kMaxRenderObjectCount) * _cbv_desc_size;
            D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
            cbv_desc.BufferLocation = m_constantBuffer->GetGPUVirtualAddress() + i * RenderConstants::kPerFrameTotalSize;
            cbv_desc.SizeInBytes = RenderConstants::kPerFrameDataSize;
            device->CreateConstantBufferView(&cbv_desc, cbvHandle);
            _cbuf_views.emplace_back(cbv_desc);
            for (uint32_t j = 0; j < RenderConstants::kMaxMaterialDataCount; j++)
            {
                D3D12_CPU_DESCRIPTOR_HANDLE cbvHandle2;
                cbvHandle2.ptr = cbvHandle.ptr + (j + 1) * _cbv_desc_size;
                cbv_desc.BufferLocation = m_constantBuffer->GetGPUVirtualAddress() + i * RenderConstants::kPerFrameTotalSize + RenderConstants::kPerFrameDataSize + j * RenderConstants::kPerMaterialDataSize;
                cbv_desc.SizeInBytes = RenderConstants::kPerMaterialDataSize;
                device->CreateConstantBufferView(&cbv_desc, cbvHandle2);
                _cbuf_views.emplace_back(cbv_desc);
            }
            for (uint32_t k = 0; k < RenderConstants::kMaxRenderObjectCount; k++)
            {
                D3D12_CPU_DESCRIPTOR_HANDLE cbvHandle2;
                cbvHandle2.ptr = cbvHandle.ptr + (1 + RenderConstants::kMaxMaterialDataCount + k) * _cbv_desc_size;
                cbv_desc.BufferLocation = m_constantBuffer->GetGPUVirtualAddress() + RenderConstants::kPerFrameTotalSize * i + RenderConstants::kPerFrameDataSize +
                    RenderConstants::kMaxMaterialDataCount * RenderConstants::kPerMaterialDataSize + k * RenderConstants::kPeObjectDataSize;
                cbv_desc.SizeInBytes = RenderConstants::kPeObjectDataSize;
                device->CreateConstantBufferView(&cbv_desc, cbvHandle2);
                _cbuf_views.emplace_back(cbv_desc);
            }
        }
        // Map and initialize the constant buffer. We don't unmap this until the
        // app closes. Keeping things mapped for the lifetime of the resource is okay.
        CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
        ThrowIfFailed(m_constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&_p_cbuffer)));
    }

    void D3DContext::CreateDepthStencilTarget()
    {
        D3D12_HEAP_PROPERTIES heapProperties = {};
        heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC depthDesc = {};
        depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depthDesc.Width = _width;
        depthDesc.Height = _height;
        depthDesc.Format = DXGI_FORMAT_D32_FLOAT; // 选择深度缓冲格式
        depthDesc.MipLevels = 1;
        depthDesc.DepthOrArraySize = 1;
        depthDesc.SampleDesc.Count = 1; // 非多重采样
        depthDesc.SampleDesc.Quality = 0;
        depthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE depthClearValue = {};
        depthClearValue.Format = DXGI_FORMAT_D32_FLOAT;
        depthClearValue.DepthStencil.Depth = 1.0f;
        depthClearValue.DepthStencil.Stencil = 0;

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = DXGI_FORMAT_D32_FLOAT; // 与深度缓冲资源的格式匹配
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Texture2D.MipSlice = 0;
        CD3DX12_CPU_DESCRIPTOR_HANDLE dsv_handle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
        for (size_t i = 0; i < RenderConstants::kFrameCount; i++)
        {
            ThrowIfFailed(m_device->CreateCommittedResource(
                &heapProperties,
                D3D12_HEAP_FLAG_NONE,
                &depthDesc,
                D3D12_RESOURCE_STATE_DEPTH_WRITE,
                &depthClearValue,
                IID_PPV_ARGS(&_depth_buffer[i])
            ));
            m_device->CreateDepthStencilView(_depth_buffer[i].Get(), &dsvDesc, dsv_handle);
            dsv_handle.Offset(1,_dsv_desc_size);
        }
    }

    void D3DContext::CreateDescriptorHeap()
    {
        // Describe and create a render target view (RTV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = RenderConstants::kFrameCount;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));
        _rtv_desc_size = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
        dsvHeapDesc.NumDescriptors = RenderConstants::kFrameCount;
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(m_dsvHeap.GetAddressOf())));
        _dsv_desc_size = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

        InitCBVSRVUAVDescHeap();
    }

    D3D12_GPU_DESCRIPTOR_HANDLE D3DContext::GetCBVGPUDescHandle(uint32_t index) const
    {
        D3D12_GPU_DESCRIPTOR_HANDLE handle{};
        handle.ptr = m_cbvHeap->GetGPUDescriptorHandleForHeapStart().ptr + _cbv_desc_size * index;
        return handle;
    }

    const D3D12_CONSTANT_BUFFER_VIEW_DESC& D3DContext::GetCBufferViewDesc(uint32_t index) const
    {
        return _cbuf_views[index];
    }

}