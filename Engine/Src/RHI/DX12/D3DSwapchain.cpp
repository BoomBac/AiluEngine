#include "RHI/DX12/D3DSwapchain.h"
#include "RHI/DX12/dxhelper.h"
#include "RHI/DX12/D3DCommandBuffer.h"
#include "Render/CommandBuffer.h"
#include "Framework/Common/Allocator.hpp"
#include "Framework/Common/Window.h"



namespace Ailu::RHI::DX12
{
    namespace
    {
        inline bool TryCurrentResourceStateFromGuard(const D3DResourceStateGuard &state_guard, Render::EResourceState &out_state,
                                                     u32 sub_res)
        {
            const u32 d3d_sub_res = sub_res == Render::kTotalSubRes ? D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES : sub_res;
            D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
            if (!state_guard.TryCurState(state, d3d_sub_res))
                return false;
            out_state = D3DConvertUtils::ToALResState(state);
            return true;
        }

        inline Render::EResourceState CurrentResourceStateFromGuard(const Render::GpuResource *resource,
                                                                    const D3DResourceStateGuard &state_guard, u32 sub_res)
        {
            Render::EResourceState state = Render::EResourceState::kCommon;
            if (TryCurrentResourceStateFromGuard(state_guard, state, sub_res))
                return state;
            return resource->GpuResource::CurrentResourceState(sub_res);
        }
    }

    D3DSwapchainTexture::D3DSwapchainTexture(D3DSwapchainInitializer &initializer) 
        : SwapchainTexture((u16) initializer._swapchain_desc.Width, (u16) initializer._swapchain_desc.Height, Render::ConvertPixelFormatFormatToRenderTexture(initializer._format))
    {
        _device = initializer._device;
        _window = initializer._window;
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
        RegisterWindowBackBuffer(_window, this);
    }

    D3DSwapchainTexture::~D3DSwapchainTexture()
    {
        UnregisterWindowBackBuffer(_window);
        for (u16 i = 0; i < _buffer_num; i++)
        {
            _back_buffers[i].Reset();
            AL_DELETE(_state_guard[i]);
        }
        _swapchain.Reset();
    }
    void D3DSwapchainTexture::PreparePresent(RHICommandBuffer *cmd)
    {
        auto d3dcmd = static_cast<D3DCommandBuffer *>(cmd);
        d3dcmd->EnsureResourceState(*_state_guard[_cur_backbuf_index], D3D12_RESOURCE_STATE_PRESENT);
        GpuResource::TrackResourceState(Render::EResourceState::kPresent);
    }

    void D3DSwapchainTexture::Present()
    {
        ThrowIfFailed(_swapchain->Present(1, 0));
        _cur_backbuf_index = _swapchain->GetCurrentBackBufferIndex();
    }

    void D3DSwapchainTexture::StateTranslation(RHICommandBuffer *rhi_cmd, Render::EResourceState new_state, u32 sub_res)
    {
        auto d3dcmd = static_cast<D3DCommandBuffer *>(rhi_cmd);
        d3dcmd->EnsureResourceState(*_state_guard[_cur_backbuf_index], D3DConvertUtils::FromALResState(new_state), sub_res);
        GpuResource::TrackResourceState(new_state, sub_res);
    }

    void D3DSwapchainTexture::ApplyResourceBarrier(RHICommandBuffer *rhi_cmd, Render::EResourceState before_state,
                                                    Render::EResourceState after_state, u32 sub_res)
    {
        auto d3dcmd = static_cast<D3DCommandBuffer *>(rhi_cmd);
        d3dcmd->ApplyResourceBarrier(*_state_guard[_cur_backbuf_index], D3DConvertUtils::FromALResState(before_state),
                                     D3DConvertUtils::FromALResState(after_state), sub_res);
    }

    void D3DSwapchainTexture::TrackResourceState(Render::EResourceState new_state, u32 sub_res)
    {
        _state_guard[_cur_backbuf_index]->TrackResourceState(D3DConvertUtils::FromALResState(new_state), sub_res);
        GpuResource::TrackResourceState(new_state, sub_res);
    }

    Render::EResourceState D3DSwapchainTexture::CurrentResourceState(u32 sub_res) const
    {
        return CurrentResourceStateFromGuard(this, *_state_guard[_cur_backbuf_index], sub_res);
    }

    bool D3DSwapchainTexture::TryCurrentResourceState(Render::EResourceState &out_state, u32 sub_res) const
    {
        if (TryCurrentResourceStateFromGuard(*_state_guard[_cur_backbuf_index], out_state, sub_res))
            return true;
        if (sub_res == Render::kTotalSubRes)
            return false;
        return GpuResource::TryCurrentResourceState(out_state, sub_res);
    }


    D3D12_CPU_DESCRIPTOR_HANDLE *D3DSwapchainTexture::TargetCPUHandle(RHICommandBuffer *cmd)
    {
        auto d3dcmd = static_cast<D3DCommandBuffer *>(cmd);
        d3dcmd->EnsureResourceState(*_state_guard[_cur_backbuf_index], D3D12_RESOURCE_STATE_RENDER_TARGET);
        GpuResource::TrackResourceState(Render::EResourceState::kRenderTarget);
        return &_rtvs[_cur_backbuf_index];
    }

    void D3DSwapchainTexture::Resize(u16 w, u16 h)
    {
        if (w == _width && h == _height)
            return;
        for (u16 i = 0; i < _buffer_num; i++)
        {
            AL_DELETE(_state_guard[i]);
            _back_buffers[i].Reset();
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


