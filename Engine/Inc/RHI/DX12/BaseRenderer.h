#pragma once
#ifndef __DX12BASE_RENDERER_H__
#define __DX12BASE_RENDERER_H__

#include <d3dx12.h>
#include <dxgi1_6.h>

using Microsoft::WRL::ComPtr;
namespace Ailu
{
    namespace Engine
    {
        class DXBaseRenderer 
        {
        public:
            DXBaseRenderer(UINT width,UINT height);
            void OnInit();
            void OnUpdate();
            void OnRender();
            void OnDestroy();
        private:
            static const UINT FrameCount = 2;

            // Pipeline objects.
            ComPtr<IDXGISwapChain4> m_swapChain;
            ComPtr<ID3D12Device> m_device;
            ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
            ComPtr<ID3D12CommandAllocator> m_commandAllocator;
            ComPtr<ID3D12CommandQueue> m_commandQueue;
            ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
            ComPtr<ID3D12PipelineState> m_pipelineState;
            ComPtr<ID3D12GraphicsCommandList> m_commandList;
            UINT m_rtvDescriptorSize;

            // Synchronization objects.
            UINT m_frameIndex;
            HANDLE m_fenceEvent;
            ComPtr<ID3D12Fence> m_fence;
            UINT64 m_fenceValue;
            UINT _width;
            UINT _height;

            void LoadPipeline();
            void LoadAssets();
            void PopulateCommandList();
            void WaitForPreviousFrame();

        };
    }
}

#endif // !DX12BASE_RENDERER_H__

