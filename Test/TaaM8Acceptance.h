#pragma once

#include "Render/Features/DDGI.h"
#include "Render/Features/SurfelGI.h"
#include "Render/Features/VolumetricFog.h"
#include "Render/RenderingStates.h"
#include "Framework/Common/Profiler.h"
#include <chrono>
#include <iomanip>

// Production features, runtime-only geometry/components, and matched same-frame RG captures.
class TaaM8Acceptance final : public RenderFeature, public RenderPass
{
public:
    static constexpr u32 kCaseFrames = 128u;
    static constexpr u32 kCaseCount = 10u;
    static constexpr u32 kSampleCount = kCaseFrames * kCaseCount;

    TaaM8Acceptance(const WString &output, Camera &camera, Scene &scene, Renderer &renderer,
                    bool long_run = false, bool performance_only = false)
        : RenderFeature("M8Acceptance"), RenderPass("M8Capture"), _output(output), _camera(camera),
          _main_camera(16.f / 9.f, .1f, 100.f), _scene(scene), _renderer(renderer), _long_run(long_run),
          _performance_only(performance_only)
    {
        _event = static_cast<ERenderPassEvent>(551u);
        _observations.open(std::filesystem::path(output) / "m8_observations.csv");
        _observations << std::setprecision(9);
        _observations << "sample,case,local,mask,frame,width,height,camera,jitter_x,jitter_y,reset,aa,object_x,"
                         "elapsed,history_cameras,render_texture_bytes,ssao_active,fog_active,rtgi_active\n";
        _graph.open(std::filesystem::path(output) / "m8_graph.csv");
        _graph << "sample,ordinal,pass,access,id,version,resource,pointer\n";
        _performance.open(std::filesystem::path(output) / "m8_performance.csv");
        _performance << "sample,passes,resources,usages,cpu_record_ms,gpu_taa_ms,gpu_static_ms,gpu_dynamic_ms,"
                        "rg_compile_ms,aa\n";
        for (auto *feature : renderer.GetFeatures())
        {
            if (feature->Name() == "TemporalAA") _taa = dynamic_cast<TemporalAA *>(feature);
            if (feature->Name() == "VolumetricFog")
            {
                auto *fog = dynamic_cast<VolumetricFog *>(feature);
                fog->_fog_density = 1.f;
                fog->_height_falloff = 0.f;
                fog->_fog_distance = 30.f;
                fog->_temporal_reprojection = true;
                fog->_z_slice_count = 32u;
            }
            if (feature->Name() == "RayTraceGI")
            {
                auto *rtgi = dynamic_cast<RayTraceGI *>(feature);
                rtgi->_accumulation_mode = EPathTracerAccumulationMode::kRealtime;
                rtgi->_use_hardware_ray_tracing = false;
                rtgi->_enable_resampling = false;
                rtgi->_enable_ris = false;
            }
        }
        _failed = !_taa;
        _cloud = MakeScope<VolumetricClouds>();
        _cloud->_speed = 0.f;
        _cloud->SetActive(false);
        renderer.AddFeature(_cloud.get());

        auto &registry = scene.GetRegister();
        const auto environment = scene.AddObject("M8Environment");
        registry.AddComponent<ECS::EnvironmentComponent>(environment);
        const auto sun_entity = scene.AddObject("M8Sun");
        auto &sun = registry.AddComponent<ECS::LightComponent>(sun_entity);
        sun._type = ECS::ELightType::kDirectional;
        sun._light = {};
        sun._light._light_color = Color(1.f, 1.f, 1.f, 1.f);
        sun._light._light_param = Vector4f(1.f, 0.f, 0.f, 0.f);
        sun._shadow = {};
        registry.GetComponent<ECS::TransformComponent>(sun_entity)->SetLocalRotation(
            Quaternion::EulerAngles(25.f, 25.f, 0.f));

        const auto volume_entity = scene.AddObject("M8DDGIVolume");
        auto &volume = registry.AddComponent<ECS::DDGIVolumeComponent>(volume_entity);
        volume._probe_count = Vector3UInt(4u, 3u, 4u);
        volume._probe_spacing = Vector3f(2.f, 2.f, 2.f);
        volume._draw_probes = false;
        volume._freeze_ray_rotation = true;
        registry.GetComponent<ECS::TransformComponent>(volume_entity)->SetLocalPosition({0.f, 0.f, -2.f});
        const auto surfel_entity = scene.AddObject("M8SurfelSettings");
        auto &surfel = registry.AddComponent<ECS::SurfelGIComponent>(surfel_entity);
        surfel._debug_draw_surfels = false;
        surfel._max_surfel_count = 4096u;
        surfel._grid_resolution = 16u;
        surfel._cascade_count = 3u;
        surfel._max_spawn_per_frame = 256u;
        surfel._max_lighting_updates_per_frame = 512u;

        auto receiver = Material::CreateStandard("M8Receiver");
        receiver->SetCullMode(ECullMode::kNone);
        receiver->SetVector("_AlbedoValue", Vector4f(.65f, .65f, .65f, 1.f));
        receiver->SetVector("_EmissionValue", Vector4f(0.f, 0.f, 0.f, 1.f));
        scene.AddObject(Quad(4.f), receiver);
        auto emitter = Material::CreateStandard("M8Emitter");
        emitter->SetCullMode(ECullMode::kNone);
        emitter->SetVector("_AlbedoValue", Vector4f(.3f, .1f, .05f, 1.f));
        emitter->SetVector("_EmissionValue", Vector4f(3.f, .8f, .2f, 1.f));
        _object = scene.AddObject(Quad(.7f), emitter);
        registry.GetComponent<ECS::TransformComponent>(_object)->SetLocalPosition({-1.5f, 0.f, -1.f});
        auto floor = scene.AddObject(Quad(4.f), receiver);
        registry.GetComponent<ECS::TransformComponent>(floor)->SetLocalRotation(
            Quaternion::EulerAngles(90.f, 0.f, 0.f));
        registry.GetComponent<ECS::TransformComponent>(floor)->SetLocalPosition({0.f, -2.f, -1.f});
        auto occluder = scene.AddObject(Quad(.5f), receiver);
        registry.GetComponent<ECS::TransformComponent>(occluder)->SetLocalPosition({.6f, 0.f, -2.f});
        CreateSprite();
        _main_camera.SetLens(60.f, 16.f / 9.f, .1f, 100.f);
        _main_camera.Position({0.f, 0.f, -5.f});
        _main_camera.OutputSize(320u, 180u);
        _main_camera._anti_aliasing = EAntiAliasing::kTAA;
        Camera::sMain = &_main_camera;
        Camera::sScene = &_camera;
    }

    ~TaaM8Acceptance() override
    {
        Camera::sCurrent = Camera::sMain = Camera::sScene = &_camera;
        _cloud->SetActive(false);
    }

    void AddRenderPasses(Renderer &renderer, const RenderingData &) override { renderer.EnqueuePass(this); }

    void Prepare(u32 sample)
    {
        _sample = sample;
        _started = true;
        if (_long_run)
        {
            if (sample == 0u) _start = std::chrono::steady_clock::now();
            _elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - _start).count();
            if (_elapsed >= 180.0) { _finished = true; return; }
        }
        else if (sample >= kSampleCount) return;
        static constexpr u32 kMasks[]{0u, 1u, 2u, 4u, 8u, 14u, 16u, 32u, 64u, 127u};
        _mask = (_long_run || _performance_only) ? 95u : kMasks[sample / kCaseFrames];
        const u32 local = sample % kCaseFrames;
        _renderer._is_use_raytracing = (_mask & 32u) != 0u;
        for (auto *feature : _renderer.GetFeatures())
        {
            const auto &name = feature->Name();
            if (name == "SSAO") feature->SetActive((_mask & 1u) != 0u);
            if (name == "VolumetricFog") feature->SetActive((_mask & 2u) != 0u);
            if (name == "SurfelGI") feature->SetActive((_mask & 4u) != 0u);
            if (name == "DDGI") feature->SetActive((_mask & 8u) != 0u);
            if (name == "RayTraceGI") feature->SetActive((_mask & 32u) != 0u);
            if (name == "SpriteRenderFeature") feature->SetActive((_mask & 64u) != 0u);
        }
        _cloud->SetActive((_mask & 16u) != 0u && (_performance_only || local != 100u));
        _cloud->_is_tile_render = true;
        _taa->SetActive(local != 96u);
        if (_long_run && sample == 300u)
        {
            // Short-lived cameras reproduce history retirement separately from ordinary two-camera reuse.
            for (u32 index = 0u; index < 4u; ++index)
                _temporary_cameras.push_back(MakeScope<Camera>(16.f / 9.f, .1f, 100.f));
        }
        _view = local >= 112u ? &_main_camera : &_camera;
        if (_long_run && sample >= 300u && sample < 304u) _view = _temporary_cameras[sample - 300u].get();
        if (_long_run && sample == 305u) _temporary_cameras.clear();
        Camera::sCurrent = _view;
        _view->_anti_aliasing = local == 98u ? EAntiAliasing::kNone : EAntiAliasing::kTAA;
        _view->Type(local >= 104u && local < 112u ? ECameraType::kOrthographic : ECameraType::kPerspective);
        const bool resized = local == 88u || local == 89u;
        _view->OutputSize(resized ? 317u : 320u, resized ? 179u : 180u);
        const f32 position_x = local >= 48u && local < 64u ? (local - 47u) * .02f : 0.f;
        _view->Position({position_x + (local == 92u ? 5.f : 0.f), 0.f, -5.f});
        const f32 yaw = local >= 64u && local < 80u ? (local - 63u) * .3f : 0.f;
        const f32 orbit_yaw = _long_run && _elapsed >= 30.0 ? std::sin(static_cast<f32>(_elapsed) * .05f) * 8.f : yaw;
        _view->Rotation(Quaternion::EulerAngles(0.f, orbit_yaw, 0.f));
        _reset = local == 0u || local == 94u;
        if (_reset) _view->MarkTemporalHistoryReset();
        _view->MarkDirty();
        _view->RecalculateMatrix();
        if (_performance_only)
        {
            _view = &_camera;
            Camera::sCurrent = _view;
            _view->Type(ECameraType::kPerspective);
            _view->OutputSize(1088u, 576u);
            _view->Position({0.f, 0.f, -5.f});
            _view->Rotation(Quaternion::EulerAngles(0.f, 0.f, 0.f));
            _view->_anti_aliasing = (sample / 256u) % 2u ? EAntiAliasing::kTAA : EAntiAliasing::kNone;
            _taa->SetActive(true);
            _view->MarkDirty();
            _view->RecalculateMatrix();
        }
        _expected_seed = _reset || local == 88u || local == 90u || local == 92u || local == 93u ||
                         local == 97u || local == 99u || local == 104u || local == 112u;
        _object_x = !_performance_only && local >= 80u && local < 88u ? -1.5f + (local - 79u) * .3f : -1.5f;
        _scene.GetRegister().GetComponent<ECS::TransformComponent>(_object)->SetLocalPosition({_object_x, 0.f, -1.f});
        _scene.GetRegister().GetComponent<ECS::TransformComponent>(_sprite_entity)->SetLocalPosition(
            {!_performance_only && local >= 80u && local < 88u ? (local - 79u) * .07f : 0.f, .9f, -1.5f});
        TimeMgr::TickTimeSinceLoad = 0.f;
    }

    void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &data) override
    {
        if (!_started || _finished || (!_long_run && _sample >= kSampleCount)) return;
        graph._is_debug = true;
        const auto current = data._rg_handles._color_tex;
        const auto resolved = data._rg_handles._taa_color;
        const auto motion = data._rg_handles._motion_vector_tex;
        const auto reactive = data._rg_handles._taa_reactive;
        const bool expected = _taa->IsActive() && _view->_anti_aliasing == EAntiAliasing::kTAA;
        _failed |= expected != resolved.IsValid();
        graph.AddPass("M8Capture", {}, [current, resolved, motion, reactive](RDG::RenderGraphBuilder &builder)
        {
            builder.Read(current);
            builder.Read(motion);
            if (resolved.IsValid()) builder.Read(resolved);
            if (reactive.IsValid()) builder.Read(reactive);
        }, [this, current, resolved, motion, reactive](RDG::RenderGraph &pass_graph, CommandBuffer *cmd,
                                                       const RenderingData &rendering_data)
        {
            Capture(*cmd, pass_graph.Resolve<Texture>(current), "current");
            Capture(*cmd, pass_graph.Resolve<Texture>(motion), "motion");
            if (resolved.IsValid()) Capture(*cmd, pass_graph.Resolve<Texture>(resolved), "resolved");
            if (reactive.IsValid()) Capture(*cmd, pass_graph.Resolve<Texture>(reactive), "reactive");
            const auto jitter = _view->GetJitter();
            _observations << _sample << ',' << _sample / kCaseFrames << ',' << _sample % kCaseFrames << ',' << _mask
                          << ',' << g_pGfxContext->GetFrameCount() << ',' << rendering_data._width << ','
                          << rendering_data._height << ',' << (_view == &_main_camera ? 1 : 0) << ',' << jitter.z << ','
                          << jitter.w << ',' << _expected_seed << ',' << static_cast<u32>(_view->_anti_aliasing)
                          << ',' << _object_x << ',' << _elapsed << ',' << _taa->GetHistoryCameraCount() << ','
                          << RenderTexture::TotalGPUMemerySize();
            for (const auto &name : {"SSAO", "VolumetricFog", "RayTraceGI"})
                for (const auto *feature : _renderer.GetFeatures())
                    if (feature->Name() == name) _observations << ',' << feature->IsActive();
            _observations << '\n';
            _observations.flush();
            u32 ordinal = 0u;
            for (const auto &pass : pass_graph._debug_passes)
            {
                for (const auto handle : pass._input_handles)
                    LogHandle(pass_graph, ordinal, pass._name, "read", handle);
                for (const auto handle : pass._output_handles)
                    LogHandle(pass_graph, ordinal, pass._name, "write", handle);
                ++ordinal;
            }
            _graph.flush();
            if (_sample % 32u == 0u) std::cout << "M8 " << _sample << " mask=" << _mask << '\n' << std::flush;
        });
    }

    void AfterRender()
    {
        if (!_started || _finished || (!_long_run && _sample >= kSampleCount)) return;
        const auto stats = _renderer.GetRenderGraph().GetCompileStats();
        const auto &state = RenderingStates::RenderData();
        const auto gpu = Profiler::Get().GetPassGPUTime();
        const auto time = [&](const String &name)
        {
            const auto it = gpu.find(name);
            return it == gpu.end() ? 0.f : it->second;
        };
        f32 compile_time = 0.f;
        for (u32 index = 0u; index < Profiler::kMaxProfileNum; ++index)
        {
            const auto *profile = Profiler::Get().GetCPUProfileData(index);
            if (profile->Name == "RenderGraph::Compile") compile_time = profile->_time;
        }
        _performance << _sample << ',' << stats._compiled_pass_count << ',' << stats._resource_count << ','
                     << stats._usage_count << ',' << state.CpuRenderRecordingTimeMs << ',' << time("TemporalAA")
                     << ',' << time("StaticMotion") << ',' << time("DynamicMotion") << ',' << compile_time
                     << ',' << static_cast<u32>(_view->_anti_aliasing) << '\n';
        _performance.flush();
    }

    bool Finished()
    {
        if (_long_run ? !_finished : _sample < kSampleCount) return false;
        for (const auto session : _sessions)
        {
            const auto status = ImageCaptureService::GetStatus(session);
            if (status._state == EImageCaptureState::kFailed) _failed = true;
            if (status._state != EImageCaptureState::kFailed && status._state != EImageCaptureState::kCompleted)
                return false;
        }
        return true;
    }

    bool _failed = false;

private:
    static Ref<Mesh> Quad(f32 radius)
    {
        auto mesh = MakeRef<Mesh>("M8Quad");
        mesh->SetVertices({{-radius, -radius, 0.f}, {-radius, radius, 0.f},
                           {radius, radius, 0.f}, {radius, -radius, 0.f}});
        mesh->SetNormals(Vector<Vector3f>(4u, Vector3f(0.f, 0.f, -1.f)));
        mesh->SetTangents(Vector<Vector4f>(4u, Vector4f(1.f, 0.f, 0.f, 1.f)));
        mesh->SetUVs({{0.f, 0.f}, {0.f, 1.f}, {1.f, 1.f}, {1.f, 0.f}});
        mesh->SetVerticesCount(4u);
        mesh->AddSubmesh({0u, 1u, 2u, 0u, 2u, 3u});
        mesh->SetBounds(Vector<AABB>(2u, AABB(Vector3f(-radius, -radius, -.01f), Vector3f(radius, radius, .01f))));
        mesh->Apply();
        return mesh;
    }

    void CreateSprite()
    {
        auto texture = Texture2D::Create(8u, 8u, ETextureFormat::kRGBA32);
        Vector<u8> pixels(8u * 8u * 4u, 255u);
        texture->SetPixelData(pixels.data(), 0u, 0u);
        texture->ApplySync();
        const auto texture_guid = Guid::Generate();
        auto &assets = ResourceMgr::Get().GetMutableAssetRegistry();
        _failed |= !assets.Publish(assets.GetOrCreateHandle<Texture2D>(texture_guid), texture, {});
        auto sprite = MakeRef<Sprite>("M8Sprite");
        sprite->_texture.SetGuid(texture_guid);
        sprite->_size = .5f;
        const auto sprite_guid = Guid::Generate();
        const auto sprite_handle = assets.GetOrCreateHandle<Sprite>(sprite_guid);
        _failed |= !assets.Publish(sprite_handle, sprite, {});
        _sprite_entity = _scene.AddObject("M8SpriteEntity");
        auto &component = _scene.GetRegister().AddComponent<ECS::SpriteRendererComponent>(_sprite_entity);
        component._sprite_guid = component._sprite_handle_guid = sprite_guid;
        component._sprite_handle = sprite_handle;
        component._color = Color(.9f, .1f, .05f, .6f);
    }

    void Capture(CommandBuffer &cmd, Texture *texture, const String &kind)
    {
        if (_performance_only) return;
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
               << handle._version << ',' << graph.GetResourceName(handle) << ','
               << graph.Resolve<GpuResource>(handle) << '\n';
    }

    WString _output;
    Camera &_camera;
    Camera _main_camera;
    Camera *_view = nullptr;
    Scene &_scene;
    Renderer &_renderer;
    TemporalAA *_taa = nullptr;
    Scope<VolumetricClouds> _cloud;
    ECS::Entity _object = ECS::kInvalidEntity;
    ECS::Entity _sprite_entity = ECS::kInvalidEntity;
    std::ofstream _observations;
    std::ofstream _graph;
    std::ofstream _performance;
    Vector<ImageCaptureSessionId> _sessions;
    u32 _sample = kSampleCount;
    u32 _mask = 0u;
    f32 _object_x = 0.f;
    bool _reset = false;
    bool _expected_seed = false;
    bool _long_run = false;
    bool _performance_only = false;
    bool _finished = false;
    bool _started = false;
    double _elapsed = 0.0;
    std::chrono::steady_clock::time_point _start;
    Vector<Scope<Camera>> _temporary_cameras;
};
