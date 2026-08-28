#ifndef __SWAPCHAIN_H__
#define __SWAPCHAIN_H__
#include "Inc/Render/Texture.h"
#include "Render/RenderConstants.h"
#include "D3DResourceBase.h"
#include <d3dx12.h>
#include <wrl/client.h>
#include <dxgi1_6.h>

using Microsoft::WRL::ComPtr;

namespace Ailu
{
	namespace RHI::DX12
	{
        struct D3DSwapchainInitializer
        {
            DXGI_SWAP_CHAIN_DESC1 _swapchain_desc;
            EALGFormat _format;
            ID3D12Device *_device;
            Window* _window;
            ID3D12CommandQueue *_command_queue;
            IDXGIFactory6 *_factory;
            bool _is_fullscreen = false;
            Vector<D3D12_CPU_DESCRIPTOR_HANDLE> _rtvs;
        };

        class D3DSwapchainTexture : public Render::SwapchainTexture
        {
        public:
            D3DSwapchainTexture(D3DSwapchainInitializer &initializer);
            virtual ~D3DSwapchainTexture();
            Render::NativeHandle NativeResource() final { return {Render::RendererAPI::ERenderAPI::kDirectX12, _back_buffers.empty() ? nullptr : _back_buffers[_cur_backbuf_index].Get()}; }
            void Resize(u16 w, u16 h) final;
            void PreparePresent(RHICommandBuffer *cmd) final;
            void Present() final;
            void StateTranslation(RHICommandBuffer* rhi_cmd,Render::EResourceState new_state,u32 sub_res) final;
            void ApplyResourceBarrier(RHICommandBuffer *rhi_cmd, Render::EResourceState before_state,
                                      Render::EResourceState after_state, u32 sub_res) final;
            void TrackResourceState(Render::EResourceState new_state, u32 sub_res = Render::kTotalSubRes) final;
            Render::EResourceState CurrentResourceState(u32 sub_res = Render::kTotalSubRes) const final;
            bool TryCurrentResourceState(Render::EResourceState &out_state, u32 sub_res = Render::kTotalSubRes) const final;
            D3D12_CPU_DESCRIPTOR_HANDLE *TargetCPUHandle(RHICommandBuffer *cmd);
            u8 GetCurrentBackBufferIndex() final
            { 
                _cur_backbuf_index = (u8) _swapchain->GetCurrentBackBufferIndex();
                return _cur_backbuf_index;
            };
        private:
            ComPtr<IDXGISwapChain3> _swapchain;
            Vector<ComPtr<ID3D12Resource>> _back_buffers;
            Vector<D3D12_CPU_DESCRIPTOR_HANDLE> _rtvs;
            ID3D12Device *_device;
            Window *_window;
            Vector<D3DResourceStateGuard*> _state_guard;
        };
	}
}


#endif// !__SWAPCHAIN_H_-
