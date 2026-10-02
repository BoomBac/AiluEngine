#pragma once

// Real deferred geometry and the Renderer-owned TemporalAA feature; no direct history invalidation calls.
class TaaSceneAcceptancePass final : public RenderPass
{
public:
    TaaSceneAcceptancePass(const WString &output, Camera &camera) : RenderPass("M3SceneCapture"), _camera(camera), _output(output)
    {
        _event = static_cast<ERenderPassEvent>(551u);
        _observations.open(std::filesystem::path(output) / "scene_observations.csv");
        _observations << "sample,phase,engine_frame,width,height,jitter_x,jitter_y,feature_active,aa_mode,factor,"
                         "expected_current_only,output_id,forward_x,forward_y,forward_z\n";
    }

    void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &data) override
    {
        if (_sample >= _total_samples)
            return;
        const auto current = data._rg_handles._color_tex;
        const auto resolved = data._rg_handles._taa_history_color;
        const auto motion = data._rg_handles._motion_vector_tex;
        const bool expected_resolve = _taa->IsActive() && _camera._anti_aliasing == EAntiAliasing::kTAA;
        if (resolved.IsValid() != expected_resolve)
            _failed = true;
        const Vector4f jitter = _camera.GetJitter();
        const String phase = _phase;
        const u32 sample = _sample;
        graph.AddPass(_name, {}, [current, resolved, motion](RDG::RenderGraphBuilder &builder)
        {
            builder.Read(current);
            builder.Read(motion);
            if (resolved.IsValid())
                builder.Read(resolved);
        }, [this, current, resolved, motion, phase, sample, jitter](RDG::RenderGraph &pass_graph, CommandBuffer *cmd,
                                                                 const RenderingData &rendering_data)
        {
            Capture(*cmd, pass_graph.Resolve<Texture>(current), sample, "current");
            Capture(*cmd, pass_graph.Resolve<Texture>(motion), sample, "motion");
            Texture *output = resolved.IsValid() ? pass_graph.Resolve<Texture>(resolved) : nullptr;
            if (output)
                Capture(*cmd, output, sample, "resolved");
            _observations << sample << ',' << phase << ',' << g_pGfxContext->GetFrameCount() << ','
                          << rendering_data._width << ',' << rendering_data._height << ',' << jitter.z << ',' << jitter.w
                          << ',' << _taa->IsActive() << ',' << static_cast<u32>(_camera._anti_aliasing) << ','
                          << _taa->_history_factor << ',' << _expected_current_only << ',' << (output ? output->ID() : 0u)
                          << ',' << _camera.Forward().x << ',' << _camera.Forward().y << ',' << _camera.Forward().z << '\n';
            _observations.flush();
            std::cout << "M3_SCENE " << sample << ' ' << phase << " resolved=" << (output != nullptr) << '\n';
        });
    }

    void Prepare(u32 sample)
    {
        _sample = sample;
        _expected_current_only = false;
        _phase = sample == 0u ? "first" : sample < 128u ? "static" : "accumulate";
        if (sample == 0u)
        {
            _taa->SetActive(true);
            _expected_current_only = true;
        }
        if (sample >= 128u && sample < 136u)
            _phase = "feature_off";
        if (sample == 128u)
        {
            _taa->SetActive(false);
            _bright_material->SetVector("_EmissionValue", Vector4f(.2f, .2f, .2f, 1.f));
        }
        if (sample == 136u)
        {
            _phase = "feature_on";
            _taa->SetActive(true);
            _expected_current_only = true;
        }
        if (sample >= 152u && sample < 160u)
            _phase = "aa_off";
        if (sample == 152u)
        {
            _camera._anti_aliasing = EAntiAliasing::kNone;
            _bright_material->SetVector("_EmissionValue", Vector4f(.65f, .65f, .65f, 1.f));
        }
        if (sample == 160u)
        {
            _phase = "aa_on";
            _camera._anti_aliasing = EAntiAliasing::kTAA;
            _expected_current_only = true;
        }
        if (sample >= 176u && sample < 184u)
        {
            _phase = "factor0";
            _taa->_history_factor = 0.f;
            _expected_current_only = true;
        }
        if (sample == 184u)
            _taa->_history_factor = .95f;
        if (sample == 192u)
        {
            _phase = "cut";
            _camera.Position({2.f, 0.f, -5.f});
            _camera.MarkTemporalHistoryReset();
            _expected_current_only = true;
        }
        if (sample >= 208u && sample < 214u)
        {
            _phase = "resize";
            _camera.OutputSize(sample % 2u == 0u ? 257u : 320u, sample % 2u == 0u ? 137u : 180u);
            _expected_current_only = true;
        }
        if (sample == 214u)
        {
            _phase = "fov";
            _camera.FovH(70.f);
            _expected_current_only = true;
        }
        if (sample == 230u)
        {
            _phase = "rotation";
            _camera.Rotation(Quaternion(0.f, .707106781187f, 0.f, .707106781187f));
            _camera.MarkTemporalHistoryReset();
            _expected_current_only = true;
        }
        if (sample == 246u)
        {
            _phase = "projection";
            _camera.Type(ECameraType::kOrthographic);
            _camera.Size(10.f);
            _camera.Position({0.f, 0.f, -5.f});
            _camera.Rotation(Quaternion());
            _camera.MarkTemporalHistoryReset();
            _expected_current_only = true;
        }
        if (_total_samples > 262u && sample >= 262u)
        {
            if (sample == 262u)
            {
                _phase = "control_reset";
                _camera.Type(ECameraType::kPerspective);
                _camera.FovH(60.f);
                _camera.Position({0.f, 0.f, -5.f});
                _camera.Rotation(Quaternion());
                _camera.MarkTemporalHistoryReset();
                _bright_material->SetVector("_EmissionValue", Vector4f(.8f, .8f, .8f, 1.f));
                _expected_current_only = true;
            }
            if (sample >= 278u && sample < 286u)
            {
                _phase = "color_drop";
                _bright_material->SetVector("_EmissionValue", Vector4f(.2f, .2f, .2f, 1.f));
            }
            if (sample == 286u)
                _bright_material->SetVector("_EmissionValue", Vector4f(.8f, .8f, .8f, 1.f));
            if (sample >= 294u && sample < 302u)
            {
                _phase = "rigid_motion";
                _rails->SetLocalPosition({.4f * static_cast<f32>(sample - 293u), 0.f, 0.f});
            }
            if (sample == 302u)
                _rails->SetLocalPosition({0.f, 0.f, 0.f});
            if (sample >= 310u && sample < 318u)
            {
                _phase = "occluded";
                _occluder->SetLocalPosition({0.f, 0.f, -1.f});
            }
            if (sample >= 318u)
            {
                _phase = "disoccluded";
                _occluder->SetLocalPosition({0.f, 0.f, 20.f});
            }
        }
        _camera.MarkDirty();
        _camera.RecalculateMatrix();
    }

    bool Finished()
    {
        if (_sample < _total_samples)
            return false;
        bool complete = true;
        for (const auto session : _sessions)
        {
            const auto status = ImageCaptureService::GetStatus(session);
            _failed |= status._state == EImageCaptureState::kFailed;
            complete &= status._state == EImageCaptureState::kCompleted;
        }
        return complete || _failed;
    }

    TemporalAA *_taa = nullptr;
    Ref<Material> _bright_material;
    ECS::TransformComponent *_rails = nullptr;
    ECS::TransformComponent *_occluder = nullptr;
    u32 _total_samples = 262u;
    bool _failed = false;

private:
    void Capture(CommandBuffer &cmd, Texture *texture, u32 sample, const String &kind)
    {
        ImageCaptureSequenceDesc sequence;
        sequence._output_directory = _output;
        sequence._file_prefix = std::to_string(sample) + "_" + kind;
        ImageCaptureTextureDesc source;
        source._debug_name = sequence._file_prefix;
        _sessions.emplace_back(ImageCaptureService::CaptureOnce(cmd, texture, source, sequence));
    }
    Camera &_camera;
    WString _output;
    std::ofstream _observations;
    Vector<ImageCaptureSessionId> _sessions;
    u32 _sample = 0u;
    String _phase;
    bool _expected_current_only = false;
};

class TaaSceneAcceptanceFeature final : public RenderFeature
{
public:
    TaaSceneAcceptanceFeature(const WString &output, Camera &camera)
        : RenderFeature("M3SceneAcceptance"), _pass(output, camera) {}
    void AddRenderPasses(Renderer &renderer, const RenderingData &) override { renderer.EnqueuePass(&_pass); }
    TaaSceneAcceptancePass _pass;
};

static Ref<Mesh> CreateTaaSceneMesh(bool background)
{
    auto mesh = MakeRef<Mesh>(background ? "M3DarkBackground" : "M3ThinRailsAndDiagonal");
    Vector<Vector3f> vertices;
    Vector<u32> indices;
    const f32 focal = 180.f * .866025403784f / 5.f;
    const auto add_quad = [&](f32 left, f32 right, f32 top, f32 bottom, f32 slope, f32 z)
    {
        const u32 base = static_cast<u32>(vertices.size());
        const auto position = [&](f32 x, f32 y) { return Vector3f((x - 160.f) / focal, (90.f - y) / focal, z); };
        vertices.insert(vertices.end(), {position(left + slope, bottom), position(left, top), position(right, top),
                                         position(right + slope, bottom)});
        indices.insert(indices.end(), {base, base + 1u, base + 2u, base, base + 2u, base + 3u});
    };
    if (background)
        add_quad(-300.f, 600.f, -300.f, 500.f, 0.f, .5f);
    else
    {
        const f32 widths[]{.4f, .8f, 1.4f, 2.4f, 4.f};
        for (u32 index = 0u; index < 5u; ++index)
            add_quad(45.f + index * 28.f, 45.f + index * 28.f + widths[index], 22.f, 158.f, 22.f, 0.f);
        add_quad(206.f, 243.f, 22.f, 158.f, 50.f, 0.f);
    }
    const u32 count = static_cast<u32>(vertices.size());
    mesh->SetVertices(std::move(vertices));
    mesh->SetNormals(Vector<Vector3f>(count, Vector3f(0.f, 0.f, -1.f)));
    mesh->SetTangents(Vector<Vector4f>(count, Vector4f(1.f, 0.f, 0.f, 1.f)));
    mesh->SetUVs(Vector<Vector2f>(count, Vector2f(.5f, .5f)));
    mesh->SetVerticesCount(count);
    mesh->AddSubmesh(indices);
    const AABB bounds(Vector3f(-20.f, -20.f, -.01f), Vector3f(20.f, 20.f, 1.f));
    mesh->SetBounds(Vector<AABB>(2u, bounds));
    mesh->Apply();
    return mesh;
}
