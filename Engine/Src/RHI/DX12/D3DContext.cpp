#include "RHI/DX12/D3DContext.h"
#include "Framework/Common/Allocator.hpp"
//#include "Ext/imgui/backends/imgui_impl_dx12.h"
#include "Framework/Common/Application.h"
#include "Framework/Common/JobSystem.h"
#include "Framework/Common/Assert.h"
#include "Framework/Common/JobSystem.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/Profiler.h"
#include "Framework/Common/RenderDebugConfig.h"
#include "RHI/DX12/D3DCommandBuffer.h"
#include "RHI/DX12/D3DGPUTimer.h"
#include "RHI/DX12/D3DSwapchain.h"
#include "RHI/DX12/GPUResourceManager.h"
#include "RHI/DX12/dxhelper.h"
#include "Render/Buffer.h"
#include "Render/Gizmo.h"
#include "Render/GpuResource.h"
#include "Render/GraphicsPipelineStateObject.h"
#include "Render/Material.h"
#include "Render/RenderingData.h"
#include "Render/RenderingStates.h"
#if AILU_ENABLE_FRAME_DEBUGGER
#include "Render/FrameDebugger/FrameCaptureService.h"
#include "Render/FrameDebugger/FrameCaptureSession.h"
#include "Render/FrameDebugger/FrameCaptureWriter.h"
#include "Render/FrameDebugger/FrameCaptureTypes.h"
#include "Render/RenderGraph/RenderGraph.h"
#endif
#include "pch.h"
#include <cstring>
#include <dxgidebug.h>
#include <limits>
#include <memory>
#include <stack>

#include "RHI/DX12/D3DBuffer.h"
#include "RHI/DX12/D3DGraphicsPipelineState.h"
#include <RHI/DX12/D3DShader.h>
#include <RHI/DX12/D3DTexture.h>
#include <d3d11.h>
#include <d3d12sdklayers.h>

#include "RHI/DX12/RayTracing/D3DRayTracingGeometry.h"
#include "RHI/DX12/RayTracing/D3DRayTracingScene.h"
#include "RHI/DX12/RayTracing/D3DRayTracingShader.h"
#include "Render/RayTracing/RayTracingShader.h"

#include "Framework/Common/EngineConfig.h"

#include "Ext/pix/Include/WinPixEventRuntime/pix3.h"
#include "Ext/renderdoc_app.h"//1.35
#include <Render/ImGuiRenderer.h>
#include <chrono>

//#define D3D_DEBUG_LAYER 1

using namespace Ailu::Render;

namespace Ailu::RHI::DX12
{
    namespace
    {
        // The D3D12 debug layer message store is drained once per frame from Present().  Every bound below
        // exists so a broken frame cannot make the diagnostic itself the bottleneck: the InfoQueue query is
        // cheap, but a single bad resource can emit thousands of identical messages per frame while writing one
        // log line costs orders of magnitude more than reading one message.
        constexpr UINT64 kMaxDebugMessagesPerDrain = 64u;
        constexpr u32 kMaxDebugMessageKinds = 16u;
        // Accumulated counts are flushed on a frame interval instead of every frame, so a persistent problem
        // costs a bounded number of lines per second rather than per frame.
        constexpr u32 kDebugMessageLogIntervalFrames = 60u;

        struct DebugMessageKind
        {
            D3D12_MESSAGE_SEVERITY _severity = D3D12_MESSAGE_SEVERITY_WARNING;
            D3D12_MESSAGE_ID _id = D3D12_MESSAGE_ID_UNKNOWN;
            u64 _count = 0u;
            String _sample;
        };

        // Set in Init() while the debug layer is enabled, cleared in the destructor.  Null means the debug layer
        // is off and the per-frame drain collapses to a single branch.
        ComPtr<ID3D12InfoQueue> s_debug_message_queue;
        Array<DebugMessageKind, kMaxDebugMessageKinds> s_debug_message_kinds;
        u32 s_debug_message_kind_count = 0u;
        u32 s_debug_message_frames_since_log = 0u;
        u64 s_debug_message_unlisted_count = 0u;

        void DrainD3DDebugLayerMessages()
        {
            const u32 frames_since_log = ++s_debug_message_frames_since_log;
            if (s_debug_message_queue != nullptr)
            {
                const UINT64 stored_count = s_debug_message_queue->GetNumStoredMessages();
                if (stored_count != 0u)
                {
                    const UINT64 read_count = std::min<UINT64>(stored_count, kMaxDebugMessagesPerDrain);
                    thread_local Vector<u8> message_storage;
                    for (UINT64 index = 0u; index < read_count; ++index)
                    {
                        SIZE_T message_size = 0u;
                        if (FAILED(s_debug_message_queue->GetMessage(index, nullptr, &message_size)) || message_size == 0u)
                            continue;
                        if (message_storage.size() < message_size)
                            message_storage.resize(message_size);
                        auto *message = reinterpret_cast<D3D12_MESSAGE *>(message_storage.data());
                        if (FAILED(s_debug_message_queue->GetMessage(index, message, &message_size)))
                            continue;

                        // Collapse repeats of the same (severity, id); the sample text is kept once.
                        u32 kind_index = s_debug_message_kind_count;
                        for (u32 i = 0u; i < s_debug_message_kind_count; ++i)
                        {
                            if (s_debug_message_kinds[i]._severity == message->Severity &&
                                s_debug_message_kinds[i]._id == message->ID)
                            {
                                kind_index = i;
                                break;
                            }
                        }
                        if (kind_index == s_debug_message_kind_count)
                        {
                            if (s_debug_message_kind_count >= kMaxDebugMessageKinds)
                            {
                                ++s_debug_message_unlisted_count;
                                continue;
                            }
                            auto &kind = s_debug_message_kinds[s_debug_message_kind_count++];
                            kind._severity = message->Severity;
                            kind._id = message->ID;
                            kind._sample.assign(message->pDescription,
                                                message->DescriptionByteLength > 0u
                                                    ? static_cast<size_t>(message->DescriptionByteLength - 1u)
                                                    : 0u);
                        }
                        ++s_debug_message_kinds[kind_index]._count;
                    }
                    // Anything past the read cap, plus whatever the store itself had to discard at its own
                    // message-count limit, is counted but not described.
                    s_debug_message_unlisted_count +=
                        (stored_count - read_count) +
                        s_debug_message_queue->GetNumMessagesDiscardedByMessageCountLimit();

                    // ID3D12InfoQueue only exposes a whole-store clear, so messages appended between the count
                    // above and this call are dropped.  That window is a few microseconds wide, the store is
                    // capped anyway, and the debug layer still streams every message to OutputDebugString, so
                    // the Visual Studio output window stays complete and a persistent problem reappears on the
                    // next frame.
                    s_debug_message_queue->ClearStoredMessages();
                }
            }

            if (frames_since_log < kDebugMessageLogIntervalFrames)
                return;
            s_debug_message_frames_since_log = 0u;
            if (s_debug_message_kind_count == 0u && s_debug_message_unlisted_count == 0u)
                return;

            for (u32 i = 0u; i < s_debug_message_kind_count; ++i)
            {
                const auto &kind = s_debug_message_kinds[i];
                LOG_WARNING("D3D12 debug layer: severity={}, id={}, count={}, sample={}",
                            static_cast<u32>(kind._severity), static_cast<u32>(kind._id), kind._count, kind._sample);
            }
            if (s_debug_message_unlisted_count > 0u)
                LOG_WARNING("D3D12 debug layer: {} further messages of other kinds suppressed",
                            s_debug_message_unlisted_count);

            s_debug_message_kinds = {};
            s_debug_message_kind_count = 0u;
            s_debug_message_unlisted_count = 0u;
        }
    }// namespace

#pragma region GpuCommandWorker

    GpuCommandWorker::GpuCommandWorker(GraphicsContext *context) : _ctx(context), _is_stop(false), _worker_thread(nullptr) {}
    GpuCommandWorker::~GpuCommandWorker()
    {
        if (_worker_thread && _worker_thread->joinable()) _worker_thread->join();
        LOG_INFO("Destory GpuCommandWorker");
    }

    void GpuCommandWorker::Push(Vector<GfxCommand *> &&cmds, SubmitParams &&params)
    {
        u32 submission_index = _next_submission_index.fetch_add(1u, std::memory_order_relaxed);
        _cmd_queue.Push(CommandGroup(std::move(cmds), std::move(params), submission_index));
        _cmd_wait_cv.notify_one();
    }

#if AILU_ENABLE_FRAME_DEBUGGER
    namespace
    {
        FrameDebugger::ECaptureObjectType CaptureObjectTypeForResource(GpuResource *res)
        {
            if (res != nullptr && dynamic_cast<Texture *>(res) != nullptr)
                return FrameDebugger::ECaptureObjectType::kTexture;
            return FrameDebugger::ECaptureObjectType::kBuffer;
        }

        // 把 RenderGraph compiled pass 的元数据解析为不可变捕获数据。该函数运行在命令录制线程，
        // 元数据（RenderGraph/CompiledRenderPass 指针）在整个 Capture 帧内保持有效，不写入最终 FrameCapture。
        u32 RecordRenderGraphPassCapture(FrameDebugger::FrameCaptureWriter &writer,
                                         const FrameDebugger::CapturePassMetadata &meta)
        {
            if (meta._compiled_pass == nullptr)
                return FrameDebugger::kInvalidFrameEventId;
            const auto *compiled_pass = meta._compiled_pass;
            auto *graph = meta._render_graph;
            if (compiled_pass == nullptr || graph == nullptr || compiled_pass->_pass == nullptr)
                return FrameDebugger::kInvalidFrameEventId;
            const auto *pass = compiled_pass->_pass;

            FrameDebugger::RenderGraphPassCapture pass_cap;
            pass_cap._name = writer.InternString(pass->_name);
            pass_cap._pass_type = (u8) pass->_type;
            pass_cap._submission_index = compiled_pass->_submission_index;
            pass_cap._allow_parallel_recording = compiled_pass->_allow_parallel_recording;

            const auto record_access = [&](const RDG::ResourceAccessRecord &record)
            {
                auto *res = graph->Resolve<GpuResource>(record._handle);
                FrameDebugger::RenderGraphResourceAccessCapture access_cap;
                access_cap._resource_id = writer.RegisterObject(res, CaptureObjectTypeForResource(res),
                                                                writer.InternString(res != nullptr ? res->Name() : ""));
                access_cap._resource_name = writer.InternString(res != nullptr ? res->Name() : "");
                access_cap._handle_id = record._handle._id;
                access_cap._handle_version = record._handle._version;
                access_cap._usage = (u32) record._access._usage;
                access_cap._load_action = (u8) record._access._load;
                access_cap._store_action = (u8) record._access._store;
                access_cap._mip_level = record._access._mip_level;
                access_cap._mip_count = record._access._mip_count;
                access_cap._array_slice = record._access._array_slice;
                access_cap._array_slice_count = record._access._array_slice_count;
                access_cap._all_sub_resources = record._access._all_sub_resources;
                writer.RecordRenderGraphResourceAccess(access_cap);
            };
            pass_cap._input_range_begin = writer.RGResourceAccessDataIndex();
            for (const auto &record: pass->_input_access_records)
                record_access(record);
            pass_cap._input_count = (u16) (writer.RGResourceAccessDataIndex() - pass_cap._input_range_begin);

            pass_cap._output_range_begin = writer.RGResourceAccessDataIndex();
            for (const auto &record: pass->_output_access_records)
                record_access(record);
            pass_cap._output_count = (u16) (writer.RGResourceAccessDataIndex() - pass_cap._output_range_begin);

            // Pre/post barriers 以事件形式由 D3DContext::ProcessGpuCommand 记录，这里不重复写入，
            // 避免 barrier 数组和统计被双倍计数。

            return writer.RecordRenderGraphPassEvent(pass_cap);
        }
    } // namespace
#endif

    void GpuCommandWorker::RecordCommandGroup(CommandGroup& group,Ref<RHICommandBuffer>& cmd)
    {
        PROFILE_BLOCK_CPU(std::format("RecordCommandGroup_{}", group._params._name))
        auto begin_time = std::chrono::high_resolution_clock::now();
        auto d3dcmd = static_cast<D3DCommandBuffer *>(cmd.get());
        d3dcmd->BeginRecordingGroup(group._params._name, group._submission_index);
        if (!group._params._released_temp_rts.empty())
        {
            auto released_temp_rts = std::move(group._params._released_temp_rts);
            d3dcmd->AddPostSubmitCallback([released_temp_rts = std::move(released_temp_rts)](u64)
            {
                for (RTHandle handle: released_temp_rts)
                    RenderTexture::ReleaseTempRT(handle);
            });
        }
        const bool emit_pix_event = g_engine_config._enable_pix && !group._params._is_end_frame && !group._params._name.empty();

#if AILU_ENABLE_FRAME_DEBUGGER
        Render::FrameDebugger::FrameCaptureSession *capture_session = Render::FrameDebugger::FrameCaptureService::ActiveSession();
        Render::FrameDebugger::FrameCaptureWriter *capture_writer = nullptr;
        if (capture_session)
        {
            capture_writer = capture_session->CreateWriter(group._submission_index, group._params._name);
        }
        // Command buffers are pooled. Always overwrite the previous frame's writer so a non-capture frame cannot
        // dereference a writer that was released when the preceding capture session was finalized.
        cmd->SetCaptureWriter(capture_writer);

        if (capture_writer)
        {
            u32 parent_event_id = RecordRenderGraphPassCapture(*capture_writer, group._params._capture_pass_metadata);
            if (emit_pix_event) PIXBeginEvent(d3dcmd->NativeCmdList(), 0u, group._params._name.c_str());
            u32 cg_event = capture_writer->BeginEvent(Render::FrameDebugger::EFrameEventType::kCommandGroup,
                                                       capture_writer->InternString(group._params._name), parent_event_id);
            capture_writer->SetCurrentParentEventId(cg_event);
            for (auto *task: group._cmds)
            {
                capture_writer->IncrementCommandIndex();
                capture_writer->ResetSubEventIndex();
                _ctx->ProcessGpuCommand(task, cmd.get());
            }
            capture_writer->EndEvent(cg_event, Render::FrameDebugger::EFrameEventExecutionResult::kExecuted);
            capture_writer->SetCurrentParentEventId(Render::FrameDebugger::kInvalidFrameEventId);
            if (emit_pix_event) PIXEndEvent(d3dcmd->NativeCmdList());
        }
        else
#endif
        {
            if (emit_pix_event) PIXBeginEvent(d3dcmd->NativeCmdList(), 0u, group._params._name.c_str());
            for (auto *task: group._cmds)
                _ctx->ProcessGpuCommand(task, cmd.get());
            if (emit_pix_event) PIXEndEvent(d3dcmd->NativeCmdList());
        }
        auto end_time = std::chrono::high_resolution_clock::now();
        f32 elapsed_ms = std::chrono::duration<f32, std::milli>(end_time - begin_time).count();
        auto &stats = d3dcmd->RecordingContext().RenderingStatesData();
        stats.Accumulate(group._params._rendering_states_data);
        stats.CommandRecordingTimeMs += elapsed_ms;
        ++stats.CommandGroupCount;
        stats.LastCommandSubmissionIndex = group._submission_index;
    }

    u32 GpuCommandWorker::EstimateRecordCost(const CommandGroup& group) const
    {
        u32 cost = 0u;
        for (const auto* command: group._cmds)
        {
            if (command == nullptr) continue;
            switch (command->GetCmdType())
            {
            case EGpuCommandType::kDraw: cost += 16u; break;
            case EGpuCommandType::kDispatch: cost += 14u; break;
            case EGpuCommandType::kDispatchRays: cost += 20u; break;
            case EGpuCommandType::kBuildAS: cost += 20u; break;
            case EGpuCommandType::kResourceUpload: cost += 12u; break;
            case EGpuCommandType::kReadBack: cost += 12u; break;
            case EGpuCommandType::kRequireResourceState: cost += 4u; break;
            case EGpuCommandType::kUavBarrier: cost += 2u; break;
            default: cost += 1u; break;
            }
        }
        return cost;
    }

    bool GpuCommandWorker::HasResourceUpload(const CommandGroup& group) const
    {
        return std::any_of(group._cmds.begin(), group._cmds.end(), [](const auto* command)
        {
            return command != nullptr && command->GetCmdType() == EGpuCommandType::kResourceUpload;
        });
    }

    void GpuCommandWorker::SubmitRecordedCommandBuffers(Vector<Ref<RHICommandBuffer>>& cmds)
    {
        if (cmds.empty())
            return;
        PROFILE_BLOCK_CPU("GpuCommandWorker_BatchedExecute")
        Vector<RHICommandBuffer *> raw_cmds;
        raw_cmds.reserve(cmds.size());
        for (auto& cmd: cmds) raw_cmds.emplace_back(cmd.get());
        _ctx->ExecuteRHICommandBuffers(raw_cmds);
        for (auto& cmd: cmds) RHICommandBufferPool::Release(cmd);
        cmds.clear();
    }

    void GpuCommandWorker::RunAsync()
    {
        SetThreadName("RenderThread");
        while (!_is_stop.load())
        {
            if (Application::Get().State() == EApplicationState::EApplicationState_Exit)
            {
                _is_stop.store(true);
                break;
            }
            PROFILE_BLOCK_CPU("GpuCommandWorker_FrameLoop")
            {
                // 帧边界：上一帧的命令缓冲已全部归还到池中，此时才允许 resize 交换链
                PROFILE_BLOCK_CPU("GpuCommandWorker_ApplySwapChainResize")
                _ctx->ApplyPendingSwapChainResizes();
            }
            {
                PROFILE_BLOCK_CPU("GpuCommandWorker_WaitForMain");
                Application::Get().WaitForMain();
            }
#if AILU_ENABLE_FRAME_DEBUGGER
            Render::FrameDebugger::FrameCaptureService::BeginFrame(_ctx->GetFrameCount());
#endif
            {
                PROFILE_BLOCK_CPU("GpuCommandWorker_ProcessPSO");
                GraphicsPipelineStateMgr::Get().ProcessPendingPSOCreationRequests();
            }
            {
                PROFILE_BLOCK_CPU("GpuCommandWorker_RunAsync")
                Vector<Ref<RHICommandBuffer>> recorded_cmds;
                Vector<WaitHandle> record_jobs;
                struct PendingGroup
                {
                    Ref<CommandGroup> _group;
                    u32 _cost = 0u;
                };
                Vector<PendingGroup> pending_groups;
                constexpr u32 kMaxRecordBatchCount = 6u;
                const auto wait_record_jobs = [&record_jobs]()
                {
                    PROFILE_BLOCK_CPU("GpuCommandWorker_WaitRecordJobs");
                for (const auto& job: record_jobs)
                    JobSystem::Get().Wait(job);
                record_jobs.clear();
                };
                const auto dispatch_pending_records = [&]()
                {
                    PROFILE_BLOCK_CPU("GpuCommandWorker_DispatchPendingRecords");
                    if (pending_groups.empty())
                        return;

                    const u32 batch_count = std::min<u32>(kMaxRecordBatchCount, static_cast<u32>(pending_groups.size()));
                    u32 remaining_cost = 0u;
                    for (const auto& pending: pending_groups)
                        remaining_cost += pending._cost;
                    u32 group_index = 0u;
                    for (u32 batch_index = 0u; batch_index < batch_count; ++batch_index)
                    {
                        const u32 groups_left = static_cast<u32>(pending_groups.size()) - group_index;
                        const u32 batches_left = batch_count - batch_index;
                        Vector<Ref<CommandGroup>> batch_groups;
                        const u32 target_cost = (remaining_cost + batches_left - 1u) / batches_left;
                        u32 batch_cost = 0u;
                        while (group_index < pending_groups.size())
                        {
                            const u32 groups_in_batch = static_cast<u32>(batch_groups.size());
                            const bool must_leave_group = groups_left - groups_in_batch <= batches_left - 1u;
                            if (!batch_groups.empty() && (batch_cost >= target_cost || must_leave_group))
                                break;
                            batch_cost += pending_groups[group_index]._cost;
                            batch_groups.emplace_back(std::move(pending_groups[group_index]._group));
                            ++group_index;
                        }
                        if (batch_groups.empty())
                            break;
                        remaining_cost -= batch_cost;

                        auto cmd = RHICommandBufferPool::Get(batch_groups.front()->_params._name);
                        recorded_cmds.emplace_back(cmd);
                        auto record_job = JobSystem::Get().CreateJob(
                            "RecordCommandBatch",
                            [this, groups = std::move(batch_groups), cmd]() mutable
                            {
                                PROFILE_BLOCK_CPU("RecordCommandBatch");
                                for (auto& group: groups)
                                    RecordCommandGroup(*group, cmd);
                            });
                        record_jobs.emplace_back(JobSystem::Get().Dispatch(record_job));
                    }
                    pending_groups.clear();
                };
                while (true)
                {
                    auto group_opt = _cmd_queue.Pop();
                    if (group_opt.has_value())
                    {
                        auto& group = group_opt.value();
                        const bool is_end_frame = group._params._is_end_frame;
                        const bool has_resource_upload = HasResourceUpload(group);
                        if (g_engine_config.enable_graphics_job && (is_end_frame || has_resource_upload))
                        {
                            dispatch_pending_records();
                            wait_record_jobs();
                            SubmitRecordedCommandBuffers(recorded_cmds);
                        }
                        if (g_engine_config.enable_graphics_job && !is_end_frame && !has_resource_upload)
                        {
                            PROFILE_BLOCK_CPU("GpuCommandWorker_QueuePendingGroup");
                            const u32 record_cost = EstimateRecordCost(group);
                            auto group_holder = MakeRef<CommandGroup>(std::move(group_opt.value()));
                            pending_groups.emplace_back(std::move(group_holder), record_cost);
                        }
                        else
                        {
                            PROFILE_BLOCK_CPU("GpuCommandWorker_PrepareGroup");
                            auto cmd = RHICommandBufferPool::Get(group._params._name);
                            RecordCommandGroup(group, cmd);
                            PROFILE_BLOCK_CPU(group._params._name + "_Execute")
                            _ctx->ExecuteRHICommandBuffer(cmd.get());
                            RHICommandBufferPool::Release(cmd);
                        }
                        if (is_end_frame)
                        {
                            PROFILE_BLOCK_CPU("EndFrame")
                            SubmitRecordedCommandBuffers(recorded_cmds);
                            EndFrame();
                            break;
                        }
                    }
                    else
                    {
                        PROFILE_BLOCK_CPU("GpuCommandWorker_WaitForCommand")
                        if (_is_stop.load())
                            break;
                        std::unique_lock<std::mutex> lock(_cmd_wait_mutex);
                        _cmd_wait_cv.wait(lock, [this] { return _is_stop.load() || !_cmd_queue.Empty(); });
                    }
                }
            }
        }
        LOG_INFO("GpuCommandWorker::RunAsync: Release {} un-executed cmd", _cmd_queue.Size());
        while (!_cmd_queue.Empty()) _cmd_queue.Pop();
    }
    void GpuCommandWorker::EndFrame()
    {
        // All recording and submission jobs for this frame have completed here.
        GraphicsPipelineStateMgr::Get().ProcessPendingPSOCreationRequests();
        u16 compiled_shader_num = 0u, compiled_compute_shader_num = 0u, compiled_raytracing_shader_num = 0u;
        while (!_pending_update_shaders.Empty())
        {
            if (auto obj = _pending_update_shaders.Pop(); obj.has_value())
            {
                if (Shader *shader = dynamic_cast<Shader *>(obj.value()); shader != nullptr)
                {
                    if (shader->PreProcessShader())
                    {
                        bool is_all_succeed = true;
                        is_all_succeed = shader->Compile(false);//暂时重编所有变体，避免材质切换变体后使用的是旧的shader
                        for (auto &mat: shader->GetAllReferencedMaterials())
                        {
                            mat->ConstructKeywords(shader);
                            // for (u16 i = 0; i < shader->PassCount(); i++)
                            // {
                            //     is_all_succeed &= shader->Compile(i, mat->ActiveVariantHash(i));
                            // }
                            if (is_all_succeed) { mat->ChangeShader(shader); }
                        }
                        compiled_shader_num++;
                    }
                }
                else if (ComputeShader *shader = dynamic_cast<ComputeShader *>(obj.value()); shader != nullptr)
                {
                    if (shader->Preprocess())
                    {
                        shader->_is_compiling.store(true);// shader Compile()也会设置这个值，这里设置一下防止读取该值时还没执行compile
                        shader->Compile(false);
                        compiled_compute_shader_num++;
                    }
                }
                else if (RayTracingShader *shader = dynamic_cast<RayTracingShader *>(obj.value()); shader != nullptr)
                {
                    shader->_is_compiling.store(true);
                    shader->Compile(false);
                    compiled_raytracing_shader_num++;
                }
            }
        }
        if (compiled_shader_num + compiled_compute_shader_num + compiled_raytracing_shader_num > 0u)
        {
            LOG_INFO("Compiled {} shaders, {} compute shaders and {} ray tracing shaders!", compiled_shader_num,
                     compiled_compute_shader_num, compiled_raytracing_shader_num);
        }
        GraphicsPipelineStateMgr::Get().ProcessPendingShaderCompiles();
        Render::RenderPipeline::Get().FrameCleanup();
        Render::RenderingStates::Reset();
        _next_submission_index.store(0u, std::memory_order_relaxed);
#if AILU_ENABLE_FRAME_DEBUGGER
        Render::FrameDebugger::FrameCaptureService::FinalizeFrame();
#endif
        if (Application::Get()._is_multi_thread_rendering.load()) Application::Get().NotifyMain();
    }
    void GpuCommandWorker::Start()
    {
        if (_worker_thread != nullptr)
            return;
        _is_stop.store(false);
        if (_worker_thread == nullptr)
            _worker_thread = AL_NEW_TAG(EMemoryTag::kJobSystem, std::thread, &GpuCommandWorker::RunAsync, this);
    }
    void GpuCommandWorker::Stop()
    {
        if (_worker_thread)
        {
            _is_stop.store(true);
            Application::Get().NotifyRender();
            _cmd_wait_cv.notify_all();
            if (_worker_thread->joinable()) _worker_thread->join();
            AL_DELETE(_worker_thread);
            LOG_INFO("Exit RenderThread")
        }
    }
    void GpuCommandWorker::RunSync()
    {
        PROFILE_BLOCK_CPU("GpuCommandWorker::RunSync")
        Vector<Ref<RHICommandBuffer>> recorded_cmds;
        Vector<WaitHandle> record_jobs;
        const auto wait_record_jobs = [&record_jobs]()
        {
            for (const auto& job: record_jobs)
                JobSystem::Get().Wait(job);
            record_jobs.clear();
        };
        while (!_cmd_queue.Empty())
        {
            auto &&group_opt = _cmd_queue.Pop();
            auto& group = group_opt.value();
            const bool is_end_frame = group._params._is_end_frame;
            const bool has_resource_upload = HasResourceUpload(group);
            if (g_engine_config.enable_graphics_job && (is_end_frame || has_resource_upload))
            {
                wait_record_jobs();
                SubmitRecordedCommandBuffers(recorded_cmds);
            }
            auto cmd = RHICommandBufferPool::Get(group._params._name);
            if (g_engine_config.enable_graphics_job && !is_end_frame && !has_resource_upload)
            {
                auto group_holder = MakeRef<CommandGroup>(std::move(group_opt.value()));
                auto record_job = JobSystem::Get().CreateJob(
                    "RecordCommandGroup",
                    [this, group_holder, cmd]() mutable { RecordCommandGroup(*group_holder, cmd); });
                record_jobs.emplace_back(JobSystem::Get().Dispatch(record_job));
            }
            else
                RecordCommandGroup(group, cmd);
            if (!g_engine_config.enable_graphics_job || is_end_frame || has_resource_upload)
            {
                PROFILE_BLOCK_CPU(group._params._name + "_Execute")
                _ctx->ExecuteRHICommandBuffer(cmd.get());
                RHICommandBufferPool::Release(cmd);
            }
            else
                recorded_cmds.emplace_back(cmd);
        }
        wait_record_jobs();
        SubmitRecordedCommandBuffers(recorded_cmds);
        {
            // 帧边界：本帧命令缓冲已全部归还到池中，此时才允许 resize 交换链
            _ctx->ApplyPendingSwapChainResizes();
            PROFILE_BLOCK_CPU("EndFrame")
            EndFrame();
        }
    }
#pragma endregion

#pragma region DX Helper
    static void GetHardwareAdapter(IDXGIFactory6 *pFactory, IDXGIAdapter4 **ppAdapter,
                                   DXGI_QUERY_VIDEO_MEMORY_INFO *p_local_video_memory_info,
                                   DXGI_QUERY_VIDEO_MEMORY_INFO *p_non_local_video_memory_info)
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
            if (SUCCEEDED(D3D12CreateDevice(pAdapter4, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr))) { break; }
        }
        *ppAdapter = pAdapter4;
    }
    static RENDERDOC_API_1_1_2 *s_rdc_api = nullptr;
    static HMODULE s_rdc_module = nullptr;

    static inline bool IsDirectXRaytracingSupported(IDXGIAdapter4 *adapter)
    {
        ComPtr<ID3D12Device> testDevice;
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 featureSupportData = {};

        return SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&testDevice))) &&
               SUCCEEDED(testDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &featureSupportData, sizeof(featureSupportData))) &&
               featureSupportData.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
    }

    static bool RdcLoadLatestRdcGpuCapturerLibrary()
    {
        if (s_rdc_module != nullptr && s_rdc_api != nullptr)
            return true;

        HMODULE module = GetModuleHandleW(L"renderdoc.dll");
        if (module == nullptr)
        {
            HKEY h_key = HKEY_LOCAL_MACHINE;
            LPCWSTR sub_key = L"SOFTWARE\\Classes\\CLSID\\{5D6BF029-A6BA-417A-8523-120492B1DCE3}\\InprocServer32\\";
            wchar_t value[256] = {};
            DWORD buffer_size = sizeof(value);
            LONG result = RegGetValue(h_key, sub_key, nullptr, RRF_RT_REG_SZ, nullptr, value, &buffer_size);
            if (result != ERROR_SUCCESS)
                return false;
            module = LoadLibraryW(value);
        }

        if (module == nullptr)
            return false;

        auto get_api = reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(module, "RENDERDOC_GetAPI"));
        if (get_api == nullptr)
        {
            LOG_ERROR("RenderDoc API entry point was not found. Error: {}", GetLastError());
            return false;
        }

        int ret = get_api(eRENDERDOC_API_Version_1_1_2, reinterpret_cast<void **>(&s_rdc_api));
        if (ret != 1 || s_rdc_api == nullptr)
        {
            LOG_ERROR("RenderDoc API initialization failed.");
            s_rdc_api = nullptr;
            return false;
        }

        s_rdc_module = module;
        s_rdc_api->SetCaptureFilePathTemplate("RenderDocCapture/Capture");
        s_rdc_api->MaskOverlayBits(RENDERDOC_OverlayBits::eRENDERDOC_Overlay_None,
                                    RENDERDOC_OverlayBits::eRENDERDOC_Overlay_None);
        LOG_INFO("RenderDoc API loaded.");
        return true;
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
    HRESULT CreateCommandSignature(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                   const std::vector<D3D12_INDIRECT_ARGUMENT_DESC> &arguments, UINT byteStride,
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
        HRESULT CreateDispatchCommandSignature(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                               ID3D12CommandSignature **ppCommandSignature)
        {
            D3D12_INDIRECT_ARGUMENT_DESC argDesc = {};
            argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

            return CreateCommandSignature(device, rootSignature, {argDesc}, sizeof(D3D12_DISPATCH_ARGUMENTS), ppCommandSignature);
        }

        // 2. 创建用于间接绘制的命令签名
        HRESULT CreateDrawCommandSignature(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                           ID3D12CommandSignature **ppCommandSignature)
        {
            D3D12_INDIRECT_ARGUMENT_DESC argDesc = {};
            argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

            return CreateCommandSignature(device, rootSignature, {argDesc}, sizeof(D3D12_DRAW_ARGUMENTS), ppCommandSignature);
        }

        // 3. 创建用于间接索引绘制的命令签名
        HRESULT CreateDrawIndexedCommandSignature(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                                  ID3D12CommandSignature **ppCommandSignature)
        {
            D3D12_INDIRECT_ARGUMENT_DESC argDesc = {};
            argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

            return CreateCommandSignature(device, rootSignature, {argDesc}, sizeof(D3D12_DRAW_INDEXED_ARGUMENTS), ppCommandSignature);
        }

        // 4. 创建带顶点缓冲区视图的间接绘制命令签名
        HRESULT CreateDrawCommandSignatureWithVBV(ID3D12Device *device, ID3D12RootSignature *rootSignature, UINT slot,
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

            return CreateCommandSignature(device, rootSignature, args, sizeof(D3D12_VERTEX_BUFFER_VIEW) + sizeof(D3D12_DRAW_ARGUMENTS),
                                          ppCommandSignature);
        }

        // 5. 创建带常量的间接调度命令签名
        HRESULT CreateDispatchCommandSignatureWithConstants(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                                            UINT rootParameterIndex, ID3D12CommandSignature **ppCommandSignature)
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

            return CreateCommandSignature(device, rootSignature, args,
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
        Ref<D3DSwapchainTexture> _swapchain;
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
                PROFILE_BLOCK_CPU("WaitForGPU")
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
            if (fence_value < fence->GetCompletedValue()) return;

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
        if (g_engine_config._enable_pix)
            PIXLoadLatestWinPixGpuCapturerLibrary();
        if (g_engine_config._enable_rdc)
            RdcLoadLatestRdcGpuCapturerLibrary();
        _cmd_worker = MakeScope<GpuCommandWorker>(this);
    }

    D3DContext::~D3DContext()
    {
        s_debug_message_queue.Reset();
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
        if (Application::Get()._is_multi_thread_rendering.load()) { _cmd_worker->Start(); }
        _is_hardware_ray_tracing_supported = IsDirectXRaytracingSupported(_p_adapter.Get());
    }

    void D3DContext::TryReleaseUnusedResources()
    {
        if (!_global_tracked_resource.empty())
        {
            u64 origin_res_num = _global_tracked_resource.size();
            auto it = _global_tracked_resource.lower_bound(_frame_count);
            if (it != _global_tracked_resource.end()) { _global_tracked_resource.erase(_global_tracked_resource.begin(), it); }
            else
                _global_tracked_resource.clear();

            origin_res_num -= _global_tracked_resource.size();
            if (origin_res_num > 0) LOG_WARNING("Release unused upload buffer with num {}.", origin_res_num);
        }
        g_pRenderTexturePool->TryCleanUp();
        if (auto num = D3DDescriptorMgr::Get().ReleaseSpace(); num > 0) { LOG_INFO("Release GPUVisibleDescriptor at with num {}.", num); }
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
    { _global_tracked_resource.insert(std::make_pair(_frame_count, resource)); }

    const u32 D3DContext::CurBackbufIndex() const
    {
        std::lock_guard<std::mutex> lock(_render_windows_mtx);
        return _render_windows[0]->_frame_index;
    }

    void D3DContext::ExecuteCommandBuffer(Ref<CommandBuffer> &cmd)
    {
        SubmitParams params{cmd->Name()};
        params._released_temp_rts = cmd->TakeReleasedTempRTs();
        params._rendering_states_data = cmd->TakeRenderingStatesData();
#if AILU_ENABLE_FRAME_DEBUGGER
        params._capture_pass_metadata = cmd->CapturePassMetadata();
#endif
        _cmd_worker->Push(cmd->TakeCommands(), std::move(params));
    }

    void D3DContext::ExecuteCommandBufferSync(Ref<CommandBuffer> &cmd)
    {
        auto released_temp_rts = cmd->TakeReleasedTempRTs();
        auto rhi_cmd = RHICommandBufferPool::Get(cmd->Name());
        rhi_cmd->RecordingContext().AccumulateRenderingStatesData(cmd->TakeRenderingStatesData());
        for (auto *gfx_cmd: cmd->GetCommands()) { ProcessGpuCommand(gfx_cmd, rhi_cmd.get()); }
        ExecuteRHICommandBuffer(rhi_cmd.get());
        for (RTHandle handle: released_temp_rts)
            RenderTexture::ReleaseTempRT(handle);
        RHICommandBufferPool::Release(rhi_cmd);
    }

    void D3DContext::ExecuteRHICommandBuffer(RHICommandBuffer *cmd)
    {
        Vector<RHICommandBuffer *> cmds{cmd};
        ExecuteRHICommandBuffers(cmds);
    }

    u64 D3DContext::ExecuteRHICommandBuffers(const Vector<RHICommandBuffer *> &cmds)
    {
        Vector<ID3D12CommandList *> native_cmds;
        Vector<D3DCommandBuffer *> d3d_cmds;
        native_cmds.reserve(cmds.size() * 2u);
        d3d_cmds.reserve(cmds.size());
        for (u32 cmd_ordinal = 0u; cmd_ordinal < cmds.size(); ++cmd_ordinal)
        {
            RHICommandBuffer *cmd = cmds[cmd_ordinal];
            if (cmd == nullptr || cmd->IsExecuted())
                continue;
            auto d3dcmd = static_cast<D3DCommandBuffer *>(cmd);
            {
                PROFILE_BLOCK_CPU("CloseCommandLists");
                d3dcmd->Close();
                d3d_cmds.emplace_back(d3dcmd);
            }
        }
        if (d3d_cmds.empty())
            return _fence_value;

        Vector<Ref<RHICommandBuffer>> queue_state_cmds;
        queue_state_cmds.reserve(d3d_cmds.size());
        u64 submitted_fence = 0u;
        auto submit_begin_time = std::chrono::high_resolution_clock::now();
        {
            std::lock_guard submit_lock(_command_submit_mtx);
            D3DQueueResourceStateTracker planned_queue_states = _queue_state_tracker;
            for (D3DCommandBuffer *d3dcmd: d3d_cmds)
            {
                auto queue_state_cmd = RHICommandBufferPool::Get("QueueResourceState");
                auto *queue_state_d3dcmd = static_cast<D3DCommandBuffer *>(queue_state_cmd.get());
                queue_state_d3dcmd->BeginResourceBarrierBatch();
                const bool has_queue_barriers =
                    planned_queue_states.ResolveInitialBarriers(*queue_state_d3dcmd, d3dcmd->StateTracker());
                queue_state_d3dcmd->EndResourceBarrierBatch();
                planned_queue_states.CommitFinalStates(d3dcmd->StateTracker());
                if (has_queue_barriers)
                {
                    queue_state_d3dcmd->Close();
                    native_cmds.emplace_back(queue_state_d3dcmd->NativeCmdList());
                    queue_state_cmds.emplace_back(std::move(queue_state_cmd));
                }
                else
                {
                    queue_state_d3dcmd->Close();
                    RHICommandBufferPool::Release(queue_state_cmd);
                }
                native_cmds.emplace_back(d3dcmd->NativeCmdList());
            }
            if (g_engine_config._enable_pix)
                PIXBeginEvent(m_commandQueue.Get(), 0u, L"RHICommandBufferBatch");
            {
                PROFILE_BLOCK_CPU("ExecuteCommandLists")
                m_commandQueue->ExecuteCommandLists(static_cast<UINT>(native_cmds.size()), native_cmds.data());
                {
                    std::lock_guard lock(_cmd_fence_mtx);
                    submitted_fence = ++_fence_value;
                    ThrowIfFailed(m_commandQueue->Signal(_p_cmd_buffer_fence.Get(), submitted_fence));
                }
            }
            for (D3DCommandBuffer *d3dcmd: d3d_cmds)
                _queue_state_tracker.CommitFinalStates(d3dcmd->StateTracker());
            if (g_engine_config._enable_pix)
                PIXEndEvent(m_commandQueue.Get());
        }
        auto submit_end_time = std::chrono::high_resolution_clock::now();
        auto &stats = d3d_cmds.front()->RecordingContext().RenderingStatesData();
        stats.CommandListCount += static_cast<u32>(native_cmds.size());
        ++stats.CommandSubmitCount;
        ++stats.CommandFenceSignalCount;
        stats.CommandSubmissionTimeMs += std::chrono::duration<f32, std::milli>(submit_end_time - submit_begin_time).count();
        {
            PROFILE_BLOCK_CPU("PostExecuteCommandLists")
            for (D3DCommandBuffer *d3dcmd: d3d_cmds)
            {
                d3dcmd->MarkSubmitted(submitted_fence);
                d3dcmd->Finalize();
                d3dcmd->RunPostSubmitCallbacks(submitted_fence);
            }
            GpuResourceRegistry::Get().Collect(_p_cmd_buffer_fence->GetCompletedValue());
        }
        for (auto &queue_state_cmd: queue_state_cmds)
        {
            auto *queue_state_d3dcmd = static_cast<D3DCommandBuffer *>(queue_state_cmd.get());
            queue_state_d3dcmd->MarkSubmitted(submitted_fence);
            queue_state_d3dcmd->Finalize();
            RHICommandBufferPool::Release(queue_state_cmd);
        }
        return submitted_fence;
    }

    void D3DContext::Destroy()
    {
        if (Application::Get()._is_multi_thread_rendering.load()) _cmd_worker->Stop();

        // Ensure that the GPU is no longer referencing resources that are about to be
        // cleaned up by the destructor.
        WaitForGpu();
        GpuResourceRegistry::Get().Collect(_p_cmd_buffer_fence->GetCompletedValue());
        GpuResourceRegistry::Get().Clear();

        // #if defined(TRACY_ENABLE)
        //         if (_tracy_d3d12_ctx)
        //         {
        //             TracyD3D12Destroy(_tracy_d3d12_ctx);
        //             _tracy_d3d12_ctx = nullptr;
        //         }
        // #endif
        if (_p_cmd_buffer_fence_event != nullptr)
        {
            CloseHandle(_p_cmd_buffer_fence_event);
            _p_cmd_buffer_fence_event = nullptr;
        }
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

    bool D3DContext::IsHardwareRayTracingSupported() const { return _is_hardware_ray_tracing_supported; }

    void D3DContext::LoadPipeline()
    {
        UINT dxgiFactoryFlags = 0;
        if (g_engine_config._enable_d3d12_debug_layer)
        {
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
#if defined(CLSID_D3D12DeviceConfiguration)
            ComPtr<ID3D12DeviceConfiguration> config;
            if (SUCCEEDED(D3D12GetInterface(CLSID_D3D12DeviceConfiguration, IID_PPV_ARGS(&config))) && config)
            {
                config->SetEnabledExperimentalFeatures(1, &D3D12ExperimentalShaderModels, nullptr, nullptr);
            }
#endif
        }

        ComPtr<IDXGIFactory6> factory;
        ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));
        GetHardwareAdapter(factory.Get(), _p_adapter.GetAddressOf(), &_local_video_memory_info, &_non_local_video_memory_info);
        ThrowIfFailed(D3D12CreateDevice(_p_adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)));
        _is_hardware_ray_tracing_supported = IsDirectXRaytracingSupported(_p_adapter.Get());
        LOG_INFO(" DirectX Raytracing Supported: {}", _is_hardware_ray_tracing_supported ? "Yes" : "No");

        ComPtr<ID3D12InfoQueue> infoQueue;
        if (SUCCEEDED(m_device->QueryInterface(IID_PPV_ARGS(&infoQueue))))
        {
            // Kept alive for the per-frame drain in Present(); null when the debug layer is disabled.
            s_debug_message_queue = infoQueue;
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

        // #if defined(TRACY_ENABLE)
        //     _tracy_d3d12_ctx = TracyD3D12Context(m_device.Get(), m_commandQueue.Get());
        //     if (_tracy_d3d12_ctx)
        //     {
        //         static const char kQueueName[] = "D3D12 Graphics Queue";
        //         TracyD3D12ContextName(_tracy_d3d12_ctx, kQueueName, (uint16_t) (sizeof(kQueueName) - 1));
        //     }
        // #endif

        //m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
        //m_frameIndex = _swapchain->GetCurrentBackBufferIndex();

        // Create a RTV for each frame.
        //for (UINT n = 0; n < RenderConstants::kFrameCount; n++)
        //{
        //    _fence_value[n] = 0;
        //}

        {
            ThrowIfFailed(
                    CommandSignatureHelper::CreateDispatchCommandSignature(m_device.Get(), nullptr, _dispatch_cmd_sig.GetAddressOf()));
            ThrowIfFailed(CommandSignatureHelper::CreateDrawCommandSignature(m_device.Get(), nullptr, _draw_cmd_sig.GetAddressOf()));
            ThrowIfFailed(CommandSignatureHelper::CreateDrawIndexedCommandSignature(m_device.Get(), nullptr,
                                                                                    _draw_indexed_cmd_sig.GetAddressOf()));
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
        D2D1_BITMAP_PROPERTIES1 bitmapProperties =
                D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                                        D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED), dpiX, dpiY);
        for (UINT n = 0; n < RenderConstants::kFrameCount; n++)
        {
            // Create a wrapped 11On12 resource of this back buffer. Since we are
            // rendering all D3D12 content first and then all D2D content, we specify
            // the In resource state as RENDER_TARGET - because D3D12 will have last
            // used it in this state - and the Out resource state as PRESENT. When
            // ReleaseWrappedResources() is called on the 11On12 device, the resource
            // will be transitioned to the PRESENT state.
            D3D11_RESOURCE_FLAGS d3d11Flags = {D3D11_BIND_RENDER_TARGET};
            ThrowIfFailed(m_d3d11On12Device->CreateWrappedResource(_color_buffer[n].Get(), &d3d11Flags, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                                   D3D12_RESOURCE_STATE_PRESENT, IID_PPV_ARGS(&m_wrappedBackBuffers[n])));

            // Create a render target for D2D to draw directly to this back buffer.
            ComPtr<IDXGISurface> surface;
            ThrowIfFailed(m_wrappedBackBuffers[n].As(&surface));
            ThrowIfFailed(m_d2dDeviceContext->CreateBitmapFromDxgiSurface(surface.Get(), &bitmapProperties, &m_d2dRenderTargets[n]));
        }
#endif
    }


    void D3DContext::LoadAssets()
    {
#ifdef _DIRECT_WRITE
        // Create D2D/DWrite objects for rendering text.
        {
            ThrowIfFailed(m_d2dDeviceContext->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &m_textBrush));
            ThrowIfFailed(m_dWriteFactory->CreateTextFormat(L"Verdana", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                                            DWRITE_FONT_STRETCH_NORMAL, 50, L"en-us", &m_textFormat));
            ThrowIfFailed(m_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER));
            ThrowIfFailed(m_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER));
        }
#endif

        // Create synchronization objects.
        {
            ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(_p_cmd_buffer_fence.GetAddressOf())));
            _p_cmd_buffer_fence->SetName(L"ALD3DFence");
            _p_cmd_buffer_fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            if (_p_cmd_buffer_fence_event == nullptr) { ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError())); }
            //WaitForGpu();
        }
    }

    void D3DContext::Present()
    {
        Vector<GfxCommand *> cmds{CommandPool::Get().Alloc<CommandPresent>()};
        _cmd_worker->Push(std::move(cmds), SubmitParams{"Present", true});
        if (!Application::Get()._is_multi_thread_rendering.load()) { _cmd_worker->RunSync(); }
        // Frameboundary drain: the debug layer store is drained here so a state error cannot hide until exit,
        // and so an undrained store cannot grow without bound across the session.
        DrainD3DDebugLayerMessages();
    }

    void D3DContext::SetMultiThreadRendering(bool enabled)
    {
        if (enabled)
            _cmd_worker->Start();
        else
            _cmd_worker->Stop();
    }

    u64 D3DContext::GetFenceValueGPU()
    {
        _cur_fence_value = _p_cmd_buffer_fence->GetCompletedValue();
        return _cur_fence_value;
    }

    u64 D3DContext::GetFenceValueCPU() const { return _fence_value; }

    void D3DContext::RegisterWindow(Window *window)
    {
        std::lock_guard<std::mutex> lock(_render_windows_mtx);
        UINT dxgiFactoryFlags = 0;

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
        swapChainDesc.Format = RenderConstants::kColorRange == EColorRange::kLDR ? ConvertToDXGIFormat(RenderConstants::kLDRFormat)
                                                                                 : ConvertToDXGIFormat(RenderConstants::kHDRFormat);
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
            initializer._format =
                    RenderConstants::kColorRange == EColorRange::kLDR ? RenderConstants::kLDRFormat : RenderConstants::kHDRFormat;
            initializer._is_fullscreen = false;
            Vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvs(RenderConstants::kFrameCount);
            for (u16 i = 0; i < RenderConstants::kFrameCount; i++) rtvs[i] = ctx->_rtv_allocation.At(i);
            initializer._rtvs = rtvs;
            initializer._swapchain_desc = swapChainDesc;
            initializer._window = window;
            ctx->_swapchain = AdoptGpuResource(new D3DSwapchainTexture(initializer));
            ctx->_swapchain_format = ConvertToDXGIFormat(initializer._format);
        }
        if (g_engine_config._enable_pix)
        {
            PIXSetTargetWindow(hwnd);
            PIXSetHUDOptions(PIXHUDOptions::PIX_HUD_SHOW_ON_NO_WINDOWS);
        }
        ctx->_frame_index = ctx->_swapchain->GetCurrentBackBufferIndex();
        ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(ctx->_fence.GetAddressOf())));
        ctx->_fence->SetName(std::format(L"{}_fence", ctx->_window->GetTitle()).c_str());
        ctx->_fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        //ctx->_swapchain->Name(ToChar(ctx->_window->GetTitle()));
        _render_windows.emplace_back(std::move(ctx));
    }

    void D3DContext::UnRegisterWindow(Window *window)
    {
        std::lock_guard<std::mutex> lock(_render_windows_mtx);
        std::erase_if(_render_windows, [&](Scope<RenderWindowCtx> &ctx) -> bool { return ctx->_window == window; });
    }

    void D3DContext::TakeCapture()
    {
        TakePixCapture();
    }

    void D3DContext::TakePixCapture()
    {
        if (!g_engine_config._enable_pix)
        {
            LOG_WARNING("PIX capture is disabled.");
            return;
        }
        if (_is_renderdoc_capture_pending || (s_rdc_api != nullptr && s_rdc_api->IsFrameCapturing()))
        {
            LOG_WARNING("RenderDoc capture is active or pending; PIX capture was ignored.");
            return;
        }
        if (_is_pix_capture_pending || _is_pix_frame_capturing)
        {
            LOG_WARNING("PIX capture is already active or pending.");
            return;
        }
        _is_pix_capture_pending = true;
    }

    void D3DContext::TakeRenderDocCapture()
    {
        if (s_rdc_api == nullptr && !RdcLoadLatestRdcGpuCapturerLibrary())
        {
            LOG_WARNING("RenderDoc is not available. Launch the Editor through RenderDoc or install RenderDoc first.");
            return;
        }
        if (_is_pix_capture_pending || _is_pix_frame_capturing)
        {
            LOG_WARNING("PIX capture is active or pending; RenderDoc capture was ignored.");
            return;
        }
        if (_is_renderdoc_capture_pending || s_rdc_api->IsFrameCapturing())
        {
            LOG_WARNING("RenderDoc capture is already active or pending.");
            return;
        }

        auto *window = Application::FocusedWindow();
        if (window == nullptr || window->GetNativeWindowPtr() == nullptr)
        {
            LOG_WARNING("RenderDoc capture requires a focused window.");
            return;
        }

        s_rdc_api->SetActiveWindow(reinterpret_cast<RENDERDOC_DevicePointer>(m_device.Get()),
                                   reinterpret_cast<RENDERDOC_WindowHandle>(window->GetNativeWindowPtr()));
        _renderdoc_capture_count_before = s_rdc_api->GetNumCaptures();
        s_rdc_api->TriggerCapture();
        _is_renderdoc_capture_pending = true;
        LOG_INFO("RenderDoc capture requested for the next frame.");
    }

    void D3DContext::ResizeSwapChain(void *window_handle, const u32 width, const u32 height)
    {
        std::lock_guard<std::mutex> lock(_render_windows_mtx);
        auto it = std::find_if(_render_windows.begin(), _render_windows.end(),
                               [&](Scope<RenderWindowCtx> &ctx) -> bool { return ctx->_window->GetNativeWindowPtr() == window_handle; });
        if (it != _render_windows.end())
        {
            u32 new_size = (static_cast<u32>(width) << 16) | static_cast<u32>(height & 0xFFFF);
            it->get()->_new_backbuffer_size.store(new_size);
        }
    }


    void D3DContext::WaitForGpu()
    {
        std::lock_guard submit_lock(_command_submit_mtx);
        std::lock_guard lock(_cmd_fence_mtx);
        ++_fence_value;
        ThrowIfFailed(m_commandQueue->Signal(_p_cmd_buffer_fence.Get(), _fence_value));
        if (_p_cmd_buffer_fence->GetCompletedValue() < _fence_value)
        {
            ThrowIfFailed(_p_cmd_buffer_fence->SetEventOnCompletion(_fence_value, _p_cmd_buffer_fence_event));
            WaitForSingleObjectEx(_p_cmd_buffer_fence_event, INFINITE, FALSE);
        }
    }

    void D3DContext::WaitForFence(u64 fence_value)
    {
        std::lock_guard lock(_cmd_fence_mtx);
        if (_p_cmd_buffer_fence->GetCompletedValue() < fence_value)
        {
            ThrowIfFailed(_p_cmd_buffer_fence->SetEventOnCompletion(fence_value, _p_cmd_buffer_fence_event));
            WaitForSingleObjectEx(_p_cmd_buffer_fence_event, INFINITE, FALSE);
        }
    }

    void D3DContext::ApplyPendingSwapChainResizes()
    {
        // 必须在帧边界（上一帧命令缓冲全部归还到池中、且当前没有任何命令缓冲在录制）执行：
        // ResizeBuffers 要求后台缓冲不再被任何对象引用，而命令缓冲的状态跟踪器会持有
        // 后台缓冲的强引用直到该缓冲归还到池中。若在 PresentImpl 里 resize，正在录制的那条
        // 命令缓冲（PresentImpl 的 cmd）仍然持有引用，ResizeBuffers 会返回 DXGI_ERROR_INVALID_CALL。
        std::lock_guard<std::mutex> lock(_render_windows_mtx);
        for (auto &ctx: _render_windows)
        {
            const u32 new_pack_size = ctx->_new_backbuffer_size.load();
            if (new_pack_size == 0u)
                continue;
            // 先清空，避免 resize 期间窗口再次触发事件时把新尺寸覆盖掉
            ctx->_new_backbuffer_size.store(0u);
            const u32 new_width = (new_pack_size >> 16) & 0xFFFF;
            const u32 new_height = new_pack_size & 0xFFFF;
            ctx->WaitForGpu(m_commandQueue.Get());
            ctx->_swapchain->Resize((u16) new_width, (u16) new_height);
            ctx->WaitForGpu(m_commandQueue.Get());
        }
    }

    void D3DContext::PresentImpl(D3DCommandBuffer *cmd)
    {
        static TimeMgr s_timer;
        PROFILE_BLOCK_CPU("Reslove")
        {
            std::lock_guard<std::mutex> lock(_render_windows_mtx);
#ifdef DEAR_IMGUI
            auto rtv_handle = *_render_windows[0]->_swapchain->TargetCPUHandle(cmd);
            cmd->SetRenderTargets(1u, &rtv_handle, nullptr);
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
                {
                    PROFILE_BLOCK_CPU("Present")
                    ctx->_swapchain->Present();
                }
                {
                    s_timer.MarkLocal();
                    //WaitForGpu();
                    ctx->MoveToNextFrame(m_commandQueue.Get());
                    //if (Application::Get().GetFrameCount() % 60 == 0)
                    Render::RenderingStates::RenderData().GpuLatency = s_timer.GetElapsedSinceLastLocalMark();
                }
                if (_is_pix_frame_capturing) { EndPixCapture(); }
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
            m_d2dDeviceContext->DrawText(text, _countof(text) - 1, m_textFormat.Get(), &textRect, m_textBrush.Get());
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
                if (it != _global_tracked_resource.end()) { _global_tracked_resource.erase(_global_tracked_resource.begin(), it); }
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

        // #if defined(TRACY_ENABLE)
        //         if (_tracy_d3d12_ctx)
        //         {
        //             TracyD3D12NewFrame(_tracy_d3d12_ctx);
        //             TracyD3D12Collect(_tracy_d3d12_ctx);
        //         }
        // #endif
        //CommandBufferPool::ReleaseAll();
        if (_is_pix_capture_pending)
        {
            _is_pix_capture_pending = false;
            _is_pix_frame_capturing = BeginPixCapture();
        }
        TryOpenRenderDocCapture();
        //if (Application::Get().GetFrameCount() % 60 == 0)
        {
            auto& rd = Render::RenderingStates::RenderData();
            rd.FrameTime = s_timer.GetElapsedSinceLastLocalMark();
            rd.FrameRate = 1000.0f / rd.FrameTime;
        }
        s_timer.MarkLocal();
    }
    bool D3DContext::BeginPixCapture()
    {
        if (!g_engine_config._enable_pix)
        {
            LOG_WARNING("PIX capture is disabled.");
            return false;
        }

        LOG_WARNING("Begin PIX capture...");
        static PIXCaptureParameters s_params{};
        auto *window = Application::FocusedWindow();
        if (window == nullptr || window->GetNativeWindowPtr() == nullptr)
        {
            LOG_WARNING("PIX capture requires a focused window.");
            return false;
        }
        PIXSetTargetWindow((HWND) window->GetNativeWindowPtr());
        _pix_capture_name = std::format(L"{}_{}{}", L"NewCapture", ToWChar(TimeMgr::CurrentTime("%Y-%m-%d_%H%M%S")), L".wpix");
        s_params.GpuCaptureParameters.FileName = _pix_capture_name.data();
        HRESULT result = PIXBeginCapture(PIX_CAPTURE_GPU, &s_params);
        if (FAILED(result))
        {
            LOG_ERROR("PIXBeginCapture failed: {}", result);
            return false;
        }
        return true;
    }

    void D3DContext::EndPixCapture()
    {
        if (g_engine_config._enable_pix)
        {
            HRESULT result = PIXEndCapture(false);
            _is_pix_frame_capturing = false;
            if (FAILED(result) && result != E_PENDING)
            {
                LOG_ERROR("PIXEndCapture failed: {}", result);
                return;
            }
            PIXOpenCaptureInUI(_pix_capture_name.data());
        }
    }

    void D3DContext::TryOpenRenderDocCapture()
    {
        if (!_is_renderdoc_capture_pending || s_rdc_api == nullptr)
            return;

        const u32 capture_count = s_rdc_api->GetNumCaptures();
        if (capture_count <= _renderdoc_capture_count_before)
            return;

        const u32 capture_index = capture_count - 1u;
        u32 path_length = 0u;
        if (s_rdc_api->GetCapture(capture_index, nullptr, &path_length, nullptr) == 0u || path_length == 0u)
        {
            LOG_ERROR("RenderDoc capture completed, but its file path could not be queried.");
            _is_renderdoc_capture_pending = false;
            return;
        }

        Vector<char> capture_path(path_length, '\0');
        if (s_rdc_api->GetCapture(capture_index, capture_path.data(), &path_length, nullptr) == 0u)
        {
            LOG_ERROR("RenderDoc capture completed, but its file path could not be read.");
            _is_renderdoc_capture_pending = false;
            return;
        }

        if (s_rdc_api->IsTargetControlConnected())
        {
            s_rdc_api->ShowReplayUI();
        }
        else
        {
            const u32 replay_pid = s_rdc_api->LaunchReplayUI(1u, capture_path.data());
            if (replay_pid == 0u)
                LOG_WARNING("RenderDoc capture saved, but its Replay UI could not be launched.");
        }
        _is_renderdoc_capture_pending = false;
    }

    void D3DContext::ResizeSwapChainImpl(const u32 width, const u32 height)
    {
        if (width == _render_windows[_cur_ctx_index]->_width && height == _render_windows[_cur_ctx_index]->_height) return;
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
        D2D1_BITMAP_PROPERTIES1 bitmapProperties =
                D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                                        D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED), dpiX, dpiY);

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
            ThrowIfFailed(m_d3d11On12Device->CreateWrappedResource(_color_buffer[n].Get(), &d3d11Flags, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                                   D3D12_RESOURCE_STATE_PRESENT, IID_PPV_ARGS(&m_wrappedBackBuffers[n])));
            // Create a render target for D2D to draw directly to this back buffer.
            ComPtr<IDXGISurface> surface;
            ThrowIfFailed(m_wrappedBackBuffers[n].As(&surface));
            ThrowIfFailed(m_d2dDeviceContext->CreateBitmapFromDxgiSurface(surface.Get(), &bitmapProperties, &m_d2dRenderTargets[n]));
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
                                            reinterpret_cast<IUnknown **>(m_commandQueue.GetAddressOf()), 1U, 0U, &d3d11Device,
                                            &m_d3d11DeviceContext, nullptr));
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
            size = AlignTo(size,256);
            auto copy_dst = _readback_pool->Acquire(size, _frame_count);
            auto cmd = RHICommandBufferPool::Get("Readback");
            auto d3dcmd = static_cast<D3DCommandBuffer *>(cmd.get());
            auto dxcmd = d3dcmd->NativeCmdList();
            res->RequireState(cmd.get(), EResourceState::kCopySource);
            dxcmd->CopyResource(copy_dst.Get(), res->NativeResource().As<ID3D12Resource>());
            ExecuteRHICommandBuffer(cmd.get());
            u64 cmd_fence_value = d3dcmd->_fence_value;
            while (_p_cmd_buffer_fence->GetCompletedValue() < cmd_fence_value) { std::this_thread::yield(); }
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
    void D3DContext::ReadBack(const D3DResource &resource, u8 *data, u32 size)
    {
        size = AlignTo(size,256);
        auto copy_dst = _readback_pool->Acquire(size, _frame_count);
        auto cmd = RHICommandBufferPool::Get("Readback");
        auto d3dcmd = static_cast<D3DCommandBuffer *>(cmd.get());
        auto dxcmd = d3dcmd->NativeCmdList();
        d3dcmd->RequireState(resource, EResourceState::kCopySource);
        dxcmd->CopyResource(copy_dst.Get(), resource.Get());
        ExecuteRHICommandBuffer(cmd.get());
        u64 cmd_fence_value = d3dcmd->_fence_value;
        while (_p_cmd_buffer_fence->GetCompletedValue() < cmd_fence_value) { std::this_thread::yield(); }
        D3D12_RANGE readbackBufferRange{0, size};
        u8 *tmp_data = nullptr;
        copy_dst->Map(0, &readbackBufferRange, reinterpret_cast<void **>(&tmp_data));
        D3D12_RANGE emptyRange{0, 0};
        copy_dst->Unmap(0, &emptyRange);
        memcpy(data, tmp_data, size);
        RHICommandBufferPool::Release(cmd);
    }

    void D3DContext::ReadBackAsync(const D3DResource &resource, u32 size,
                                   std::function<void(const u8 *)> callback)
    {
        auto copy_dst = _readback_pool->Acquire(size, _frame_count);// 已对齐分配
        auto cmd = RHICommandBufferPool::Get("Readback");
        auto d3dcmd = static_cast<D3DCommandBuffer *>(cmd.get());
        auto dxcmd = d3dcmd->NativeCmdList();

        d3dcmd->RequireState(resource, EResourceState::kCopySource);
        dxcmd->CopyBufferRegion(copy_dst.Get(), 0, resource.Get(), 0, size);// 替换 CopyResource

        ExecuteRHICommandBuffer(cmd.get());
        u64 fence_value = static_cast<D3DCommandBuffer *>(cmd.get())->_fence_value;

        auto copy_dst_capture = copy_dst;// 确保 lambda 生命周期
        JobSystem::Get().Dispatch(
                [this, copy_dst_capture, size, fence_value, callback]()
                {
                    while (_p_cmd_buffer_fence->GetCompletedValue() < fence_value) std::this_thread::yield();
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
            res->RequireState(cmd.get(), EResourceState::kCopySource);
            dxcmd->CopyResource(copy_dst.Get(), res->NativeResource().As<ID3D12Resource>());
            ExecuteRHICommandBuffer(cmd.get());
            u64 cmd_fence_value = static_cast<D3DCommandBuffer *>(cmd.get())->_fence_value;
            JobSystem::Get().Dispatch(
                    [&]() mutable
                    {
                        while (_p_cmd_buffer_fence->GetCompletedValue() < cmd_fence_value) { std::this_thread::yield(); }
                        D3D12_RANGE readbackBufferRange{0, size};
                        u8 *data = nullptr;
                        copy_dst->Map(0, &readbackBufferRange, reinterpret_cast<void **>(&data));
                        D3D12_RANGE emptyRange{0, 0};
                        copy_dst->Unmap(0, &emptyRange);
                        callback(data);
                    });
            RHICommandBufferPool::Release(cmd);
        }
        else
        {
            LOG_ERROR("Readback only support buffer resource!");
        }
    }

    void D3DContext::CreateResource(GpuResource *res) { CreateResource(res, nullptr); }

    void D3DContext::CreateResourceSync(GpuResource *res) { CreateResourceSync(res, nullptr); }

    void D3DContext::CreateResource(GpuResource *res, UploadParams *params)
    {
        if (res->GetResourceType() == EGpuResType::kGraphicsPSO || res->GetResourceType() == EGpuResType::kRenderTexture)
        {
            res->Upload(this, nullptr, params);
            AL_DELETE(params);
            res->SetCreatedFence(0u);
        }
        else
        {
            auto cmd = CommandPool::Get().Alloc<CommandGpuResourceUpload>();
            cmd->_res = res != nullptr ? res->Handle() : GpuResourceHandle{};
            cmd->_params = params;
            Vector<GfxCommand *> cmds = {cmd};
            _cmd_worker->Push(std::move(cmds), SubmitParams{"ResourceCreate: " + res->Name()});
        }
    }

    void D3DContext::CreateResourceSync(GpuResource *res, UploadParams *params)
    {
        if (res->GetResourceType() == EGpuResType::kGraphicsPSO || res->GetResourceType() == EGpuResType::kRenderTexture)
        {
            res->Upload(this, nullptr, params);
            AL_DELETE(params);
            res->SetCreatedFence(0u);
            return;
        }

        auto cmd = CommandPool::Get().Alloc<CommandGpuResourceUpload>();
        cmd->_res = res != nullptr ? res->Handle() : GpuResourceHandle{};
        cmd->_params = params;
        SubmitGpuCommandSync(cmd);
    }

    void D3DContext::ProcessGpuCommand(GfxCommand *cmd, RHICommandBuffer *cmd_buffer)
    {
        D3DCommandBuffer *d3dcmd = static_cast<D3DCommandBuffer *>(cmd_buffer);
        auto dxcmd = d3dcmd->NativeCmdList();
        if (cmd->GetCmdType() == EGpuCommandType::kAllocConstBuffer)
        {
            auto alloc_cmd = static_cast<CommandAllocConstBuffer *>(cmd);
            // 数据已在录制阶段上传到帧上传缓冲区，这里只需登记GPU句柄供compute dispatch路径使用
            UploadBuffer::Allocation alloc;
            alloc.GPU = alloc_cmd->_gpu_handle;
            alloc._size = alloc_cmd->_size;
            d3dcmd->_allocations[alloc_cmd->_name] = alloc;
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
                if (clear_cmd->_flag & EClearFlag::kStencil) depth_flag |= D3D12_CLEAR_FLAG_STENCIL;
                dxcmd->ClearDepthStencilView(*d3dcmd->_depth, depth_flag, clear_cmd->_depth, clear_cmd->_stencil, 0, nullptr);
            }
        }
        else if (cmd->GetCmdType() == EGpuCommandType::kSetTarget)
        {
            auto set_cmd = static_cast<CommandSetTarget *>(cmd);
            auto *depth_target = static_cast<RenderTexture *>(GpuResourceRegistry::Get().Resolve(set_cmd->_depth_target));
            d3dcmd->ResetRenderTarget();
            if (set_cmd->_color_target_num == 0)
            {
                if (depth_target != nullptr)
                {
                    cmd_buffer->RecordingContext().SetRenderTargetState(EALGFormat::kALGFormatUNKOWN,
                                                                        depth_target->PixelFormat(), 0);
                    auto drt = static_cast<D3DRenderTexture *>(depth_target);
                    d3dcmd->SetActiveRenderTarget(depth_target);
                    d3dcmd->_color_count = 0u;
                    d3dcmd->_depth = drt->TargetCPUHandle(d3dcmd, set_cmd->_depth_index);
                    d3dcmd->_scissors[0] = D3DConvertUtils::ToD3DRect(set_cmd->_viewports[0]);
                    d3dcmd->_viewports[0] = D3DConvertUtils::ToD3DViewport(set_cmd->_viewports[0]);
                    d3dcmd->SetRenderTargets(0u, nullptr, d3dcmd->_depth);
                    d3dcmd->SetScissors(1u, d3dcmd->_scissors.data());
                    d3dcmd->SetViewports(1u, d3dcmd->_viewports.data());
                }
            }
            else
            {
                d3dcmd->_color_count = set_cmd->_color_target_num;
                bool is_depth_valid = depth_target != nullptr;
                d3dcmd->_depth =
                        is_depth_valid
                                ? static_cast<D3DRenderTexture *>(depth_target)->TargetCPUHandle(d3dcmd, set_cmd->_depth_index)
                                : nullptr;
                D3D12_CPU_DESCRIPTOR_HANDLE handles[RenderConstants::kMaxMRTNum]{};
                for (u16 i = 0; i < d3dcmd->_color_count; ++i)
                {
                    auto *color_target = static_cast<RenderTexture *>(GpuResourceRegistry::Get().Resolve(set_cmd->_color_target[i]));
                    if (color_target == nullptr)
                        return;
                    d3dcmd->SetActiveRenderTarget(color_target);
                    d3dcmd->_scissors[i] = D3DConvertUtils::ToD3DRect(set_cmd->_viewports[i]);
                    d3dcmd->_viewports[i] = D3DConvertUtils::ToD3DViewport(set_cmd->_viewports[i]);
                    EALGFormat color_format = color_target->PixelFormat();
                    cmd_buffer->RecordingContext().SetRenderTargetState(
                            color_format, is_depth_valid ? depth_target->PixelFormat() : EALGFormat::kALGFormatUNKOWN,
                            (u8) i);
                    D3D12_CPU_DESCRIPTOR_HANDLE *rtv;
                    if (color_target->IsSwapChain())
                    {
                        auto rt = static_cast<D3DSwapchainTexture *>(color_target);
                        rtv = rt->TargetCPUHandle(d3dcmd);
                    }
                    else
                    {
                        auto rt = static_cast<D3DRenderTexture *>(color_target);
                        rtv = rt->TargetCPUHandle(d3dcmd, set_cmd->_color_indices[i]);
                    }
                    d3dcmd->_colors[i] = rtv;
                    handles[i] = *d3dcmd->_colors[i];
                }
                d3dcmd->SetScissors(set_cmd->_color_target_num, d3dcmd->_scissors.data());
                d3dcmd->SetViewports(set_cmd->_color_target_num, d3dcmd->_viewports.data());
                d3dcmd->SetRenderTargets(d3dcmd->_color_count, handles, d3dcmd->_depth);
            }
        }
        else if (cmd->GetCmdType() == EGpuCommandType::kCustom)
        {
            auto custom_cmd = static_cast<CommandCustom *>(cmd);
            custom_cmd->_func(cmd_buffer);
        }
        else if (cmd->GetCmdType() == EGpuCommandType::kScissorRect)
        {
            auto scissor_cmd = static_cast<CommandScissor *>(cmd);
            D3D12_RECT d3d_rects[RenderConstants::kMaxMRTNum]{};
            for (u32 i = 0; i < scissor_cmd->_num; ++i) { d3d_rects[i] = D3DConvertUtils::ToD3DRect(scissor_cmd->_rects[i]); }
            d3dcmd->SetScissors(scissor_cmd->_num, d3d_rects);
        }
        else if (cmd->GetCmdType() == EGpuCommandType::kResourceUpload)
        {
            auto upload_cmd = static_cast<CommandGpuResourceUpload *>(cmd);
            if (auto *res = GpuResourceRegistry::Get().Resolve(upload_cmd->_res); res != nullptr)
            {
                res->Upload(this, cmd_buffer, upload_cmd->_params);
                res->Name(res->Name());//这里将name写入d3d resource，之前resource一直为空
                res->SetCreatedFence(_fence_value + 1u);
            }
        }
        else if (cmd->GetCmdType() == EGpuCommandType::kRequireResourceState)
        {
            auto state_cmd = static_cast<CommandRequireResourceState *>(cmd);
            if (auto *res = GpuResourceRegistry::Get().Resolve(state_cmd->_res); res != nullptr)
                res->RequireState(cmd_buffer, state_cmd->_state, state_cmd->_sub_res);
        }
        else if (cmd->GetCmdType() == EGpuCommandType::kUavBarrier)
        {
            auto barrier_cmd = static_cast<CommandUavBarrier *>(cmd);
            if (auto *res = GpuResourceRegistry::Get().Resolve(barrier_cmd->_res); res != nullptr)
                res->UavBarrier(cmd_buffer);
            else
                cmd_buffer->UavBarrier();
        }
        else if (cmd->GetCmdType() == EGpuCommandType::kDraw)
        {
            ++d3dcmd->Statistics()._draw_command_count;
            auto draw_cmd = static_cast<CommandDraw *>(cmd);
            const auto &material_state = draw_cmd->_material_draw_state;
            auto *shader = static_cast<Shader *>(GpuResourceRegistry::Get().Resolve(material_state._shader));
            auto *vb = static_cast<VertexBuffer *>(GpuResourceRegistry::Get().Resolve(draw_cmd->_vb));
            auto *ib = static_cast<IndexBuffer *>(GpuResourceRegistry::Get().Resolve(draw_cmd->_ib));
            auto *arg_buffer = static_cast<GPUBuffer *>(GpuResourceRegistry::Get().Resolve(draw_cmd->_arg_buffer));
            AL_ASSERT(shader != nullptr);
            if (draw_cmd->_vb.IsValid() && vb == nullptr)
                LOG_ERROR("[DX12] Draw skipped: vertex buffer handle unresolved ({}, {})", draw_cmd->_vb._index, draw_cmd->_vb._generation);
            if (draw_cmd->_ib.IsValid() && ib == nullptr)
                LOG_ERROR("[DX12] Draw skipped: index buffer handle unresolved ({}, {})", draw_cmd->_ib._index, draw_cmd->_ib._generation);

#if AILU_ENABLE_FRAME_DEBUGGER
            auto *capture_writer = cmd_buffer->CaptureWriter();
            u32 draw_event_id = 0u;
            Render::FrameDebugger::DrawEventCapture draw_cap;
            if (capture_writer)
            {
                draw_cap._pass_index = draw_cmd->_pass_index;
                draw_cap._sub_mesh = draw_cmd->_sub_mesh;
                draw_cap._variant_hash = material_state._variant_hash;
                draw_cap._vertex_count = draw_cmd->_vertex_count;
                draw_cap._index_count = draw_cmd->_index_num;
                draw_cap._index_start = draw_cmd->_index_start;
                draw_cap._instance_count = draw_cmd->_instance_count;
                draw_cap._start_instance = draw_cmd->_start_instance;
                draw_cap._argument_offset = draw_cmd->_arg_offset;
                draw_cap._is_indexed = (ib != nullptr);
                draw_cap._is_indirect = (arg_buffer != nullptr);
                draw_cap._is_procedural = (vb == nullptr);
                draw_cap._material_binding_invalid_reasons = material_state._material_binding_invalid_reasons;
                draw_cap._material_binding_result = material_state._material_binding_result;
            }
#endif

            if (!material_state._is_ready)
            {
                LOG_WARNING("[DX12] Draw skipped: shader not ready material={} shader={} vb_handle=({}, {}) ib_handle=({}, {})",
                            draw_cmd->_material_id, shader, draw_cmd->_vb._index, draw_cmd->_vb._generation,
                            draw_cmd->_ib._index, draw_cmd->_ib._generation);
#if AILU_ENABLE_FRAME_DEBUGGER
                if (capture_writer)
                {
                    auto mat_id = static_cast<Render::FrameDebugger::CaptureObjectId>(draw_cmd->_material_id);
                    auto shader_id = capture_writer->RegisterObject(shader, Render::FrameDebugger::ECaptureObjectType::kShader, 0u);
                    draw_event_id = capture_writer->RecordDrawEvent(draw_cap, mat_id, shader_id);
                    capture_writer->SetEventResult(draw_event_id, Render::FrameDebugger::EFrameEventExecutionResult::kSkippedShaderNotReady);
                }
#endif
                return;
            }
            shader->Bind(cmd_buffer, material_state._pass_index, material_state._variant_hash);
            cmd_buffer->RecordingContext().ConfigureRasterizerState(material_state._raster_state_hash);
            const auto &binding_snapshot = material_state._bindings;
            for (u16 i = 0u; i < binding_snapshot._entry_count; ++i)
            {
                const auto &binding = binding_snapshot._entries[i];
                auto *binding_resource = GpuResourceRegistry::Get().Resolve(binding._resource);
                if (binding_resource == nullptr && binding._resource_type != EBindResDescType::kConstBufferRaw)
                    continue;
                auto resource = PipelineResource(binding_resource, binding._resource_type, binding._slot, binding._priority);
                resource._addi_info = binding._addi_info;
                cmd_buffer->RecordingContext().SubmitBindResource(resource);
            }
            auto pso = cmd_buffer->RecordingContext().FindMatchPSO();
            if (pso == nullptr)
            {
                LOG_WARNING("[DX12] Draw skipped: PSO unavailable material={} vb_handle=({}, {}) ib_handle=({}, {})",
                            draw_cmd->_material_id, draw_cmd->_vb._index, draw_cmd->_vb._generation,
                            draw_cmd->_ib._index, draw_cmd->_ib._generation);
            }
#if AILU_ENABLE_FRAME_DEBUGGER
            if (capture_writer)
            {
                draw_cap._pso_dirty_reasons = (u32)cmd_buffer->RecordingContext().TakePsoDirtyReasons();
                draw_cap._pso_lookup_result = pso == nullptr ? (u8)Render::FrameDebugger::EPsoLookupResult::kCreationRequested :
                    (draw_cap._pso_dirty_reasons == 0u ? (u8)Render::FrameDebugger::EPsoLookupResult::kCacheHit :
                                                        (u8)Render::FrameDebugger::EPsoLookupResult::kCacheMiss);
                draw_cap._pso_bind_reason = pso == nullptr ? (u8)Render::FrameDebugger::EPsoBindReason::kCacheHit :
                    (d3dcmd->IsGraphicsPSOActive(pso) ? (u8)Render::FrameDebugger::EPsoBindReason::kCacheHit :
                                                          (u8)Render::FrameDebugger::EPsoBindReason::kPsoObjectChanged);
                Render::FrameDebugger::PsoLookupCapture pso_lookup;
                pso_lookup._dirty_reasons = draw_cap._pso_dirty_reasons;
                pso_lookup._pso_lookup_result = draw_cap._pso_lookup_result;
                pso_lookup._pso_bind_reason = draw_cap._pso_bind_reason;
                capture_writer->RecordPsoLookup(pso_lookup);

                Render::FrameDebugger::MaterialBindingCapture material_binding;
                material_binding._result = material_state._material_binding_result;
                material_binding._invalid_reasons = material_state._material_binding_invalid_reasons;
                material_binding._material_resource_version = material_state._material_version;
                material_binding._layout_version = material_state._binding_layout_version;
                material_binding._variant_hash = material_state._variant_hash;
                material_binding._global_layout_version = material_state._global_layout_version;
                material_binding._global_binding_version = material_state._global_binding_version;
                capture_writer->RecordMaterialBinding(material_binding);
            }
#endif
            if (pso != nullptr)
            {
#if AILU_ENABLE_FRAME_DEBUGGER
                if (capture_writer)
                {
                    auto mat_id = static_cast<Render::FrameDebugger::CaptureObjectId>(draw_cmd->_material_id);
                    auto shader_id = capture_writer->RegisterObject(shader, Render::FrameDebugger::ECaptureObjectType::kShader,
                                                                     capture_writer->InternString(shader->Name()));
                    auto pso_id = capture_writer->RegisterObject(pso, Render::FrameDebugger::ECaptureObjectType::kGraphicsPso, 0u);
                    draw_cap._pso_id = pso_id;
                    // CommandDraw uses zero as "use the complete bound buffer". Capture the same resolved values
                    // used by the actual draw path; indexed draws still have a meaningful vertex-buffer count.
                    if (draw_cap._vertex_count == 0u && vb)
                        draw_cap._vertex_count = vb->GetVertexCount();
                    if (draw_cap._is_indexed && draw_cap._index_count == 0u)
                        draw_cap._index_count = ib->GetCount();
                    if (vb)
                    {
                        auto vb_id = capture_writer->RegisterObject(vb, Render::FrameDebugger::ECaptureObjectType::kVertexBuffer, 0u);
                        draw_cap._vertex_buffer_id = vb_id;
                    }
                    if (ib)
                    {
                        auto ib_id = capture_writer->RegisterObject(ib, Render::FrameDebugger::ECaptureObjectType::kIndexBuffer, 0u);
                        draw_cap._index_buffer_id = ib_id;
                    }
                    draw_event_id = capture_writer->RecordDrawEvent(draw_cap, mat_id, shader_id);
                    capture_writer->SetCurrentDrawEventId(draw_event_id);
                }
#endif
                AL_ASSERT(shader == pso->StateDescriptor()._p_vertex_shader);
                bool is_indexed_draw = ib != nullptr;
                bool is_instance_draw = draw_cmd->_instance_count > 1;
                BindParams params;
                params._params._vb_binder._layout = &shader->PipelineInputLayout(material_state._pass_index,
                                                                                                      material_state._variant_hash);
                pso->Bind(cmd_buffer, params);
                if (draw_cmd->_is_scene_primitive_draw)
                    dxcmd->SetGraphicsRoot32BitConstants(0u, 4u, &draw_cmd->_primitive_draw_data, 0u);
                bool is_produced = vb == nullptr;
                const void *layout = params._params._vb_binder._layout;
                const u64 vb_view_version = vb == nullptr ? 0u : vb->GetViewVersion();
                if (!is_produced && d3dcmd->IsVertexBufferActive(vb, layout, vb_view_version))
                {
                    ++d3dcmd->Statistics()._vb_bind_cache_hit_count;
#if AILU_ENABLE_FRAME_DEBUGGER
                    if (capture_writer)
                    {
                        Render::FrameDebugger::GeometryBindingCapture geo;
                        geo._vertex_buffer_bound = true;
                        geo._vertex_buffer_reasons = 0u;
                        geo._vertex_buffer_view_version = vb_view_version;
                        geo._input_layout_id = (u64)layout;
                        capture_writer->RecordGeometryBinding(geo);
                    }
#endif
                }
                else if (!is_produced)
                {
                    ++d3dcmd->Statistics()._vb_bind_cache_miss_count;
                    const void *previous_vb = d3dcmd->_active_vb;
                    const void *previous_vb_layout = d3dcmd->_active_vb_layout;
                    const u64 previous_vb_view_version = d3dcmd->_active_vb_view_version;
                    vb->Bind(d3dcmd, params);
#if AILU_ENABLE_FRAME_DEBUGGER
                    if (capture_writer)
                    {
                        u32 vb_reasons = 0u;
                        if (previous_vb == nullptr)
                            vb_reasons |= (u32) Render::FrameDebugger::EGeometryBindingInvalidReason::kFirstBind;
                        else
                        {
                            if (previous_vb != vb)
                                vb_reasons |= (u32) Render::FrameDebugger::EGeometryBindingInvalidReason::kBufferChanged;
                            if (previous_vb_layout != layout)
                                vb_reasons |= (u32) Render::FrameDebugger::EGeometryBindingInvalidReason::kInputLayoutChanged;
                            if (previous_vb_view_version != vb_view_version)
                                vb_reasons |= (u32) Render::FrameDebugger::EGeometryBindingInvalidReason::kViewVersionChanged;
                        }
                        Render::FrameDebugger::GeometryBindingCapture geo;
                        geo._vertex_buffer_bound = true;
                        geo._vertex_buffer_reasons = vb_reasons;
                        geo._vertex_buffer_view_version = vb_view_version;
                        geo._input_layout_id = (u64) layout;
                        capture_writer->RecordGeometryBinding(geo);
                    }
#endif
                    d3dcmd->SetVertexBufferActive(vb, layout, vb_view_version);
                }
                const u64 ib_view_version = ib == nullptr ? 0u : ib->GetViewVersion();
                if (is_indexed_draw && (!d3dcmd->IsIndexBufferActive(ib, ib_view_version)))
                {
                    const void *previous_ib = d3dcmd->_active_ib;
                    const u64 previous_ib_view_version = d3dcmd->_active_ib_view_version;
                    ib->Bind(d3dcmd, params);
#if AILU_ENABLE_FRAME_DEBUGGER
                    if (capture_writer)
                    {
                        u32 ib_reasons = 0u;
                        if (previous_ib == nullptr)
                            ib_reasons |= (u32) Render::FrameDebugger::EGeometryBindingInvalidReason::kFirstBind;
                        else
                        {
                            if (previous_ib != ib)
                                ib_reasons |= (u32) Render::FrameDebugger::EGeometryBindingInvalidReason::kBufferChanged;
                            if (previous_ib_view_version != ib_view_version)
                                ib_reasons |= (u32) Render::FrameDebugger::EGeometryBindingInvalidReason::kViewVersionChanged;
                        }
                        Render::FrameDebugger::GeometryBindingCapture geo;
                        geo._index_buffer_bound = true;
                        geo._index_buffer_reasons = ib_reasons;
                        geo._index_buffer_view_version = ib_view_version;
                        capture_writer->RecordGeometryBinding(geo);
                    }
#endif
                    d3dcmd->SetIndexBufferActive(ib, ib_view_version);
                }
                ++d3dcmd->Statistics()._draw_call;
                u32 vertex_count = is_produced ? 3u : vb->GetVertexCount() * draw_cmd->_instance_count;//目前只有程序化矩形
                vertex_count = draw_cmd->_vertex_count > 0 ? draw_cmd->_vertex_count : vertex_count;
                u32 triangle_count = is_indexed_draw ? ib->GetCount() / 3
                                     : vb ? vb->GetVertexCount() / 3
                                                     : 0u;
                triangle_count *= draw_cmd->_instance_count;
                d3dcmd->Statistics()._triangle_num += triangle_count;
                d3dcmd->Statistics()._vertex_num += vertex_count;
                if (arg_buffer)
                {
                    D3DGPUBuffer *d3d_buf = static_cast<D3DGPUBuffer *>(arg_buffer);
                    d3d_buf->RequireState(cmd_buffer, EResourceState::kIndirectArgument);
                    dxcmd->ExecuteIndirect(is_indexed_draw ? _draw_indexed_cmd_sig.Get() : _draw_cmd_sig.Get(), 1u,
                                           d3d_buf->NativeResource().As<ID3D12Resource>(), draw_cmd->_arg_offset,
                                           d3d_buf->GetCounterBuffer(), 0u);
                }
                else
                {
                    if (is_indexed_draw)
                    {
                        const u32 index_count = draw_cmd->_index_num == 0u ? ib->GetCount() : draw_cmd->_index_num;
                        dxcmd->DrawIndexedInstanced(index_count, draw_cmd->_instance_count, draw_cmd->_index_start, 0,
                                                   draw_cmd->_start_instance);
                    }
                    else
                        dxcmd->DrawInstanced(vb ? vb->GetVertexCount() : draw_cmd->_vertex_count,
                                             draw_cmd->_instance_count, 0, 0);
                }
#if AILU_ENABLE_FRAME_DEBUGGER
                if (capture_writer)
                {
                    capture_writer->FinalizeLastDrawBindingRange();
                    capture_writer->SetEventResult(draw_event_id, Render::FrameDebugger::EFrameEventExecutionResult::kExecuted);
                    capture_writer->SetCurrentDrawEventId(Render::FrameDebugger::kInvalidFrameEventId);
                }
#endif
            }
#if AILU_ENABLE_FRAME_DEBUGGER
            else if (capture_writer)
            {
                auto mat_id = static_cast<Render::FrameDebugger::CaptureObjectId>(draw_cmd->_material_id);
                auto shader_id = capture_writer->RegisterObject(shader, Render::FrameDebugger::ECaptureObjectType::kShader, 0u);
                draw_event_id = capture_writer->RecordDrawEvent(draw_cap, mat_id, shader_id);
                capture_writer->SetEventResult(draw_event_id, Render::FrameDebugger::EFrameEventExecutionResult::kSkippedPsoNotReady);
            }
#endif
            cmd_buffer->RecordingContext().ClearResolvedBindResources();
        }
        else if (cmd->GetCmdType() == EGpuCommandType::kCopyCounter)
        {
            auto cmd_cpc = static_cast<CommandCopyCounter *>(cmd);
            D3DGPUBuffer *src_d3d_buf = static_cast<D3DGPUBuffer *>(GpuResourceRegistry::Get().Resolve(cmd_cpc->_src));
            D3DGPUBuffer *dst_d3d_buf = static_cast<D3DGPUBuffer *>(GpuResourceRegistry::Get().Resolve(cmd_cpc->_dst));
            if (src_d3d_buf == nullptr || dst_d3d_buf == nullptr)
                return;
            d3dcmd->RequireState(src_d3d_buf->CounterResource(), EResourceState::kCopySource);
            d3dcmd->RequireState(dst_d3d_buf->Resource(), EResourceState::kCopyDest);
            dxcmd->CopyBufferRegion(dst_d3d_buf->Resource().Get(), cmd_cpc->_dst_offset,
                                    src_d3d_buf->CounterResource().Get(), 0u, sizeof(u32));
        }
        else if (cmd->GetCmdType() == EGpuCommandType::kDispatch)
        {
            auto cmd_disp = static_cast<CommandDispatch *>(cmd);
            auto *compute_shader = static_cast<ComputeShader *>(GpuResourceRegistry::Get().Resolve(cmd_disp->_cs));
            auto *dispatch_arg_buffer = static_cast<GPUBuffer *>(GpuResourceRegistry::Get().Resolve(cmd_disp->_arg_buffer));
#if AILU_ENABLE_FRAME_DEBUGGER
            auto *capture_writer = cmd_buffer->CaptureWriter();
            const auto record_dispatch = [&](Render::FrameDebugger::EFrameEventExecutionResult result)
            {
                Render::FrameDebugger::DispatchEventCapture disp_cap;
                if (compute_shader != nullptr)
                {
                    disp_cap._compute_shader_id = capture_writer->RegisterObject(compute_shader,
                                                                                 Render::FrameDebugger::ECaptureObjectType::kComputeShader,
                                                                                 capture_writer->InternString(compute_shader->Name()));
                    disp_cap._kernel_name = capture_writer->InternString(ComputeShaderKernelRegistry::Get().GetName(cmd_disp->_kernel));
                }
                disp_cap._group_num_x = cmd_disp->_group_num_x;
                disp_cap._group_num_y = cmd_disp->_group_num_y;
                disp_cap._group_num_z = cmd_disp->_group_num_z;
                disp_cap._is_indirect = dispatch_arg_buffer != nullptr;
                disp_cap._argument_offset = cmd_disp->_arg_offset;
                if (result == Render::FrameDebugger::EFrameEventExecutionResult::kExecuted)
                {
                    disp_cap._binding_range_begin = capture_writer->BindingDataIndex();
                    u16 binding_count = 0u;
                    for (u16 i = 0u; i < cmd_disp->_bindings._entry_count && i < 32u; ++i)
                    {
                        const auto &entry = cmd_disp->_bindings._entries[i];
                        auto *resource = GpuResourceRegistry::Get().Resolve(entry._resource);
                        if (resource == nullptr && entry._resource_type != EBindResDescType::kConstBufferRaw)
                            continue;
                        Render::FrameDebugger::PipelineBindingCapture binding_cap;
                        binding_cap._slot = entry._slot;
                        binding_cap._source = (u8)Render::FrameDebugger::EBindingSource::kShader;
                        if (entry._priority == PipelineResource::kPriorityGlobal)
                            binding_cap._source = (u8)Render::FrameDebugger::EBindingSource::kGlobal;
                        else if (entry._priority == PipelineResource::kPriorityCmd)
                            binding_cap._source = (u8)Render::FrameDebugger::EBindingSource::kCommand;
                        binding_cap._cache_result = (u8)Render::FrameDebugger::EBindingCacheResult::kBound;
                        binding_cap._shader_resource_type = (u32) entry._resource_type;
                        binding_cap._slot_name = capture_writer->InternString(compute_shader->SlotToName(cmd_disp->_kernel, entry._slot));
                        binding_cap._key._resource_id = resource == nullptr ? 0u : capture_writer->RegisterObject(resource, CaptureObjectTypeForResource(resource),
                                                                                       capture_writer->InternString(resource->Name()));
                        binding_cap._key._resource_type = (u32) entry._resource_type;
                        binding_cap._key._gpu_handle = entry._addi_info._gpu_handle;
                        binding_cap._key._native_resource = (u64) entry._addi_info._native_res_ptr;
                        binding_cap._key._view_index = entry._addi_info._view_index;
                        binding_cap._key._sub_resource = entry._addi_info._sub_res;
                        binding_cap._key._slot = entry._slot;
                        capture_writer->RecordBinding(binding_cap);
                        ++binding_count;
                    }
                    disp_cap._binding_count = binding_count;
                }
                u32 event_id = capture_writer->RecordDispatchEvent(disp_cap);
                capture_writer->SetEventResult(event_id, result);
            };
#endif
            if (compute_shader == nullptr || !compute_shader->IsKernelValid(cmd_disp->_kernel))
            {
                LOG_WARNING("D3DContext skipped invalid compute dispatch");
#if AILU_ENABLE_FRAME_DEBUGGER
                if (capture_writer)
                    record_dispatch(Render::FrameDebugger::EFrameEventExecutionResult::kSkippedInvalidDispatch);
#endif
                return;
            }
            bool is_indirect = dispatch_arg_buffer != nullptr;
            if (!cmd_disp->_bindings._is_ready)
            {
                LOG_WARNING("D3DContext skipped compute dispatch with unavailable snapshot: shader({}) kernel({})",
                            compute_shader->Name(), ComputeShaderKernelRegistry::Get().GetName(cmd_disp->_kernel));
#if AILU_ENABLE_FRAME_DEBUGGER
                if (capture_writer)
                    record_dispatch(Render::FrameDebugger::EFrameEventExecutionResult::kSkippedInvalidResource);
#endif
                return;
            }
            compute_shader->Bind(d3dcmd, cmd_disp->_kernel, cmd_disp->_bindings);
            if (is_indirect)
            {
                D3DGPUBuffer *d3d_buf = static_cast<D3DGPUBuffer *>(dispatch_arg_buffer);
                d3d_buf->RequireState(cmd_buffer, EResourceState::kIndirectArgument);
                dxcmd->ExecuteIndirect(_dispatch_cmd_sig.Get(), 1u, d3d_buf->NativeResource().As<ID3D12Resource>(), cmd_disp->_arg_offset,
                                       d3d_buf->GetCounterBuffer(), 0u);
            }
            else
            {
                constexpr u64 kMaxDispatchThreadGroupCount = 4194303u;
                const u64 total_thread_group_count = static_cast<u64>(cmd_disp->_group_num_x) * cmd_disp->_group_num_y * cmd_disp->_group_num_z;
                if (total_thread_group_count > kMaxDispatchThreadGroupCount)
                {
                    LOG_WARNING("D3DContext skipped oversized dispatch shader({}) kernel({}) group({}, {}, {})", compute_shader->Name(),
                                ComputeShaderKernelRegistry::Get().GetName(cmd_disp->_kernel), cmd_disp->_group_num_x, cmd_disp->_group_num_y,
                                cmd_disp->_group_num_z);
#if AILU_ENABLE_FRAME_DEBUGGER
                    if (capture_writer)
                        record_dispatch(Render::FrameDebugger::EFrameEventExecutionResult::kSkippedInvalidDispatch);
#endif
                    return;
                }
                dxcmd->Dispatch(cmd_disp->_group_num_x, cmd_disp->_group_num_y, cmd_disp->_group_num_z);
            }
            ++d3dcmd->Statistics()._dispatch_call;
#if AILU_ENABLE_FRAME_DEBUGGER
            if (capture_writer)
                record_dispatch(Render::FrameDebugger::EFrameEventExecutionResult::kExecuted);
#endif
        }
        else if (cmd->GetCmdType() == EGpuCommandType::kCommandProfiler)
        {
            auto cmd_profiler = static_cast<CommandProfiler *>(cmd);

            // #if defined(TRACY_ENABLE)
            //             static std::stack<std::unique_ptr<tracy::D3D12ZoneScope>> s_tracy_gpu_zone_stack{};
            //             const bool tracy_active = _tracy_d3d12_ctx != nullptr;
            // #endif

            if (cmd_profiler->_is_start)
            {
                if (g_engine_config._enable_pix) PIXBeginEvent(dxcmd, 0u, cmd_profiler->_name.c_str());
                cmd_profiler->_gpu_index = Profiler::Get().StartGpuProfile(cmd_buffer, cmd_profiler->_name);
                Profiler::Get().AddGPUProfilerHierarchy(true, (u32) cmd_profiler->_gpu_index);
                cmd_profiler->_cpu_index = Profiler::Get().StartCPUProfile(cmd_profiler->_name + "_Submit");
                Profiler::Get().AddCPUProfilerHierarchy(true, (u32) cmd_profiler->_cpu_index);
                d3dcmd->PushProfiler(cmd_profiler);

                // #if defined(TRACY_ENABLE)
                //                 if (tracy_active)
                //                 {
                //                     // Dynamic-name GPU zone bound to the currently recording command list.
                //                     // This zone spans from BeginProfiler() to EndProfiler() in the command stream.
                //                     const char* file = __FILE__;
                //                     const char* function = __FUNCTION__;
                //                     auto zone = std::make_unique<tracy::D3D12ZoneScope>(
                //                         _tracy_d3d12_ctx,
                //                         (uint32_t)__LINE__,
                //                         file, std::strlen(file),
                //                         function, std::strlen(function),
                //                         cmd_profiler->_name.c_str(), cmd_profiler->_name.size(),
                //                         dxcmd,
                //                         true);
                //                     s_tracy_gpu_zone_stack.push(std::move(zone));
                //                 }
                // #endif
            }
            else
            {
                CommandProfiler *begin_profiler = d3dcmd->TopProfiler();
                AL_ASSERT(begin_profiler != nullptr);
                Profiler::Get().EndGpuProfile(cmd_buffer, begin_profiler->_gpu_index);
                Profiler::Get().AddGPUProfilerHierarchy(false, (u32) begin_profiler->_gpu_index);
                Profiler::Get().EndCPUProfile(begin_profiler->_cpu_index);
                Profiler::Get().AddCPUProfilerHierarchy(false, (u32) begin_profiler->_cpu_index);
                d3dcmd->PopProfiler();
                if (g_engine_config._enable_pix) PIXEndEvent(dxcmd);

                // #if defined(TRACY_ENABLE)
                //                 if (tracy_active && !s_tracy_gpu_zone_stack.empty())
                //                 {
                //                     // Destroying the scope writes end timestamp and resolves query data.
                //                     s_tracy_gpu_zone_stack.pop();
                //                 }
                // #endif
            }
        }
        else if (cmd->GetCmdType() == EGpuCommandType::kReadBack)
        {
            auto cmd_rb = static_cast<CommandReadBack *>(cmd);
            auto *readback_resource = GpuResourceRegistry::Get().Resolve(cmd_rb->_res);
            if (readback_resource == nullptr)
            {
                LOG_WARNING("D3DContext::ProcessGpuCommand: Readback resource is nullptr!");
                return;
            }
            u64 size = cmd_rb->_is_counter_value ? 4u : std::min<u64>(readback_resource->GetSize(), (u64) cmd_rb->_size);
            auto copy_dst = _readback_pool->Acquire(size, _frame_count);
            D3DGPUBuffer *d3dbuffer = static_cast<D3DGPUBuffer *>(readback_resource);
            const D3DResource &copy_src = cmd_rb->_is_counter_value ? d3dbuffer->CounterResource() : d3dbuffer->Resource();
            d3dcmd->RequireState(copy_src, EResourceState::kCopySource);
            dxcmd->CopyBufferRegion(copy_dst.Get(), 0u, copy_src.Get(), 0u, size);

            auto copy_dst_capture = copy_dst;// 确保 lambda 生命周期
            ReadbackCallback callback = std::move(cmd_rb->_callback);
            d3dcmd->AddPostSubmitCallback(
                    [this, copy_dst_capture, size, callback](u64 fence_value)
                    {
                        JobSystem::Get().Dispatch(
                                [this, copy_dst_capture, size, fence_value, callback]()
                                {
                                    while (_p_cmd_buffer_fence->GetCompletedValue() < fence_value)
                                        std::this_thread::yield();
                                    D3D12_RANGE range{0, size};
                                    u8 *raw_data = nullptr;
                                    copy_dst_capture->Map(0, &range,
                                                           reinterpret_cast<void **>(const_cast<u8 **>(&raw_data)));
                                    callback(raw_data, (u32) size);
                                    copy_dst_capture->Unmap(0, nullptr);
                                });
                    });
        }
        else if (cmd->GetCmdType() == EGpuCommandType::kPresent)
        {
            auto pending_releases = GpuResourceRegistry::Get().TakePendingReleases();
            if (!pending_releases.empty())
            {
                d3dcmd->AddPostSubmitCallback([pending_releases = std::move(pending_releases)](u64 fence)
                {
                    for (const auto handle: pending_releases)
                        GpuResourceRegistry::Get().Retire(handle, fence);
                });
            }
            PresentImpl(d3dcmd);
        }
        else if (cmd->GetCmdType() == EGpuCommandType::kDestroyGpuResource)
        {
            auto destroy_cmd = static_cast<CommandDestroyGpuResource *>(cmd);
            const auto handle = destroy_cmd->_handle;
            d3dcmd->AddPostSubmitCallback([handle](u64 fence)
            {
                GpuResourceRegistry::Get().Retire(handle, fence);
            });
        }
        else if (cmd->GetCmdType() == EGpuCommandType::kBuildAS)
        {
            auto cmd_bas = static_cast<CommandBuildAS *>(cmd);
            if (cmd_bas->_is_blas)
            {
                if (cmd_bas->_is_update) { AL_ASSERT_MSG(false, "BLAS update is not supported yet!"); }
                else
                {
                    auto blas = static_cast<D3DRayTracingGeometry *>(GpuResourceRegistry::Get().Resolve(cmd_bas->_dst));
                    if (blas == nullptr)
                        return;
                    auto scratch_res = blas->_scratch_resource.Get();
                    auto blas_res = blas->_blas_resource.Get();
                    AL_ASSERT(scratch_res != nullptr && blas_res != nullptr);
                    d3dcmd->RequireState(blas->_scratch_resource, EResourceState::kUnorderedAccess);
                    d3dcmd->RequireState(blas->_blas_resource, EResourceState::kRaytracingAccelerationStructure);
                    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bottomLevelBuildDesc = {};
                    bottomLevelBuildDesc.Inputs = blas->_inputs;
                    bottomLevelBuildDesc.ScratchAccelerationStructureData = scratch_res->GetGPUVirtualAddress();
                    bottomLevelBuildDesc.DestAccelerationStructureData = blas_res->GetGPUVirtualAddress();
                    auto dxcmd = d3dcmd->NativeCmdList();
                    dxcmd->BuildRaytracingAccelerationStructure(&bottomLevelBuildDesc, 0, nullptr);
                    d3dcmd->UavBarrier(blas->_blas_resource);
                }
            }
            else
            {
                auto tlas = static_cast<D3DRayTracingScene *>(GpuResourceRegistry::Get().Resolve(cmd_bas->_dst));
                if (tlas == nullptr)
                    return;
                if (tlas->_scratch_resource)
                    d3dcmd->RequireState(tlas->_scratch_resource, EResourceState::kUnorderedAccess);
                if (tlas->_tlas_resource)
                    d3dcmd->RequireState(tlas->_tlas_resource, EResourceState::kRaytracingAccelerationStructure);
                d3dcmd->NativeCmdList()->BuildRaytracingAccelerationStructure(&tlas->GetBuildDesc(cmd_bas->_is_update), 0, nullptr);
            }
        }
        else if (cmd->GetCmdType() == EGpuCommandType::kDispatchRays)
        {
            auto cmd_dr = static_cast<CommandDispatchRays *>(cmd);
            auto *shader = static_cast<RayTracingShader *>(GpuResourceRegistry::Get().Resolve(cmd_dr->_shader));
            auto *scene = static_cast<RayTracingScene *>(GpuResourceRegistry::Get().Resolve(cmd_dr->_scene));
            if (shader == nullptr)
                return;
            shader->SetScene(scene);
            shader->DispatchRays(d3dcmd, cmd_dr->_w, cmd_dr->_h, cmd_dr->_depth);
        }
        else
        {
        };
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
