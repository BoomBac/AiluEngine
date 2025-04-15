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
#include "Platform/WinWindow.h"
#include "Framework/Math/ALMath.hpp"
#include "Framework/Common/TimeMgr.h"
#include "Framework/Common/Container.hpp"
#include "DescriptorManager.h"
#include "Render/RenderPipeline.h"

using Microsoft::WRL::ComPtr;
namespace Ailu
{
    struct SubmitParams
    {
        String _name;
        bool _is_end_frame = false;
    };
    class GpuCommandWorker
    {
    public:
        GpuCommandWorker(GraphicsContext* context);
        ~GpuCommandWorker();
        void Push(Vector<GfxCommand *>&& cmds,SubmitParams params);
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
            ~CommandGroup()
            {
                for(auto& c : _cmds)
                    CommandPool::Get().Free(c);
                _cmds.clear();
            }
            void operator=(CommandGroup&& other) noexcept
            {
                for(auto& c : _cmds)
                    CommandPool::Get().Free(c);
                _cmds.clear();
                _params = other._params;
                _cmds = std::move(other._cmds);
                other._params = SubmitParams{};
                other._cmds.clear();
            }
        };
        void RunAsync();
        bool Pop(CommandGroup& group);
        void EndFrame();
    private:
        LockFreeQueue<Object*> _pending_update_shaders;
        std::mutex _mutex;
        std::mutex _run_mutex;
        Queue<CommandGroup> _cmd_queue;
        std::thread* _worker_thread;
        std::condition_variable _cv;
        GraphicsContext* _ctx;
        bool _is_stop;
    };

    class D3DContext : public GraphicsContext
    {
        friend class D3DCommandBuffer;
    public:
        D3DContext(WinWindow* window);
        ~D3DContext();
        void Init() final;
        void Present() final;
        u64 GetFenceValueGPU() const final;
        u64 GetFenceValueCPU() const final;
        void TakeCapture() final;
        void ResizeSwapChain(const u32 width, const u32 height) final;
        virtual u64 GetFrameCount() const final { return _frame_count; };
        std::tuple<u32, u32> GetSwapChainSize() const final { return std::make_pair(_width, _height);};
        IGPUTimer* GetTimer() final { return _p_gpu_timer.get(); }
        const u32 CurBackbufIndex() const final {return m_frameIndex ;};
        void TryReleaseUnusedResources() final;
        f32 TotalGPUMemeryUsage() final;

        RenderPipeline* GetPipeline() final;
        void RegisterPipeline(RenderPipeline* pipiline) final;

        ID3D12Device* GetDevice() { return m_device.Get(); };
        void TrackResource(ComPtr<ID3D12Resource> resource);

        void ReadBack(GpuResource* res,u8* data,u32 size);
        void ReadBackAsync(GpuResource* res,std::function<void(u8*)> callback);
        void CreateResource(GpuResource* res) final;
        void CreateResource(GpuResource* res,UploadParams* params) final;
        void ProcessGpuCommand(GfxCommand * cmd,RHICommandBuffer* cmd_buffer) final;
        void SubmitGpuCommandSync(GfxCommand * cmd) final;
        void CompileShaderAsync(Shader* shader) final {_cmd_worker->SubmitUpdateShader(shader);};
        void CompileShaderAsync(ComputeShader* shader) {_cmd_worker->SubmitUpdateShader(shader);};

        void ExecuteCommandBuffer(Ref<CommandBuffer>& cmd) final;
        void ExecuteCommandBufferSync(Ref<CommandBuffer> &cmd) final;
        void WaitForGpu() final;
        void WaitForFence(u64 fence_value) final;
    #if defined(DEAR_IMGUI)
        /// @brief 记录imgui使用的rt，在绘制之前统一转换至srv
        /// @param tex 
        void RecordImguiUsedTexture(RenderTexture* tex) {_imgui_used_rt.insert(tex);};
    #endif

    private:
        void ExecuteRHICommandBuffer(RHICommandBuffer* cmd) final;
        void Destroy();
        void LoadPipeline();
        void LoadAssets();
        void FlushCommandQueue(ID3D12CommandQueue* cmd_queue,ID3D12Fence* fence,u64& fence_value);
        void BeginCapture();
        void EndCapture();
        void ResizeSwapChainImpl(const u32 width, const u32 height);
        void MoveToNextFrame();
        void PresentImpl(D3DCommandBuffer* cmd);
#ifdef _DIRECT_WRITE
        void InitDirectWriteContext();
#endif

    private:
        inline static const u32 kResourceCleanupIntervalTick = 2000u;
        //1600 * 900 * 4 * 20
        inline static constexpr u64 kMaxRenderTextureMemorySize = 115200000u;
        WinWindow* _window;
        //u32 _cbv_desc_num = 0u;
        // Pipeline objects.
        DXGI_FORMAT _backbuffer_format;
        ComPtr<IDXGISwapChain3> m_swapChain;
        ComPtr<ID3D12Device> m_device;
        ComPtr<ID3D12Resource> _color_buffer[RenderConstants::kFrameCount];
        ComPtr<ID3D12CommandAllocator> m_commandAllocators[RenderConstants::kFrameCount];
        ComPtr<ID3D12CommandQueue> m_commandQueue;
        ComPtr<ID3D12GraphicsCommandList> m_commandList;
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
        CPUVisibleDescriptorAllocation _rtv_allocation;
        // Synchronization objects.
        u8 m_frameIndex;
        HANDLE m_fenceEvent;
        u64 _fence_value[RenderConstants::kFrameCount];
        ComPtr<ID3D12Fence> _p_cmd_buffer_fence;
        std::multimap<u64, ComPtr<ID3D12Resource>> _global_tracked_resource;
        std::mutex _resource_task_lock;
        u32 _width;
        u32 _height;
        float m_aspectRatio;
        bool _is_next_frame_capture = false;
        bool _is_cur_frame_capturing = false;
        WString _cur_capture_name;
        RenderPipeline* _pipiline = nullptr;
        std::atomic<u32> _new_backbuffer_size;
        Scope<GpuCommandWorker> _cmd_worker;
    #if defined(DEAR_IMGUI)
        std::set<RenderTexture*,ObjectPtrCompare> _imgui_used_rt;
    #endif
    };


}

#endif // !__D3D_CONTEXT_H__

