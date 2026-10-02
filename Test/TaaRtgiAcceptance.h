#pragma once

#include "Render/Features/RayTraceGI.h"
#include "Render/ImageCapture/Adapters/RenderGraphImageCaptureBridge.h"
#include <iomanip>

class TaaRtgiAcceptance final : public RenderFeature, public RenderPass
{
public:
    TaaRtgiAcceptance(const WString &output, Camera &camera, Renderer &renderer)
        : RenderFeature("M7Acceptance"), RenderPass("M7Capture"), _output(output), _camera(camera)
    {
        _event = static_cast<ERenderPassEvent>(551u);
        for (auto *feature : renderer.GetFeatures())
        {
            if (feature->Name() == "TemporalAA") _taa = dynamic_cast<TemporalAA *>(feature);
            if (feature->Name() == "RayTraceGI") _rtgi = dynamic_cast<RayTraceGI *>(feature);
        }
        _failed = !_taa || !_rtgi;
        _observations.open(std::filesystem::path(output) / "m7_observations.csv");
        _observations << std::setprecision(9);
        _observations << "sample,backend,mode,local,engine_frame,width,height,jitter_x,jitter_y,"
                         "position_x,yaw,hardware_supported\n";
        _graph.open(std::filesystem::path(output) / "m7_graph.csv");
        _graph << "sample,ordinal,pass,access,id,version,resource,texture\n";
        _reconstruction = ComputeShader::Create(
            std::filesystem::absolute("Test/TaaRtgiReconstruction.hlsl").wstring());
        _reconstruction_kernel = _reconstruction->FindKernel("Reconstruct");
        _failed |= _reconstruction_kernel == kInvalidComputeShaderKernelId;
    }

    void AddRenderPasses(Renderer &renderer, const RenderingData &) override { renderer.EnqueuePass(this); }

    void Prepare(u32 sample)
    {
        for (auto session : _selectors)
        {
            RenderGraphImageCaptureBridge::RemoveSelector(session);
            if (ImageCaptureService::GetStatus(session)._submitted_frame_count == 0u)
                ImageCaptureService::ReportError(session, "M7 target did not execute in its designated sample");
        }
        _selectors.clear();
        _sample = sample;
        if (_failed || sample >= kSampleCount) return;
        const u32 mode = (sample / 64u) % 5u, local = sample % 64u;
        _rtgi->SetActive(true);
        _rtgi->_use_hardware_ray_tracing = sample >= 320u;
        _rtgi->_accumulation_mode = mode == 3u ? EPathTracerAccumulationMode::kReference :
            mode == 4u ? EPathTracerAccumulationMode::kDisabled : EPathTracerAccumulationMode::kRealtime;
        _rtgi->_debug_temporal_target = 0u;
        _taa->SetActive(mode == 2u);
        _camera._anti_aliasing = mode == 0u ? EAntiAliasing::kNone : EAntiAliasing::kTAA;
        _position_x = local < 32u ? 0.f : local < 40u ? (local - 31u) * .015f : .12f;
        _yaw = local < 40u ? 0.f : (local - 39u) * .2f;
        if (mode >= 3u) { _position_x = local < 48u ? 0.f : .2f; _yaw = 0.f; }
        if (mode < 3u && local >= 48u) { _position_x = local < 52u ? 0.f : 2.f; _yaw = 0.f; }
        const bool resized = mode < 3u && local >= 48u && local < 50u;
        _camera.OutputSize(resized ? 317u : 320u, resized ? 179u : 180u);
        _camera.SetLens(mode < 3u && local >= 54u ? 70.f : 60.f, 16.f / 9.f, .1f, 100.f);
        _camera.Position({_position_x, 0.f, -5.f});
        _camera.Rotation(Quaternion::EulerAngles(0.f, _yaw, 0.f));
        if (local == 0u || local == 56u) _camera.MarkTemporalHistoryReset();
        _camera.MarkDirty();
        _camera.RecalculateMatrix();
        Select("raw", "Debug GI", false, 1u);
        if (mode != 4u)
        {
            Select("history", "Debug GI", false, 0u);
            Select("heat", "Denoise", true, 2u);
        }
    }

    void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &data) override
    {
        if (_failed || _sample >= kSampleCount) return;
        graph._is_debug = true;
        const auto current = data._rg_handles._color_tex;
        const auto resolved = data._rg_handles._taa_color.IsValid()
            ? data._rg_handles._taa_color : current;
        const auto motion = data._rg_handles._motion_vector_tex;
        const auto depth = data._rg_handles._depth_tex;
        const auto normal = data._rg_handles._gbuffers[0];
        const auto reactive = data._rg_handles._taa_reactive;
        TextureDesc world_desc;
        world_desc._width = data._width;
        world_desc._height = data._height;
        world_desc._format = EALGFormat::kALGFormatR32G32B32A32_FLOAT;
        world_desc._is_random_access = true;
        world_desc._fixed_size = true;
        if (!_world || _world->Width() != data._width || _world->Height() != data._height)
            _world = RenderTexture::Create(world_desc, "M7WorldReconstruction");
        const auto world = graph.Import(_world.get());
        auto world_output = std::make_shared<RDG::RGHandle>();
        graph.AddPass("M7Reconstruct", RDG::PassDesc(RDG::EPassType::kCompute),
            [depth, normal, world, world_output](RDG::RenderGraphBuilder &builder)
            {
                builder.Read(depth); builder.Read(normal);
                *world_output = builder.Write(world, EResourceUsage::kWriteUAV);
            }, [this, depth, normal, world_output]
               (RDG::RenderGraph &pass_graph, CommandBuffer *cmd, const RenderingData &rendering_data)
            {
                _reconstruction->SetTexture(_reconstruction_kernel, "_AcceptanceDepth",
                                            pass_graph.Resolve<Texture>(depth));
                _reconstruction->SetTexture(_reconstruction_kernel, "_AcceptanceNormal",
                                            pass_graph.Resolve<Texture>(normal));
                _reconstruction->SetTexture(_reconstruction_kernel, "_AcceptanceWorld",
                                            pass_graph.Resolve<Texture>(*world_output));
                const auto [x, y, z] = _reconstruction->CalculateDispatchNum(
                    _reconstruction_kernel, rendering_data._width, rendering_data._height, 1u);
                cmd->Dispatch(_reconstruction.get(), _reconstruction_kernel, x, y, z);
            });
        graph.AddPass("M7Capture", {},
            [current, resolved, motion, depth, reactive, world_output](RDG::RenderGraphBuilder &builder)
        {
            builder.Read(current); builder.Read(resolved); builder.Read(motion); builder.Read(depth);
            if (reactive.IsValid()) builder.Read(reactive);
            builder.Read(*world_output);
        }, [this, current, resolved, motion, depth, reactive, world_output]
           (RDG::RenderGraph &pass_graph, CommandBuffer *cmd, const RenderingData &)
        {
            Capture(*cmd, pass_graph.Resolve<Texture>(current), "current");
            Capture(*cmd, pass_graph.Resolve<Texture>(resolved), "resolved");
            Capture(*cmd, pass_graph.Resolve<Texture>(motion), "motion");
            Capture(*cmd, pass_graph.Resolve<Texture>(depth), "depth");
            Capture(*cmd, pass_graph.Resolve<Texture>(*world_output), "world");
            if (reactive.IsValid()) Capture(*cmd, pass_graph.Resolve<Texture>(reactive), "reactive");
            const auto jitter = _camera.GetJitter();
            _observations << _sample << ',' << _sample / 320u << ',' << (_sample / 64u) % 5u << ','
                << _sample % 64u << ',' << g_pGfxContext->GetFrameCount() << ','
                << _camera.OutputSize().x << ','
                << _camera.OutputSize().y << ',' << jitter.z << ',' << jitter.w << ',' << _position_x << ','
                << _yaw << ',' << g_pGfxContext->IsHardwareRayTracingSupported() << '\n';
            u32 ordinal = 0u;
            for (const auto &pass : pass_graph._debug_passes)
            {
                if (pass._name.find("RayTrace GI") != String::npos || pass._name == "Denoise" ||
                    pass._name.find("Motion") != String::npos || pass._name == "Debug GI")
                {
                    for (auto handle : pass._input_handles) LogHandle(pass_graph, ordinal, pass._name, "read", handle);
                    for (auto handle : pass._output_handles)
                        LogHandle(pass_graph, ordinal, pass._name, "write", handle);
                }
                ++ordinal;
            }
            _observations.flush(); _graph.flush();
            if (_sample % 32u == 0u) std::cout << "M7 " << _sample << '\n' << std::flush;
        });
    }

    bool Finished()
    {
        if (_failed) return true;
        if (_sample < kSampleCount) return false;
        bool completed = true;
        for (auto session : _sessions)
        {
            const auto status = ImageCaptureService::GetStatus(session);
            _failed |= status._state == EImageCaptureState::kFailed;
            completed &= status._state == EImageCaptureState::kCompleted;
        }
        return completed || _failed;
    }

    ~TaaRtgiAcceptance()
    {
        if (_rtgi) _rtgi->SetActive(false);
        for (auto session : _sessions) RenderGraphImageCaptureBridge::RemoveSelector(session);
    }

    bool _failed = false;
    static constexpr u32 kSampleCount = 640u;

private:
    void Select(const String &kind, const String &pass, bool output, u32 access)
    {
        ImageCaptureSequenceDesc desc;
        desc._output_directory = _output; desc._file_prefix = std::to_string(_sample) + "_" + kind;
        const auto session = ImageCaptureService::BeginSequence(desc);
        RenderGraphCaptureSelector selector;
        selector._pass_name = pass; selector._capture_output = output; selector._input_access_index = access;
        RenderGraphImageCaptureBridge::SetSelector(session, selector);
        _sessions.push_back(session);
        _selectors.push_back(session);
    }

    void Capture(CommandBuffer &cmd, Texture *texture, const String &kind)
    {
        ImageCaptureSequenceDesc desc;
        desc._output_directory = _output; desc._file_prefix = std::to_string(_sample) + "_" + kind;
        ImageCaptureTextureDesc source;
        source._debug_name = desc._file_prefix;
        _sessions.push_back(ImageCaptureService::CaptureOnce(cmd, texture, source, desc));
    }

    void LogHandle(RDG::RenderGraph &graph, u32 ordinal, const String &pass, const char *access, RDG::RGHandle handle)
    {
        _graph << _sample << ',' << ordinal << ',' << pass << ',' << access << ',' << handle._id << ','
            << handle._version << ',' << graph.GetResourceName(handle) << ',' << graph.Resolve<Texture>(handle) << '\n';
    }

    WString _output;
    Camera &_camera;
    TemporalAA *_taa = nullptr;
    RayTraceGI *_rtgi = nullptr;
    Ref<ComputeShader> _reconstruction;
    Ref<RenderTexture> _world;
    ComputeShaderKernelId _reconstruction_kernel = kInvalidComputeShaderKernelId;
    std::ofstream _observations;
    std::ofstream _graph;
    Vector<ImageCaptureSessionId> _sessions;
    Vector<ImageCaptureSessionId> _selectors;
    u32 _sample = kSampleCount;
    f32 _position_x = 0.f;
    f32 _yaw = 0.f;
};
