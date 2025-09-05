#pragma once
#ifndef __D3D_CONTEXT_H__
#define __D3D_CONTEXT_H__

#include <d3dx12.h>
#include <dxgi1_6.h>


#ifdef _DIRECT_WRITE
#include <dwrite.h>
#include <d2d1_3.h>
#include <d3d11on12.h>
#endif



#include "Render/RenderConstants.h"
#include "Render/GraphicsContext.h"
#include "D3DResourceBase.h"
#include "Platform/WinWindow.h"
#include "Framework/Math/ALMath.hpp"
#include "Framework/Common/TimeMgr.h"
#include "Framework/Common/Container.hpp"
#include "DescriptorManager.h"
#include "UploadBuffer.h"
#include "Render/RenderPipeline.h"
#include "Render/Shader.h"

using Microsoft::WRL::ComPtr;
using Ailu::Render::RenderPipeline;
using Ailu::Render::GpuResource;
using Ailu::Render::GfxCommand;
using Ailu::Render::Shader;
using Ailu::Render::ComputeShader;
using Ailu::Render::CommandBuffer;
using Ailu::Render::RHICommandBuffer;
using Ailu::Render::UploadParams;

namespace Ailu::RHI::DX12
{
    struct SubmitParams
    {
        String _name;
        bool _is_end_frame = false;
    };
    class GpuCommandWorker
    {
    public:
        GpuCommandWorker(Render::GraphicsContext* context);
        ~GpuCommandWorker();
        void Push(Vector<GfxCommand *>&& cmds,SubmitParams&& params);
        void RunSync();
        //async scope
        void Start();
        void Stop();
        void SubmitUpdateShader(Object* obj)
        {
            _pending_update_shaders.Push(obj);
        }
    private:
        struct CommandGroup
        {
            Vector<GfxCommand *> _cmds;
            SubmitParams _params;
            CommandGroup() = default;
            CommandGroup(Vector<GfxCommand *>&& cmds,SubmitParams&& params) : _cmds(std::move(cmds)), _params(std::move(params)){}
            ~CommandGroup()
            {
                for(auto& c : _cmds)
                    Render::CommandPool::Get().DeAlloc(c);
                _cmds.clear();
            }
            CommandGroup& operator=(CommandGroup&& other) noexcept
            {
                for(auto& c : _cmds)
                    Render::CommandPool::Get().DeAlloc(c);
                _cmds.clear();
                _params = other._params;
                _cmds = std::move(other._cmds);
                other._params = SubmitParams{};
                other._cmds.clear();
                return *this;
            }
            CommandGroup(CommandGroup&& other) noexcept
            {
                for(auto& c : _cmds)
                    Render::CommandPool::Get().DeAlloc(c);
                _cmds.clear();
                _params = other._params;
                _cmds = std::move(other._cmds);
                other._params = SubmitParams{};
                other._cmds.clear();
            }
        };
        void RunAsync();
        void EndFrame();
    private:
        Core::LockFreeQueue<Object*,64> _pending_update_shaders;
        Core::LockFreeQueue<CommandGroup,512> _cmd_queue;
        std::thread* _worker_thread;
        Render::GraphicsContext* _ctx;
        bool _is_stop;
    };

    class D3DSwapchainTexture;
    class D3DContext : public Render::GraphicsContext
    {
        friend class D3DCommandBuffer;
    public:
        D3DContext();
        ~D3DContext();
        void Init() final;
        void Present() final;
        u64 GetFenceValueGPU() final;
        u64 GetFenceValueCPU() const final;
        void RegisterWindow(Window *window);
        void UnRegisterWindow(Window *window);
        void TakeCapture() final;
        void ResizeSwapChain(void *window_handle, const u32 width, const u32 height) final;
        virtual u64 GetFrameCount() const final { return _frame_count; };
        IGPUTimer* GetTimer() final { return _p_gpu_timer.get(); }
        void TryReleaseUnusedResources() final;
        f32 TotalGPUMemeryUsage() final;

        ID3D12Device* GetDevice() { return m_device.Get(); };
        void TrackResource(ComPtr<ID3D12Resource> resource);

        void ReadBack(GpuResource* res,u8* data,u32 size);

        void ReadBack(ID3D12Resource* res,D3DResourceStateGuard& state_guard,u8* data, u32 size);
        void ReadBackAsync(ID3D12Resource* res,D3DResourceStateGuard& state_guard,u32 size,std::function<void(const u8*)> callback);
        void ReadBackAsync(GpuResource* res,std::function<void(u8*)> callback);
        void CreateResource(GpuResource* res) final;
        void CreateResource(GpuResource* res,UploadParams* params) final;
        void ProcessGpuCommand(GfxCommand * cmd,RHICommandBuffer* cmd_buffer) final;
        void SubmitGpuCommandSync(GfxCommand * cmd) final;
        void CompileShaderAsync(Shader* shader) final {_cmd_worker->SubmitUpdateShader(shader);};
        void CompileShaderAsync(Render::ComputeShader* shader) {_cmd_worker->SubmitUpdateShader(shader);};
        const u32 CurBackbufIndex() const;
        void ExecuteCommandBuffer(Ref<CommandBuffer>& cmd) final;
        void ExecuteCommandBufferSync(Ref<CommandBuffer> &cmd) final;
        void ExecuteRHICommandBuffer(RHICommandBuffer* cmd) final;
        void WaitForGpu() final;
        void WaitForFence(u64 fence_value) final;
    private:
        void Destroy();
        void LoadPipeline();
        void LoadAssets();
        void BeginCapture();
        void EndCapture();
        void ResizeSwapChainImpl(const u32 width, const u32 height);
        void PresentImpl(D3DCommandBuffer* cmd);
#ifdef _DIRECT_WRITE
        void InitDirectWriteContext();
#endif

    private:
        struct RenderWindowCtx;
    private:
        inline static const u32 kResourceCleanupIntervalTick = 8000u;
        //1600 * 900 * 4 * 20
        inline static constexpr u64 kMaxRenderTextureMemorySize = 115200000u;
        inline static constexpr u32 kMaxIndirectDispatchCount = 2048u;
        inline static constexpr u32 kMaxIndirectDrawCount     = 2048u;
        Vector<Scope<RenderWindowCtx>> _render_windows;
        u32 _cur_ctx_index = 0u;
        //u32 _cbv_desc_num = 0u;
        // Pipeline objects.
        ComPtr<ID3D12Device> m_device;
        ComPtr<ID3D12CommandQueue> m_commandQueue;
        ComPtr<IDXGIAdapter4> _p_adapter;
        DXGI_QUERY_VIDEO_MEMORY_INFO _local_video_memory_info;
        DXGI_QUERY_VIDEO_MEMORY_INFO _non_local_video_memory_info;
        Scope<IGPUTimer> _p_gpu_timer;
#ifdef _DIRECT_WRITE
        IDWriteFactory * _dw_factory;
        IDWriteTextFormat * _dw_textformat;
        ComPtr<ID3D11DeviceContext> m_d3d11DeviceContext;
        ComPtr<ID3D11On12Device> m_d3d11On12Device;
        ComPtr<IDWriteFactory> m_dWriteFactory;
        ComPtr<ID2D1Factory3> m_d2dFactory;
        ComPtr<ID2D1Device2> m_d2dDevice;
        ComPtr<ID2D1DeviceContext2> m_d2dDeviceContext;
        ComPtr<ID3D11Resource> m_wrappedBackBuffers[RenderConstants::kFrameCount];
        ComPtr<ID2D1Bitmap1> m_d2dRenderTargets[RenderConstants::kFrameCount];

        ComPtr<ID2D1SolidColorBrush> m_textBrush;
        ComPtr<IDWriteTextFormat> m_textFormat;
#endif

        u64 _frame_count = 0u;
        //CPUVisibleDescriptorAllocation _rtv_allocation;
        // Synchronization objects.
        //u8 m_frameIndex = 0u;
        u64 _cur_fence_value;
        u64 _fence_value;
        //HANDLE m_fenceEvent;
        //std::atomic<u64> _fence_value[Render::RenderConstants::kFrameCount];
        ComPtr<ID3D12Fence> _p_cmd_buffer_fence;
        std::multimap<u64, ComPtr<ID3D12Resource>> _global_tracked_resource;
        std::mutex _resource_task_lock;
        float m_aspectRatio;
        bool _is_next_frame_capture = false;
        bool _is_cur_frame_capturing = false;
        WString _cur_capture_name;
        Scope<GpuCommandWorker> _cmd_worker;
        //command signature
        ComPtr<ID3D12CommandSignature> _dispatch_cmd_sig;
        ComPtr<ID3D12CommandSignature> _draw_cmd_sig;
        ComPtr<ID3D12CommandSignature> _draw_indexed_cmd_sig;
        Scope<ReadbackBufferPool> _readback_pool;
    };


}

#endif // !__D3D_CONTEXT_H__

