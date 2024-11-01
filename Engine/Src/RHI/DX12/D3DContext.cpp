#include "RHI/DX12/D3DContext.h"
#include "RHI/DX12/GPUResourceManager.h"
#include "Ext/imgui/backends/imgui_impl_dx12.h"
#include "Framework/Common/Assert.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/Profiler.h"
#include "RHI/DX12/D3DCommandBuffer.h"
#include "RHI/DX12/D3DGPUTimer.h"
#include "RHI/DX12/dxhelper.h"
#include "Render/FrameResource.h"
#include "Render/Gizmo.h"
#include "Render/GraphicsPipelineStateObject.h"
#include "Render/RenderingData.h"
#include "pch.h"
#include <dxgidebug.h>
#include <limits>

#include <d3d11.h>



#ifdef _PIX_DEBUG
#include "Ext/pix/Include/WinPixEventRuntime/pix3.h"
#endif// _PIX_DEBUG


namespace Ailu
{
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

    CPUVisibleDescriptorAllocator *g_pCPUDescriptorAllocator;
    GPUVisibleDescriptorAllocator *g_pGPUDescriptorAllocator;

    D3DContext::D3DContext(WinWindow *window) : _window(window), _width(window->GetWidth()), _height(window->GetHeight())
    {
        m_aspectRatio = (float) _width / (float) _height;
#ifdef _PIX_DEBUG
        PIXLoadLatestWinPixGpuCapturerLibrary();
#endif// _PIX_DEBUG
    }

    D3DContext::~D3DContext()
    {
        Destroy();
        LOG_INFO("D3DContext Destroy");
    }

    void D3DContext::Init()
    {
        g_pGfxContext = this;
        g_pCPUDescriptorAllocator = new CPUVisibleDescriptorAllocator();
        g_pGPUDescriptorAllocator = new GPUVisibleDescriptorAllocator();
        LoadPipeline();
        LoadAssets();
#ifdef DEAR_IMGUI
        //init imgui
        {
            _imgui_allocation = g_pGPUDescriptorAllocator->Allocate(1);
            auto [cpu_handle, gpu_handle] = _imgui_allocation.At(0);
            auto ret = ImGui_ImplDX12_Init(m_device.Get(), RenderConstants::kFrameCount,
                                           RenderConstants::kColorRange == EColorRange::kLDR ? ConvertToDXGIFormat(RenderConstants::kLDRFormat) : ConvertToDXGIFormat(RenderConstants::kHDRFormat),
                                           _imgui_allocation.Page()->GetHeap().Get(), cpu_handle, gpu_handle);
        }
#endif// DEAR_IMGUI
        m_commandList->Close();
        ID3D12CommandList *ppCommandLists[] = {m_commandList.Get()};
        m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
        WaitForGpu();
        _p_gpu_timer = MakeScope<D3DGPUTimer>(m_device.Get(), m_commandQueue.Get(), RenderConstants::kFrameCount);
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
        if (auto num = g_pGPUDescriptorAllocator->ReleaseSpace(); num > 0)
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

    u64 D3DContext::ExecuteCommandBuffer(Ref<CommandBuffer> &cmd)
    {
        cmd->Close();
        auto d3dcmd = static_cast<D3DCommandBuffer *>(cmd.get());
        ID3D12CommandList *ppCommandLists[] = {d3dcmd->GetCmdList()};
#ifdef _PIX_DEBUG
        PIXBeginEvent(m_commandQueue.Get(), cmd->GetID(), ToWStr(cmd->GetName()).c_str());
#endif// _PIX_DEBUG
        m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
        ++_fence_value;
        ThrowIfFailed(m_commandQueue->Signal(_p_cmd_buffer_fence.Get(), _fence_value));
#ifdef _PIX_DEBUG
        PIXEndEvent(m_commandQueue.Get());
#endif// _PIX_DEBUG

        if (_cmd_target_fence_value.contains(cmd->GetID()))
            _cmd_target_fence_value[cmd->GetID()] = _fence_value;
        else
            _cmd_target_fence_value.insert(std::make_pair(cmd->GetID(), _fence_value));
        return _fence_value;
    }

    u64 D3DContext::ExecuteAndWaitCommandBuffer(Ref<CommandBuffer> &cmd)
    {
        auto ret = ExecuteCommandBuffer(cmd);
        WaitForGpu();
        return ret;
    }

    void D3DContext::BeginBackBuffer(CommandBuffer *cmd)
    {
        auto dxcmd = static_cast<D3DCommandBuffer *>(cmd)->GetCmdList();
        auto bar_before = CD3DX12_RESOURCE_BARRIER::Transition(_color_buffer[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        dxcmd->ResourceBarrier(1, &bar_before);
        //CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, _rtv_desc_size);
        auto rtv_handle = _rtv_allocation.At(m_frameIndex);
        dxcmd->OMSetRenderTargets(1, &rtv_handle, FALSE, nullptr);
        dxcmd->ClearRenderTargetView(rtv_handle, Colors::kBlack, 0, nullptr);
    }

    void D3DContext::EndBackBuffer(CommandBuffer *cmd)
    {
#ifndef _DIRECT_WRITE
        auto dxcmd = static_cast<D3DCommandBuffer *>(cmd)->GetCmdList();
        auto bar_after = CD3DX12_RESOURCE_BARRIER::Transition(_color_buffer[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        dxcmd->ResourceBarrier(1, &bar_after);
#endif// !_DIRECT_WRITE
    }

    void D3DContext::DrawOverlay(CommandBuffer *cmd)
    {
        ProfileBlock p(cmd, "ImGui");
        auto d3dcmd = static_cast<D3DCommandBuffer *>(cmd);
        _imgui_allocation.SetupDescriptorHeap(static_cast<D3DCommandBuffer *>(cmd));
#ifdef DEAR_IMGUI
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), d3dcmd->GetCmdList());
#endif// DEAR_IMGUI
    }

    void D3DContext::Destroy()
    {
        // Ensure that the GPU is no longer referencing resources that are about to be
        // cleaned up by the destructor.
        WaitForGpu();
        CloseHandle(m_fenceEvent);
        //_p_gpu_timer->ReleaseDevice();
        //在这里析构分配器应该会有问题，纹理实际在在这之后还需要归还之前的分配
        DESTORY_PTR(g_pCPUDescriptorAllocator);
        DESTORY_PTR(g_pGPUDescriptorAllocator);
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
        _rtv_allocation = g_pCPUDescriptorAllocator->Allocate(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, RenderConstants::kFrameCount);

        // Create a RTV for each frame.
        for (UINT n = 0; n < RenderConstants::kFrameCount; n++)
        {
            ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&_color_buffer[n])));
            m_device->CreateRenderTargetView(_color_buffer[n].Get(), nullptr, _rtv_allocation.At(n));
            _color_buffer[n]->SetName(std::format(L"BackBuffer_{}",n).c_str());
            ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[n])));
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
        //if (!_resize_msg.empty())
        //{
        //    auto& [w, h] = _resize_msg.top();
        //    ResizeSwapChainImpl(w,h);
        //    do
        //    {
        //        _resize_msg.pop();
        //    } while (!_resize_msg.empty());
        //}
        auto cmd = CommandBufferPool::Get("GUI Reslove");
        {
            ProfileBlock p(cmd.get(), "GUI Reslove");
            g_pGfxContext->BeginBackBuffer(cmd.get());
            g_pGfxContext->DrawOverlay(cmd.get());
            g_pGfxContext->EndBackBuffer(cmd.get());
        }
        g_pGfxContext->ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);

        DoResourceTask();
        //ThrowIfFailed(m_commandAllocators[m_frameIndex]->Reset());
        //ThrowIfFailed(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), nullptr));
        //ThrowIfFailed(m_commandList->Close());
//		ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
//		m_commandQueue->ExecuteCommandLists(1, ppCommandLists);
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
        // Present the frame.
        ThrowIfFailed(m_swapChain->Present(1, 0));
        {
            CPUProfileBlock p("WaitForGPU");
            WaitForGpu();
            m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
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
                LOG_INFO("D3DContext::Present: Resource cleanup, {} byte released.",release_size);
            }
            if (RenderTexture::TotalGPUMemerySize() > kMaxRenderTextureMemorySize)
            {
                g_pRenderTexturePool->TryCleanUp();
                g_pGPUDescriptorAllocator->ReleaseSpace();
            }
        }
        _p_gpu_timer->EndFrame();
        CommandBufferPool::ReleaseAll();
        if (_is_next_frame_capture)
        {
            BeginCapture();
            _is_next_frame_capture = false;
            _is_cur_frame_capturing = true;
        }
    }

    void D3DContext::DoResourceTask()
    {
        while (!_resource_task.empty())
        {
            _resource_task.front()();
            _resource_task.pop();
        }
    }

    const u64 &D3DContext::GetFenceValue(const u32 &cmd_index) const
    {
        static u64 u64_max = std::numeric_limits<u64>::max();
        if (!_cmd_target_fence_value.contains(cmd_index))
        {
            LOG_WARNING("GetFenceValue with invaild cmd index");
            return u64_max;
        }
        return _cmd_target_fence_value.at(cmd_index);
    }

    u64 D3DContext::GetCurFenceValue() const
    {
        return _p_cmd_buffer_fence->GetCompletedValue();
    }

    bool D3DContext::IsCommandBufferReady(const u32 cmd_index)
    {
        static u64 u64_max = std::numeric_limits<u64>::max();
        if (!_cmd_target_fence_value.contains(cmd_index))
        {
            return true;
        }
        else
        {
            return _p_cmd_buffer_fence->GetCompletedValue() >= _cmd_target_fence_value.at(cmd_index);
        }
    }

    void D3DContext::SubmitRHIResourceBuildTask(RHIResourceTask task)
    {
        _resource_task.push(task);
    }

    void D3DContext::TakeCapture()
    {
        _is_next_frame_capture = true;
    }

    void D3DContext::TrackResource(FrameResource *resource)
    {
        //该函数在cmd记录执行过程中调用，所有该资源的围栏值为当前cmd执行在执行cmd fence value+1时
        resource->SetFenceValue(_fence_value + 1);
    }

    bool D3DContext::IsResourceReferencedByGPU(FrameResource *resource)
    {
        return _p_cmd_buffer_fence->GetCompletedValue() <= resource->GetFenceValue();
    }

    void D3DContext::ResizeSwapChain(const u32 width, const u32 height)
    {
        ResizeSwapChainImpl(width, height);
        //_resize_msg.push(std::make_tuple(width, height));
    }


    void D3DContext::WaitForGpu()
    {
        FlushCommandQueue(m_commandQueue.Get(), _p_cmd_buffer_fence.Get(), _fence_value);
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
            _color_buffer[n]->SetName(std::format(L"BackBuffer_{}",n).c_str());
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
}// namespace Ailu