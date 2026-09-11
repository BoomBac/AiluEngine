#include "RHI/DX12/D3DSwapchain.h"
#include "RHI/DX12/dxhelper.h"
#include "RHI/DX12/D3DCommandBuffer.h"
#include "RHI/DX12/D3DContext.h"
#include "Render/CommandBuffer.h"
#include "Framework/Common/Allocator.hpp"
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
        ThrowIfFailed(initializer._factory->CreateSwapChainForHwnd(initializer._command_queue, (HWND) initializer._window->GetNativeWindowPtr(), &initializer._swapchain_desc, nullptr, nullptr, &swapChain));
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
        SetCreatedFence(0u);
        RegisterWindowBackBuffer(_window, this);
    }

    D3DSwapchainTexture::~D3DSwapchainTexture()
    {
        UnregisterWindowBackBuffer(_window);
        for (u16 i = 0; i < _buffer_num; i++) _back_buffers[i].Reset();
        _swapchain.Reset();
    }
    void D3DSwapchainTexture::PreparePresent(RHICommandBuffer *cmd)
    {
        auto d3dcmd = static_cast<D3DCommandBuffer *>(cmd);
        d3dcmd->RequireState(_back_buffers[_cur_backbuf_index], Render::EResourceState::kPresent);
    }

    void D3DSwapchainTexture::Present()
    {
        ThrowIfFailed(_swapchain->Present(1, 0));
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
        for (u16 i = 0; i < _buffer_num; i++)
            _back_buffers[i].Reset();
        SwapchainTexture::Resize(w, h);
        DXGI_SWAP_CHAIN_DESC desc = {};
        _swapchain->GetDesc(&desc);
        ThrowIfFailed(_swapchain->ResizeBuffers(_buffer_num, w, h, desc.BufferDesc.Format, desc.Flags));
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


