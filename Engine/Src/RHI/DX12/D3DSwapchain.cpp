#include "RHI/DX12/D3DSwapchain.h"
#include "RHI/DX12/dxhelper.h"
#include "RHI/DX12/D3DCommandBuffer.h"
#include "Render/CommandBuffer.h"
#include "Framework/Common/Allocator.hpp"
#include "Framework/Common/Window.h"



namespace Ailu::RHI::DX12
{
    D3DSwapchainTexture::D3DSwapchainTexture(D3DSwapchainInitializer &initializer) 
        : SwapchainTexture((u16) initializer._swapchain_desc.Width, (u16) initializer._swapchain_desc.Height, Render::ConvertPixelFormatFormatToRenderTexture(initializer._format))
    {
        _device = initializer._device;
        _buffer_num = (u16) initializer._swapchain_desc.BufferCount;
        _rtvs = initializer._rtvs;
        _back_buffers.resize(initializer._swapchain_desc.BufferCount);
        _pixel_format = initializer._format;
        _state_guard.resize(_buffer_num);
        ComPtr<IDXGISwapChain1> swapChain;
        ThrowIfFailed(initializer._factory->CreateSwapChainForHwnd(initializer._command_queue, (HWND) initializer._window->GetNativeWindowPtr(), &initializer._swapchain_desc, nullptr, nullptr, &swapChain));
        // This sample does not support fullscreen transitions.
        ThrowIfFailed(initializer._factory->MakeWindowAssociation((HWND)initializer._window->GetNativeWindowPtr(), DXGI_MWA_NO_ALT_ENTER));
        ThrowIfFailed(swapChain->SetFullscreenState(FALSE, nullptr));
        ThrowIfFailed(swapChain.As(&_swapchain));
        for (u16 i = 0; i < initializer._swapchain_desc.BufferCount; i++)
        {
            ThrowIfFailed(_swapchain->GetBuffer(i, IID_PPV_ARGS(_back_buffers[i].GetAddressOf())));
            _back_buffers[i]->SetName(std::format(L"BackBuffer_{}", i).c_str());
            initializer._device->CreateRenderTargetView(_back_buffers[i].Get(), nullptr, _rtvs[i]);
            _state_guard[i] = AL_NEW(D3DResourceStateGuard,_back_buffers[i].Get(), D3D12_RESOURCE_STATE_PRESENT, 1u);
        }
        _load_action = Render::ELoadStoreAction::kNotCare;
        _cur_backbuf_index = _swapchain->GetCurrentBackBufferIndex();
        s_window_backbuffers[reinterpret_cast<u64>(initializer._window)] = this;
    }

    D3DSwapchainTexture::~D3DSwapchainTexture()
    {
        for (u16 i = 0; i < _buffer_num; i++)
        {
            _back_buffers[i].Reset();
            AL_DELETE(_state_guard[i]);
        }
        _swapchain.Reset();
    }
    void D3DSwapchainTexture::PreparePresent(RHICommandBuffer *cmd)
    {
        _state_guard[_cur_backbuf_index]->MakesureResourceState(static_cast<D3DCommandBuffer *>(cmd)->NativeCmdList(), D3D12_RESOURCE_STATE_PRESENT);
    }

    void D3DSwapchainTexture::Present()
    {
        ThrowIfFailed(_swapchain->Present(1, 0));
        _cur_backbuf_index = _swapchain->GetCurrentBackBufferIndex();
    }

    D3D12_CPU_DESCRIPTOR_HANDLE *D3DSwapchainTexture::TargetCPUHandle(RHICommandBuffer *cmd)
    {
        _state_guard[_cur_backbuf_index]->MakesureResourceState(static_cast<D3DCommandBuffer *>(cmd)->NativeCmdList(), D3D12_RESOURCE_STATE_RENDER_TARGET);
        RenderTexture::ResetRenderTarget(this);
        return &_rtvs[_cur_backbuf_index];
    }

    void D3DSwapchainTexture::Resize(u16 w, u16 h)
    {
        if (w == _width && h == _height)
            return;
        for (u16 i = 0; i < _buffer_num; i++)
        {
            _back_buffers[i].Reset();
            AL_DELETE(_state_guard[i]);
        }
        SwapchainTexture::Resize(w, h);
        DXGI_SWAP_CHAIN_DESC desc = {};
        _swapchain->GetDesc(&desc);
        ThrowIfFailed(_swapchain->ResizeBuffers(_buffer_num, w, h, desc.BufferDesc.Format, desc.Flags));
        _cur_backbuf_index = _swapchain->GetCurrentBackBufferIndex();//重新获取，不然resize时会黑屏
        for (u16 i = 0; i < _buffer_num; i++)
        {
            ThrowIfFailed(_swapchain->GetBuffer(i, IID_PPV_ARGS(&_back_buffers[i])));
            _back_buffers[i]->SetName(std::format(L"BackBuffer_{}", i).c_str());
            _device->CreateRenderTargetView(_back_buffers[i].Get(), nullptr, _rtvs[i]);
            _state_guard[i] = AL_NEW(D3DResourceStateGuard, _back_buffers[i].Get(), D3D12_RESOURCE_STATE_PRESENT, 1u);
        }
    }
}


