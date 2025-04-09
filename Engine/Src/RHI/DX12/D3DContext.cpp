#include "RHI/DX12/D3DContext.h"
#include "Ext/imgui/backends/imgui_impl_dx12.h"
#include "Framework/Common/Assert.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/Profiler.h"
#include "Framework/Common/Application.h"
#include "Framework/Common/JobSystem.h"
#include "RHI/DX12/D3DCommandBuffer.h"
#include "RHI/DX12/D3DGPUTimer.h"
#include "RHI/DX12/GPUResourceManager.h"
#include "RHI/DX12/dxhelper.h"
#include "Render/Gizmo.h"
#include "Render/GpuResource.h"
#include "Render/GraphicsPipelineStateObject.h"
#include "Render/RenderingData.h"
#include "Render/Material.h"
#include "Render/Buffer.h"
#include "pch.h"
#include <dxgidebug.h>
#include <limits>

#include "RHI/DX12/D3DGraphicsPipelineState.h"
#include <RHI/DX12/D3DTexture.h>
#include <d3d11.h>


#ifdef _PIX_DEBUG
#include "Ext/pix/Include/WinPixEventRuntime/pix3.h"
#include "Ext/renderdoc_app.h"//1.35
#endif                        // _PIX_DEBUG


namespace Ailu
{
    #pragma region GpuCommandWorker

    GpuCommandWorker::GpuCommandWorker(GraphicsContext* context) : _ctx(context),_is_stop(false),
    _worker_thread(nullptr)
    {
        
    }
    GpuCommandWorker::~GpuCommandWorker()
    {
        if (_worker_thread && _worker_thread->joinable())
            _worker_thread->join();
        LOG_INFO("Destory GpuCommandWorker");
    }

    void GpuCommandWorker::Push(Vector<GfxCommand *>&& cmds,SubmitParams params)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        CommandGroup group;
        _cmd_queue.emplace(std::move(cmds),params);
        _cv.notify_one();
    }
    bool GpuCommandWorker::Pop(GpuCommandWorker::CommandGroup& group)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        if (!_cmd_queue.empty())
        {
            group = std::move(_cmd_queue.front());
            _cmd_queue.pop();
            return true;
        }
        return false;
    }
    void GpuCommandWorker::RunAsync()
    {
        SetThreadName("RenderThread");
        while(!_is_stop)
        {
            Application::Get().WaitForMain();
            if (Application::Get().State() == EApplicationState::EApplicationState_Exit)
            {
                _is_stop = true;
                break;
            }
            std::unique_lock<std::mutex> lock(_run_mutex);
            _cv.wait(lock, [this] { return (!_cmd_queue.empty()); });
            CPUProfileBlock b("GpuCommandWorker::RunAsync");
            while (true)
            {
                CommandGroup group;
                if (Pop(group))
                {
                    auto cmd = RHICommandBufferPool::Get(group._params._name);
                    for(auto* task : group._cmds)
                        _ctx->ProcessGpuCommand(task,cmd.get());
                    if (group._cmds.size() > 1)
                    {
                        CPUProfileBlock b(group._params._name +"_Execute");
                        _ctx->ExecuteRHICommandBuffer(cmd.get());
                    }
                    RenderTexture::ResetRenderTarget();
                    RHICommandBufferPool::Release(cmd);
                    if (group._params._is_end_frame)
                    {
                        CPUProfileBlock b("EndFrame");
                        EndFrame();
                        break;
                    }
                }
                else
                {
                    std::this_thread::yield();
                }
            }
        }
    }
    void GpuCommandWorker::EndFrame()
    {
        u16 compiled_shader_num = 0u,compiled_compute_shader_num = 0u;
        while (!_pending_update_shaders.Empty())
        {
            if (auto obj = _pending_update_shaders.Pop(); obj.has_value())
            {
                if (Shader* shader = dynamic_cast<Shader*>(obj.value()); shader != nullptr)
                {
                    if (shader->PreProcessShader())
                    {
                        bool is_all_succeed = true;
                        is_all_succeed = shader->Compile(); //暂时重编所有变体，避免材质切换变体后使用的是旧的shader
                        for (auto &mat: shader->GetAllReferencedMaterials())
                        {
                            mat->ConstructKeywords(shader);
                            // for (u16 i = 0; i < shader->PassCount(); i++)
                            // {
                            //     is_all_succeed &= shader->Compile(i, mat->ActiveVariantHash(i));
                            // }
                            if (is_all_succeed)
                            {
                                mat->ChangeShader(shader);
                            }
                        }
                        compiled_shader_num++;
                    }
                }
                else if (ComputeShader* shader = dynamic_cast<ComputeShader*>(obj.value()); shader != nullptr)
                {
                    if (shader->Preprocess())
                    {
                        shader->_is_compiling.store(true);// shader Compile()也会设置这个值，这里设置一下防止读取该值时还没执行compile
                        shader->Compile(); 
                        compiled_compute_shader_num++;
                    }
                }
            }
        }
        if (compiled_shader_num + compiled_compute_shader_num > 0u)
        {
            LOG_INFO("Compiled {} shaders and {} compute shaders!", compiled_shader_num,compiled_compute_shader_num);
        }
        if (Application::Get()._is_multi_thread_rendering)
            Application::Get().NotifyMain();
        RenderingStates::Reset();
    }
    void GpuCommandWorker::Start()
    {
        if (_worker_thread == nullptr)
        {
            _worker_thread = new std::thread(&GpuCommandWorker::RunAsync, this);
        }
        _is_stop = false;
    }
    void GpuCommandWorker::Stop()
    {
        if (_worker_thread)
        {
            _is_stop = true;
            if (_worker_thread->joinable())
                _worker_thread->join();
            DESTORY_PTR(_worker_thread);
        }
    }
    void GpuCommandWorker::RunSync()
    {
        CPUProfileBlock b("GpuCommandWorker::RunSync");
        while(!_cmd_queue.empty())
        {
            CommandGroup& group = _cmd_queue.front();
            auto cmd = RHICommandBufferPool::Get(group._params._name);
            for(auto* task : group._cmds)
                _ctx->ProcessGpuCommand(task, cmd.get());
            {
                CPUProfileBlock b(group._params._name +"_Execute");
                _ctx->ExecuteRHICommandBuffer(cmd.get());
            }
            RenderTexture::ResetRenderTarget();
            RHICommandBufferPool::Release(cmd);
            _cmd_queue.pop();
        }
    }
#pragma endregion

    #pragma region DX Helper
    static void GetHardwareAdapter(IDXGIFactory6 *pFactory, IDXGIAdapter4 **ppAdapter, DXGI_QUERY_VIDEO_MEMORY_INFO *p_local_video_memory_info, DXGI_QUERY_VIDEO_MEMORY_INFO *p_non_local_video_memory_info)
    {
        IDXGIAdapter *pAdapter = nullptr;
        IDXGIAdapter4 *pAdapter4 = nullptr;
        *ppAdapter = nullptr;
        for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != pFactory->EnumAdapters(adapterIndex, &pAdapter); adapterIndex++)
        {
            DXGI_ADAPTER_DESC3 desc{};
            LOG_INFO("Info(Adapter index: {})------------------------------------------------------------------------------", adapterIndex);
            if (SUCCEEDED(pAdapter->QueryInterface(IID_PPV_ARGS(&pAdapter4))))
            {
                pAdapter4->GetDesc3(&desc);
                LOG_INFO(L"Description: {}", desc.Description);
                LOG_INFO("DedicatedSystemMemory: {} mb", desc.DedicatedSystemMemory / 1024 / 1024);
                LOG_INFO("SharedSystemMemory: {} mb", desc.SharedSystemMemory / 1024 / 1024);
            }
            //
            // Obtain the default video memory information for the local and non-local segment groups.
            //
            if (FAILED(pAdapter4->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, p_local_video_memory_info)))
            {
                LOG_ERROR("Failed to query initial video memory info for local segment group");
            }
            else
            {
                //When querying video memory budget for GPU upload heaps, MemorySegmentGroup needs to be DXGI_MEMORY_SEGMENT_GROUP_LOCAL.
                LOG_INFO("LocalSegmentGroup");
                LOG_INFO("AvailableForReservation: {} mb", p_local_video_memory_info->AvailableForReservation / 1024 / 1024);
                LOG_INFO("Budget: {} mb", p_local_video_memory_info->Budget / 1024 / 1024);
                LOG_INFO("CurrentReservation: {} mb", p_local_video_memory_info->CurrentReservation / 1024 / 1024);
                //实时获取这个值
                LOG_INFO("CurrentUsage: {} mb", p_local_video_memory_info->CurrentUsage / 1024 / 1024);
            }

            if (FAILED(pAdapter4->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, p_non_local_video_memory_info)))
            {
                LOG_ERROR("Failed to query initial video memory info for non-local segment group");
            }
            else
            {
                LOG_INFO("NonLocalSegmentGroup");
                LOG_INFO("AvailableForReservation: {} mb", p_non_local_video_memory_info->AvailableForReservation / 1024 / 1024);
                LOG_INFO("Budget: {} mb", p_non_local_video_memory_info->Budget / 1024 / 1024);
                LOG_INFO("CurrentReservation: {} mb", p_non_local_video_memory_info->CurrentReservation / 1024 / 1024);
                LOG_INFO("CurrentUsage: {} mb", p_non_local_video_memory_info->CurrentUsage / 1024 / 1024);
            }

            LOG_INFO("-----------------------------------------------------------------------------------------------------");
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

    static RENDERDOC_API_1_1_2 *g_rdc_api = nullptr;
    static void RdcLoadLatestRdcGpuCapturerLibrary()
    {
        HKEY hKey = HKEY_LOCAL_MACHINE;                                                                        // 根键
        LPCWSTR subKey = L"SOFTWARE\\Classes\\CLSID\\{5D6BF029-A6BA-417A-8523-120492B1DCE3}\\InprocServer32\\";// 子键路径
        //LPCWSTR valueName = L"YourValue";     // 值名称
        wchar_t value[256];              // 存储结果
        DWORD bufferSize = sizeof(value);// 缓冲区大小
        // 获取值
        LONG result = RegGetValue(
                hKey,
                subKey,
                nullptr,//default name
                RRF_RT_REG_SZ,
                nullptr,
                value,
                &bufferSize);

        if (result == ERROR_SUCCESS)
        {
            if (HMODULE mod = LoadLibrary(value))
            {
                pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI) GetProcAddress(mod, "RENDERDOC_GetAPI");
                int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2, (void **) &g_rdc_api);
                AL_ASSERT(ret == 1);
                g_rdc_api->SetCaptureFilePathTemplate("RenderDocCapture/Capture");
                g_rdc_api->MaskOverlayBits(RENDERDOC_OverlayBits::eRENDERDOC_Overlay_None, RENDERDOC_OverlayBits::eRENDERDOC_Overlay_None);
            }
            else
                LOG_ERROR("RenderDoc Lode Error: {}", GetLastError());
        }
        else
            LOG_ERROR(L"Failed to get renderdoc dll path. Error: {}", result)
    };
    namespace D3DConvertUtils
    {
        D3D12_VIEWPORT ToD3DViewport(const Rect &viewport)
        {
            D3D12_VIEWPORT ret;
            ret.TopLeftX = (f32)viewport.left;
            ret.TopLeftY = (f32)viewport.top;
            ret.Width =    (f32)viewport.width;
            ret.Height =   (f32)viewport.height;
            ret.MinDepth = 0.0f;
            ret.MaxDepth = 1.0f;
            return ret;
        }
        D3D12_RECT ToD3DRect(const Rect &rect)
        {
            D3D12_RECT ret;
            ret.left = rect.left;
            ret.top = rect.top;
            ret.right = rect.width;
            ret.bottom = rect.height;
            return ret;
        }
    }
    #pragma endregion

    #pragma region D3DContext
    D3DContext::D3DContext(WinWindow *window) : _window(window), _width(window->GetWidth()), _height(window->GetHeight())
    {
        m_aspectRatio = (float) _width / (float) _height;
#ifdef _PIX_DEBUG
        PIXLoadLatestWinPixGpuCapturerLibrary();
        //RdcLoadLatestRdcGpuCapturerLibrary();
#endif// _PIX_DEBUG
        _cmd_worker = MakeScope<GpuCommandWorker>(this);
    }

    D3DContext::~D3DContext()
    {
        Destroy();
        LOG_INFO("D3DContext Destroy");
    }

    void D3DContext::Init()
    {
        g_pGfxContext = this;
        LoadPipeline();
        LoadAssets();
#ifdef DEAR_IMGUI
        //init imgui
        {
            auto bind_heap = D3DDescriptorMgr::Get().GetBindHeap();
            auto [cpu_handle, gpu_handle] = std::make_tuple(bind_heap->GetCPUDescriptorHandleForHeapStart(),bind_heap->GetGPUDescriptorHandleForHeapStart());
            auto ret = ImGui_ImplDX12_Init(m_device.Get(), RenderConstants::kFrameCount,
                                           RenderConstants::kColorRange == EColorRange::kLDR ? ConvertToDXGIFormat(RenderConstants::kLDRFormat) : ConvertToDXGIFormat(RenderConstants::kHDRFormat),
                                           bind_heap, cpu_handle, gpu_handle);
        }
#endif// DEAR_IMGUI
        m_commandList->Close();
        ID3D12CommandList *ppCommandLists[] = {m_commandList.Get()};
        m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
        WaitForGpu();
        _p_gpu_timer = MakeScope<D3DGPUTimer>(m_device.Get(), m_commandQueue.Get(), RenderConstants::kFrameCount);
        if (Application::Get()._is_multi_thread_rendering)
        {
            _cmd_worker->Start();
        }
    }

    void D3DContext::TryReleaseUnusedResources()
    {
        if (!_global_tracked_resource.empty())
        {
            u64 origin_res_num = _global_tracked_resource.size();
            auto it = _global_tracked_resource.lower_bound(_frame_count);
            if (it != _global_tracked_resource.end())
            {
                _global_tracked_resource.erase(_global_tracked_resource.begin(), it);
            }
            else
                _global_tracked_resource.clear();

            origin_res_num -= _global_tracked_resource.size();
            if (origin_res_num > 0)
                g_pLogMgr->LogWarningFormat("Release unused upload buffer with num {}.", origin_res_num);
        }
        g_pRenderTexturePool->TryCleanUp();
        if (auto num = D3DDescriptorMgr::Get().ReleaseSpace(); num > 0)
        {
            g_pLogMgr->LogFormat("Release GPUVisibleDescriptor at with num {}.", num);
        }
    }

    f32 D3DContext::TotalGPUMemeryUsage()
    {
        f32 usage = 0.f;
        if (FAILED(_p_adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &_local_video_memory_info)))
        {
            LOG_ERROR("Failed to query initial video memory info for local segment group");
        }
        else
        {
            //When querying video memory budget for GPU upload heaps, MemorySegmentGroup needs to be DXGI_MEMORY_SEGMENT_GROUP_LOCAL.
            //LOG_INFO("CurrentUsage: {} mb", );
            usage = (f32) _local_video_memory_info.CurrentUsage * 9.5367431640625E-07;
        }
        return usage;
        //if (FAILED(_p_adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &_non_local_video_memory_info)))
        //{
        //	LOG_ERROR("Failed to query initial video memory info for non-local segment group");
        //}
        //else
        //{
        //	LOG_INFO("CurrentUsage: {} mb", _non_local_video_memory_info.CurrentUsage / 1024 / 1024);
        //}
    }

    RenderPipeline *D3DContext::GetPipeline()
    {
        return _pipiline;
    }

    void D3DContext::RegisterPipeline(RenderPipeline *pipiline)
    {
        _pipiline = pipiline;
    };

    void D3DContext::TrackResource(ComPtr<ID3D12Resource> resource)
    {
        _global_tracked_resource.insert(std::make_pair(_frame_count, resource));
    }

    void D3DContext::ExecuteCommandBuffer(Ref<CommandBuffer> &cmd)
    {
        _cmd_worker->Push(std::move(cmd->_commands),SubmitParams{cmd->Name()});
    }

    void D3DContext::ExecuteCommandBufferSync(Ref<CommandBuffer> &cmd)
    {
        auto rhi_cmd = RHICommandBufferPool::Get(cmd->Name());
        for(auto* gfx_cmd : cmd->_commands)
        {
            ProcessGpuCommand(gfx_cmd,rhi_cmd.get());
        }
        ExecuteRHICommandBuffer(rhi_cmd.get());
        RHICommandBufferPool::Release(rhi_cmd);
    }

    void D3DContext::ExecuteRHICommandBuffer(RHICommandBuffer* cmd)
    {
        if (cmd->IsExecuted())
            return;
        auto d3dcmd = static_cast<D3DCommandBuffer *>(cmd);
        d3dcmd->Close();
        ID3D12CommandList *ppCommandLists[] = {d3dcmd->NativeCmdList()};
#ifdef _PIX_DEBUG
        PIXBeginEvent(m_commandQueue.Get(), cmd->ID(), ToWStr(cmd->Name()).c_str());
#endif// _PIX_DEBUG
        m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
        ++_fence_value[m_frameIndex];
        ThrowIfFailed(m_commandQueue->Signal(_p_cmd_buffer_fence.Get(), _fence_value[m_frameIndex]));
#ifdef _PIX_DEBUG
        PIXEndEvent(m_commandQueue.Get());
#endif// _PIX_DEBUG
        d3dcmd->_fence_value = _fence_value[m_frameIndex];
        d3dcmd->PostExecute();
    }

    void D3DContext::Destroy()
    {
        // Ensure that the GPU is no longer referencing resources that are about to be
        // cleaned up by the destructor.
        WaitForGpu();
        CloseHandle(m_fenceEvent);
        if (Application::Get()._is_multi_thread_rendering)
            _cmd_worker->Stop();
        //_p_gpu_timer->ReleaseDevice();
        //在这里析构分配器应该会有问题，纹理实际在在这之后还需要归还之前的分配
        D3DDescriptorMgr::Shutdown();
        // 在程序终止时调用 ReportLiveObjects() 函数
        IDXGIDebug1 *pDebug = nullptr;
        if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&pDebug))))
        {
            pDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_SUMMARY);
            pDebug->Release();
        }
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
        GetHardwareAdapter(factory.Get(), _p_adapter.GetAddressOf(), &_local_video_memory_info, &_non_local_video_memory_info);
        ThrowIfFailed(D3D12CreateDevice(_p_adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)));
        D3DDescriptorMgr::Init();
        // Describe and create the command queue.
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));
        // Describe and create the swap chain.
        _backbuffer_format = RenderConstants::kColorRange == EColorRange::kLDR ? ConvertToDXGIFormat(RenderConstants::kLDRFormat) : ConvertToDXGIFormat(RenderConstants::kHDRFormat);
        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
        swapChainDesc.BufferCount = RenderConstants::kFrameCount;
        swapChainDesc.Width = _width;
        swapChainDesc.Height = _height;
        swapChainDesc.Format = _backbuffer_format;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        auto hwnd = static_cast<HWND>(_window->GetNativeWindowPtr());
        ComPtr<IDXGISwapChain1> swapChain;
        ThrowIfFailed(factory->CreateSwapChainForHwnd(m_commandQueue.Get(), hwnd, &swapChainDesc, nullptr, nullptr, &swapChain));
        // This sample does not support fullscreen transitions.
        ThrowIfFailed(factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER));
        ThrowIfFailed(swapChain->SetFullscreenState(FALSE, nullptr));
        ThrowIfFailed(swapChain.As(&m_swapChain));
#if defined(_PIX_DEBUG)
        PIXSetTargetWindow(hwnd);
        PIXSetHUDOptions(PIXHUDOptions::PIX_HUD_SHOW_ON_NO_WINDOWS);
#endif
        m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
        _rtv_allocation = D3DDescriptorMgr::Get().AllocCPU(RenderConstants::kFrameCount,D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        // Create a RTV for each frame.
        for (UINT n = 0; n < RenderConstants::kFrameCount; n++)
        {
            ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&_color_buffer[n])));
            m_device->CreateRenderTargetView(_color_buffer[n].Get(), nullptr, _rtv_allocation.At(n));
            _color_buffer[n]->SetName(std::format(L"BackBuffer_{}", n).c_str());
            ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[n])));
            _fence_value[n] = 0;
        }
#ifdef _DIRECT_WRITE
        InitDirectWriteContext();
        // Query the desktop's dpi settings, which will be used to create
        // D2D's render targets.
        float dpiX;
        float dpiY;
#pragma warning(push)
#pragma warning(disable : 4996)// GetDesktopDpi is deprecated.
        m_d2dFactory->GetDesktopDpi(&dpiX, &dpiY);
#pragma warning(pop)
        D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
                D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED),
                dpiX,
                dpiY);
        for (UINT n = 0; n < RenderConstants::kFrameCount; n++)
        {
            // Create a wrapped 11On12 resource of this back buffer. Since we are
            // rendering all D3D12 content first and then all D2D content, we specify
            // the In resource state as RENDER_TARGET - because D3D12 will have last
            // used it in this state - and the Out resource state as PRESENT. When
            // ReleaseWrappedResources() is called on the 11On12 device, the resource
            // will be transitioned to the PRESENT state.
            D3D11_RESOURCE_FLAGS d3d11Flags = {D3D11_BIND_RENDER_TARGET};
            ThrowIfFailed(m_d3d11On12Device->CreateWrappedResource(
                    _color_buffer[n].Get(),
                    &d3d11Flags,
                    D3D12_RESOURCE_STATE_RENDER_TARGET,
                    D3D12_RESOURCE_STATE_PRESENT,
                    IID_PPV_ARGS(&m_wrappedBackBuffers[n])));

            // Create a render target for D2D to draw directly to this back buffer.
            ComPtr<IDXGISurface> surface;
            ThrowIfFailed(m_wrappedBackBuffers[n].As(&surface));
            ThrowIfFailed(m_d2dDeviceContext->CreateBitmapFromDxgiSurface(
                    surface.Get(),
                    &bitmapProperties,
                    &m_d2dRenderTargets[n]));
        }
#endif
        ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[m_frameIndex].Get(), nullptr, IID_PPV_ARGS(&m_commandList)));
    }


    void D3DContext::LoadAssets()
    {
#ifdef _DIRECT_WRITE
        // Create D2D/DWrite objects for rendering text.
        {
            ThrowIfFailed(m_d2dDeviceContext->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &m_textBrush));
            ThrowIfFailed(m_dWriteFactory->CreateTextFormat(
                    L"Verdana",
                    NULL,
                    DWRITE_FONT_WEIGHT_NORMAL,
                    DWRITE_FONT_STYLE_NORMAL,
                    DWRITE_FONT_STRETCH_NORMAL,
                    50,
                    L"en-us",
                    &m_textFormat));
            ThrowIfFailed(m_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER));
            ThrowIfFailed(m_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER));
        }
#endif

        // Create synchronization objects.
        {
            //ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
            ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(_p_cmd_buffer_fence.GetAddressOf())));
            //m_fenceValues[m_frameIndex]++;
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
        Vector<GfxCommand *> cmds{CommandPool::Get().Alloc<CommandPresent>()};
        _cmd_worker->Push(std::move(cmds), SubmitParams{"Present",true});
        if (!Application::Get()._is_multi_thread_rendering)
        {
            _cmd_worker->RunSync();
        }
    }

    u64 D3DContext::GetFenceValueGPU() const
    {
        return _p_cmd_buffer_fence->GetCompletedValue();
    }

    u64 D3DContext::GetFenceValueCPU() const
    {
        return _fence_value[m_frameIndex];
    }

    void D3DContext::TakeCapture()
    {
        //g_rdc_api->TriggerCapture();
        //if (!g_rdc_api->IsTargetControlConnected())
        //    g_rdc_api->LaunchReplayUI(1, nullptr);
        _is_next_frame_capture = true;
    }

    void D3DContext::ResizeSwapChain(const u32 width, const u32 height)
    {
        ResizeSwapChainImpl(width, height);
        //_resize_msg.push(std::make_tuple(width, height));
    }


    void D3DContext::WaitForGpu()
    {
        FlushCommandQueue(m_commandQueue.Get(), _p_cmd_buffer_fence.Get(), _fence_value[m_frameIndex]);
    }

    void D3DContext::WaitForFence(u64 fence_value)
    {
        FlushCommandQueue(m_commandQueue.Get(), _p_cmd_buffer_fence.Get(), fence_value);
    }

    void D3DContext::FlushCommandQueue(ID3D12CommandQueue *cmd_queue, ID3D12Fence *fence, u64 &fence_value)
    {
        ++fence_value;
        ThrowIfFailed(cmd_queue->Signal(fence, fence_value));
        if (fence->GetCompletedValue() < fence_value)
        {
            ThrowIfFailed(fence->SetEventOnCompletion(fence_value, m_fenceEvent));
            WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
        }
    }
    void D3DContext::MoveToNextFrame()
    {
        //执行时已经递增了，这里不再需要
        // Schedule a Signal command in the queue.
        const UINT64 currentFenceValue = _fence_value[m_frameIndex];
        ThrowIfFailed(m_commandQueue->Signal(_p_cmd_buffer_fence.Get(), currentFenceValue));

        // Update the frame index.之前交换后，此时索引即为本帧工作的buffer
        m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

        // 如果该buffer上一次绘制还未结束，等待
        if (_p_cmd_buffer_fence->GetCompletedValue() < _fence_value[m_frameIndex])
        {
            CPUProfileBlock p("WaitForGPU");
            ThrowIfFailed(_p_cmd_buffer_fence->SetEventOnCompletion(_fence_value[m_frameIndex], m_fenceEvent));
            WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
        }

        // Set the fence value for the next frame.
        _fence_value[m_frameIndex] = currentFenceValue + 1;
    }
    void D3DContext::PresentImpl(D3DCommandBuffer* cmd)
    {
        static TimeMgr s_timer;
        CPUProfileBlock b("Present");
        auto dxcmd = cmd->NativeCmdList();
        {
            u32 pid = Profiler::Get().StartGpuProfile(cmd,"Present");
            Profiler::Get().AddGPUProfilerHierarchy(true,pid);
            auto bar_before = CD3DX12_RESOURCE_BARRIER::Transition(_color_buffer[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
            dxcmd->ResourceBarrier(1, &bar_before);
            auto rtv_handle = _rtv_allocation.At(m_frameIndex);
            dxcmd->OMSetRenderTargets(1, &rtv_handle, FALSE, nullptr);
            dxcmd->ClearRenderTargetView(rtv_handle, Colors::kBlack, 0, nullptr);
    #ifdef DEAR_IMGUI
            auto bind_heap = D3DDescriptorMgr::Get().GetBindHeap();
            dxcmd->SetDescriptorHeaps(1u,&bind_heap);
            for(auto&it : _imgui_used_rt)
                it->StateTranslation(cmd,EResourceState::kPixelShaderResource,kTotalSubRes);
            _imgui_used_rt.clear();
            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), dxcmd);
    #endif// DEAR_IMGUI
    
    #ifndef _DIRECT_WRITE
            auto bar_after = CD3DX12_RESOURCE_BARRIER::Transition(_color_buffer[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
            dxcmd->ResourceBarrier(1, &bar_after);
    #endif// !_DIRECT_WRITE
    
    #ifdef DEAR_IMGUI
            ImGuiIO &io = ImGui::GetIO();
            // Update and Render additional Platform Windows
            if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
            {
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault(nullptr, (void *) m_commandList.Get());
            }
    #endif// DEAR_IMGUI \
    
    #ifdef _DIRECT_WRITE
    
            //RenderUI
            {
                D2D1_SIZE_F rtSize = m_d2dRenderTargets[m_frameIndex]->GetSize();
                D2D1_RECT_F textRect = D2D1::RectF(0, 0, rtSize.width, rtSize.height);
                static const WCHAR text[] = L"11On12";
    
                // Acquire our wrapped render target resource for the current back buffer.
                m_d3d11On12Device->AcquireWrappedResources(m_wrappedBackBuffers[m_frameIndex].GetAddressOf(), 1);
    
                // Render text directly to the back buffer.
                m_d2dDeviceContext->SetTarget(m_d2dRenderTargets[m_frameIndex].Get());
                m_d2dDeviceContext->BeginDraw();
                m_d2dDeviceContext->SetTransform(D2D1::Matrix3x2F::Identity());
                m_d2dDeviceContext->DrawText(
                        text,
                        _countof(text) - 1,
                        m_textFormat.Get(),
                        &textRect,
                        m_textBrush.Get());
                ThrowIfFailed(m_d2dDeviceContext->EndDraw());
    
                // Release our wrapped render target resource. Releasing
                // transitions the back buffer resource to the state specified
                // as the OutState when the wrapped resource was created.
                m_d3d11On12Device->ReleaseWrappedResources(m_wrappedBackBuffers[m_frameIndex].GetAddressOf(), 1);
    
                // Flush to submit the 11 command list to the shared command queue.
                m_d3d11DeviceContext->Flush();
            }
    #endif//  _DIRECT_WRITE
            Profiler::Get().EndGpuProfile(cmd,pid);
            Profiler::Get().AddGPUProfilerHierarchy(false,pid);
        }
        // Present the frame.
        ExecuteRHICommandBuffer(cmd);
        ThrowIfFailed(m_swapChain->Present(1, 0));
        {
            s_timer.MarkLocal();
            MoveToNextFrame();
            RenderingStates::s_gpu_latency = s_timer.GetElapsedSinceLastLocalMark();
        }
        if (_is_cur_frame_capturing)
        {
            EndCapture();
        }

        ++_frame_count;
        if (_frame_count % kResourceCleanupIntervalTick == 0)
        {
            if (_global_tracked_resource.size() > 0)
            {
                u64 origin_res_num = _global_tracked_resource.size();
                auto it = _global_tracked_resource.lower_bound(_frame_count);
                if (it != _global_tracked_resource.end())
                {
                    _global_tracked_resource.erase(_global_tracked_resource.begin(), it);
                }
                else
                    _global_tracked_resource.clear();
                origin_res_num -= _global_tracked_resource.size();
                g_pLogMgr->LogWarningFormat("D3DContext::Present: Resource cleanup, {} resources released.", origin_res_num);
            }
            if (u32 release_size = GpuResourceManager::Get()->ReleaseSpace(); release_size > 0)
            {
                LOG_INFO("D3DContext::Present: Resource cleanup, {} byte released.", release_size);
            }
            if (RenderTexture::TotalGPUMemerySize() > kMaxRenderTextureMemorySize)
            {
                g_pRenderTexturePool->TryCleanUp();
                D3DDescriptorMgr::Get().ReleaseSpace();
            }
        }
        _p_gpu_timer->EndFrame();
        //CommandBufferPool::ReleaseAll();
        if (_is_next_frame_capture)
        {
            BeginCapture();
            _is_next_frame_capture = false;
            _is_cur_frame_capturing = true;
        }
        CommandPool::Get().EndFrame();
        RenderingStates::s_frame_time = s_timer.GetElapsedSinceLastLocalMark();
        RenderingStates::s_frame_rate = 1000.0f / RenderingStates::s_frame_time;
        s_timer.MarkLocal();
    }
    void D3DContext::BeginCapture()
    {
#ifdef _PIX_DEBUG
        LOG_WARNING("Begin take capture...");
        static PIXCaptureParameters parms{};
        static u32 s_capture_count = 0u;
        _cur_capture_name = std::format(L"{}_{}{}", L"NewCapture", ToWStr(TimeMgr::CurrentTime("%Y-%m-%d_%H%M%S")), L".wpix");
        parms.GpuCaptureParameters.FileName = _cur_capture_name.data();
        PIXBeginCapture(PIX_CAPTURE_GPU, &parms);
        _is_next_frame_capture = true;
#else
        LOG_WARNING("No available gpu debug enabled!");
#endif// _PIX_DEBUG
    }
    void D3DContext::EndCapture()
    {
#ifdef _PIX_DEBUG
        PIXEndCapture(true);
        _is_cur_frame_capturing = false;
        PIXOpenCaptureInUI(_cur_capture_name.data());
        //ShellExecute(NULL, L"open", _cur_capture_name.data(), NULL, NULL, SW_SHOWNORMAL);
#endif// _PIX_DEBUG
    }
    void D3DContext::ResizeSwapChainImpl(const u32 width, const u32 height)
    {
        if (width == _width && height == _height)
            return;
        m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
#ifdef _DIRECT_WRITE
        m_d2dDeviceContext->SetTarget(nullptr);
#endif
        //m_d3d11On12Device->ReleaseWrappedResources(m_wrappedBackBuffers[m_frameIndex].GetAddressOf(), 1);
        WaitForGpu();
        // Release the resources holding references to the swap chain (requirement of
        // IDXGISwapChain::ResizeBuffers) and reset the frame fence values to the
        // current fence value.
        for (UINT n = 0; n < RenderConstants::kFrameCount; n++)
        {
            _color_buffer[n].Reset();
#ifdef _DIRECT_WRITE
            m_wrappedBackBuffers[n].Reset();
            m_d2dRenderTargets[n].Reset();
#endif
        }
#ifdef _DIRECT_WRITE
        m_d3d11DeviceContext->Flush();
#endif
        // Resize the swap chain to the desired dimensions.
        DXGI_SWAP_CHAIN_DESC desc = {};
        m_swapChain->GetDesc(&desc);
        ThrowIfFailed(m_swapChain->ResizeBuffers(RenderConstants::kFrameCount, width, height, desc.BufferDesc.Format, desc.Flags));
        //BOOL fullscreenState;
        //ThrowIfFailed(m_swapChain->GetFullscreenState(&fullscreenState, nullptr));
        //m_windowedMode = !fullscreenState;
        m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
        _width = width;
        _height = height;
#ifdef _DIRECT_WRITE
        // Query the desktop's dpi settings, which will be used to create
        // D2D's render targets.
        float dpiX;
        float dpiY;
#pragma warning(push)
#pragma warning(disable : 4996)// GetDesktopDpi is deprecated.
        m_d2dFactory->GetDesktopDpi(&dpiX, &dpiY);
#pragma warning(pop)
        D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
                D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED),
                dpiX,
                dpiY);

        for (UINT n = 0; n < RenderConstants::kFrameCount; n++)
        {
            ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&_color_buffer[n])));
            _color_buffer[n]->SetName(L"back_buffer");
            m_device->CreateRenderTargetView(_color_buffer[n].Get(), nullptr, _rtv_allocation.At(n));
            // Create a wrapped 11On12 resource of this back buffer. Since we are
            // rendering all D3D12 content first and then all D2D content, we specify
            // the In resource state as RENDER_TARGET - because D3D12 will have last
            // used it in this state - and the Out resource state as PRESENT. When
            // ReleaseWrappedResources() is called on the 11On12 device, the resource
            // will be transitioned to the PRESENT state.
            D3D11_RESOURCE_FLAGS d3d11Flags = {D3D11_BIND_RENDER_TARGET};
            ThrowIfFailed(m_d3d11On12Device->CreateWrappedResource(
                    _color_buffer[n].Get(),
                    &d3d11Flags,
                    D3D12_RESOURCE_STATE_RENDER_TARGET,
                    D3D12_RESOURCE_STATE_PRESENT,
                    IID_PPV_ARGS(&m_wrappedBackBuffers[n])));
            // Create a render target for D2D to draw directly to this back buffer.
            ComPtr<IDXGISurface> surface;
            ThrowIfFailed(m_wrappedBackBuffers[n].As(&surface));
            ThrowIfFailed(m_d2dDeviceContext->CreateBitmapFromDxgiSurface(
                    surface.Get(),
                    &bitmapProperties,
                    &m_d2dRenderTargets[n]));
            //rtvHandle.Offset(1, _rtv_desc_size);
            //ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[n])));
        }
#else
        for (UINT n = 0; n < RenderConstants::kFrameCount; n++)
        {
            ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&_color_buffer[n])));
            _color_buffer[n]->SetName(std::format(L"BackBuffer_{}", n).c_str());
            m_device->CreateRenderTargetView(_color_buffer[n].Get(), nullptr, _rtv_allocation.At(n));
        }
#endif//_DIRECT_WRITE
    }

#ifdef _DIRECT_WRITE
    void D3DContext::InitDirectWriteContext()
    {
        //ThrowIfFailed(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown **>(&_dw_factory)));
        ComPtr<ID3D11Device> d3d11_dev = nullptr;
        UINT d3d11DeviceFlags = 0U;
        D2D1_FACTORY_OPTIONS d2dFactoryOptions = {};
#ifdef _DEBUG
        d3d11DeviceFlags = D3D11_CREATE_DEVICE_DEBUG | D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        d2dFactoryOptions.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#else
        d3d11DeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#endif
        ComPtr<ID3D11Device> d3d11Device;
        ThrowIfFailed(D3D11On12CreateDevice(m_device.Get(), d3d11DeviceFlags, nullptr, 0U,
                                            reinterpret_cast<IUnknown **>(m_commandQueue.GetAddressOf()), 1U, 0U, &d3d11Device, &m_d3d11DeviceContext, nullptr));
        ThrowIfFailed(d3d11Device.As(&m_d3d11On12Device));
        // Create D2D/DWrite components.
        {
            D2D1_DEVICE_CONTEXT_OPTIONS deviceOptions = D2D1_DEVICE_CONTEXT_OPTIONS_NONE;
            ThrowIfFailed(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory3), &d2dFactoryOptions, &m_d2dFactory));
            ComPtr<IDXGIDevice> dxgiDevice;
            ThrowIfFailed(m_d3d11On12Device.As(&dxgiDevice));
            ThrowIfFailed(m_d2dFactory->CreateDevice(dxgiDevice.Get(), &m_d2dDevice));
            ThrowIfFailed(m_d2dDevice->CreateDeviceContext(deviceOptions, &m_d2dDeviceContext));
            ThrowIfFailed(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &m_dWriteFactory));
        }
    }
#endif
    void D3DContext::ReadBack(GpuResource* res,u8* data,u32 size)
    {
        if (res->GetResourceType() == EGpuResType::kBuffer)
        {
            size = ALIGN_TO_256(size);
            ComPtr<ID3D12Resource> copy_dst;
            auto res_desc = CD3DX12_RESOURCE_DESC::Buffer(size);
            res_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            auto heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
            heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
            res_desc.Flags &= ~D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            ThrowIfFailed(m_device->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &res_desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                                                            nullptr, IID_PPV_ARGS(copy_dst.GetAddressOf())));
            auto cmd = RHICommandBufferPool::Get("Readback");
            auto d3dcmd = static_cast<D3DCommandBuffer *>(cmd.get());
            auto dxcmd = d3dcmd->NativeCmdList();
            res->StateTranslation(cmd.get(),EResourceState::kCopySource,UINT32_MAX);
            dxcmd->CopyResource(copy_dst.Get(), reinterpret_cast<ID3D12Resource*>(res->NativeResource()));
            //_state_guard.MakesureResourceState(dxcmd, _p_d3d_res.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            ExecuteRHICommandBuffer(cmd.get());
            u64 cmd_fence_value = d3dcmd->_fence_value;
            while (_p_cmd_buffer_fence->GetCompletedValue() < cmd_fence_value)
            {
                std::this_thread::yield();
            }
            D3D12_RANGE readbackBufferRange{0, size};
            u8* tmp_data = nullptr;
            copy_dst->Map(0, &readbackBufferRange, reinterpret_cast<void **>(&tmp_data));
            D3D12_RANGE emptyRange{0, 0};
            copy_dst->Unmap(0, &emptyRange);
            memcpy(data, tmp_data, size);
            RHICommandBufferPool::Release(cmd);
            TrackResource(copy_dst);
        }
        else
        {
            LOG_ERROR("Readback only support buffer resource!");
        }
    }
    void D3DContext::ReadBackAsync(GpuResource* res,std::function<void(u8*)> callback)
    {
        if (res->GetResourceType() == EGpuResType::kBuffer)
        {
            u32 size = res->GetSize();
            ComPtr<ID3D12Resource> copy_dst;
            auto res_desc = CD3DX12_RESOURCE_DESC::Buffer(size);
            res_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            auto heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
            heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
            res_desc.Flags &= ~D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            ThrowIfFailed(m_device->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &res_desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                                                            nullptr, IID_PPV_ARGS(copy_dst.GetAddressOf())));
            auto cmd = RHICommandBufferPool::Get("Readback");
            auto dxcmd = static_cast<D3DCommandBuffer *>(cmd.get())->NativeCmdList();
            res->StateTranslation(cmd.get(),EResourceState::kCopySource,UINT32_MAX);
            dxcmd->CopyResource(copy_dst.Get(), reinterpret_cast<ID3D12Resource*>(res->NativeResource()));
            //_state_guard.MakesureResourceState(dxcmd, _p_d3d_res.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            ExecuteRHICommandBuffer(cmd.get());
            u64 cmd_fence_value = static_cast<D3DCommandBuffer *>(cmd.get())->_fence_value;
            JobSystem::Get().Dispatch([&]() mutable {
                while (_p_cmd_buffer_fence->GetCompletedValue() < cmd_fence_value)
                {
                    std::this_thread::yield();
                }
                D3D12_RANGE readbackBufferRange{0, size};
                u8* data = nullptr;
                copy_dst->Map(0, &readbackBufferRange, reinterpret_cast<void **>(&data));
                D3D12_RANGE emptyRange{0, 0};
                copy_dst->Unmap(0, &emptyRange);
                callback(data);
            });
            TrackResource(copy_dst);
            RHICommandBufferPool::Release(cmd);
        }
        else
        {
            LOG_ERROR("Readback only support buffer resource!");
        }
    }

    void D3DContext::CreateResource(GpuResource *res)
    {
        CreateResource(res,nullptr);
    }

    
    void D3DContext::CreateResource(GpuResource *res, UploadParams *params)
    {
        if (res->GetResourceType() == EGpuResType::kGraphicsPSO || res->GetResourceType() == EGpuResType::kRenderTexture)
        {
            res->Upload(this,nullptr,params);
            DESTORY_PTR(params);
        }
        else
        {
            auto cmd = CommandPool::Get().Alloc<CommandGpuResourceUpload>();
            cmd->_res = res;
            cmd->_params = params;
            Vector<GfxCommand*> cmds = {cmd};
            _cmd_worker->Push(std::move(cmds),SubmitParams{"ResourceCreate: " + res->Name()});
        }
    }

    void D3DContext::ProcessGpuCommand(GfxCommand *cmd, RHICommandBuffer *cmd_buffer)
    {
        D3DCommandBuffer *d3dcmd = static_cast<D3DCommandBuffer *>(cmd_buffer);
        auto dxcmd = d3dcmd->NativeCmdList();
        static std::stack<CommandProfiler*> s_begin_profiler_stack{};
        if (cmd->GetCmdType() == EGpuCommandType::kAllocConstBuffer)
        {
            auto alloc_cmd = static_cast<CommandAllocConstBuffer *>(cmd);
            d3dcmd->AllocConstBuffer(alloc_cmd->_name, alloc_cmd->_size, alloc_cmd->_data);
        }
        else if (cmd->GetCmdType() == EGpuCommandType::kClearTarget)
        {
            auto clear_cmd = static_cast<CommandClearTarget *>(cmd);
            if (clear_cmd->_flag & EClearFlag::kColor)
            {
                u16 color_num = std::min<u16>(clear_cmd->_color_target_num, d3dcmd->_color_count);
                for (u16 i = 0; i < color_num; ++i)
                {
                    dxcmd->ClearRenderTargetView(*d3dcmd->_colors[i], clear_cmd->_colors[i], 0, nullptr);
                }
            }
            if (clear_cmd->_flag & EClearFlag::kDepth)
            {
                D3D12_CLEAR_FLAGS depth_flag = D3D12_CLEAR_FLAG_DEPTH;
                if (clear_cmd->_flag & EClearFlag::kStencil)
                    depth_flag |= D3D12_CLEAR_FLAG_STENCIL;
                dxcmd->ClearDepthStencilView(*d3dcmd->_depth, depth_flag, clear_cmd->_depth, clear_cmd->_stencil, 0, nullptr);
            }
        }
        else if (cmd->GetCmdType() == EGpuCommandType::kSetTarget)
        {
            auto set_cmd = static_cast<CommandSetTarget *>(cmd);
            d3dcmd->ResetRenderTarget();
            if (set_cmd->_color_target_num == 0)
            {
                if (set_cmd->_depth_target != nullptr)
                {
                    
                    GraphicsPipelineStateMgr::SetRenderTargetState(EALGFormat::EALGFormat::kALGFormatUnknown, set_cmd->_depth_target->PixelFormat(), 0);
                    auto drt = static_cast<D3DRenderTexture *>(set_cmd->_depth_target);
                    d3dcmd->_color_count = 0u;
                    d3dcmd->_depth = drt->TargetCPUHandle(d3dcmd, set_cmd->_depth_index);
                    d3dcmd->_scissors[0] = D3DConvertUtils::ToD3DRect(set_cmd->_viewports[0]);
                    d3dcmd->_viewports[0] = D3DConvertUtils::ToD3DViewport(set_cmd->_viewports[0]);
                    dxcmd->OMSetRenderTargets(0, nullptr, 0, d3dcmd->_depth);
                    dxcmd->RSSetScissorRects(1, d3dcmd->_scissors.data());
                    dxcmd->RSSetViewports(1, d3dcmd->_viewports.data());
                    d3dcmd->MarkUsedResource(set_cmd->_depth_target);
                }
            }
            else
            {
                d3dcmd->_color_count = set_cmd->_color_target_num;
                bool is_depth_valid = set_cmd->_depth_target != nullptr;
                d3dcmd->_depth = is_depth_valid? static_cast<D3DRenderTexture *>(set_cmd->_depth_target)->TargetCPUHandle(d3dcmd, set_cmd->_depth_index) : nullptr;
                static D3D12_CPU_DESCRIPTOR_HANDLE handles[8];
                for (u16 i = 0; i < d3dcmd->_color_count; ++i)
                {
                    auto rt = static_cast<D3DRenderTexture *>(set_cmd->_color_target[i]);
                    GraphicsPipelineStateMgr::SetRenderTargetState(rt->PixelFormat(),is_depth_valid? set_cmd->_depth_target->PixelFormat(): EALGFormat::EALGFormat::kALGFormatUnknown, (u8)i);
                    d3dcmd->_colors[i] = rt->TargetCPUHandle(d3dcmd, set_cmd->_color_indices[i]);
                    d3dcmd->_scissors[i] = D3DConvertUtils::ToD3DRect(set_cmd->_viewports[i]);
                    d3dcmd->_viewports[i] = D3DConvertUtils::ToD3DViewport(set_cmd->_viewports[i]);
                    d3dcmd->MarkUsedResource(set_cmd->_color_target[i]);
                    handles[i] = *d3dcmd->_colors[i];
                }
                if (is_depth_valid)
                    d3dcmd->MarkUsedResource(set_cmd->_depth_target);
                dxcmd->RSSetScissorRects(set_cmd->_color_target_num, d3dcmd->_scissors.data());
                dxcmd->RSSetViewports(set_cmd->_color_target_num, d3dcmd->_viewports.data());
                dxcmd->OMSetRenderTargets(d3dcmd->_color_count,handles , false, d3dcmd->_depth);
            }
        }
        else if (cmd->GetCmdType() == EGpuCommandType::kCustom)
        {
            auto custom_cmd = static_cast<CommandCustom *>(cmd);
            custom_cmd->_func();
        }
        else if (cmd->GetCmdType() == EGpuCommandType::kResourceUpload)
        {
            auto upload_cmd = static_cast<CommandGpuResourceUpload *>(cmd);
            if (ObjectRegister::Get().Alive(upload_cmd->_res))
            {
                upload_cmd->_res->Upload(this,cmd_buffer,upload_cmd->_params);
                upload_cmd->_res->Name(upload_cmd->_res->Name());//这里将name写入d3d resource，之前resource一直为空
            }
        }
        else if (cmd->GetCmdType() == EGpuCommandType::kTransResourceState)
        {
            auto transf_cmd = static_cast<CommandTranslateState *>(cmd);
            transf_cmd->_res->StateTranslation(cmd_buffer, transf_cmd->_new_state,transf_cmd->_sub_res);
        }
        else if (cmd->GetCmdType() == EGpuCommandType::kDraw)
        {
            auto draw_cmd = static_cast<CommandDraw *>(cmd);
            draw_cmd->_mat->Bind(draw_cmd->_pass_index);
            if (auto pso = GraphicsPipelineStateMgr::Get().FindMatchPSO(); pso != nullptr)
            {
                AL_ASSERT(draw_cmd->_mat->GetShader() == pso->StateDescriptor()._p_vertex_shader);
                bool is_indexed_draw = draw_cmd->_ib != nullptr;
                bool is_instance_draw = draw_cmd->_instance_count > 1;
                BindParamsVB params;
                params._layout = &draw_cmd->_mat->GetShader()->PipelineInputLayout(draw_cmd->_pass_index);
                bool is_produced = draw_cmd->_vb == nullptr;
                if (!is_produced)
                    draw_cmd->_vb->Bind(d3dcmd, &params);
                if (is_indexed_draw)
                    draw_cmd->_ib->Bind(d3dcmd, nullptr);
                if (draw_cmd->_per_obj_cb != nullptr)
                    pso->SetPipelineResource(PipelineResource(draw_cmd->_per_obj_cb, EBindResDescType::kConstBuffer, RenderConstants::kCBufNamePerObject, PipelineResource::kPriorityCmd));
                for(auto& it : d3dcmd->_allocations)
                {
                    auto& [name,alloc] = it;
                    if (pso->IsValidPipelineResource(EBindResDescType::kConstBuffer, name))
                    {
                        auto res = PipelineResource(d3dcmd->_upload_buf.get(), EBindResDescType::kConstBufferRaw, name, PipelineResource::kPriorityCmd);
                        res._addi_info._gpu_handle = alloc.GPU;
                        pso->SetPipelineResource(res);
                    }
                }

                ++RenderingStates::s_temp_draw_call;
                u32 vertex_count = is_produced? 3u : draw_cmd->_vb->GetVertexCount() * draw_cmd->_instance_count;//目前只有程序化矩形
                u32 triangle_count = is_indexed_draw? draw_cmd->_ib->GetCount() / 3 : draw_cmd->_vb->GetVertexCount() / 3;
                triangle_count *= draw_cmd->_instance_count;
                RenderingStates::s_temp_triangle_num += triangle_count;
                RenderingStates::s_temp_vertex_num += vertex_count;
                pso->Bind(cmd_buffer, nullptr);
                if (is_indexed_draw)
                    dxcmd->DrawIndexedInstanced(draw_cmd->_ib->GetCount(),draw_cmd->_instance_count,0,0,0);
                else
                    dxcmd->DrawInstanced(draw_cmd->_vb->GetVertexCount(),draw_cmd->_instance_count, 0, 0);
            }
        }
        else if (cmd->GetCmdType() == EGpuCommandType::kDispatch)
        {
            auto cmd_disp = static_cast<CommandDispatch*>(cmd);
            u16 thread_group_x = std::max<u16>(1u, cmd_disp->_group_num_x);
            u16 thread_group_y = std::max<u16>(1u, cmd_disp->_group_num_y);
            u16 thread_group_z = std::max<u16>(1u, cmd_disp->_group_num_z);
            ShaderVariantHash active_variant = cmd_disp->_cs->ActiveVariant(cmd_disp->_kernel);
            cmd_disp->_cs->Bind(d3dcmd,cmd_disp->_kernel,thread_group_x,thread_group_y,thread_group_z);
            for(auto& it : d3dcmd->_allocations)
            {
                auto& [name,alloc] = it;
                if (auto slot = cmd_disp->_cs->NameToSlot(name,cmd_disp->_kernel,active_variant); slot >= 0)
                {
                    dxcmd->SetComputeRootConstantBufferView(slot, alloc.GPU);
                }
            }
            dxcmd->Dispatch(thread_group_x, thread_group_y, thread_group_z);
            ++RenderingStates::s_temp_dispatch_call;
        }
        else if (cmd->GetCmdType() == EGpuCommandType::kCommandProfiler)
        {
            auto cmd_profiler = static_cast<CommandProfiler*>(cmd);
            if (cmd_profiler->_is_start)
            {
                cmd_profiler->_gpu_index = Profiler::Get().StartGpuProfile(cmd_buffer, cmd_profiler->_name);
                Profiler::Get().AddGPUProfilerHierarchy(true, (u32)cmd_profiler->_gpu_index);
                cmd_profiler->_cpu_index = Profiler::Get().StartCPUProfile(cmd_profiler->_name + "_Submit");
                Profiler::Get().AddCPUProfilerHierarchy(true, (u32)cmd_profiler->_cpu_index);
                s_begin_profiler_stack.push(cmd_profiler);
            }
            else
            {
                Profiler::Get().EndGpuProfile(cmd_buffer, s_begin_profiler_stack.top()->_gpu_index);
                Profiler::Get().AddGPUProfilerHierarchy(false, (u32)s_begin_profiler_stack.top()->_gpu_index);
                Profiler::Get().EndCPUProfile(s_begin_profiler_stack.top()->_cpu_index);
                Profiler::Get().AddCPUProfilerHierarchy(false, (u32)s_begin_profiler_stack.top()->_cpu_index);
                s_begin_profiler_stack.pop();
            }
        }
        else if (cmd->GetCmdType() == EGpuCommandType::kPresent)
            PresentImpl(d3dcmd);
        else {};
    }
    void D3DContext::SubmitGpuCommandSync(GfxCommand *cmd)
    {
        auto rhi_cmd = RHICommandBufferPool::Get("SyncTask");
        ProcessGpuCommand(cmd, rhi_cmd.get());
        ExecuteRHICommandBuffer(rhi_cmd.get());
        RHICommandBufferPool::Release(rhi_cmd);
    }
#pragma endregion
}// namespace Ailu
