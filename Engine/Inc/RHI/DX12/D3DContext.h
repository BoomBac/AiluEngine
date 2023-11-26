#pragma once
#ifndef __D3D_CONTEXT_H__
#define __D3D_CONTEXT_H__

#include <d3dx12.h>
#include <dxgi1_6.h>

#include "Render/RenderConstants.h"
#include "Render/GraphicsContext.h"
#include "Platform/WinWindow.h"
#include "Framework/Math/ALMath.hpp"
#include "RHI/DX12/D3DCommandBuffer.h"

using Microsoft::WRL::ComPtr;
namespace Ailu
{
    class D3DContext : public GraphicsContext
    {
        friend class D3DRendererAPI;
        friend class D3DCommandBuffer;
    public:
        D3DContext(WinWindow* window);
        ~D3DContext();
        void Init() override;
        void Present() override;
        void Clear(Vector4f color, float depth, bool clear_color, bool clear_depth);
        inline static D3DContext* GetInstance() { return s_p_d3dcontext; };

        ID3D12Device* GetDevice();
        ID3D12GraphicsCommandList* GetCmdList();
        ID3D12GraphicsCommandList* GetTaskCmdList();
        ComPtr<ID3D12DescriptorHeap> GetDescriptorHeap();
        D3D12_CPU_DESCRIPTOR_HANDLE& GetSRVCPUDescriptorHandle(uint32_t index);
        D3D12_GPU_DESCRIPTOR_HANDLE& GetSRVGPUDescriptorHandle(uint32_t index);
        std::tuple<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> GetSRVDescriptorHandle();
        const D3D12_CONSTANT_BUFFER_VIEW_DESC& GetCBufferViewDesc(uint32_t index) const;
        uint8_t* GetCBufferPtr();
        uint8_t* GetPerFrameCbufData() final;
        uint8_t* GetPerMaterialCbufData(uint32_t mat_index) final;
        D3D12_CONSTANT_BUFFER_VIEW_DESC* GetPerFrameCbufGPURes();
        ScenePerFrameData* GetPerFrameCbufDataStruct();
        void ExecuteCommandBuffer(Ref<D3DCommandBuffer>& cmd);

        void DrawIndexedInstanced(uint32_t index_count, uint32_t instance_count, const Matrix4x4f& transform);
        void DrawInstanced(uint32_t vertex_count, uint32_t instance_count, const Matrix4x4f& transform);
        void DrawInstanced(uint32_t vertex_count, uint32_t instance_count);

    private:
        void Destroy();
        void LoadPipeline();
        void LoadAssets();
        void PopulateCommandList();
        void WaitForGpu();
        void WaitForTask();
        void MoveToNextFrame();
        void InitCBVSRVUAVDescHeap();
        void CreateDepthStencilTarget();
        void CreateDescriptorHeap();
        D3D12_GPU_DESCRIPTOR_HANDLE GetCBVGPUDescHandle(uint32_t index) const;

    private:
        inline static D3DContext* s_p_d3dcontext = nullptr;
        bool _b_has_task = false;
        WinWindow* _window;
        uint32_t _cbv_desc_num = 0u;
        // Pipeline objects.
        ComPtr<IDXGISwapChain3> m_swapChain;
        ComPtr<ID3D12Device> m_device;
        ComPtr<ID3D12Resource> _color_buffer[RenderConstants::kFrameCount];
        ComPtr<ID3D12Resource> _depth_buffer[RenderConstants::kFrameCount];
        ComPtr<ID3D12CommandAllocator> m_commandAllocators[RenderConstants::kFrameCount];
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

        ComPtr<ID3D12GraphicsCommandList> _task_cmd;
        ComPtr<ID3D12CommandAllocator> _task_cmd_alloc;

        std::list<std::function<void()>> _all_commands{};

        uint32_t _render_object_index = 0u;

        ComPtr<ID3D12Resource> m_constantBuffer;
        ScenePerFrameData _perframe_scene_data;
        std::vector<D3D12_CONSTANT_BUFFER_VIEW_DESC> _cbuf_views;

        // Synchronization objects.
        uint8_t m_frameIndex;
        HANDLE m_fenceEvent;
        ComPtr<ID3D12Fence> m_fence;
        uint64_t m_fenceValues[RenderConstants::kFrameCount];


        uint32_t _width;
        uint32_t _height;
        float m_aspectRatio;
    };
}

#endif // !__D3D_CONTEXT_H__

