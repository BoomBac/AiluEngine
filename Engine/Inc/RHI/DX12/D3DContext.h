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
#include "Framework/Assets/Mesh.h"

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
        Matrix4x4f _MatrixP;
        Matrix4x4f _MatrixVP;
        float padding[16]; // Padding so the constant buffer is 256-byte aligned.
    };
    static_assert((sizeof(SceneConstantBuffer) % 256) == 0, "Constant Buffer size must be 256-byte aligned");

    class D3DContext : public GraphicsContext
    {
        friend class D3DRendererAPI;
    public:
        D3DContext(WinWindow* window);
        ~D3DContext();
        void Init() override;
        void Present() override;
        void Clear(Vector4f color, float depth, bool clear_color, bool clear_depth);
        inline static D3DContext* GetInstance() { return s_p_d3dcontext; };

        ID3D12Device* GetDevice();
        ID3D12GraphicsCommandList* GetCmdList();
        ComPtr<ID3D12DescriptorHeap> GetDescriptorHeap();
        const D3D12_CONSTANT_BUFFER_VIEW_DESC& GetCBufferViewDesc(uint32_t index) const;
        uint8_t* GetCBufferPtr();

        void DrawIndexedInstanced(uint32_t index_count, uint32_t instance_count, const Matrix4x4f& transform);
        void DrawInstanced(uint32_t vertex_count, uint32_t instance_count, const Matrix4x4f& transform);

    private:
        void Destroy();
        void LoadPipeline();
        void LoadAssets();
        void PopulateCommandList();
        void WaitForGpu();
        void MoveToNextFrame();
        void InitCBVSRVUAVDescHeap();
        void CreateDepthStencilTarget();
        void CreateDescriptorHeap();
        D3D12_GPU_DESCRIPTOR_HANDLE GetCBVGPUDescHandle(uint32_t index) const;


    private:
        inline static D3DContext* s_p_d3dcontext = nullptr;
        WinWindow* _window;

        // Pipeline objects.
        ComPtr<IDXGISwapChain3> m_swapChain;
        ComPtr<ID3D12Device> m_device;
        ComPtr<ID3D12Resource> _color_buffer[D3DConstants::kFrameCount];
        ComPtr<ID3D12Resource> _depth_buffer[D3DConstants::kFrameCount];
        ComPtr<ID3D12CommandAllocator> m_commandAllocators[D3DConstants::kFrameCount];
        ComPtr<ID3D12CommandQueue> m_commandQueue;
        ComPtr<ID3D12RootSignature> m_rootSignature;
        ComPtr<ID3D12PipelineState> m_pipelineState;
        ComPtr<ID3D12GraphicsCommandList> m_commandList;
        ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
        ComPtr<ID3D12DescriptorHeap> m_cbvHeap;
        ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
        uint8_t m_rtvDescriptorSize;
        uint8_t _dsv_desc_size;
        uint8_t _cbv_desc_size;
        uint8_t* _p_cbuffer = nullptr;

        // App resources.
        ComPtr<ID3D12Resource> m_vertexBuffer;
        D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;

        Ref<VertexBuffer> _p_vertex_buf;
        Ref<VertexBuffer> _p_vertex_buf0;
        Ref<IndexBuffer> _p_index_buf;

        Ref<Shader> _p_standard_shader;
        Ref<Material> _mat_red;
        Ref<Material> _mat_green;

        Ref<Mesh> _plane;

        uint32_t _render_object_index = 0u;

        ComPtr<ID3D12Resource> m_constantBuffer;
        SceneConstantBuffer _perframe_scene_data;
        std::vector<D3D12_CONSTANT_BUFFER_VIEW_DESC> _cbuf_views;

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

