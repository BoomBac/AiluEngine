#pragma once
#ifndef __D3D_CONTEXT_H__
#define __D3D_CONTEXT_H__

#include <d3dx12.h>
#include <dxgi1_6.h>

#include "Render/RenderConstants.h"
#include "Render/GraphicsContext.h"
#include "Platform/WinWindow.h"
#include "Framework/Math/ALMath.hpp"
#include "Framework/Common/TimeMgr.h"

using Microsoft::WRL::ComPtr;
namespace Ailu
{
    class D3DContext : public GraphicsContext
    {
        friend class D3DCommandBuffer;
    public:
        inline static D3DContext* GetInstance() { return s_p_d3dcontext; };
        D3DContext(WinWindow* window);
        ~D3DContext();
        void Init() final;
        void Present() final;
        const u64& GetFenceValue(const u32& cmd_index) const final;
        u64 GetCurFenceValue() const final;
        void SubmitRHIResourceBuildTask(RHIResourceTask task) final;
        void TakeCapture() final;

        ID3D12Device* GetDevice() { return m_device.Get(); };
        //return cbv/uav/src desc heap
        ComPtr<ID3D12DescriptorHeap> GetDescriptorHeap();
        std::tuple<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> GetSRVDescriptorHandle();
        std::tuple<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> GetUAVDescriptorHandle();
        D3D12_CPU_DESCRIPTOR_HANDLE GetRTVDescriptorHandle();
        D3D12_CPU_DESCRIPTOR_HANDLE GetDSVDescriptorHandle();

        u64 ExecuteCommandBuffer(Ref<CommandBuffer>& cmd) final;
        void BeginBackBuffer(CommandBuffer* cmd);
        void EndBackBuffer(CommandBuffer* cmd);
        void DrawOverlay(CommandBuffer* cmd);

    private:
        void Destroy();
        void LoadPipeline();
        void LoadAssets();
        void WaitForGpu();
        void FlushCommandQueue(ID3D12CommandQueue* cmd_queue,ID3D12Fence* fence,u64& fence_value);
        void InitCBVSRVUAVDescHeap();
        void CreateDepthStencilTarget();
        void CreateDescriptorHeap();
        D3D12_GPU_DESCRIPTOR_HANDLE GetCBVGPUDescHandle(u32 index) const;

    private:
        inline static D3DContext* s_p_d3dcontext = nullptr;
        WinWindow* _window;
        u32 _cbv_desc_num = 0u;
        // Pipeline objects.
        ComPtr<IDXGISwapChain3> m_swapChain;
        ComPtr<ID3D12Device> m_device;
        ComPtr<ID3D12Resource> _color_buffer[RenderConstants::kFrameCount];
        ComPtr<ID3D12Resource> _depth_buffer[RenderConstants::kFrameCount];
        ComPtr<ID3D12CommandAllocator> m_commandAllocators[RenderConstants::kFrameCount];
        ComPtr<ID3D12CommandQueue> m_commandQueue;
        ComPtr<ID3D12GraphicsCommandList> m_commandList;
        ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
        ComPtr<ID3D12DescriptorHeap> m_cbvHeap;
        ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
        u8 _rtv_desc_size;
        u8 _dsv_desc_size;
        u8 _cbv_desc_size;
        // Synchronization objects.
        u8 m_frameIndex;
        HANDLE m_fenceEvent;
        u64 _fence_value = 0u;
        ComPtr<ID3D12Fence> _p_cmd_buffer_fence;
        std::unordered_map<u32, u64> _cmd_target_fence_value;
        Queue<RHIResourceTask> _resource_task;
        TimeMgr _timer;
        u32 _width;
        u32 _height;
        float m_aspectRatio;
        bool _is_cur_frame_capture = false;
        WString _cur_capture_name;
    };
}

#endif // !__D3D_CONTEXT_H__

