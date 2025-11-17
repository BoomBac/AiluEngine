#include "RHI/DX12/D3DContext.h"
//#include "Ext/imgui/backends/imgui_impl_dx12.h"
#include "Framework/Common/Application.h"
#include "Framework/Common/Assert.h"
#include "Framework/Common/JobSystem.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/Profiler.h"
#include "RHI/DX12/D3DCommandBuffer.h"
#include "RHI/DX12/D3DGPUTimer.h"
#include "RHI/DX12/GPUResourceManager.h"
#include "RHI/DX12/dxhelper.h"
#include "RHI/DX12/D3DSwapchain.h"
#include "Render/Buffer.h"
#include "Render/Gizmo.h"
#include "Render/GpuResource.h"
#include "Render/GraphicsPipelineStateObject.h"
#include "Render/Material.h"
#include "Render/RenderingData.h"
#include "pch.h"
#include <dxgidebug.h>
#include <limits>

#include "RHI/DX12/D3DBuffer.h"
#include "RHI/DX12/D3DGraphicsPipelineState.h"
#include <RHI/DX12/D3DShader.h>
#include <RHI/DX12/D3DTexture.h>
#include <d3d11.h>

#ifdef _PIX_DEBUG
#include "Ext/pix/Include/WinPixEventRuntime/pix3.h"
#include "Ext/renderdoc_app.h"//1.35
#endif                        // _PIX_DEBUG
#include <Render/ImGuiRenderer.h>

using namespace Ailu::Render;

namespace Ailu::RHI::DX12
{
#pragma region GpuCommandWorker

    GpuCommandWorker::GpuCommandWorker(GraphicsContext *context) : _ctx(context), _is_stop(false),
                                                                   _worker_thread(nullptr)
    {
    }
    GpuCommandWorker::~GpuCommandWorker()
    {
        if (_worker_thread && _worker_thread->joinable())
            _worker_thread->join();
        LOG_INFO("Destory GpuCommandWorker");
    }

    void GpuCommandWorker::Push(Vector<GfxCommand *> &&cmds, SubmitParams &&params)
    {
        while (_cmd_queue.Full())
            std::this_thread::yield();
        _cmd_queue.Push(CommandGroup(std::move(cmds), std::move(params)));
    }
    void GpuCommandWorker::RunAsync()
    {
        SetThreadName("RenderThread");
        while (!_is_stop)
        {
            Application::Get().WaitForMain();
            if (Application::Get().State() == EApplicationState::EApplicationState_Exit)
            {
                _is_stop = true;
                break;
            }
            CPUProfileBlock b("GpuCommandWorker::RunAsync");
            if (Application::Get().State() == EApplicationState::EApplicationState_Exit)
            {
                _is_stop = true;
                break;
            }
            while (true)
            {
                auto group_opt = _cmd_queue.Pop();
                if (group_opt.has_value())
                {
                    auto cmd = RHICommandBufferPool::Get(group_opt.value()._params._name);
                    for (auto *task: group_opt.value()._cmds)
                        _ctx->ProcessGpuCommand(task, cmd.get());
                    if (group_opt.value()._cmds.size() >= 1)
                    {
                        CPUProfileBlock b(group_opt.value()._params._name + "_Execute");
                        _ctx->ExecuteRHICommandBuffer(cmd.get());
                    }
                    RenderTexture::ResetRenderTarget();
                    RHICommandBufferPool::Release(cmd);
                    if (group_opt.value()._params._is_end_frame)
                    {
                        CPUProfileBlock b("EndFrame");
                        EndFrame();
                        break;
                    }
                }
                else
                {
                    std::this_thread::yield();
                }
            }
        }
        LOG_INFO("GpuCommandWorker::RunAsync: Release {} un-executed cmd", _cmd_queue.Size())
        while (!_cmd_queue.Empty())
            _cmd_queue.Pop();
    }
    void GpuCommandWorker::EndFrame()
    {
        u16 compiled_shader_num = 0u, compiled_compute_shader_num = 0u;
        while (!_pending_update_shaders.Empty())
        {
            if (auto obj = _pending_update_shaders.Pop(); obj.has_value())
            {
                if (Shader *shader = dynamic_cast<Shader *>(obj.value()); shader != nullptr)
                {
                    if (shader->PreProcessShader())
                    {
                        bool is_all_succeed = true;
                        is_all_succeed = shader->Compile();//暂时重编所有变体，避免材质切换变体后使用的是旧的shader
                        for (auto &mat: shader->GetAllReferencedMaterials())
                        {
                            mat->ConstructKeywords(shader);
                            // for (u16 i = 0; i < shader->PassCount(); i++)
                            // {
                            //     is_all_succeed &= shader->Compile(i, mat->ActiveVariantHash(i));
                            // }
                            if (is_all_succeed)
                            {
                                mat->ChangeShader(shader);
                            }
                        }
                        compiled_shader_num++;
                    }
                }
                else if (ComputeShader *shader = dynamic_cast<ComputeShader *>(obj.value()); shader != nullptr)
                {
                    if (shader->Preprocess())
                    {
                        shader->_is_compiling.store(true);// shader Compile()也会设置这个值，这里设置一下防止读取该值时还没执行compile
                        shader->Compile();
                        compiled_compute_shader_num++;
                    }
                }
            }
        }
        if (compiled_shader_num + compiled_compute_shader_num > 0u)
        {
            LOG_INFO("Compiled {} shaders and {} compute shaders!", compiled_shader_num, compiled_compute_shader_num);
        }
        if (Application::Get()._is_multi_thread_rendering)
            Application::Get().NotifyMain();
        Render::RenderingStates::Reset();
    }
    void GpuCommandWorker::Start()
    {
        if (_worker_thread == nullptr)
        {
            _worker_thread = new std::thread(&GpuCommandWorker::RunAsync, this);
        }
        _is_stop = false;
    }
    void GpuCommandWorker::Stop()
    {
        if (_worker_thread)
        {
            _is_stop = true;
            if (_worker_thread->joinable())
                _worker_thread->join();
            DESTORY_PTR(_worker_thread);
            LOG_INFO("Exit RenderThread")
        }
    }
    void GpuCommandWorker::RunSync()
    {
        CPUProfileBlock b("GpuCommandWorker::RunSync");
        while (!_cmd_queue.Empty())
        {
            auto &&group_opt = _cmd_queue.Pop();
            auto cmd = RHICommandBufferPool::Get(group_opt.value()._params._name);
            for (auto *task: group_opt.value()._cmds)
                _ctx->ProcessGpuCommand(task, cmd.get());
            {
                CPUProfileBlock b(group_opt.value()._params._name + "_Execute");
                _ctx->ExecuteRHICommandBuffer(cmd.get());
            }
            RenderTexture::ResetRenderTarget();
            RHICommandBufferPool::Release(cmd);
        }
        {
            CPUProfileBlock b("EndFrame");
            EndFrame();
        }
    }
#pragma endregion

#pragma region DX Helper
    static void GetHardwareAdapter(IDXGIFactory6 *pFactory, IDXGIAdapter4 **ppAdapter, DXGI_QUERY_VIDEO_MEMORY_INFO *p_local_video_memory_info, DXGI_QUERY_VIDEO_MEMORY_INFO *p_non_local_video_memory_info)
    {
        IDXGIAdapter *pAdapter = nullptr;
        IDXGIAdapter4 *pAdapter4 = nullptr;
        *ppAdapter = nullptr;
        for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != pFactory->EnumAdapters(adapterIndex, &pAdapter); adapterIndex++)
        {
            DXGI_ADAPTER_DESC3 desc{};
            LOG_INFO("Info(Adapter index: {})------------------------------------------------------------------------------", adapterIndex);
            if (SUCCEEDED(pAdapter->QueryInterface(IID_PPV_ARGS(&pAdapter4))))
            {
                pAdapter4->GetDesc3(&desc);
                LOG_INFO(L"Description: {}", desc.Description);
                LOG_INFO("DedicatedSystemMemory: {} mb", desc.DedicatedSystemMemory / 1024 / 1024);
                LOG_INFO("SharedSystemMemory: {} mb", desc.SharedSystemMemory / 1024 / 1024);
            }
            //
            // Obtain the default video memory information for the local and non-local segment groups.
            //
            if (FAILED(pAdapter4->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, p_local_video_memory_info)))
            {
                LOG_ERROR("Failed to query initial video memory info for local segment group");
            }
            else
            {
                //When querying video memory budget for GPU upload heaps, MemorySegmentGroup needs to be DXGI_MEMORY_SEGMENT_GROUP_LOCAL.
                LOG_INFO("LocalSegmentGroup");
                LOG_INFO("AvailableForReservation: {} mb", p_local_video_memory_info->AvailableForReservation / 1024 / 1024);
                LOG_INFO("Budget: {} mb", p_local_video_memory_info->Budget / 1024 / 1024);
                LOG_INFO("CurrentReservation: {} mb", p_local_video_memory_info->CurrentReservation / 1024 / 1024);
                //实时获取这个值
                LOG_INFO("CurrentUsage: {} mb", p_local_video_memory_info->CurrentUsage / 1024 / 1024);
            }

            if (FAILED(pAdapter4->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, p_non_local_video_memory_info)))
            {
                LOG_ERROR("Failed to query initial video memory info for non-local segment group");
            }
            else
            {
                LOG_INFO("NonLocalSegmentGroup");
                LOG_INFO("AvailableForReservation: {} mb", p_non_local_video_memory_info->AvailableForReservation / 1024 / 1024);
                LOG_INFO("Budget: {} mb", p_non_local_video_memory_info->Budget / 1024 / 1024);
                LOG_INFO("CurrentReservation: {} mb", p_non_local_video_memory_info->CurrentReservation / 1024 / 1024);
                LOG_INFO("CurrentUsage: {} mb", p_non_local_video_memory_info->CurrentUsage / 1024 / 1024);
            }

            LOG_INFO("-----------------------------------------------------------------------------------------------------");
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                // Don't select the Basic Render Driver adapter.
                continue;
            }
            // Check to see if the adapter supports Direct3D 12, but don't create the
            // actual device yet.
            if (SUCCEEDED(D3D12CreateDevice(pAdapter4, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)))
            {
                break;
            }
        }
        *ppAdapter = pAdapter4;
    }
#ifdef _PIX_DEBUG
    static RENDERDOC_API_1_1_2 *g_rdc_api = nullptr;

#endif// _PIX_DEBUG

    static void RdcLoadLatestRdcGpuCapturerLibrary()
    {
        HKEY hKey = HKEY_LOCAL_MACHINE;                                                                        // 根键
        LPCWSTR subKey = L"SOFTWARE\\Classes\\CLSID\\{5D6BF029-A6BA-417A-8523-120492B1DCE3}\\InprocServer32\\";// 子键路径
        //LPCWSTR valueName = L"YourValue";     // 值名称
        wchar_t value[256];              // 存储结果
        DWORD bufferSize = sizeof(value);// 缓冲区大小
        // 获取值
        LONG result = RegGetValue(
                hKey,
                subKey,
                nullptr,//default name
                RRF_RT_REG_SZ,
                nullptr,
                value,
                &bufferSize);
#ifdef _PIX_DEBUG
        if (result == ERROR_SUCCESS)
        {
            if (HMODULE mod = LoadLibrary(value))
            {
                pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI) GetProcAddress(mod, "RENDERDOC_GetAPI");
                int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2, (void **) &g_rdc_api);
                AL_ASSERT(ret == 1);
                g_rdc_api->SetCaptureFilePathTemplate("RenderDocCapture/Capture");
                g_rdc_api->MaskOverlayBits(RENDERDOC_OverlayBits::eRENDERDOC_Overlay_None, RENDERDOC_OverlayBits::eRENDERDOC_Overlay_None);
            }
            else
                LOG_ERROR("RenderDoc Lode Error: {}", GetLastError());
        }
        else
            LOG_ERROR(L"Failed to get renderdoc dll path. Error: {}", result)
#endif
    };

    static void EnableShaderBasedValidation()
    {
        ComPtr<ID3D12Debug> spDebugController0;
        ComPtr<ID3D12Debug1> spDebugController1;
        ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&spDebugController0)));
        ThrowIfFailed(spDebugController0->QueryInterface(IID_PPV_ARGS(&spDebugController1)));
        spDebugController1->SetEnableGPUBasedValidation(true);
    }
    namespace D3DConvertUtils
    {
        D3D12_VIEWPORT ToD3DViewport(const Rect &viewport)
        {
            D3D12_VIEWPORT ret;
            ret.TopLeftX = (f32) viewport.left;
            ret.TopLeftY = (f32) viewport.top;
            ret.Width = (f32) viewport.width;
            ret.Height = (f32) viewport.height;
            ret.MinDepth = 0.0f;
            ret.MaxDepth = 1.0f;
            return ret;
        }
        D3D12_RECT ToD3DRect(const Rect &rect)
        {
            D3D12_RECT ret;
            ret.left = rect.left;
            ret.top = rect.top;
            ret.right = rect.width;
            ret.bottom = rect.height;
            return ret;
        }
    }// namespace D3DConvertUtils
#pragma endregion

#pragma region SignatureHelper
    // 创建命令签名的实用函数
    HRESULT CreateCommandSignature(
            ID3D12Device *device,
            ID3D12RootSignature *rootSignature,
            const std::vector<D3D12_INDIRECT_ARGUMENT_DESC> &arguments,
            UINT byteStride,
            ID3D12CommandSignature **ppCommandSignature)
    {
        D3D12_COMMAND_SIGNATURE_DESC desc = {};
        desc.ByteStride = byteStride;
        desc.NumArgumentDescs = static_cast<UINT>(arguments.size());
        desc.pArgumentDescs = arguments.data();
        desc.NodeMask = 0;

        return device->CreateCommandSignature(&desc, rootSignature, IID_PPV_ARGS(ppCommandSignature));
    }

    namespace CommandSignatureHelper
    {
        // 1. 创建用于间接调度的命令签名
        HRESULT CreateDispatchCommandSignature(
                ID3D12Device *device,
                ID3D12RootSignature *rootSignature,
                ID3D12CommandSignature **ppCommandSignature)
        {
            D3D12_INDIRECT_ARGUMENT_DESC argDesc = {};
            argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

            return CreateCommandSignature(
                    device,
                    rootSignature,
                    {argDesc},
                    sizeof(D3D12_DISPATCH_ARGUMENTS),
                    ppCommandSignature);
        }

        // 2. 创建用于间接绘制的命令签名
        HRESULT CreateDrawCommandSignature(
                ID3D12Device *device,
                ID3D12RootSignature *rootSignature,
                ID3D12CommandSignature **ppCommandSignature)
        {
            D3D12_INDIRECT_ARGUMENT_DESC argDesc = {};
            argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

            return CreateCommandSignature(
                    device,
                    rootSignature,
                    {argDesc},
                    sizeof(D3D12_DRAW_ARGUMENTS),
                    ppCommandSignature);
        }

        // 3. 创建用于间接索引绘制的命令签名
        HRESULT CreateDrawIndexedCommandSignature(
                ID3D12Device *device,
                ID3D12RootSignature *rootSignature,
                ID3D12CommandSignature **ppCommandSignature)
        {
            D3D12_INDIRECT_ARGUMENT_DESC argDesc = {};
            argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

            return CreateCommandSignature(
                    device,
                    rootSignature,
                    {argDesc},
                    sizeof(D3D12_DRAW_INDEXED_ARGUMENTS),
                    ppCommandSignature);
        }

        // 4. 创建带顶点缓冲区视图的间接绘制命令签名
        HRESULT CreateDrawCommandSignatureWithVBV(
                ID3D12Device *device,
                ID3D12RootSignature *rootSignature,
                UINT slot,
                ID3D12CommandSignature **ppCommandSignature)
        {
            std::vector<D3D12_INDIRECT_ARGUMENT_DESC> args;

            // 顶点缓冲区视图
            D3D12_INDIRECT_ARGUMENT_DESC vbvArg = {};
            vbvArg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW;
            vbvArg.VertexBuffer.Slot = slot;
            args.push_back(vbvArg);

            // 绘制命令
            D3D12_INDIRECT_ARGUMENT_DESC drawArg = {};
            drawArg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
            args.push_back(drawArg);

            return CreateCommandSignature(
                    device,
                    rootSignature,
                    args,
                    sizeof(D3D12_VERTEX_BUFFER_VIEW) + sizeof(D3D12_DRAW_ARGUMENTS),
                    ppCommandSignature);
        }

        // 5. 创建带常量的间接调度命令签名
        HRESULT CreateDispatchCommandSignatureWithConstants(
                ID3D12Device *device,
                ID3D12RootSignature *rootSignature,
                UINT rootParameterIndex,
                ID3D12CommandSignature **ppCommandSignature)
        {
            std::vector<D3D12_INDIRECT_ARGUMENT_DESC> args;

            // 常量数据
            D3D12_INDIRECT_ARGUMENT_DESC constantArg = {};
            constantArg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
            constantArg.Constant.RootParameterIndex = rootParameterIndex;
            constantArg.Constant.DestOffsetIn32BitValues = 0;
            constantArg.Constant.Num32BitValuesToSet = 1;// 设置1个32位值
            args.push_back(constantArg);

            // 调度命令
            D3D12_INDIRECT_ARGUMENT_DESC dispatchArg = {};
            dispatchArg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
            args.push_back(dispatchArg);

            return CreateCommandSignature(
                    device,
                    rootSignature,
                    args,
                    sizeof(UINT) + sizeof(D3D12_DISPATCH_ARGUMENTS),// 常量+调度参数
                    ppCommandSignature);
        }
    };// namespace CommandSignatureHelper
#pragma endregion

#pragma region D3DContext

    struct D3DContext::RenderWindowCtx
    {
        CPUVisibleDescriptorAllocation _rtv_allocation;
        Window *_window;
        D3DSwapchainTexture *_swapchain;
        DXGI_FORMAT _swapchain_format;
        u32 _width, _height;
        std::atomic<u32> _new_backbuffer_size;

        u8 _frame_index = 0u;
        u64 _cur_fence_value = 0;
        HANDLE _fence_event = nullptr;
        ComPtr<ID3D12Fence> _fence;
        u64 _fence_value[Render::RenderConstants::kFrameCount] = {};

        mutable std::mutex _mtx;// 新增：保护 fence/帧索引

        void MoveToNextFrame(ID3D12CommandQueue *queue)
        {
            std::lock_guard lock(_mtx);

            const UINT64 currentFenceValue = _fence_value[_frame_index];
            _frame_index = _swapchain->GetCurrentBackBufferIndex();

            // 如果该buffer上一次绘制还未结束，等待
            u64 gpu_fence_value = _fence->GetCompletedValue();
            if (gpu_fence_value < _fence_value[_frame_index])
            {
                CPUProfileBlock p("WaitForGPU");
                ThrowIfFailed(_fence->SetEventOnCompletion(_fence_value[_frame_index], _fence_event));
                WaitForSingleObjectEx(_fence_event, INFINITE, FALSE);
            }

            // Set the fence value for the next frame.
            _fence_value[_frame_index] = currentFenceValue + 1;
            ThrowIfFailed(queue->Signal(_fence.Get(), _fence_value[_frame_index]));
        }

        void WaitForGpu(ID3D12CommandQueue *queue)
        {
            std::lock_guard lock(_mtx);
            u64 fence_value = _fence_value[_frame_index];
            FlushCommandQueue(queue, _fence.Get(), fence_value);
            _fence_value[_frame_index] = fence_value;
        }

        void WaitForFence(ID3D12CommandQueue *queue, u64 fence_value)
        {
            std::lock_guard lock(_mtx);
            FlushCommandQueue(queue, _fence.Get(), fence_value);
        }

        void FlushCommandQueue(ID3D12CommandQueue *cmd_queue, ID3D12Fence *fence, u64 &fence_value)
        {
            // 注意：这里 fence_value 是局部副本，必须在锁下修改
            if (fence_value < fence->GetCompletedValue())
                return;

            ++fence_value;
            ThrowIfFailed(cmd_queue->Signal(fence, fence_value));
            if (fence->GetCompletedValue() < fence_value)
            {
                ThrowIfFailed(fence->SetEventOnCompletion(fence_value, _fence_event));
                WaitForSingleObjectEx(_fence_event, INFINITE, FALSE);
            }
        }
    };



    D3DContext::D3DContext()
    {
#ifdef _PIX_DEBUG
        PIXLoadLatestWinPixGpuCapturerLibrary();
        //RdcLoadLatestRdcGpuCapturerLibrary();
#endif// _PIX_DEBUG
        _cmd_worker = MakeScope<GpuCommandWorker>(this);
    }

    D3DContext::~D3DContext()
    {
        Destroy();
        LOG_INFO("D3DContext Destroy");
    }

    void D3DContext::Init()
    {
        g_pGfxContext = this;
        _fence_value = 0u;
        LoadPipeline();
        LoadAssets();
        _readback_pool = MakeScope<ReadbackBufferPool>(m_device.Get());
        _p_gpu_timer = MakeScope<D3DGPUTimer>(m_device.Get(), m_commandQueue.Get(), RenderConstants::kFrameCount);
        if (Application::Get()._is_multi_thread_rendering)
        {
            _cmd_worker->Start();
        }
    }

    void D3DContext::TryReleaseUnusedResources()
    {
        if (!_global_tracked_resource.empty())
        {
            u64 origin_res_num = _global_tracked_resource.size();
            auto it = _global_tracked_resource.lower_bound(_frame_count);
            if (it != _global_tracked_resource.end())
            {
                _global_tracked_resource.erase(_global_tracked_resource.begin(), it);
            }
            else
                _global_tracked_resource.clear();

            origin_res_num -= _global_tracked_resource.size();
            if (origin_res_num > 0)
                LOG_WARNING("Release unused upload buffer with num {}.", origin_res_num);
        }
        g_pRenderTexturePool->TryCleanUp();
        if (auto num = D3DDescriptorMgr::Get().ReleaseSpace(); num > 0)
        {
            LOG_INFO("Release GPUVisibleDescriptor at with num {}.", num);
        }
    }

    f32 D3DContext::TotalGPUMemeryUsage()
    {
        f32 usage = 0.f;
        if (FAILED(_p_adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &_local_video_memory_info)))
        {
            LOG_ERROR("Failed to query initial video memory info for local segment group");
        }
        else
        {
            //When querying video memory budget for GPU upload heaps, MemorySegmentGroup needs to be DXGI_MEMORY_SEGMENT_GROUP_LOCAL.
            //LOG_INFO("CurrentUsage: {} mb", );
            usage = (f32) _local_video_memory_info.CurrentUsage * 9.5367431640625E-07f;
        }
        return usage;
        //if (FAILED(_p_adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &_non_local_video_memory_info)))
        //{
        //	LOG_ERROR("Failed to query initial video memory info for non-local segment group");
        //}
        //else
        //{
        //	LOG_INFO("CurrentUsage: {} mb", _non_local_video_memory_info.CurrentUsage / 1024 / 1024);
        //}
    }

    void D3DContext::TrackResource(ComPtr<ID3D12Resource> resource)
    {
        _global_tracked_resource.insert(std::make_pair(_frame_count, resource));
    }

    const u32 D3DContext::CurBackbufIndex() const
    {
        return _render_windows[0]->_frame_index;
    }

    void D3DContext::ExecuteCommandBuffer(Ref<CommandBuffer> &cmd)
    {
        _cmd_worker->Push(std::move(cmd->_commands), SubmitParams{cmd->Name()});
    }

    void D3DContext::ExecuteCommandBufferSync(Ref<CommandBuffer> &cmd)
    {
        auto rhi_cmd = RHICommandBufferPool::Get(cmd->Name());
        for (auto *gfx_cmd: cmd->_commands)
        {
            ProcessGpuCommand(gfx_cmd, rhi_cmd.get());
        }
        ExecuteRHICommandBuffer(rhi_cmd.get());
        RHICommandBufferPool::Release(rhi_cmd);
    }

    void D3DContext::ExecuteRHICommandBuffer(RHICommandBuffer *cmd)
    {
        if (cmd->IsExecuted())
            return;
        auto d3dcmd = static_cast<D3DCommandBuffer *>(cmd);
        d3dcmd->Close();
        ID3D12CommandList *ppCommandLists[] = {d3dcmd->NativeCmdList()};
#ifdef _PIX_DEBUG
        PIXBeginEvent(m_commandQueue.Get(), cmd->ID(), ToWChar(cmd->Name()).c_str());
#endif// _PIX_DEBUG
        m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
        ++_fence_value;
        ThrowIfFailed(m_commandQueue->Signal(_p_cmd_buffer_fence.Get(), _fence_value));
#ifdef _PIX_DEBUG
        PIXEndEvent(m_commandQueue.Get());
#endif// _PIX_DEBUG
        d3dcmd->_fence_value = _fence_value;
        d3dcmd->PostExecute();
    }

    void D3DContext::Destroy()
    {
        // Ensure that the GPU is no longer referencing resources that are about to be
        // cleaned up by the destructor.
        WaitForGpu();
        //CloseHandle(m_fenceEvent);
        if (Application::Get()._is_multi_thread_rendering)
            _cmd_worker->Stop();
        //_p_gpu_timer->ReleaseDevice();
        //在这里析构分配器应该会有问题，纹理实际在在这之后还需要归还之前的分配
        D3DDescriptorMgr::Shutdown();
        // 在程序终止时调用 ReportLiveObjects() 函数
        IDXGIDebug1 *pDebug = nullptr;
        if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&pDebug))))
        {
            pDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_SUMMARY);
            pDebug->Release();
        }
    }

    void D3DContext::LoadPipeline()
    {
        UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
        // Enable the debug layer (requires the Graphics Tools "optional feature").
        // NOTE: Enabling the debug layer after device creation will invalidate the active device.
        {
            ComPtr<ID3D12Debug> debugController;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
            {
                debugController->EnableDebugLayer();

                // Enable additional debug layers.
                dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
            }
        }
        //https://learn.microsoft.com/zh-cn/windows/win32/direct3d12/using-d3d12-debug-layer-gpu-based-validation
        //EnableShaderBasedValidation();
#endif

        ComPtr<IDXGIFactory6> factory;
        ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));
        GetHardwareAdapter(factory.Get(), _p_adapter.GetAddressOf(), &_local_video_memory_info, &_non_local_video_memory_info);
        ThrowIfFailed(D3D12CreateDevice(_p_adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)));

        ComPtr<ID3D12InfoQueue> infoQueue;
        if (SUCCEEDED(m_device->QueryInterface(IID_PPV_ARGS(&infoQueue))))
        {
            // 中断条件（BREAK）设置
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);// 严重错误
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);     // 普通错误，如你遇到的 Reset 错误
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, FALSE);  // 可选：不在警告时中断

            // （可选）过滤无关紧要的消息
            D3D12_MESSAGE_ID denyIds[] = {
                    D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
                    D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,
                    // 你可以把 COMMAND_ALLOCATOR_SYNC 留下，不屏蔽它
            };

            D3D12_INFO_QUEUE_FILTER filter = {};
            filter.DenyList.NumIDs = _countof(denyIds);
            filter.DenyList.pIDList = denyIds;
            infoQueue->AddStorageFilterEntries(&filter);
        }

        D3DDescriptorMgr::Init();
        // Describe and create the command queue.
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

        //m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
        //m_frameIndex = _swapchain->GetCurrentBackBufferIndex();

        // Create a RTV for each frame.
        //for (UINT n = 0; n < RenderConstants::kFrameCount; n++)
        //{
        //    _fence_value[n] = 0;
        //}

        {
            ThrowIfFailed(CommandSignatureHelper::CreateDispatchCommandSignature(m_device.Get(), nullptr, _dispatch_cmd_sig.GetAddressOf()));
            ThrowIfFailed(CommandSignatureHelper::CreateDrawCommandSignature(m_device.Get(), nullptr, _draw_cmd_sig.GetAddressOf()));
            ThrowIfFailed(CommandSignatureHelper::CreateDrawIndexedCommandSignature(m_device.Get(), nullptr, _draw_indexed_cmd_sig.GetAddressOf()));
        }
#ifdef _DIRECT_WRITE
        InitDirectWriteContext();
        // Query the desktop's dpi settings, which will be used to create
        // D2D's render targets.
        float dpiX;
        float dpiY;
#pragma warning(push)
#pragma warning(disable : 4996)// GetDesktopDpi is deprecated.
        m_d2dFactory->GetDesktopDpi(&dpiX, &dpiY);
#pragma warning(pop)
        D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
                D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED),
                dpiX,
                dpiY);
        for (UINT n = 0; n < RenderConstants::kFrameCount; n++)
        {
            // Create a wrapped 11On12 resource of this back buffer. Since we are
            // rendering all D3D12 content first and then all D2D content, we specify
            // the In resource state as RENDER_TARGET - because D3D12 will have last
            // used it in this state - and the Out resource state as PRESENT. When
            // ReleaseWrappedResources() is called on the 11On12 device, the resource
            // will be transitioned to the PRESENT state.
            D3D11_RESOURCE_FLAGS d3d11Flags = {D3D11_BIND_RENDER_TARGET};
            ThrowIfFailed(m_d3d11On12Device->CreateWrappedResource(
                    _color_buffer[n].Get(),
                    &d3d11Flags,
                    D3D12_RESOURCE_STATE_RENDER_TARGET,
                    D3D12_RESOURCE_STATE_PRESENT,
                    IID_PPV_ARGS(&m_wrappedBackBuffers[n])));

            // Create a render target for D2D to draw directly to this back buffer.
            ComPtr<IDXGISurface> surface;
            ThrowIfFailed(m_wrappedBackBuffers[n].As(&surface));
            ThrowIfFailed(m_d2dDeviceContext->CreateBitmapFromDxgiSurface(
                    surface.Get(),
                    &bitmapProperties,
                    &m_d2dRenderTargets[n]));
        }
#endif
    }


    void D3DContext::LoadAssets()
    {
#ifdef _DIRECT_WRITE
        // Create D2D/DWrite objects for rendering text.
        {
            ThrowIfFailed(m_d2dDeviceContext->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &m_textBrush));
            ThrowIfFailed(m_dWriteFactory->CreateTextFormat(
                    L"Verdana",
                    NULL,
                    DWRITE_FONT_WEIGHT_NORMAL,
                    DWRITE_FONT_STYLE_NORMAL,
                    DWRITE_FONT_STRETCH_NORMAL,
                    50,
                    L"en-us",
                    &m_textFormat));
            ThrowIfFailed(m_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER));
            ThrowIfFailed(m_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER));
        }
#endif

        // Create synchronization objects.
        {
            ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(_p_cmd_buffer_fence.GetAddressOf())));
            _p_cmd_buffer_fence->SetName(L"ALD3DFence");
            //m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            //if (m_fenceEvent == nullptr)
            //{
            //    ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
            //}
            //WaitForGpu();
        }
    }

    void D3DContext::Present()
    {
        Vector<GfxCommand *> cmds{CommandPool::Get().Alloc<CommandPresent>()};
        _cmd_worker->Push(std::move(cmds), SubmitParams{"Present", true});
        if (!Application::Get()._is_multi_thread_rendering)
        {
            _cmd_worker->RunSync();
        }
    }

    u64 D3DContext::GetFenceValueGPU()
    {
        _cur_fence_value = _p_cmd_buffer_fence->GetCompletedValue();
        return _cur_fence_value;
    }

    u64 D3DContext::GetFenceValueCPU() const
    {
        return _fence_value;
    }

    void D3DContext::RegisterWindow(Window *window)
    {
        UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
        // Enable the debug layer (requires the Graphics Tools "optional feature").
        // NOTE: Enabling the debug layer after device creation will invalidate the active device.
        {
            ComPtr<ID3D12Debug> debugController;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
            {
                debugController->EnableDebugLayer();

                // Enable additional debug layers.
                dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
            }
        }
        //https://learn.microsoft.com/zh-cn/windows/win32/direct3d12/using-d3d12-debug-layer-gpu-based-validation
        //EnableShaderBasedValidation();
#endif

        ComPtr<IDXGIFactory6> factory;
        ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

        auto ctx = MakeScope<RenderWindowCtx>();
        ctx->_window = window;
        ctx->_rtv_allocation = D3DDescriptorMgr::Get().AllocCPU(RenderConstants::kFrameCount, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        ctx->_width = window->GetWidth();
        ctx->_height = window->GetHeight();
        // Describe and create the swap chain.
        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
        swapChainDesc.BufferCount = RenderConstants::kFrameCount;
        swapChainDesc.Width = window->GetWidth();
        swapChainDesc.Height = window->GetHeight();
        swapChainDesc.Format = RenderConstants::kColorRange == EColorRange::kLDR ? ConvertToDXGIFormat(RenderConstants::kLDRFormat) : ConvertToDXGIFormat(RenderConstants::kHDRFormat);
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        auto hwnd = static_cast<HWND>(ctx->_window->GetNativeWindowPtr());
        {
            D3DSwapchainInitializer initializer;
            initializer._command_queue = m_commandQueue.Get();
            initializer._device = m_device.Get();
            initializer._factory = factory.Get();
            initializer._format = RenderConstants::kColorRange == EColorRange::kLDR ? RenderConstants::kLDRFormat : RenderConstants::kHDRFormat;
            initializer._is_fullscreen = false;
            Vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvs(RenderConstants::kFrameCount);
            for (u16 i = 0; i < RenderConstants::kFrameCount; i++)
                rtvs[i] = ctx->_rtv_allocation.At(i);
            initializer._rtvs = rtvs;
            initializer._swapchain_desc = swapChainDesc;
            initializer._window = window;
            ctx->_swapchain = AL_NEW(D3DSwapchainTexture, initializer);
            ctx->_swapchain_format = ConvertToDXGIFormat(initializer._format);
        }
#if defined(_PIX_DEBUG)
        PIXSetTargetWindow(hwnd);
        PIXSetHUDOptions(PIXHUDOptions::PIX_HUD_SHOW_ON_NO_WINDOWS);
#endif
        ctx->_frame_index = ctx->_swapchain->GetCurrentBackBufferIndex();
        ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(ctx->_fence.GetAddressOf())));
        ctx->_fence->SetName(std::format(L"{}_fence", ctx->_window->GetTitle()).c_str());
        ctx->_fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        //ctx->_swapchain->Name(ToChar(ctx->_window->GetTitle()));
        _render_windows.emplace_back(std::move(ctx));
    }

    void D3DContext::UnRegisterWindow(Window *window)
    {
        std::erase_if(_render_windows, [&](Scope<RenderWindowCtx> &ctx) -> bool
                      { return ctx->_window == window; });
    }

    void D3DContext::TakeCapture()
    {
        //g_rdc_api->TriggerCapture();
        //if (!g_rdc_api->IsTargetControlConnected())
        //    g_rdc_api->LaunchReplayUI(1, nullptr);
        _is_next_frame_capture = true;
    }

    void D3DContext::ResizeSwapChain(void *window_handle, const u32 width, const u32 height)
    {
        auto it = std::find_if(_render_windows.begin(), _render_windows.end(), [&](Scope<RenderWindowCtx> &ctx) -> bool
                               { return ctx->_window->GetNativeWindowPtr() == window_handle; });
        if (it != _render_windows.end())
        {
            u32 new_size = (static_cast<u32>(width) << 16) | static_cast<u32>(height & 0xFFFF);
            it->get()->_new_backbuffer_size.store(new_size);
        }
    }


    void D3DContext::WaitForGpu()
    {
        for (auto &ctx: _render_windows)
        {
            ctx->WaitForGpu(m_commandQueue.Get());
        }
    }

    void D3DContext::WaitForFence(u64 fence_value)
    {
        for (auto &ctx: _render_windows)
        {
            ctx->WaitForFence(m_commandQueue.Get(), fence_value);
        }
    }

    void D3DContext::PresentImpl(D3DCommandBuffer *cmd)
    {
        static TimeMgr s_timer;
        CPUProfileBlock b("Reslove");
#ifdef DEAR_IMGUI
        auto dxcmd = cmd->NativeCmdList();
        auto rtv_handle = *_render_windows[0]->_swapchain->TargetCPUHandle(cmd);
        dxcmd->OMSetRenderTargets(1, &rtv_handle, FALSE, nullptr);
        //dxcmd->ClearRenderTargetView(rtv_handle, Colors::kBlack, 0, nullptr);
        ImGuiRenderer::Get().Render(cmd);
#endif// DEAR_IMGUI
        // Present the frame.
        for (auto &ctx: _render_windows)
        {
#ifndef _DIRECT_WRITE
            ctx->_swapchain->PreparePresent(cmd);
#endif// !_DIRECT_WRITE
        }
        ExecuteRHICommandBuffer(cmd);
        for (auto &ctx: _render_windows)
        {
            ctx->_swapchain->Present();
            {
                s_timer.MarkLocal();
                //WaitForGpu();
                ctx->MoveToNextFrame(m_commandQueue.Get());
                //if (Application::Get().GetFrameCount() % 60 == 0)
                Render::RenderingStates::s_gpu_latency = s_timer.GetElapsedSinceLastLocalMark();
            }
            if (_is_cur_frame_capturing)
            {
                EndCapture();
            }
            {
                u32 new_pack_size = ctx->_new_backbuffer_size.load();
                if (new_pack_size != 0u)
                {
                    u32 new_width = (new_pack_size >> 16) & 0xFFFF;
                    u32 new_height = new_pack_size & 0xFFFF;
                    ctx->WaitForGpu(m_commandQueue.Get());
                    ctx->_swapchain->Resize(new_width, new_height);
                    ctx->WaitForGpu(m_commandQueue.Get());
                    ctx->_new_backbuffer_size.store(0u);
                }
            }
        }

#ifdef _DIRECT_WRITE
            //RenderUI
            {
                D2D1_SIZE_F rtSize = m_d2dRenderTargets[m_frameIndex]->GetSize();
                D2D1_RECT_F textRect = D2D1::RectF(0, 0, rtSize.width, rtSize.height);
                static const WCHAR text[] = L"11On12";

                // Acquire our wrapped render target resource for the current back buffer.
                m_d3d11On12Device->AcquireWrappedResources(m_wrappedBackBuffers[m_frameIndex].GetAddressOf(), 1);

                // Render text directly to the back buffer.
                m_d2dDeviceContext->SetTarget(m_d2dRenderTargets[m_frameIndex].Get());
                m_d2dDeviceContext->BeginDraw();
                m_d2dDeviceContext->SetTransform(D2D1::Matrix3x2F::Identity());
                m_d2dDeviceContext->DrawText(
                        text,
                        _countof(text) - 1,
                        m_textFormat.Get(),
                        &textRect,
                        m_textBrush.Get());
                ThrowIfFailed(m_d2dDeviceContext->EndDraw());

                // Release our wrapped render target resource. Releasing
                // transitions the back buffer resource to the state specified
                // as the OutState when the wrapped resource was created.
                m_d3d11On12Device->ReleaseWrappedResources(m_wrappedBackBuffers[m_frameIndex].GetAddressOf(), 1);

                // Flush to submit the 11 command list to the shared command queue.
                m_d3d11DeviceContext->Flush();
            }
#endif//  _DIRECT_WRITE
        
        if (_frame_count % kResourceCleanupIntervalTick == 0)
        {
            if (_global_tracked_resource.size() > 0)
            {
                u64 origin_res_num = _global_tracked_resource.size();
                auto it = _global_tracked_resource.lower_bound(_frame_count);
                if (it != _global_tracked_resource.end())
                {
                    _global_tracked_resource.erase(_global_tracked_resource.begin(), it);
                }
                else
                    _global_tracked_resource.clear();
                origin_res_num -= _global_tracked_resource.size();
                LOG_WARNING("D3DContext::Present: Resource cleanup, {} resources released.", origin_res_num);
            }
            if (u32 release_size = GpuResourceManager::Get()->ReleaseSpace(); release_size > 0)
            {
                LOG_INFO("D3DContext::Present: Resource cleanup, {} byte released.", release_size);
            }
            if (RenderTexture::TotalGPUMemerySize() > kMaxRenderTextureMemorySize)
            {
                g_pRenderTexturePool->TryCleanUp();
                D3DDescriptorMgr::Get().ReleaseSpace();
            }
        }
        _readback_pool->Tick(_frame_count);
        ++_frame_count;
        _p_gpu_timer->EndFrame();
        Profiler::Get().CollectGPUTimeData();
        //CommandBufferPool::ReleaseAll();
        if (_is_next_frame_capture)
        {
            BeginCapture();
            _is_next_frame_capture = false;
            _is_cur_frame_capturing = true;
        }
        //if (Application::Get().GetFrameCount() % 60 == 0)
        {
            Render::RenderingStates::s_frame_time = s_timer.GetElapsedSinceLastLocalMark();
            Render::RenderingStates::s_frame_rate = 1000.0f / Render::RenderingStates::s_frame_time;
        }
        s_timer.MarkLocal();
    }
    void D3DContext::BeginCapture()
    {
#ifdef _PIX_DEBUG
        LOG_WARNING("Begin take capture...");
        static PIXCaptureParameters parms{};
        static u32 s_capture_count = 0u;
        PIXSetTargetWindow((HWND) Application::FocusedWindow()->GetNativeWindowPtr());
        _cur_capture_name = std::format(L"{}_{}{}", L"NewCapture", ToWChar(TimeMgr::CurrentTime("%Y-%m-%d_%H%M%S")), L".wpix");
        parms.GpuCaptureParameters.FileName = _cur_capture_name.data();
        PIXBeginCapture(PIX_CAPTURE_GPU, &parms);
        _is_next_frame_capture = true;
#else
        LOG_WARNING("No available gpu debug enabled!");
#endif// _PIX_DEBUG
    }
    void D3DContext::EndCapture()
    {
#ifdef _PIX_DEBUG
        PIXEndCapture(true);
        _is_cur_frame_capturing = false;
        PIXOpenCaptureInUI(_cur_capture_name.data());
        //ShellExecute(NULL, L"open", _cur_capture_name.data(), NULL, NULL, SW_SHOWNORMAL);
#endif// _PIX_DEBUG
    }
    void D3DContext::ResizeSwapChainImpl(const u32 width, const u32 height)
    {
        if (width == _render_windows[_cur_ctx_index]->_width && height == _render_windows[_cur_ctx_index]->_height)
            return;
        //m_frameIndex = _swapchain->GetCurrentBackBufferIndex();
#ifdef _DIRECT_WRITE
        m_d2dDeviceContext->SetTarget(nullptr);
#endif
        //m_d3d11On12Device->ReleaseWrappedResources(m_wrappedBackBuffers[m_frameIndex].GetAddressOf(), 1);
        WaitForGpu();
        // Release the resources holding references to the swap chain (requirement of
        // IDXGISwapChain::ResizeBuffers) and reset the frame fence values to the
        // current fence value.
        for (UINT n = 0; n < RenderConstants::kFrameCount; n++)
        {
#ifdef _DIRECT_WRITE
            m_wrappedBackBuffers[n].Reset();
            m_d2dRenderTargets[n].Reset();
#endif
        }
#ifdef _DIRECT_WRITE
        m_d3d11DeviceContext->Flush();
#endif
        // Resize the swap chain to the desired dimensions.
        //DXGI_SWAP_CHAIN_DESC desc = {};
        //m_swapChain->GetDesc(&desc);
        //ThrowIfFailed(m_swapChain->ResizeBuffers(RenderConstants::kFrameCount, width, height, desc.BufferDesc.Format, desc.Flags));
        //BOOL fullscreenState;
        //ThrowIfFailed(m_swapChain->GetFullscreenState(&fullscreenState, nullptr));
        //m_windowedMode = !fullscreenState;
        //m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
        WaitForGpu();
        _render_windows[_cur_ctx_index]->_width = width;
        _render_windows[_cur_ctx_index]->_height = height;
#ifdef _DIRECT_WRITE
        // Query the desktop's dpi settings, which will be used to create
        // D2D's render targets.
        float dpiX;
        float dpiY;
#pragma warning(push)
#pragma warning(disable : 4996)// GetDesktopDpi is deprecated.
        m_d2dFactory->GetDesktopDpi(&dpiX, &dpiY);
#pragma warning(pop)
        D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
                D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED),
                dpiX,
                dpiY);

        for (UINT n = 0; n < RenderConstants::kFrameCount; n++)
        {
            ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&_color_buffer[n])));
            _color_buffer[n]->SetName(L"back_buffer");
            m_device->CreateRenderTargetView(_color_buffer[n].Get(), nullptr, _rtv_allocation.At(n));
            // Create a wrapped 11On12 resource of this back buffer. Since we are
            // rendering all D3D12 content first and then all D2D content, we specify
            // the In resource state as RENDER_TARGET - because D3D12 will have last
            // used it in this state - and the Out resource state as PRESENT. When
            // ReleaseWrappedResources() is called on the 11On12 device, the resource
            // will be transitioned to the PRESENT state.
            D3D11_RESOURCE_FLAGS d3d11Flags = {D3D11_BIND_RENDER_TARGET};
            ThrowIfFailed(m_d3d11On12Device->CreateWrappedResource(
                    _color_buffer[n].Get(),
                    &d3d11Flags,
                    D3D12_RESOURCE_STATE_RENDER_TARGET,
                    D3D12_RESOURCE_STATE_PRESENT,
                    IID_PPV_ARGS(&m_wrappedBackBuffers[n])));
            // Create a render target for D2D to draw directly to this back buffer.
            ComPtr<IDXGISurface> surface;
            ThrowIfFailed(m_wrappedBackBuffers[n].As(&surface));
            ThrowIfFailed(m_d2dDeviceContext->CreateBitmapFromDxgiSurface(
                    surface.Get(),
                    &bitmapProperties,
                    &m_d2dRenderTargets[n]));
            //rtvHandle.Offset(1, _rtv_desc_size);
            //ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[n])));
        }
#else

#endif//_DIRECT_WRITE
    }

#ifdef _DIRECT_WRITE
    void D3DContext::InitDirectWriteContext()
    {
        //ThrowIfFailed(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown **>(&_dw_factory)));
        ComPtr<ID3D11Device> d3d11_dev = nullptr;
        UINT d3d11DeviceFlags = 0U;
        D2D1_FACTORY_OPTIONS d2dFactoryOptions = {};
#ifdef _DEBUG
        d3d11DeviceFlags = D3D11_CREATE_DEVICE_DEBUG | D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        d2dFactoryOptions.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#else
        d3d11DeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#endif
        ComPtr<ID3D11Device> d3d11Device;
        ThrowIfFailed(D3D11On12CreateDevice(m_device.Get(), d3d11DeviceFlags, nullptr, 0U,
                                            reinterpret_cast<IUnknown **>(m_commandQueue.GetAddressOf()), 1U, 0U, &d3d11Device, &m_d3d11DeviceContext, nullptr));
        ThrowIfFailed(d3d11Device.As(&m_d3d11On12Device));
        // Create D2D/DWrite components.
        {
            D2D1_DEVICE_CONTEXT_OPTIONS deviceOptions = D2D1_DEVICE_CONTEXT_OPTIONS_NONE;
            ThrowIfFailed(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory3), &d2dFactoryOptions, &m_d2dFactory));
            ComPtr<IDXGIDevice> dxgiDevice;
            ThrowIfFailed(m_d3d11On12Device.As(&dxgiDevice));
            ThrowIfFailed(m_d2dFactory->CreateDevice(dxgiDevice.Get(), &m_d2dDevice));
            ThrowIfFailed(m_d2dDevice->CreateDeviceContext(deviceOptions, &m_d2dDeviceContext));
            ThrowIfFailed(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &m_dWriteFactory));
        }
    }
#endif
    void D3DContext::ReadBack(GpuResource *res, u8 *data, u32 size)
    {
        if (res->GetResourceType() == EGpuResType::kBuffer)
        {
            size = ALIGN_TO_256(size);
            auto copy_dst = _readback_pool->Acquire(size, _frame_count);
            auto cmd = RHICommandBufferPool::Get("Readback");
            auto d3dcmd = static_cast<D3DCommandBuffer *>(cmd.get());
            auto dxcmd = d3dcmd->NativeCmdList();
            res->StateTranslation(cmd.get(), EResourceState::kCopySource, UINT32_MAX);
            dxcmd->CopyResource(copy_dst.Get(), reinterpret_cast<ID3D12Resource *>(res->NativeResource()));
            //_state_guard.MakesureResourceState(dxcmd, _p_d3d_res.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            ExecuteRHICommandBuffer(cmd.get());
            u64 cmd_fence_value = d3dcmd->_fence_value;
            while (_p_cmd_buffer_fence->GetCompletedValue() < cmd_fence_value)
            {
                std::this_thread::yield();
            }
            D3D12_RANGE readbackBufferRange{0, size};
            u8 *tmp_data = nullptr;
            copy_dst->Map(0, &readbackBufferRange, reinterpret_cast<void **>(&tmp_data));
            D3D12_RANGE emptyRange{0, 0};
            copy_dst->Unmap(0, &emptyRange);
            memcpy(data, tmp_data, size);
            RHICommandBufferPool::Release(cmd);
            TrackResource(copy_dst);
        }
        else
        {
            LOG_ERROR("Readback only support buffer resource!");
        }
    }
    void D3DContext::ReadBack(ID3D12Resource *res, D3DResourceStateGuard &state_guard, u8 *data, u32 size)
    {
        size = ALIGN_TO_256(size);
        auto copy_dst = _readback_pool->Acquire(size, _frame_count);
        auto cmd = RHICommandBufferPool::Get("Readback");
        auto d3dcmd = static_cast<D3DCommandBuffer *>(cmd.get());
        auto dxcmd = d3dcmd->NativeCmdList();
        auto old_state = state_guard.CurState();
        state_guard.MakesureResourceState(dxcmd, D3D12_RESOURCE_STATE_COPY_SOURCE);
        dxcmd->CopyResource(copy_dst.Get(), res);
        state_guard.MakesureResourceState(dxcmd, old_state);
        ExecuteRHICommandBuffer(cmd.get());
        u64 cmd_fence_value = d3dcmd->_fence_value;
        while (_p_cmd_buffer_fence->GetCompletedValue() < cmd_fence_value)
        {
            std::this_thread::yield();
        }
        D3D12_RANGE readbackBufferRange{0, size};
        u8 *tmp_data = nullptr;
        copy_dst->Map(0, &readbackBufferRange, reinterpret_cast<void **>(&tmp_data));
        D3D12_RANGE emptyRange{0, 0};
        copy_dst->Unmap(0, &emptyRange);
        memcpy(data, tmp_data, size);
        RHICommandBufferPool::Release(cmd);
    }

    void D3DContext::ReadBackAsync(ID3D12Resource *src, D3DResourceStateGuard &state_guard, u32 size, std::function<void(const u8 *)> callback)
    {
        auto copy_dst = _readback_pool->Acquire(size, _frame_count);// 已对齐分配
        auto cmd = RHICommandBufferPool::Get("Readback");
        auto dxcmd = static_cast<D3DCommandBuffer *>(cmd.get())->NativeCmdList();

        auto old_state = state_guard.CurState();
        state_guard.MakesureResourceState(dxcmd, D3D12_RESOURCE_STATE_COPY_SOURCE);
        dxcmd->CopyBufferRegion(copy_dst.Get(), 0, src, 0, size);// 替换 CopyResource
        state_guard.MakesureResourceState(dxcmd, old_state);

        ExecuteRHICommandBuffer(cmd.get());
        u64 fence_value = static_cast<D3DCommandBuffer *>(cmd.get())->_fence_value;

        auto copy_dst_capture = copy_dst;// 确保 lambda 生命周期
        JobSystem::Get().Dispatch([this, copy_dst_capture, size, fence_value, callback]()
                                  {
                                      while (_p_cmd_buffer_fence->GetCompletedValue() < fence_value)
                                          std::this_thread::yield();
                                      D3D12_RANGE range{0, size};
                                      u8 *raw_data = nullptr;
                                      copy_dst_capture->Map(0, &range, reinterpret_cast<void **>(const_cast<u8 **>(&raw_data)));
                                      //u8* copy_data = AL_NEW(u8,size);
                                      //memcpy(copy_data, raw_data, size);
                                      callback(raw_data);
                                      copy_dst_capture->Unmap(0, nullptr);
                                      //AL_FREE(copy_data);
                                  });

        RHICommandBufferPool::Release(cmd);// 无需早于 callback 完成
    }

    void D3DContext::ReadBackAsync(GpuResource *res, std::function<void(u8 *)> callback)
    {
        if (res->GetResourceType() == EGpuResType::kBuffer)
        {
            u64 size = res->GetSize();
            auto copy_dst = _readback_pool->Acquire(size, _frame_count);
            auto cmd = RHICommandBufferPool::Get("Readback");
            auto dxcmd = static_cast<D3DCommandBuffer *>(cmd.get())->NativeCmdList();
            res->StateTranslation(cmd.get(), EResourceState::kCopySource, UINT32_MAX);
            dxcmd->CopyResource(copy_dst.Get(), reinterpret_cast<ID3D12Resource *>(res->NativeResource()));
            //_state_guard.MakesureResourceState(dxcmd, _p_d3d_res.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            ExecuteRHICommandBuffer(cmd.get());
            u64 cmd_fence_value = static_cast<D3DCommandBuffer *>(cmd.get())->_fence_value;
            JobSystem::Get().Dispatch([&]() mutable
                                      {
                while (_p_cmd_buffer_fence->GetCompletedValue() < cmd_fence_value)
                {
                    std::this_thread::yield();
                }
                D3D12_RANGE readbackBufferRange{0, size};
                u8* data = nullptr;
                copy_dst->Map(0, &readbackBufferRange, reinterpret_cast<void **>(&data));
                D3D12_RANGE emptyRange{0, 0};
                copy_dst->Unmap(0, &emptyRange);
                callback(data); });
            RHICommandBufferPool::Release(cmd);
        }
        else
        {
            LOG_ERROR("Readback only support buffer resource!");
        }
    }

    void D3DContext::CreateResource(GpuResource *res)
    {
        CreateResource(res, nullptr);
    }


    void D3DContext::CreateResource(GpuResource *res, UploadParams *params)
    {
        if (res->GetResourceType() == EGpuResType::kGraphicsPSO || res->GetResourceType() == EGpuResType::kRenderTexture)
        {
            res->Upload(this, nullptr, params);
            DESTORY_PTR(params);
        }
        else
        {
            auto cmd = CommandPool::Get().Alloc<CommandGpuResourceUpload>();
            cmd->_res = res;
            cmd->_params = params;
            Vector<GfxCommand *> cmds = {cmd};
            _cmd_worker->Push(std::move(cmds), SubmitParams{"ResourceCreate: " + res->Name()});
        }
    }

    void D3DContext::ProcessGpuCommand(GfxCommand *cmd, RHICommandBuffer *cmd_buffer)
    {
        D3DCommandBuffer *d3dcmd = static_cast<D3DCommandBuffer *>(cmd_buffer);
        auto dxcmd = d3dcmd->NativeCmdList();
        static std::stack<CommandProfiler *> s_begin_profiler_stack{};
        if (cmd->GetCmdType() == EGpuCommandType::kAllocConstBuffer)
        {
            auto alloc_cmd = static_cast<CommandAllocConstBuffer *>(cmd);
            d3dcmd->AllocConstBuffer(alloc_cmd->_name, alloc_cmd->_size, alloc_cmd->_data);
        }
        else if (cmd->GetCmdType() == EGpuCommandType::kClearTarget)
        {
            auto clear_cmd = static_cast<CommandClearTarget *>(cmd);
            if (clear_cmd->_flag & EClearFlag::kColor)
            {
                u16 color_num = std::min<u16>(clear_cmd->_color_target_num, d3dcmd->_color_count);
                for (u16 i = 0; i < color_num; ++i)
                {
                    dxcmd->ClearRenderTargetView(*d3dcmd->_colors[i], clear_cmd->_colors[i].Data(), 0, nullptr);
                }
            }
            if (clear_cmd->_flag & EClearFlag::kDepth)
            {
                D3D12_CLEAR_FLAGS depth_flag = D3D12_CLEAR_FLAG_DEPTH;
                if (clear_cmd->_flag & EClearFlag::kStencil)
                    depth_flag |= D3D12_CLEAR_FLAG_STENCIL;
                dxcmd->ClearDepthStencilView(*d3dcmd->_depth, depth_flag, clear_cmd->_depth, clear_cmd->_stencil, 0, nullptr);
            }
        }
        else if (cmd->GetCmdType() == EGpuCommandType::kSetTarget)
        {
            auto set_cmd = static_cast<CommandSetTarget *>(cmd);
            d3dcmd->ResetRenderTarget();
            if (set_cmd->_color_target_num == 0)
            {
                if (set_cmd->_depth_target != nullptr)
                {
                    GraphicsPipelineStateMgr::SetRenderTargetState(EALGFormat::EALGFormat::kALGFormatUNKOWN, set_cmd->_depth_target->PixelFormat(), 0);
                    auto drt = static_cast<D3DRenderTexture *>(set_cmd->_depth_target);
                    d3dcmd->_color_count = 0u;
                    d3dcmd->_depth = drt->TargetCPUHandle(d3dcmd, set_cmd->_depth_index);
                    d3dcmd->_scissors[0] = D3DConvertUtils::ToD3DRect(set_cmd->_viewports[0]);
                    d3dcmd->_viewports[0] = D3DConvertUtils::ToD3DViewport(set_cmd->_viewports[0]);
                    dxcmd->OMSetRenderTargets(0, nullptr, 0, d3dcmd->_depth);
                    dxcmd->RSSetScissorRects(1, d3dcmd->_scissors.data());
                    dxcmd->RSSetViewports(1, d3dcmd->_viewports.data());
                    d3dcmd->MarkUsedResource(set_cmd->_depth_target);
                }
            }
            else
            {
                d3dcmd->_color_count = set_cmd->_color_target_num;
                bool is_depth_valid = set_cmd->_depth_target != nullptr;
                d3dcmd->_depth = is_depth_valid ? static_cast<D3DRenderTexture *>(set_cmd->_depth_target)->TargetCPUHandle(d3dcmd, set_cmd->_depth_index) : nullptr;
                static D3D12_CPU_DESCRIPTOR_HANDLE handles[8];
                for (u16 i = 0; i < d3dcmd->_color_count; ++i)
                {
                    d3dcmd->_scissors[i] = D3DConvertUtils::ToD3DRect(set_cmd->_viewports[i]);
                    d3dcmd->_viewports[i] = D3DConvertUtils::ToD3DViewport(set_cmd->_viewports[i]);
                    EALGFormat::EALGFormat color_format = set_cmd->_color_target[i]->PixelFormat();
                    GraphicsPipelineStateMgr::SetRenderTargetState(color_format, is_depth_valid ? set_cmd->_depth_target->PixelFormat() : EALGFormat::EALGFormat::kALGFormatUNKOWN, (u8) i);
                    D3D12_CPU_DESCRIPTOR_HANDLE* rtv;
                    if (set_cmd->_color_target[i]->IsSwapChain())
                    {
                        auto rt = static_cast<D3DSwapchainTexture *>(set_cmd->_color_target[i]);
                        rtv = rt->TargetCPUHandle(d3dcmd);
                    }
                    else
                    {
                        auto rt = static_cast<D3DRenderTexture *>(set_cmd->_color_target[i]);
                        rtv = rt->TargetCPUHandle(d3dcmd, set_cmd->_color_indices[i]);
                    }
                    d3dcmd->_colors[i] = rtv;
                    d3dcmd->MarkUsedResource(set_cmd->_color_target[i]);
                    handles[i] = *d3dcmd->_colors[i];
                }
                if (is_depth_valid)
                    d3dcmd->MarkUsedResource(set_cmd->_depth_target);
                dxcmd->RSSetScissorRects(set_cmd->_color_target_num, d3dcmd->_scissors.data());
                dxcmd->RSSetViewports(set_cmd->_color_target_num, d3dcmd->_viewports.data());
                dxcmd->OMSetRenderTargets(d3dcmd->_color_count, handles, false, d3dcmd->_depth);
            }
        }
        else if (cmd->GetCmdType() == EGpuCommandType::kCustom)
        {
            auto custom_cmd = static_cast<CommandCustom *>(cmd);
            custom_cmd->_func();
        }
        else if (cmd->GetCmdType() == EGpuCommandType::kScissorRect)
        {
            auto scissor_cmd = static_cast<CommandScissor*>(cmd);
            static D3D12_RECT s_d3d_rects[RenderConstants::kMaxMRTNum];
            for (u32 i = 0; i < scissor_cmd->_num; ++i)
            {
                s_d3d_rects[i] = D3DConvertUtils::ToD3DRect(scissor_cmd->_rects[i]);
            }
            dxcmd->RSSetScissorRects(scissor_cmd->_num, s_d3d_rects);
        }
        else if (cmd->GetCmdType() == EGpuCommandType::kResourceUpload)
        {
            auto upload_cmd = static_cast<CommandGpuResourceUpload *>(cmd);
            if (ObjectRegister::Get().Alive(upload_cmd->_res))
            {
                upload_cmd->_res->Upload(this, cmd_buffer, upload_cmd->_params);
                upload_cmd->_res->Name(upload_cmd->_res->Name());//这里将name写入d3d resource，之前resource一直为空
            }
        }
        else if (cmd->GetCmdType() == EGpuCommandType::kTransResourceState)
        {
            auto transf_cmd = static_cast<CommandTranslateState *>(cmd);
            transf_cmd->_res->StateTranslation(cmd_buffer, transf_cmd->_new_state, transf_cmd->_sub_res);
        }
        else if (cmd->GetCmdType() == EGpuCommandType::kDraw)
        {
            auto draw_cmd = static_cast<CommandDraw *>(cmd);
            draw_cmd->_mat->Bind(draw_cmd->_pass_index);
            if (auto pso = GraphicsPipelineStateMgr::Get().FindMatchPSO(); pso != nullptr)
            {
                AL_ASSERT(draw_cmd->_mat->GetShader() == pso->StateDescriptor()._p_vertex_shader);
                bool is_indexed_draw = draw_cmd->_ib != nullptr;
                bool is_instance_draw = draw_cmd->_instance_count > 1;
                BindParams params;
                params._params._vb_binder._layout = &draw_cmd->_mat->GetShader()->PipelineInputLayout(draw_cmd->_pass_index);
                bool is_produced = draw_cmd->_vb == nullptr;
                if (!is_produced)
                    draw_cmd->_vb->Bind(d3dcmd, params);
                if (is_indexed_draw)
                    draw_cmd->_ib->Bind(d3dcmd, params);
                for (auto &it: d3dcmd->_allocations)
                {
                    auto &[name, alloc] = it;
                    if (pso->IsValidPipelineResource(EBindResDescType::kConstBuffer, name))
                    {
                        auto res = PipelineResource(d3dcmd->_upload_buf.get(), EBindResDescType::kConstBufferRaw, name, PipelineResource::kPriorityCmd);
                        res._addi_info._gpu_handle = alloc.GPU;
                        pso->SetPipelineResource(res);
                    }
                }
                if (draw_cmd->_per_obj_cb != nullptr)
                    pso->SetPipelineResource(PipelineResource(draw_cmd->_per_obj_cb, EBindResDescType::kConstBuffer, RenderConstants::kCBufNamePerObject, PipelineResource::kPriorityCmd));
                ++Render::RenderingStates::s_temp_draw_call;
                u32 vertex_count = is_produced ? 3u : draw_cmd->_vb->GetVertexCount() * draw_cmd->_instance_count;//目前只有程序化矩形
                u32 triangle_count = is_indexed_draw ? draw_cmd->_ib->GetCount() / 3 : draw_cmd->_vb? draw_cmd->_vb->GetVertexCount() / 3 : 0u;
                triangle_count *= draw_cmd->_instance_count;
                Render::RenderingStates::s_temp_triangle_num += triangle_count;
                Render::RenderingStates::s_temp_vertex_num += vertex_count;
                pso->Bind(cmd_buffer, params);
                d3dcmd->MarkUsedResource(pso);
                if (draw_cmd->_arg_buffer)
                {
                    D3DGPUBuffer *d3d_buf = static_cast<D3DGPUBuffer *>(draw_cmd->_arg_buffer);
                    dxcmd->ExecuteIndirect(is_indexed_draw ? _draw_indexed_cmd_sig.Get() : _draw_cmd_sig.Get(), 1u,
                                           d3d_buf->GetNativeResource(), draw_cmd->_arg_offset, d3d_buf->GetCounterBuffer(), 0u);
                }
                else
                {
                    if (is_indexed_draw)
                    {
                        const u32 index_count = draw_cmd->_index_num == 0u ? draw_cmd->_ib->GetCount() : draw_cmd->_index_num;
                        dxcmd->DrawIndexedInstanced(index_count, draw_cmd->_instance_count, draw_cmd->_index_start, 0, 0);
                    }
                    else
                        dxcmd->DrawInstanced(draw_cmd->_vb->GetVertexCount(), draw_cmd->_instance_count, 0, 0);
                }
            }
        }
        else if (cmd->GetCmdType() == EGpuCommandType::kCopyCounter)
        {
            auto cmd_cpc = static_cast<CommandCopyCounter *>(cmd);
            // 获取 ID3D12Resource* 对象
            D3DGPUBuffer *src_d3d_buf = static_cast<D3DGPUBuffer *>(cmd_cpc->_src);
            D3DGPUBuffer *dst_d3d_buf = static_cast<D3DGPUBuffer *>(cmd_cpc->_dst);
            auto src_old_state = src_d3d_buf->_counter_state_guard.CurState();
            auto dst_old_state = dst_d3d_buf->_state_guard.CurState();
            src_d3d_buf->_counter_state_guard.MakesureResourceState(dxcmd, D3D12_RESOURCE_STATE_COPY_SOURCE);
            dst_d3d_buf->_state_guard.MakesureResourceState(dxcmd, D3D12_RESOURCE_STATE_COPY_DEST);
            dxcmd->CopyBufferRegion(dst_d3d_buf->GetNativeResource(), cmd_cpc->_dst_offset, src_d3d_buf->GetCounterBuffer(), 0u, sizeof(u32));
            src_d3d_buf->_counter_state_guard.MakesureResourceState(dxcmd, src_old_state);
            dst_d3d_buf->_state_guard.MakesureResourceState(dxcmd, dst_old_state);
        }
        else if (cmd->GetCmdType() == EGpuCommandType::kDispatch)
        {
            auto cmd_disp = static_cast<CommandDispatch *>(cmd);
            bool is_indirect = cmd_disp->_arg_buffer != nullptr;
            ShaderVariantHash active_variant = cmd_disp->_cs->ActiveVariant(cmd_disp->_kernel);
            cmd_disp->_cs->Bind(d3dcmd, cmd_disp->_kernel);
            for (auto &it: d3dcmd->_allocations)
            {
                auto &[name, alloc] = it;
                if (auto slot = cmd_disp->_cs->NameToSlot(name, cmd_disp->_kernel, active_variant); slot >= 0)
                {
                    dxcmd->SetComputeRootConstantBufferView(slot, alloc.GPU);
                }
            }
            if (is_indirect)
            {
                D3DGPUBuffer *d3d_buf = static_cast<D3DGPUBuffer *>(cmd_disp->_arg_buffer);
                dxcmd->ExecuteIndirect(_dispatch_cmd_sig.Get(), 1u, d3d_buf->GetNativeResource(), cmd_disp->_arg_offset,
                                       d3d_buf->GetCounterBuffer(), 0u);
            }
            else
            {
                dxcmd->Dispatch(cmd_disp->_group_num_x, cmd_disp->_group_num_y, cmd_disp->_group_num_z);
            }
            ++Render::RenderingStates::s_temp_dispatch_call;
        }
        else if (cmd->GetCmdType() == EGpuCommandType::kCommandProfiler)
        {
            auto cmd_profiler = static_cast<CommandProfiler *>(cmd);
            if (cmd_profiler->_is_start)
            {
                cmd_profiler->_gpu_index = Profiler::Get().StartGpuProfile(cmd_buffer, cmd_profiler->_name);
                Profiler::Get().AddGPUProfilerHierarchy(true, (u32) cmd_profiler->_gpu_index);
                cmd_profiler->_cpu_index = Profiler::Get().StartCPUProfile(cmd_profiler->_name + "_Submit");
                Profiler::Get().AddCPUProfilerHierarchy(true, (u32) cmd_profiler->_cpu_index);
                s_begin_profiler_stack.push(cmd_profiler);
            }
            else
            {
                Profiler::Get().EndGpuProfile(cmd_buffer, s_begin_profiler_stack.top()->_gpu_index);
                Profiler::Get().AddGPUProfilerHierarchy(false, (u32) s_begin_profiler_stack.top()->_gpu_index);
                Profiler::Get().EndCPUProfile(s_begin_profiler_stack.top()->_cpu_index);
                Profiler::Get().AddCPUProfilerHierarchy(false, (u32) s_begin_profiler_stack.top()->_cpu_index);
                s_begin_profiler_stack.pop();
            }
        }
        else if (cmd->GetCmdType() == EGpuCommandType::kReadBack)
        {
            auto cmd_rb = static_cast<CommandReadBack *>(cmd);
            if (cmd_rb->_res == nullptr)
            {
                LOG_WARNING("D3DContext::ProcessGpuCommand: Readback resource is nullptr!");
                return;
            }
            u64 size = cmd_rb->_is_counter_value ? 4u : std::min<u64>(cmd_rb->_res->GetSize(), (u64) cmd_rb->_size);
            auto copy_dst = _readback_pool->Acquire(size, _frame_count);
            D3DGPUBuffer *d3dbuffer = static_cast<D3DGPUBuffer *>(cmd_rb->_res);
            D3DResourceStateGuard *state_guard = cmd_rb->_is_counter_value ? &d3dbuffer->_counter_state_guard : &d3dbuffer->_state_guard;
            ID3D12Resource *copy_src = cmd_rb->_is_counter_value ? d3dbuffer->GetCounterBuffer() : d3dbuffer->GetNativeResource();
            auto old_state = state_guard->CurState();
            state_guard->MakesureResourceState(dxcmd, D3D12_RESOURCE_STATE_COPY_SOURCE);
            dxcmd->CopyBufferRegion(copy_dst.Get(), 0u, copy_src, 0u, size);
            state_guard->MakesureResourceState(dxcmd, old_state);

            u64 fence_value = _fence_value + 1;
            auto copy_dst_capture = copy_dst;// 确保 lambda 生命周期
            ReadbackCallback callback = std::move(cmd_rb->_callback);
            JobSystem::Get().Dispatch([this, copy_dst_capture, size, fence_value, callback]()
                                      {
                while (_p_cmd_buffer_fence->GetCompletedValue() < fence_value)
                    std::this_thread::yield();
                D3D12_RANGE range{0, size};
                u8* raw_data = nullptr;
                copy_dst_capture->Map(0, &range, reinterpret_cast<void **>(const_cast<u8**>(&raw_data)));
                callback(raw_data,(u32)size);
                copy_dst_capture->Unmap(0, nullptr); });
        }
        else if (cmd->GetCmdType() == EGpuCommandType::kPresent)
            PresentImpl(d3dcmd);
        else {};
    }
    void D3DContext::SubmitGpuCommandSync(GfxCommand *cmd)
    {
        auto rhi_cmd = RHICommandBufferPool::Get("SyncTask");
        ProcessGpuCommand(cmd, rhi_cmd.get());
        ExecuteRHICommandBuffer(rhi_cmd.get());
        RHICommandBufferPool::Release(rhi_cmd);
    }
#pragma endregion
}// namespace Ailu::RHI::DX12
