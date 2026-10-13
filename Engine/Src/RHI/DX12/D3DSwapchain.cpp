#include "RHI/DX12/D3DSwapchain.h"
#include "RHI/DX12/StreamlineDx12Bridge.h"
#include "RHI/DX12/StreamlineRuntime.h"
#include "RHI/DX12/dxhelper.h"
#include "RHI/DX12/D3DCommandBuffer.h"
#include "RHI/DX12/D3DContext.h"
#include "Render/CommandBuffer.h"
#include "Framework/Common/Allocator.hpp"
#include "Framework/Common/Log.h"
#include "Framework/Common/Window.h"



namespace Ailu::RHI::DX12
{
    D3DSwapchainTexture::D3DSwapchainTexture(D3DSwapchainInitializer &initializer) 
        : SwapchainTexture((u16) initializer._swapchain_desc.Width, (u16) initializer._swapchain_desc.Height, Render::ConvertPixelFormatFormatToRenderTexture(initializer._format))
    {
        _device = initializer._device;
        _window = initializer._window;
        _buffer_num = (u16) initializer._swapchain_desc.BufferCount;
        _rtvs = initializer._rtvs;
        _back_buffers.resize(initializer._swapchain_desc.BufferCount);
        _pixel_format = initializer._format;
        ComPtr<IDXGISwapChain1> swapChain;
        IDXGIFactory6* creation_factory = initializer._proxy_factory != nullptr
                                                  ? initializer._proxy_factory : initializer._factory;
        ThrowIfFailed(creation_factory->CreateSwapChainForHwnd(initializer._command_queue,
                                                                (HWND) initializer._window->GetNativeWindowPtr(),
                                                                &initializer._swapchain_desc, nullptr, nullptr,
                                                                &swapChain));
        _is_streamline_proxy = initializer._streamline_runtime != nullptr && initializer._proxy_factory != nullptr;
        if (_is_streamline_proxy)
        {
            ComPtr<IDXGISwapChain1> native_swapchain;
            if (StreamlineDx12Bridge::UpgradeSwapchain(*initializer._streamline_runtime, swapChain.Get(),
                                                        native_swapchain.GetAddressOf()))
            {
                ThrowIfFailed(native_swapchain.As(&_native_swapchain));
            }
            else
            {
                LOG_ERROR("Streamline: native swapchain lookup failed; aborting incomplete DXGI initialization.");
                ThrowIfFailed(E_NOINTERFACE);
            }
        }
        else
            ThrowIfFailed(swapChain.As(&_native_swapchain));
        // This sample does not support fullscreen transitions.
        ThrowIfFailed(initializer._factory->MakeWindowAssociation((HWND)initializer._window->GetNativeWindowPtr(), DXGI_MWA_NO_ALT_ENTER));
        ThrowIfFailed(swapChain->SetFullscreenState(FALSE, nullptr));
        ThrowIfFailed(swapChain.As(&_swapchain));
        for (u16 i = 0; i < initializer._swapchain_desc.BufferCount; i++)
        {
            ThrowIfFailed(_swapchain->GetBuffer(i, IID_PPV_ARGS(_back_buffers[i].GetAddressOf())));
            _back_buffers[i].ResetStateId();
            _back_buffers[i]->SetName(std::format(L"BackBuffer_{}", i).c_str());
            initializer._device->CreateRenderTargetView(_back_buffers[i].Get(), nullptr, _rtvs[i]);
            static_cast<D3DContext &>(Render::GraphicsContext::Get()).SetExternalState(_back_buffers[i], Render::EResourceState::kPresent);
        }
        _load_action = Render::ELoadStoreAction::kNotCare;
        _cur_backbuf_index = _swapchain->GetCurrentBackBufferIndex();
        if (_is_streamline_proxy)
            LOG_INFO("Streamline: hooked DXGI swapchain APIs for window '{}'.",
                     ToChar(initializer._window->GetTitle()));
        SetCreatedFence(0u);
        RegisterWindowBackBuffer(_window, this);
    }

    D3DSwapchainTexture::~D3DSwapchainTexture()
    {
        UnregisterWindowBackBuffer(_window);
        for (u16 i = 0; i < _buffer_num; i++) _back_buffers[i].Reset();
        _native_swapchain.Reset();
        _swapchain.Reset();
    }
    void D3DSwapchainTexture::PreparePresent(RHICommandBuffer *cmd)
    {
        auto d3dcmd = static_cast<D3DCommandBuffer *>(cmd);
        d3dcmd->RequireState(_back_buffers[_cur_backbuf_index], Render::EResourceState::kPresent);
    }

    void D3DSwapchainTexture::Present()
    {
        static std::atomic<bool> s_logged_streamline_present = false;
        if (_is_streamline_proxy && !s_logged_streamline_present.exchange(true))
            LOG_INFO("Streamline: first proxy Present observed.");
        const HRESULT present_hr = _swapchain->Present(1, 0);
        if (FAILED(present_hr))
            static_cast<D3DContext &>(Render::GraphicsContext::Get()).ReportDeviceRemoved(present_hr, "IDXGISwapChain::Present");
        ThrowIfFailed(present_hr);
        _cur_backbuf_index = _swapchain->GetCurrentBackBufferIndex();
    }

    void D3DSwapchainTexture::RequireState(RHICommandBuffer *rhi_cmd, Render::EResourceState state, u32 sub_res)
    {
        auto d3dcmd = static_cast<D3DCommandBuffer *>(rhi_cmd);
        d3dcmd->RequireState(_back_buffers[_cur_backbuf_index], state, sub_res);
    }

    void D3DSwapchainTexture::UavBarrier(RHICommandBuffer *rhi_cmd)
    {
        auto d3dcmd = static_cast<D3DCommandBuffer *>(rhi_cmd);
        d3dcmd->UavBarrier(_back_buffers[_cur_backbuf_index]);
    }


    D3D12_CPU_DESCRIPTOR_HANDLE *D3DSwapchainTexture::TargetCPUHandle(RHICommandBuffer *cmd)
    {
        auto d3dcmd = static_cast<D3DCommandBuffer *>(cmd);
        d3dcmd->RequireState(_back_buffers[_cur_backbuf_index], Render::EResourceState::kRenderTarget);
        return &_rtvs[_cur_backbuf_index];
    }

    void D3DSwapchainTexture::Resize(u16 w, u16 h)
    {
        if (w == _width && h == _height)
            return;
        // ResizeBuffers 要求调用时没有任何未释放的后台缓冲引用，否则返回 DXGI_ERROR_INVALID_CALL。
        // 因此 setExternalState/resize 必须发生在帧边界（见 D3DContext::ApplyPendingSwapChainResizes），
        // 此时上一帧的命令缓冲已全部归还到池中、没有任何命令缓冲在录制。
        for (u16 i = 0; i < _buffer_num; i++)
        {
            if (_back_buffers[i].Get() == nullptr)
                continue;
            // 此时引用计数应为 1（仅 _back_buffers 自身持有），大于 1 说明还有地方没释放后台缓冲。
            const ULONG ref_count = _back_buffers[i].Get()->AddRef();
            _back_buffers[i].Get()->Release();
            if (ref_count > 1u)
                LOG_WARNING("D3DSwapchainTexture::Resize: back buffer {} still has {} reference(s) before ResizeBuffers", i, ref_count);
        }
        for (u16 i = 0; i < _buffer_num; i++)
            _back_buffers[i].Reset();
        SwapchainTexture::Resize(w, h);
        DXGI_SWAP_CHAIN_DESC desc = {};
        ThrowIfFailed(_native_swapchain->GetDesc(&desc));
        const HRESULT hr = _swapchain->ResizeBuffers(_buffer_num, w, h, desc.BufferDesc.Format, desc.Flags);
        if (FAILED(hr))
        {
            LOG_ERROR("D3DSwapchainTexture::Resize: ResizeBuffers({}x{}) failed, hr = 0x{:08X}", w, h, static_cast<u32>(hr));
            static_cast<D3DContext &>(Render::GraphicsContext::Get()).ReportDeviceRemoved(hr, "IDXGISwapChain::ResizeBuffers");
        }
        ThrowIfFailed(hr);
        _cur_backbuf_index = _swapchain->GetCurrentBackBufferIndex();//重新获取，不然resize时会黑屏
        for (u16 i = 0; i < _buffer_num; i++)
        {
            ThrowIfFailed(_swapchain->GetBuffer(i, IID_PPV_ARGS(_back_buffers[i].GetAddressOf())));
            _back_buffers[i].ResetStateId();
            _back_buffers[i]->SetName(std::format(L"BackBuffer_{}", i).c_str());
            _device->CreateRenderTargetView(_back_buffers[i].Get(), nullptr, _rtvs[i]);
            static_cast<D3DContext &>(Render::GraphicsContext::Get()).SetExternalState(_back_buffers[i], Render::EResourceState::kPresent);
        }
    }
}
