#pragma once
#ifndef __D3D_CONTEXT_H__
#define __D3D_CONTEXT_H__

#include <d3dx12.h>
#include <dxgi1_6.h>

#include "D3DConstants.h"
#include "Render/Camera.h"
#include "Render/GraphicsContext.h"
#include "Platform/WinWindow.h"
#include "Framework/Math/ALMath.hpp"
#include "Render/Buffer.h"
#include "Render/Shader.h"
#include "Render/Material.h"

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

    class D3DContext : public GraphicsContext
    {
    public:
        D3DContext(WinWindow* window);
        ~D3DContext();
        void Init() override;
        void Present() override;
        void Clear(Vector4f color, float depth, bool clear_color, bool clear_depth);
        static D3DContext* GetInstance();

        ID3D12Device* GetDevice();
        ID3D12GraphicsCommandList* GetCmdList();

    private:
        void Destroy();
        void LoadPipeline();
        void LoadAssets();
        void PopulateCommandList();
        void WaitForGpu();
        void MoveToNextFrame();

    private:
        inline static D3DContext* s_p_d3dcontext = nullptr;
        WinWindow* _window;

        // Pipeline objects.
        CD3DX12_VIEWPORT m_viewport;
        CD3DX12_RECT m_scissorRect;
        ComPtr<IDXGISwapChain3> m_swapChain;
        ComPtr<ID3D12Device> m_device;
        ComPtr<ID3D12Resource> m_renderTargets[D3DConstants::kFrameCount];
        ComPtr<ID3D12CommandAllocator> m_commandAllocators[D3DConstants::kFrameCount];
        ComPtr<ID3D12CommandQueue> m_commandQueue;
        ComPtr<ID3D12RootSignature> m_rootSignature;
        ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
        ComPtr<ID3D12DescriptorHeap> m_cbvHeap;
        ComPtr<ID3D12PipelineState> m_pipelineState;
        ComPtr<ID3D12GraphicsCommandList> m_commandList;
        uint8_t m_rtvDescriptorSize;

        // App resources.
        ComPtr<ID3D12Resource> m_vertexBuffer;
        D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;

        Ref<VertexBuffer> _p_vertex_buf;
        Ref<VertexBuffer> _p_vertex_buf0;
        Ref<IndexBuffer> _p_index_buf;

        Ref<Shader> _p_vs;
        Ref<Shader> _p_ps;
        Ref<Material> _mat_red;
        Ref<Material> _mat_green;

        ComPtr<ID3D12Resource> m_constantBuffer;
        SceneConstantBuffer m_constantBufferData;
        uint8_t* m_pCbvDataBegin;

        // Synchronization objects.
        uint8_t m_frameIndex;
        HANDLE m_fenceEvent;
        ComPtr<ID3D12Fence> m_fence;
        uint64_t m_fenceValues[D3DConstants::kFrameCount];


        uint32_t _width;
        uint32_t _height;
        float m_aspectRatio;

        std::unique_ptr<Camera> _p_scene_camera;


    };
}

#endif // !__D3D_CONTEXT_H__
