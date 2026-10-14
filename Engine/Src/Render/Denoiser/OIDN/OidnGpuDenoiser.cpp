#include "../../../RHI/DX12/OidnGpuInteropD3D12.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/ThreadPool.h"
#include "RHI/DX12/D3DCommandBuffer.h"
#include "RHI/DX12/D3DContext.h"
#include "RHI/DX12/D3DTexture.h"
#include "Render/CommandBuffer.h"
#include "Render/Denoiser/PathTracingDenoiser.h"
#include "Render/GraphicsContext.h"
#include "Render/RenderGraph/RenderGraph.h"
#include "Render/RenderingData.h"
#include <atomic>
#include <chrono>
#include <d3dcompiler.h>
#include <filesystem>
#include <mutex>

namespace Ailu::Render
{
#if AILU_WITH_OIDN_GPU
    namespace
    {
        using namespace RHI::DX12;
        using Microsoft::WRL::ComPtr;
        using Clock = std::chrono::steady_clock;
        void Check(HRESULT result)
        {
            if (FAILED(result)) throw std::runtime_error("OIDN DX12 HRESULT=" + std::to_string(result));
        }
        class SharedBuffer final : public GPUBuffer
        {
        public:
            SharedBuffer(ID3D12Resource *resource, u32 bytes) : GPUBuffer(Description(bytes)), _resource(resource)
            { SetCreatedFence(0u); }
            NativeHandle NativeResource() override { return {RendererAPI::ERenderAPI::kDirectX12, _resource.Get()}; }
            void RequireState(RHICommandBuffer *cmd, EResourceState state, u32 sub = kTotalSubRes) override
            { static_cast<D3DCommandBuffer *>(cmd)->RequireState(_resource, state, sub); }
            void UavBarrier(RHICommandBuffer *cmd) override
            { static_cast<D3DCommandBuffer *>(cmd)->UavBarrier(_resource); }

        private:
            static BufferDesc Description(u32 bytes)
            {
                BufferDesc desc;
                desc._size = bytes;
                desc._element_num = bytes / 4u;
                desc._element_size = 4u;
                desc._target = EGPUBufferTarget::kRaw;
                return desc;
            }
            D3DResource _resource;
        };
        struct Session
        {
            ComPtr<ID3D12Device> _device;
            ComPtr<ID3D12Fence> _submission_fence;
            OidnGpuInteropD3D12 _interop;
            ComPtr<ID3D12RootSignature> _root;
            ComPtr<ID3D12PipelineState> _pack, _unpack, _clear;
            ComPtr<ID3D12DescriptorHeap> _descriptors;
            std::mutex _mutex;
            std::shared_ptr<std::atomic<bool>> _activation_owner;
            std::atomic<EDenoiserStatus> _status{EDenoiserStatus::kInitializing};
            std::atomic<bool> _busy{true}, _stop{false}, _cleanup_queued{false}, _cleaned{false};
            std::atomic<u64> _generation{0u}, _last_native_fence{0u}, _unpack_fence{0u};
            u32 _native_pending = 0u;
            std::string _error;
            u64 _request = 0u;
            u32 _width = 0u, _height = 0u;
            int _quality = 5;

            void Initialize(const std::wstring &shader_path, const std::wstring &runtime_path)
            {
                if (_stop.load())
                {
                    _busy.store(false);
                    return;
                }
                try
                {
                    if (!_interop.Initialize(_device.Get(), nullptr, _width, _height, runtime_path, true, _quality))
                        throw std::runtime_error(_interop.Reason());
                    D3D12_DESCRIPTOR_RANGE ranges[2]{};
                    ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1u, 0u, 0u, 0u};
                    ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1u, 1u, 0u, 1u};
                    D3D12_ROOT_PARAMETER parameters[5]{};
                    parameters[0].ParameterType = parameters[3].ParameterType =
                            D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                    parameters[0].DescriptorTable = {1u, &ranges[0]};
                    parameters[3].DescriptorTable = {1u, &ranges[1]};
                    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
                    parameters[1].Descriptor.ShaderRegister = 0u;
                    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
                    parameters[2].Descriptor.ShaderRegister = 1u;
                    parameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
                    parameters[4].Constants = {0u, 0u, 2u};
                    D3D12_ROOT_SIGNATURE_DESC desc{5u, parameters, 0u, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
                    ComPtr<ID3DBlob> signature, errors;
                    Check(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors));
                    Check(_device->CreateRootSignature(0u, signature->GetBufferPointer(), signature->GetBufferSize(),
                                                       IID_PPV_ARGS(&_root)));
                    const char *entries[] = {"PackHDR", "UnpackHDR", "ClearInvalidCounter"};
                    ID3D12PipelineState **pipelines[] = {_pack.GetAddressOf(), _unpack.GetAddressOf(),
                                                         _clear.GetAddressOf()};
                    for (u32 i = 0u; i < 3u; ++i)
                    {
                        ComPtr<ID3DBlob> code;
                        const HRESULT result = D3DCompileFromFile(
                                shader_path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, entries[i], "cs_5_1",
                                D3DCOMPILE_ENABLE_STRICTNESS, 0u, &code, &errors);
                        if (FAILED(result))
                            throw std::runtime_error(
                                    errors ? std::string(static_cast<const char *>(errors->GetBufferPointer()))
                                           : "OIDN shader missing");
                        D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};
                        pipeline.pRootSignature = _root.Get();
                        pipeline.CS = {code->GetBufferPointer(), code->GetBufferSize()};
                        Check(_device->CreateComputePipelineState(&pipeline, IID_PPV_ARGS(pipelines[i])));
                    }
                    D3D12_DESCRIPTOR_HEAP_DESC heap{};
                    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                    heap.NumDescriptors = 2u;
                    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                    Check(_device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&_descriptors)));
                    _status.store(EDenoiserStatus::kIdle);
                }
                catch (const std::exception &error)
                {
                    _error = error.what();
                    _interop.Shutdown();
                    _root.Reset();
                    _clear.Reset();
                    _pack.Reset();
                    _unpack.Reset();
                    _descriptors.Reset();
                    LOG_WARNING("OIDN GPU initialization unavailable: {}", _error);
                    _status.store(EDenoiserStatus::kUnsupported);
                }
                _busy.store(false);
            }
            void Stop()
            {
                std::lock_guard lock(_mutex);
                _stop.store(true);
            }
            bool Retired(const std::shared_ptr<Session> &self)
            {
                if (_activation_owner && !_activation_owner->load()) Stop();
                if (!_stop.load()) return false;
                if (_cleaned.load()) return true;
                std::lock_guard lock(_mutex);
                const bool device_lost = FAILED(_device->GetDeviceRemovedReason());
                if (_busy.load() || (!device_lost && (_native_pending != 0u || _submission_fence->GetCompletedValue() <
                                                                                       _last_native_fence.load())))
                    return false;
                if (!_cleanup_queued.exchange(true))
                    Core::ThreadPool::Get().Enqueue("OIDN GPU retirement",
                                                    [self]
                                                    {
                                                        // Synchronization is confined to retirement on a background worker.
                                                        self->_interop.Shutdown();
                                                        self->_clear.Reset();
                                                        self->_pack.Reset();
                                                        self->_unpack.Reset();
                                                        self->_root.Reset();
                                                        self->_descriptors.Reset();
                                                        self->_cleaned.store(true);
                                                    });
                return false;
            }
        };
        struct Ticket
        {
            std::shared_ptr<Session> _session;
            bool _native_recorded = false, _submitted = false;
            EDenoiserStatus _phase = EDenoiserStatus::kPackPending;
            ~Ticket()
            {
                std::lock_guard lock(_session->_mutex);
                if (!_submitted)
                {
                    if (_native_recorded) --_session->_native_pending;
                    if (_session->_status.load() == _phase)
                        _session->_status.store(_phase == EDenoiserStatus::kPackPending ? EDenoiserStatus::kIdle
                                                                                        : EDenoiserStatus::kFiltering);
                }
            }
            void Submitted(u64 fence)
            {
                std::lock_guard lock(_session->_mutex);
                _session->_last_native_fence.store(fence);
                --_session->_native_pending;
                _submitted = true;
            }
        };
        void Bind(Session &session, D3DCommandBuffer &cmd)
        {
            auto *native = cmd.NativeCmdList();
            cmd.SetDescriptorHeap(session._descriptors.Get(), -1);
            native->SetComputeRootSignature(session._root.Get());
            const auto gpu = session._descriptors->GetGPUDescriptorHandleForHeapStart();
            native->SetComputeRootDescriptorTable(0u, gpu);
            native->SetComputeRootDescriptorTable(3u, gpu);
            native->SetComputeRootUnorderedAccessView(1u, session._interop.Input()->GetGPUVirtualAddress());
            native->SetComputeRootShaderResourceView(2u, session._interop.Output()->GetGPUVirtualAddress());
            const u32 dimensions[] = {session._width, session._height};
            native->SetComputeRoot32BitConstants(4u, 2u, dimensions, 0u);
        }
    }// namespace
    class OidnGpuDenoiser final : public IPathTracingDenoiser
    {
    public:
        void Reset(u64 generation) override
        {
            _generation = generation;
            if (_session) _session->_generation.store(generation);
            _published = {};
            _diagnostics._last_sample_count = 0u;
            _diagnostics._latency_ms = 0.0f;
            _next_spp = 4u;
        }
        void SetActive(bool active) override
        {
            _active = active;
            if (!active) Retire();
            if (g_pGfxContext && RendererAPI::GetAPI() == RendererAPI::ERenderAPI::kDirectX12)
                static_cast<D3DContext *>(g_pGfxContext)->CollectExternalGpuLifetimes();
        }
        void Shutdown() override { SetActive(false); }
        EDenoiserStatus Status() const override
        {
            if (!_active) return EDenoiserStatus::kDisabled;
            if (IsRetiring()) return EDenoiserStatus::kRetiring;
            return _session ? _session->_status.load() : EDenoiserStatus::kInitializing;
        }
        const char *Reason() const override
        {
            if (_session && !_session->_busy.load() && !_session->_error.empty()) return _session->_error.c_str();
            switch (Status())
            {
                case EDenoiserStatus::kDisabled:
                    return "Disabled";
                case EDenoiserStatus::kUnsupported:
                    return "Unsupported";
                case EDenoiserStatus::kPackPending:
                    return "PackPending";
                case EDenoiserStatus::kFiltering:
                    return "Filtering";
                case EDenoiserStatus::kUnpacking:
                    return "Unpacking";
                case EDenoiserStatus::kRetiring:
                    return "Retiring";
                case EDenoiserStatus::kReady:
                    return "Ready";
                case EDenoiserStatus::kFailed:
                    return "Failed";
                case EDenoiserStatus::kIdle:
                    return "Idle";
                default:
                    return "Initializing";
            }
        }
        PathTracingDenoiserDiagnostics Diagnostics() const override { return _diagnostics; }
        PathTracingDenoiserOutput Record(RDG::RenderGraph &graph, const PathTracingDenoiserInput &input) override
        {
            if (!_active || !input._raw_executed || !input._radiance_owner) return {};
            auto *context = dynamic_cast<D3DContext *>(g_pGfxContext);
            if (!context) return {};
            const int quality = 4 + static_cast<int>(input._quality);
            if (_session && (_session->_stop.load() || _session->_width != input._width ||
                             _session->_height != input._height || _session->_quality != quality))
                Retire();
            if (IsRetiring()) return {};
            _retiring.reset();
            if (!_session)
            {
                if (uint64_t(input._width) * input._height * 16u + 16u > UINT32_MAX) return {};
                auto session = std::make_shared<Session>();
                session->_device = context->GetDevice();
                session->_submission_fence = context->SubmissionFence();
                session->_width = input._width;
                session->_height = input._height;
                session->_quality = quality;
                session->_activation_owner = input._activation_owner;
                session->_generation.store(_generation);
                context->RegisterExternalGpuLifetime([session] { session->Stop(); },
                                                     [session] { return session->Retired(session); });
                const auto shader = (std::filesystem::path(ResourceMgr::EngineResRootPath()) /
                                     L"Shaders/hlsl/ray_trace/oidn_pack.hlsl")
                                            .wstring();
                const auto runtime = std::filesystem::path(AILU_OIDN_RUNTIME_DIRECTORY).wstring();
                Core::ThreadPool::Get().Enqueue("OIDN GPU initialize",
                                                [session, shader, runtime] { session->Initialize(shader, runtime); });
                _session = session;
                return {};
            }
            auto session = _session;
            if (session->_busy.load())
            {
                auto result = _published;
                if (result._valid) result._display_texture = graph.Import(_previews[_front].get());
                return result;
            }
            auto phase = session->_status.load();
            if (phase == EDenoiserStatus::kInitializing || phase == EDenoiserStatus::kUnsupported ||
                phase == EDenoiserStatus::kFailed)
                return {};
            if (!_input)
            {
                _input = AdoptGpuResource(
                        new SharedBuffer(session->_interop.Input(), input._width * input._height * 16u + 16u));
                _output = AdoptGpuResource(
                        new SharedBuffer(session->_interop.Output(), input._width * input._height * 16u));
                TextureDesc desc(input._width, input._height, ERenderTargetFormat::kRGBAFloat);
                desc._is_random_access = true;
                desc._is_linear = true;
                _previews[0] = RenderTexture::Create(desc, "OIDN HDR Preview A");
                _previews[1] = RenderTexture::Create(desc, "OIDN HDR Preview B");
                _diagnostics._shared_mb = float(session->_interop.Input()->GetDesc().Width * 2u) / (1024.0f * 1024.0f);
            }
            if (!_previews[0]->IsReady() || !_previews[1]->IsReady()) return {};
            if (phase == EDenoiserStatus::kFiltering)
            {
                const u64 done = session->_interop.DoneFence()->GetCompletedValue();
                if (done == UINT64_MAX)
                {
                    session->_error = "OIDN shared fence reports device removal";
                    session->_status.store(EDenoiserStatus::kFailed);
                    return {};
                }
                if (done >= session->_request)
                {
                    if (_job_input._generation != _generation) session->_status.store(EDenoiserStatus::kIdle);
                    else
                        RecordUnpack(graph, session);
                }
                else if (Clock::now() - _started > std::chrono::seconds(30))
                {
                    session->_error = "OIDN completion timeout; preview disabled until backend reactivation";
                    session->_status.store(EDenoiserStatus::kFailed);
                }
            }
            else if (phase == EDenoiserStatus::kUnpacking && session->_unpack_fence.load() > 0u &&
                     session->_submission_fence->GetCompletedValue() >= session->_unpack_fence.load())
            {
                if (_job_input._generation == _generation)
                {
                    _published._generation = _generation;
                    _published._backend_instance = _job_input._backend_instance;
                    _published._request_id = session->_request;
                    _published._width = input._width;
                    _published._height = input._height;
                    _published._source_sample_count = _job_input._sample_count;
                    _published._valid = true;
                    _front = _write;
                    _write = 1u - _write;
                    const u32 spp = _job_input._sample_count;
                    _next_spp = spp < 8u    ? 8u
                                : spp < 16u ? 16u
                                : spp < 32u ? 32u
                                : spp < 64u ? 64u
                                            : spp + std::max(1u, input._sample_interval);
                    _diagnostics._last_sample_count = spp;
                    _diagnostics._latency_ms =
                            std::chrono::duration<float, std::milli>(Clock::now() - _started).count();
                }
                session->_status.store(EDenoiserStatus::kReady);
            }
            phase = session->_status.load();
            if ((phase == EDenoiserStatus::kIdle || phase == EDenoiserStatus::kReady) &&
                input._sample_count + 1u >= _next_spp)
                RecordPack(graph, input, session);
            auto result = _published;
            if (result._valid) result._display_texture = graph.Import(_previews[_front].get());
            return result;
        }

    private:
        bool IsRetiring() const
        {
            const auto previous = _retiring.lock();
            return previous && !previous->_cleaned.load();
        }
        void Retire()
        {
            if (_session)
            {
                _session->Stop();
                _retiring = _session;
                _session.reset();
            }
            _input.reset();
            _output.reset();
            _previews[0].reset();
            _previews[1].reset();
            _published = {};
            _job_input = {};
            _diagnostics = {};
            _next_spp = 4u;
        }
        void RecordPack(RDG::RenderGraph &graph, PathTracingDenoiserInput input,
                        const std::shared_ptr<Session> &session)
        {
            session->_status.store(EDenoiserStatus::kPackPending);
            const u64 request = ++_next_request_id;
            session->_request = request;
            input._sample_count += 1u;// Candidate count; execute gate below proves this frame's Temporal ran.
            _job_input = input;
            auto ticket = std::make_shared<Ticket>();
            ticket->_session = session;
            auto buffer = _input;
            const auto shared = graph.Import(buffer.get());
            graph.AddPass(
                    "OIDN Pack HDR", RDG::PassDesc(RDG::EPassType::kCompute),
                    [input, shared](RDG::RenderGraphBuilder &builder)
                    {
                        builder.Read(input._radiance, EResourceUsage::kReadSRV);
                        (void) builder.Write(shared, EResourceUsage::kWriteUAV);
                    },
                    [input, session, ticket, buffer, request](RDG::RenderGraph &pass_graph, CommandBuffer *cmd,
                                                              const RenderingData &)
                    {
                        auto *source = pass_graph.Resolve<Texture>(input._radiance);
                        cmd->RecordNativeCallback(
                                [input, session, ticket, buffer, request, source](RHICommandBuffer *rhi)
                                {
                                    std::lock_guard lock(session->_mutex);
                                    if (session->_stop.load() ||
                                        (session->_activation_owner && !session->_activation_owner->load()) ||
                                        session->_generation.load() != input._generation || !*input._raw_executed)
                                        return;
                                    auto *cmd = static_cast<D3DCommandBuffer *>(rhi);
                                    auto *resource = source->NativeResource().As<ID3D12Resource>();
                                    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
                                    srv.Format = resource->GetDesc().Format;
                                    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                                    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                                    srv.Texture2D.MipLevels = 1u;
                                    session->_device->CreateShaderResourceView(
                                            resource, &srv,
                                            session->_descriptors->GetCPUDescriptorHandleForHeapStart());
                                    Bind(*session, *cmd);
                                    cmd->SetComputePipelineState(session->_clear.Get());
                                    cmd->NativeCmdList()->Dispatch(1u, 1u, 1u);
                                    buffer->UavBarrier(cmd);
                                    cmd->SetComputePipelineState(session->_pack.Get());
                                    cmd->NativeCmdList()->Dispatch((input._width + 7u) / 8u, (input._height + 7u) / 8u,
                                                                   1u);
                                    buffer->RequireState(cmd, EResourceState::kCommon);
                                    cmd->AddExternalGpuSignal(session->_interop.ReadyFence(), request);
                                    ++session->_native_pending;
                                    ticket->_native_recorded = true;
                                    cmd->AddPostSubmitCallback(
                                            [session, ticket, request](u64 fence)
                                            {
                                                session->_busy.store(true);
                                                ticket->Submitted(fence);
                                                Core::ThreadPool::Get().Enqueue(
                                                        "OIDN GPU filter",
                                                        [session, request]
                                                        {
                                                            const bool ok =
                                                                    session->_interop.QueueExternalFilter(request);
                                                            if (!ok) session->_error = session->_interop.Reason();
                                                            session->_status.store(ok ? EDenoiserStatus::kFiltering
                                                                                      : EDenoiserStatus::kFailed);
                                                            session->_busy.store(false);
                                                        });
                                            });
                                });
                    });
            _started = Clock::now();
        }
        void RecordUnpack(RDG::RenderGraph &graph, const std::shared_ptr<Session> &session)
        {
            session->_status.store(EDenoiserStatus::kUnpacking);
            session->_unpack_fence.store(0u);
            auto buffer = _output;
            auto preview = _previews[_write];
            auto ticket = std::make_shared<Ticket>();
            ticket->_session = session;
            ticket->_phase = EDenoiserStatus::kUnpacking;
            const auto shared = graph.Import(buffer.get()), target = graph.Import(preview.get());
            graph.AddPass(
                    "OIDN Unpack HDR", RDG::PassDesc(RDG::EPassType::kCompute),
                    [shared, target](RDG::RenderGraphBuilder &builder)
                    {
                        builder.Read(shared, EResourceUsage::kReadSRV);
                        (void) builder.Write(target, EResourceUsage::kWriteUAV);
                    },
                    [session, buffer, preview, ticket](RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &)
                    {
                        cmd->RecordNativeCallback(
                                [session, buffer, preview, ticket](RHICommandBuffer *rhi)
                                {
                                    std::lock_guard lock(session->_mutex);
                                    if (session->_stop.load() ||
                                        (session->_activation_owner && !session->_activation_owner->load()))
                                        return;
                                    auto *cmd = static_cast<D3DCommandBuffer *>(rhi);
                                    auto *resource = preview->NativeResource().As<ID3D12Resource>();
                                    auto cpu = session->_descriptors->GetCPUDescriptorHandleForHeapStart();
                                    cpu.ptr += session->_device->GetDescriptorHandleIncrementSize(
                                            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                                    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
                                    uav.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
                                    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                                    session->_device->CreateUnorderedAccessView(resource, nullptr, &uav, cpu);
                                    Bind(*session, *cmd);
                                    cmd->SetComputePipelineState(session->_unpack.Get());
                                    cmd->AddCompletedExternalGpuWait(session->_interop.DoneFence(), session->_request);
                                    cmd->NativeCmdList()->Dispatch((session->_width + 7u) / 8u,
                                                                   (session->_height + 7u) / 8u, 1u);
                                    buffer->RequireState(cmd, EResourceState::kCommon);
                                    preview->UavBarrier(cmd);
                                    ++session->_native_pending;
                                    ticket->_native_recorded = true;
                                    cmd->AddPostSubmitCallback(
                                            [session, ticket](u64 fence)
                                            {
                                                ticket->Submitted(fence);
                                                session->_unpack_fence.store(fence);
                                            });
                                });
                    });
        }
        std::shared_ptr<Session> _session;
        std::weak_ptr<Session> _retiring;
        Ref<SharedBuffer> _input, _output;
        Ref<RenderTexture> _previews[2];
        PathTracingDenoiserOutput _published;
        PathTracingDenoiserInput _job_input;
        PathTracingDenoiserDiagnostics _diagnostics;
        Clock::time_point _started{};
        u64 _generation = 0u;
        u64 _next_request_id = 0u;
        u32 _next_spp = 4u, _front = 0u, _write = 1u;
        bool _active = false;
    };
#else
    class OidnGpuDenoiser final : public IPathTracingDenoiser
    {
    public:
        void Reset(u64) override {}
        void Shutdown() override {}
        PathTracingDenoiserOutput Record(RDG::RenderGraph &, const PathTracingDenoiserInput &) override { return {}; }
        EDenoiserStatus Status() const override { return EDenoiserStatus::kUnsupported; }
        const char *Reason() const override { return "AILU_WITH_OIDN_GPU=0"; }
    };
#endif
    std::unique_ptr<IPathTracingDenoiser> CreateOidnGpuDenoiser() { return std::make_unique<OidnGpuDenoiser>(); }
}// namespace Ailu::Render
