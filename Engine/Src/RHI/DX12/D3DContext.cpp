#include "pch.h"
#include <d3dcompiler.h>

#include "Ext/imgui/backends/imgui_impl_dx12.h"
#include "RHI/DX12/D3DContext.h"
#include "RHI/DX12/dxhelper.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/Application.h"
#include "RHI/DX12/D3DBuffer.h"
#include "Render/RenderCommand.h"
#include "RHI/DX12/D3DShader.h"
#include "RHI/DX12/D3DGraphicsPipelineState.h"
#include "Framework/Parser/AssetParser.h"
#include "RHI/DX12/D3DCommandBuffer.h"
#include "Render/RenderingData.h"
#include "Objects/TransformComponent.h"



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
        return begin + D3DConstants::kPerFrameDataSize + D3DConstants::kPerMaterialDataSize * D3DConstants::kMaxMaterialDataCount + object_index * D3DConstants::kPeObjectDataSize;
    }

    static std::vector<UINT8> GenerateTextureData()
    {
        UINT TextureWidth = 256, TextureHeight = 256,TexturePixelSize = 4;
        const UINT rowPitch = TextureWidth * TexturePixelSize;
        const UINT cellPitch = rowPitch >> 3;        // The width of a cell in the checkboard texture.
        const UINT cellHeight = TextureWidth >> 3;    // The height of a cell in the checkerboard texture.
        const UINT textureSize = rowPitch * TextureHeight;

        std::vector<UINT8> data(textureSize);
        UINT8* pData = &data[0];

        for (UINT n = 0; n < textureSize; n += TexturePixelSize)
        {
            UINT x = n % rowPitch;
            UINT y = n / rowPitch;
            UINT i = x / cellPitch;
            UINT j = y / cellHeight;

            if (i % 2 == j % 2)
            {
                pData[n] = 0x00;        // R
                pData[n + 1] = 0x00;    // G
                pData[n + 2] = 0x00;    // B
                pData[n + 3] = 0xff;    // A
            }
            else
            {
                pData[n] = 0xff;        // R
                pData[n + 1] = 0xff;    // G
                pData[n + 2] = 0xff;    // B
                pData[n + 3] = 0xff;    // A
            }
        }

        return data;
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
        Gizmo::Init();
        //init imgui
        D3D12_DESCRIPTOR_HEAP_DESC SrvHeapDesc;
        SrvHeapDesc.NumDescriptors = 1;
        SrvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        SrvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        SrvHeapDesc.NodeMask = 0;
        ThrowIfFailed(m_device->CreateDescriptorHeap(
            &SrvHeapDesc, IID_PPV_ARGS(&g_pd3dSrvDescHeapImGui)));

        auto ret = ImGui_ImplDX12_Init(m_device.Get(), D3DConstants::kFrameCount,
            DXGI_FORMAT_R8G8B8A8_UNORM, g_pd3dSrvDescHeapImGui,
            g_pd3dSrvDescHeapImGui->GetCPUDescriptorHandleForHeapStart(),
            g_pd3dSrvDescHeapImGui->GetGPUDescriptorHandleForHeapStart());

        //scene data
        _p_scene_camera = std::make_unique<Camera>(16.0F / 9.0F);
        _p_scene_camera->SetPosition(0, 0, -5.0f);
        _p_scene_camera->SetLens(1.57f, 16.f / 9.f, 1.f, 5000.f);
        Camera::sCurrent = _p_scene_camera.get();

        m_commandList->Close();
        ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
        m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
        WaitForGpu();
    }

    void D3DContext::Present()
    {
        // Record all the commands we need to render the scene into the command list.
        PopulateCommandList();

        // Execute the command list.
        ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
        m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
        ImGuiIO& io = ImGui::GetIO();
        // Update and Render additional Platform Windows
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault(nullptr, (void*)m_commandList.Get());
        }
        // Present the frame.
       ThrowIfFailed(m_swapChain->Present(1, 0));
        //hrowIfFailed(m_swapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING));
        WaitForGpu();
        MoveToNextFrame();
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

    ComPtr<ID3D12DescriptorHeap> D3DContext::GetDescriptorHeap()
    {
        return m_cbvHeap;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE& D3DContext::GetSRVCPUDescriptorHandle(uint32_t index)
    {
        static uint32_t base = D3DConstants::kFrameCount + D3DConstants::kMaxMaterialDataCount * D3DConstants::kFrameCount + 
            D3DConstants::kMaxRenderObjectCount * D3DConstants::kFrameCount;
        //CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
        auto cpu_handle = m_cbvHeap->GetCPUDescriptorHandleForHeapStart();
        cpu_handle.ptr += _cbv_desc_size * (base + index);
        return cpu_handle;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE& D3DContext::GetSRVGPUDescriptorHandle(uint32_t index)
    {
        static uint32_t base = D3DConstants::kFrameCount + D3DConstants::kMaxMaterialDataCount * D3DConstants::kFrameCount +
            D3DConstants::kMaxRenderObjectCount * D3DConstants::kFrameCount;
        auto cpu_handle = m_cbvHeap->GetGPUDescriptorHandleForHeapStart();
        cpu_handle.ptr += _cbv_desc_size * (base + index);
        return cpu_handle;
    }

    uint8_t* D3DContext::GetCBufferPtr()
    {
        return _p_cbuffer;
    }

    void D3DContext::ExecuteCommandBuffer(Ref<D3DComandBuffer> cmd)
    {
        for (auto& call : cmd->_commands)
        {
            call();
        }
    }

    void D3DContext::DrawIndexedInstanced(uint32_t index_count, uint32_t instance_count, const Matrix4x4f& transform)
    {
        ++RenderingStates::s_draw_call;
        m_commandList->SetGraphicsRootConstantBufferView(0, GetCBufferViewDesc(1 + D3DConstants::kMaxMaterialDataCount + _render_object_index).BufferLocation);
        //m_commandList->SetGraphicsRootDescriptorTable(0, GetCBVGPUDescHandle(1 + D3DConstants::kMaxMaterialDataCount + _render_object_index));
        memcpy(_p_cbuffer + D3DConstants::kPerFrameDataSize + D3DConstants::kPerMaterialDataSize * D3DConstants::kMaxMaterialDataCount + D3DConstants::kPerFrameDataSize * (_render_object_index++),
            &transform, sizeof(transform));
        m_commandList->DrawIndexedInstanced(index_count, instance_count, 0, 0, 0);
    }

    void D3DContext::DrawInstanced(uint32_t vertex_count, uint32_t instance_count, const Matrix4x4f& transform)
    {
        ++RenderingStates::s_draw_call;
        m_commandList->SetGraphicsRootConstantBufferView(0, GetCBufferViewDesc(1 + D3DConstants::kMaxMaterialDataCount + _render_object_index).BufferLocation);
        //m_commandList->SetGraphicsRootDescriptorTable(0, GetCBVGPUDescHandle(1 + D3DConstants::kMaxMaterialDataCount + _render_object_index));
        memcpy(_p_cbuffer + D3DConstants::kPerFrameDataSize + D3DConstants::kPerMaterialDataSize * D3DConstants::kMaxMaterialDataCount + D3DConstants::kPerFrameDataSize * (_render_object_index++),
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
            CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
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
        g_pd3dSrvDescHeapImGui->Release();
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

        ThrowIfFailed(D3D12CreateDevice(
            hardwareAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&m_device)
        ));

        // Describe and create the command queue.
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

        ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

        // Describe and create the swap chain.
        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
        swapChainDesc.BufferCount = D3DConstants::kFrameCount;
        swapChainDesc.Width = _width;
        swapChainDesc.Height = _height;
        swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        auto hwnd = static_cast<HWND>(_window->GetNativeWindowPtr());
        ComPtr<IDXGISwapChain1> swapChain;
        ThrowIfFailed(factory->CreateSwapChainForHwnd(
            m_commandQueue.Get(),        // Swap chain needs the queue so that it can force a flush on it.
            hwnd,
            &swapChainDesc,
            nullptr,
            nullptr,
            &swapChain
        ));

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
            for (UINT n = 0; n < D3DConstants::kFrameCount; n++)
            {
                ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&_color_buffer[n])));
                m_device->CreateRenderTargetView(_color_buffer[n].Get(), nullptr, rtvHandle);
                rtvHandle.Offset(1, m_rtvDescriptorSize);
                ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[n])));
            }
        }
        CreateDepthStencilTarget();
        // Create the command list.
        ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[m_frameIndex].Get(), nullptr, IID_PPV_ARGS(&m_commandList)));
        // Command lists are created in the recording state, but there is nothing
        // to record yet. The main loop expects it to be closed, so close it now.
        //ThrowIfFailed(m_commandList->Close());
    }

    Ref<GraphicsPipelineState> _p_gizmo_pso;

    void D3DContext::LoadAssets()
    {
        GraphicsPipelineStateMgr::BuildPSOCache();

        _p_standard_shader = ShaderLibrary::Add(GetResPath("Shaders/shaders.hlsl"));
        _mat_green = MakeRef<Material>(_p_standard_shader, "GreenColor");

        GraphicsPipelineStateInitializer pso_initializer{};
        pso_initializer._blend_state = BlendState{};
        pso_initializer._b_has_rt = true;
        pso_initializer._depth_stencil_state = TStaticDepthStencilState<true, ECompareFunc::kLess>::GetRHI();
        pso_initializer._ds_format = EALGFormat::kALGFormatD32_FLOAT;
        pso_initializer._rt_formats[0] = EALGFormat::kALGFormatR8G8B8A8_UNORM;
        pso_initializer._rt_nums = 1;
        pso_initializer._topology = ETopology::kTriangle;
        pso_initializer._p_vertex_shader = _p_standard_shader;
        pso_initializer._p_pixel_shader = _p_standard_shader;
        pso_initializer._raster_state = TStaticRasterizerState<ECullMode::kNone, EFillMode::kSolid>::GetRHI();
        GraphicsPipelineState* pso = GraphicsPipelineState::Create(pso_initializer);
        pso->Build();
        
        _p_gizmo_pso = D3DGraphicsPipelineState::GetGizmoPSO();
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
            auto parser = TStaticAssetLoader<EResourceType::kStaticMesh, EMeshLoader>::GetParser(EMeshLoader::kFbx);
            
            //_plane = parser->Parser(GET_RES_PATH(Meshs/plane.fbx));
            //_plane = parser->Parser(GET_RES_PATH(Meshs/gizmo.fbx));

            _tree = parser->Parser(GetResPath("Meshs/stone.fbx"));

            auto png_parser = TStaticAssetLoader<EResourceType::kImage, EImageLoader>::GetParser(EImageLoader::kPNG);
            auto tga_parser = TStaticAssetLoader<EResourceType::kImage, EImageLoader>::GetParser(EImageLoader::kTGA);

            //_tex_water = tga_parser->Parser(GET_RES_PATH(Textures/PK_stone03_static_0_D.tga));
            tga_parser->Parser(GetResPath("Textures/PK_stone03_static_0_D.tga"));
            tga_parser->Parser(GetResPath("Textures/PK_stone03_static_0_N.tga"));

            _mat_green->SetTexture("TexAlbedo", TexturePool::Get("PK_stone03_static_0_D"));
            _mat_green->SetTexture("TexNormal", TexturePool::Get("PK_stone03_static_0_N"));


            _p_actor = Actor::Create<Actor>();
            ;
            _p_actor->AddChild(Actor::Create<Actor>());
            _p_actor->AddComponent<TransformComponent>();

            _p_vertex_buf.reset(VertexBuffer::Create({
                {"POSITION",EShaderDateType::kFloat3,0},
                }));
            _p_vertex_buf0.reset(VertexBuffer::Create({
                {"POSITION",EShaderDateType::kFloat3,0},
                }));
            _p_vertex_buf->SetStream(reinterpret_cast<float*>(testMesh), sizeof(testMesh), 0);
            //_p_vertex_buf->SetStream(reinterpret_cast<float*>(color), sizeof(color), 1);

            _p_vertex_buf0->SetStream(reinterpret_cast<float*>(testMesh0), sizeof(testMesh0), 0);
            //_p_vertex_buf0->SetStream(reinterpret_cast<float*>(color), sizeof(color), 1);
            _p_index_buf.reset(IndexBuffer::Create(indices, 36));
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

        auto pos0 = GraphicsPipelineStateMgr::GetPso(EGraphicsPSO::kStandShaded);
        pos0->Bind();
        pos0->SubmitBindResource(&_cbuf_views[0], EBindResDescType::kConstBuffer);

        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
        CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, _dsv_desc_size);
        m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
        {          
            Renderer::BeginScene();
            _render_object_index = 0;
        }
        //Clear({ 0.3f, 0.2f, 0.4f, 1.0f },1.0f,true,true);
        auto cmd = D3DComandBufferPool::GetCommandBuffer();
        cmd->Clear();
        cmd->SetViewports({ Viewport{0,0,(uint16_t)_window->GetWidth(),(uint16_t)_window->GetHeight()} });
        cmd->SetScissorRects({ Viewport{0,0,(uint16_t)_window->GetWidth(),(uint16_t)_window->GetHeight()} });
        //cmd->SetClearColor({ 0.3f, 0.2f, 0.4f, 1.0f });
        cmd->ClearRenderTarget({ 0.3f, 0.2f, 0.4f, 1.0f }, 1.0, true, true);
        //cmd->ClearRenderTarget({ 0.0f, 0.0f, 0.0f, 1.0f }, 1.0, true, true);
        cmd->SetViewProjectionMatrices(Transpose(_p_scene_camera->GetView()), Transpose(_p_scene_camera->GetProjection()));

        _mat_green->SetVector("_Color", Vector4f{ 0.0f,1.0f,0.0f,1.0f });
        Matrix4x4f rot{};
        auto* transf = _p_actor->GetComponent<TransformComponent>();
        //transf->Position({0.0f,sin(TimeMgr::GetScaledWorldTime()) * 10.0f,0.0f});
        
        //MatrixRotationY(rot, TimeMgr::GetScaledWorldTime());
        cmd->DrawRenderer(_tree,Transpose(transf->GetWorldMatrix()),_mat_green);
        ExecuteCommandBuffer(cmd);
        auto draw_call = &RenderingStates::s_draw_call;
        {
            Renderer::EndScene();
        }
        D3DComandBufferPool::ReleaseCommandBuffer(cmd);
        
        //DrawGizmo
        {
            _p_gizmo_pso->Bind();
            //_p_gizmo_pso->SubmitBindResource(&_cbuf_views[0], EBindResDescType::kConstBuffer,0u);
            _p_gizmo_pso->SubmitBindResource(&_cbuf_views[0], EBindResDescType::kConstBuffer);

            Gizmo::Submit();
        }

        //imgui draw
        {
            m_commandList->SetDescriptorHeaps(1, &g_pd3dSrvDescHeapImGui);
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
        D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc = {};
        cbvHeapDesc.NumDescriptors = (1u + D3DConstants::kMaxMaterialDataCount + D3DConstants::kMaxRenderObjectCount) * D3DConstants::kFrameCount + D3DConstants::kMaxTextureCount;
        cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        ThrowIfFailed(device->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&m_cbvHeap)));
        _cbv_desc_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        _cbuf_views.reserve(cbvHeapDesc.NumDescriptors);
        //constbuffer
        auto heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto res_desc = CD3DX12_RESOURCE_DESC::Buffer(D3DConstants::kPerFrameTotalSize * D3DConstants::kFrameCount);
        ThrowIfFailed(device->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &res_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_constantBuffer)));
        // Describe and create a constant buffer view.
        for (uint32_t i = 0; i < D3DConstants::kFrameCount; i++)
        {
            D3D12_CPU_DESCRIPTOR_HANDLE cbvHandle;
            cbvHandle.ptr = m_cbvHeap->GetCPUDescriptorHandleForHeapStart().ptr + i * (1 + D3DConstants::kMaxMaterialDataCount + D3DConstants::kMaxRenderObjectCount) * _cbv_desc_size;
            D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
            cbv_desc.BufferLocation = m_constantBuffer->GetGPUVirtualAddress() + i * D3DConstants::kPerFrameTotalSize;
            cbv_desc.SizeInBytes = D3DConstants::kPerFrameDataSize;
            device->CreateConstantBufferView(&cbv_desc, cbvHandle);
            _cbuf_views.emplace_back(cbv_desc);
            for (uint32_t j = 0; j < D3DConstants::kMaxMaterialDataCount; j++)
            {
                D3D12_CPU_DESCRIPTOR_HANDLE cbvHandle2;
                cbvHandle2.ptr = cbvHandle.ptr + (j + 1) * _cbv_desc_size;
                cbv_desc.BufferLocation = m_constantBuffer->GetGPUVirtualAddress() + i * D3DConstants::kPerFrameTotalSize + D3DConstants::kPerFrameDataSize + j * D3DConstants::kPerMaterialDataSize;
                cbv_desc.SizeInBytes = D3DConstants::kPerMaterialDataSize;
                device->CreateConstantBufferView(&cbv_desc, cbvHandle2);
                _cbuf_views.emplace_back(cbv_desc);
            }
            for (uint32_t k = 0; k < D3DConstants::kMaxRenderObjectCount; k++)
            {
                D3D12_CPU_DESCRIPTOR_HANDLE cbvHandle2;
                cbvHandle2.ptr = cbvHandle.ptr + (1 + D3DConstants::kMaxMaterialDataCount + k) * _cbv_desc_size;
                cbv_desc.BufferLocation = m_constantBuffer->GetGPUVirtualAddress() + D3DConstants::kPerFrameTotalSize * i + D3DConstants::kPerFrameDataSize +
                    D3DConstants::kMaxMaterialDataCount * D3DConstants::kPerMaterialDataSize + k * D3DConstants::kPeObjectDataSize;
                cbv_desc.SizeInBytes = D3DConstants::kPeObjectDataSize;
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
        for (size_t i = 0; i < D3DConstants::kFrameCount; i++)
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
        rtvHeapDesc.NumDescriptors = D3DConstants::kFrameCount;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));
        m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
        dsvHeapDesc.NumDescriptors = D3DConstants::kFrameCount;
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