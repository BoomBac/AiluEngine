#pragma once
#ifndef __D3D_CONTEXT_H__
#define __D3D_CONTEXT_H__

#include <d3dx12.h>
#include <dxgi1_6.h>
#include <atomic>
#include <condition_variable>
#include <mutex>

// Tracy GPU profiling (D3D12). This header is lightweight when TRACY_ENABLE is not defined.
#include "tracy/TracyD3D12.hpp"


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
#include "Render/RenderingStates.h"
#include "Render/Shader.h"
#include "Render/RayTracing/RayTracingShader.h"

using Microsoft::WRL::ComPtr;
using Ailu::Render::RenderPipeline;
using Ailu::Render::GpuResource;
using Ailu::Render::GfxCommand;
using Ailu::Render::Shader;
using Ailu::Render::ComputeShader;
using Ailu::Render::RayTracingShader;
using Ailu::Render::CommandBuffer;
using Ailu::Render::RHICommandBuffer;
using Ailu::Render::UploadParams;
using Ailu::Render::BuildParams;

namespace Ailu::RHI::DX12
{
    struct SubmitParams
    {
        String _name;
        bool _is_end_frame = false;
        Render::CommandRenderingStatesData _rendering_states_data;
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
            LOG_INFO("Submit shader update for {}", obj->Name());
            _pending_update_shaders.Push(obj);
        }
    private:
        struct CommandGroup
        {
            Vector<GfxCommand *> _cmds;
            SubmitParams _params;
            u32 _submission_index = 0u;
            CommandGroup() = default;
            CommandGroup(Vector<GfxCommand *>&& cmds,SubmitParams&& params,u32 submission_index)
                : _cmds(std::move(cmds)), _params(std::move(params)), _submission_index(submission_index){}
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
                _params = std::move(other._params);
                _submission_index = other._submission_index;
                _cmds = std::move(other._cmds);
                other._params = SubmitParams{};
                other._submission_index = 0u;
                other._cmds.clear();
                return *this;
            }
            CommandGroup(CommandGroup&& other) noexcept
            {
                for(auto& c : _cmds)
                    Render::CommandPool::Get().DeAlloc(c);
                _cmds.clear();
                _params = std::move(other._params);
                _submission_index = other._submission_index;
                _cmds = std::move(other._cmds);
                other._params = SubmitParams{};
                other._submission_index = 0u;
                other._cmds.clear();
            }
        };
        void RecordCommandGroup(CommandGroup& group,Ref<RHICommandBuffer>& cmd);
        u32 EstimateRecordCost(const CommandGroup& group) const;
        bool HasResourceUpload(const CommandGroup& group) const;
        void SubmitRecordedCommandBuffers(Vector<Ref<RHICommandBuffer>>& cmds);
        void RunAsync();
        void EndFrame();
    private:
        Core::LockFreeQueue<Object*,64> _pending_update_shaders;
        Core::ParallelQueue<CommandGroup> _cmd_queue;
        std::thread* _worker_thread;
        Render::GraphicsContext* _ctx;
        std::atomic<bool> _is_stop;
        std::atomic<u32> _next_submission_index = 0u;
        std::mutex _cmd_wait_mutex;
        std::condition_variable _cmd_wait_cv;
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
        void SetMultiThreadRendering(bool enabled) final;
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

        ID3D12Device5* GetDevice() { return m_device.Get(); };
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
        void CompileShaderAsync(Render::RayTracingShader* shader) final {_cmd_worker->SubmitUpdateShader(shader);};
        const u32 CurBackbufIndex() const;
        void ExecuteCommandBuffer(Ref<CommandBuffer>& cmd) final;
        void ExecuteCommandBufferSync(Ref<CommandBuffer> &cmd) final;
        void ExecuteRHICommandBuffer(RHICommandBuffer* cmd) final;
        u64 ExecuteRHICommandBuffers(const Vector<RHICommandBuffer *> &cmds) final;
        void WaitForGpu() final;
        void CreateResourceSync(GpuResource* res) final;
        void CreateResourceSync(GpuResource* res,UploadParams* params) final;
        void WaitForFence(u64 fence_value) final;

        bool IsHardwareRayTracingSupported() const final;
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
        mutable std::mutex _render_windows_mtx;
        u32 _cur_ctx_index = 0u;
        //u32 _cbv_desc_num = 0u;
        // Pipeline objects.
        ComPtr<ID3D12Device5> m_device;
        ComPtr<ID3D12CommandQueue> m_commandQueue;

        TracyD3D12Ctx _tracy_d3d12_ctx = nullptr;
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
        HANDLE _p_cmd_buffer_fence_event = nullptr;
        mutable std::mutex _cmd_fence_mtx;
        std::multimap<u64, ComPtr<ID3D12Resource>> _global_tracked_resource;
        std::mutex _resource_task_lock;
        float m_aspectRatio;
        bool _is_next_frame_capture = false;
        bool _is_cur_frame_capturing = false;
        WString _cur_capture_name;
        Scope<GpuCommandWorker> _cmd_worker;
        struct ScheduledResourceState
        {
            D3DResourceStateGuard *_global_state = nullptr;
            Vector<D3D12_RESOURCE_STATES> _states;
        };
        HashMap<ID3D12Resource *, ScheduledResourceState> _scheduled_resource_states;
        //command signature
        ComPtr<ID3D12CommandSignature> _dispatch_cmd_sig;
        ComPtr<ID3D12CommandSignature> _draw_cmd_sig;
        ComPtr<ID3D12CommandSignature> _draw_indexed_cmd_sig;
        Scope<ReadbackBufferPool> _readback_pool;
        bool _is_hardware_ray_tracing_supported = false;
    };


}

#endif // !__D3D_CONTEXT_H__

