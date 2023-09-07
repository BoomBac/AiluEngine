#pragma once
#ifndef __DX12BASE_RENDERER_H__
#define __DX12BASE_RENDERER_H__

#include <d3dx12.h>
#include <dxgi1_6.h>

#include "Render/Camera.h"

#include "Framework/Math/ALMath.hpp"

using Microsoft::WRL::ComPtr;
namespace Ailu
{
    struct SimpleVertex
    {
        Vector3f position;
        Vector4f color;
    };

    struct SceneConstantBuffer
    {
        Matrix4x4f _MatrixV;
        Matrix4x4f _MatrixVP;
        float padding[32]; // Padding so the constant buffer is 256-byte aligned.
    };
    static_assert((sizeof(SceneConstantBuffer) % 256) == 0, "Constant Buffer size must be 256-byte aligned");

    class DXBaseRenderer
    {
    public:
        DXBaseRenderer(UINT width, UINT height);
        void OnInit();
        void OnUpdate();
        void OnRender();
        void OnDestroy();
    private:
        static const UINT FrameCount = 2;

        // Pipeline objects.
        CD3DX12_VIEWPORT m_viewport;
        CD3DX12_RECT m_scissorRect;
        ComPtr<IDXGISwapChain3> m_swapChain;
        ComPtr<ID3D12Device> m_device;
        ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
        ComPtr<ID3D12CommandAllocator> m_commandAllocators[FrameCount];
        ComPtr<ID3D12CommandQueue> m_commandQueue;
        ComPtr<ID3D12RootSignature> m_rootSignature;
        ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
        ComPtr<ID3D12DescriptorHeap> m_cbvHeap;
        ComPtr<ID3D12PipelineState> m_pipelineState;
        ComPtr<ID3D12GraphicsCommandList> m_commandList;
        UINT m_rtvDescriptorSize;

        // App resources.
        ComPtr<ID3D12Resource> m_vertexBuffer;
        D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;

        ComPtr<ID3D12Resource> m_constantBuffer;
        SceneConstantBuffer m_constantBufferData;
        UINT8* m_pCbvDataBegin;

        // Synchronization objects.
        UINT m_frameIndex;
        HANDLE m_fenceEvent;
        ComPtr<ID3D12Fence> m_fence;
        UINT64 m_fenceValues[FrameCount];

        UINT _width;
        UINT _height;
        float m_aspectRatio;

        std::unique_ptr<Camera> _p_scene_camera;

        void LoadPipeline();
        void LoadAssets();
        void PopulateCommandList();
        void WaitForGpu();
        void MoveToNextFrame();
    };
}

#endif // !DX12BASE_RENDERER_H__

