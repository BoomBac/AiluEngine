#pragma once

#include "Render/Features/VolumetricClouds.h"
#include "Render/ImageCapture/Adapters/RenderGraphImageCaptureBridge.h"
#include "Framework/Common/TimeMgr.h"
#include <iomanip>

// Exercise the production feature through AddFeature; never change default Renderer registration or save a scene.
class TaaCloudAcceptance final : public RenderFeature, public RenderPass
{
public:
    TaaCloudAcceptance(const WString &output, Camera &camera, Renderer &renderer)
        : RenderFeature("M6Acceptance"), RenderPass("M6Capture"), _output(output), _camera(camera)
    {
        _event = static_cast<ERenderPassEvent>(551u);
        _observations.open(std::filesystem::path(output) / "m6_observations.csv");
        _observations << std::setprecision(9);
        _observations << "sample,combination,local,engine_frame,width,height,jitter_x,jitter_y,position_x,yaw,enabled\n";
        _graph.open(std::filesystem::path(output) / "m6_graph.csv");
        _graph << "sample,ordinal,pass,access,id,version,resource,texture\n";
        for (auto *feature : renderer.GetFeatures())
            if (feature->Name() == "TemporalAA")
                _taa = dynamic_cast<TemporalAA *>(feature);
        _failed = _taa == nullptr;
        _cloud = MakeScope<VolumetricClouds>();
        _cloud->SetActive(false);
        _cloud->_speed = 0.f;
        renderer.AddFeature(_cloud.get());
    }

    void AddRenderPasses(Renderer &renderer, const RenderingData &) override { renderer.EnqueuePass(this); }

    void Prepare(u32 sample)
    {
        for (const auto session : _sessions)
            if (ImageCaptureService::GetStatus(session)._submitted_frame_count != 0u)
                RenderGraphImageCaptureBridge::RemoveSelector(session);
        _sample = sample;
        if (sample >= kSampleCount) return;
        const u32 combination = sample / 96u;
        const u32 local = sample % 96u;
        _cloud->_is_tile_render = (combination & 1u) != 0u;
        _taa->SetActive((combination & 2u) != 0u);
        _camera._anti_aliasing = (combination & 2u) ? EAntiAliasing::kTAA : EAntiAliasing::kNone;
        _enabled = !(combination == 3u && local == 84u);
        _cloud->SetActive(_enabled);
        _position_x = local < 32u ? 0.f : local < 64u ? (local - 31u) * .02f : .64f;
        _yaw = local < 64u ? 0.f : (local - 63u) * .4f;
        if (combination == 3u && local >= 80u)
        {
            _position_x = local >= 88u ? 20.f : 0.f;
            _yaw = 0.f;
        }
        const bool resized = combination == 3u && local >= 80u && local < 82u;
        _camera.OutputSize(resized ? 317u : 320u, resized ? 179u : 180u);
        _camera.Position({_position_x, 0.f, -5.f});
        _camera.Rotation(Quaternion::EulerAngles(-25.f, _yaw, 0.f));
        if (local == 0u || (combination == 3u && local == 92u)) _camera.MarkTemporalHistoryReset();
        _camera.MarkDirty();
        _camera.RecalculateMatrix();
        TimeMgr::TickTimeSinceLoad = 0.f;
        if (_enabled)
        {
            Select("cloud", "VolumetricClouds_Compose", false, 0u);
            Select("sparse", "VolumetricClouds_Compute", true, 0u);
        }
    }

    void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &data) override
    {
        if (_sample >= kSampleCount) return;
        graph._is_debug = true;
        const auto current = data._rg_handles._color_tex;
        const auto resolved = data._rg_handles._taa_color.IsValid() ? data._rg_handles._taa_color : current;
        const auto motion = data._rg_handles._motion_vector_tex;
        const auto reactive = data._rg_handles._taa_reactive;
        graph.AddPass("M6Capture", {}, [current, resolved, motion, reactive](RDG::RenderGraphBuilder &builder)
        {
            builder.Read(current);
            builder.Read(resolved);
            builder.Read(motion);
            if (reactive.IsValid()) builder.Read(reactive);
        }, [this, current, resolved, motion, reactive]
           (RDG::RenderGraph &pass_graph, CommandBuffer *cmd, const RenderingData &)
        {
            Capture(*cmd, pass_graph.Resolve<Texture>(current), "current");
            Capture(*cmd, pass_graph.Resolve<Texture>(resolved), "resolved");
            Capture(*cmd, pass_graph.Resolve<Texture>(motion), "motion");
            if (reactive.IsValid()) Capture(*cmd, pass_graph.Resolve<Texture>(reactive), "reactive");
            const auto jitter = _camera.GetJitter();
            _observations << _sample << ',' << _sample / 96u << ',' << _sample % 96u << ','
                          << g_pGfxContext->GetFrameCount() << ',' << _camera.OutputSize().x << ','
                          << _camera.OutputSize().y << ',' << jitter.z << ',' << jitter.w << ','
                          << _position_x << ',' << _yaw << ',' << _enabled << '\n';
            u32 ordinal = 0u;
            for (const auto &pass : pass_graph._debug_passes)
            {
                if (pass._name.find("Motion") != String::npos || pass._name.find("VolumetricClouds") != String::npos ||
                    pass._name.find("Sky") != String::npos || pass._name.find("Atmosphere") != String::npos)
                {
                    for (const auto handle : pass._input_handles) LogHandle(pass_graph, ordinal, pass._name, "read", handle);
                    for (const auto handle : pass._output_handles) LogHandle(pass_graph, ordinal, pass._name, "write", handle);
                }
                ++ordinal;
            }
            _observations.flush();
            _graph.flush();
            if (_sample % 16u == 0u) std::cout << "M6 " << _sample << '\n' << std::flush;
        });
    }

    bool Finished()
    {
        if (_sample < kSampleCount) return false;
        bool completed = true;
        for (const auto session : _sessions)
        {
            const auto status = ImageCaptureService::GetStatus(session);
            _failed |= status._state == EImageCaptureState::kFailed;
            completed &= status._state == EImageCaptureState::kCompleted;
        }
        return completed || _failed;
    }

    ~TaaCloudAcceptance()
    {
        _cloud->SetActive(false);
        for (const auto session : _sessions) RenderGraphImageCaptureBridge::RemoveSelector(session);
    }

    bool _failed = false;
    static constexpr u32 kSampleCount = 384u;

private:
    void Select(const String &kind, const String &pass, bool output, u32 access)
    {
        ImageCaptureSequenceDesc desc;
        desc._output_directory = _output;
        desc._file_prefix = std::to_string(_sample) + "_" + kind;
        const auto session = ImageCaptureService::BeginSequence(desc);
        RenderGraphCaptureSelector selector;
        selector._pass_name = pass;
        selector._capture_output = output;
        selector._input_access_index = access;
        RenderGraphImageCaptureBridge::SetSelector(session, selector);
        _sessions.push_back(session);
    }

    void Capture(CommandBuffer &cmd, Texture *texture, const String &kind)
    {
        ImageCaptureSequenceDesc desc;
        desc._output_directory = _output;
        desc._file_prefix = std::to_string(_sample) + "_" + kind;
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
    Scope<VolumetricClouds> _cloud;
    std::ofstream _observations;
    std::ofstream _graph;
    Vector<ImageCaptureSessionId> _sessions;
    u32 _sample = kSampleCount;
    f32 _position_x = 0.f;
    f32 _yaw = 0.f;
    bool _enabled = true;
};
