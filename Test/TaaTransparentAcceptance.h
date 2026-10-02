#pragma once

#include "Render/2D/Sprite.h"
#include "Framework/Common/ResourceMgr.h"

// GizmoPass uses per-object DrawMesh for this shader. Exercise that binding before TAA as the M5 stress case.
class TaaM5BillboardPass final : public RenderPass
{
public:
    TaaM5BillboardPass() : RenderPass("M5BillboardRaster") { _event = static_cast<ERenderPassEvent>(480u); }
    void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &data) override
    {
        if (!_enabled)
            return;
        const auto input_color = data._rg_handles._color_target;
        const auto depth = data._rg_handles._depth_target;
        auto output_color = std::make_shared<RDG::RGHandle>();
        const auto matrix = _matrix;
        graph.AddPass("M5BillboardRaster", {}, [input_color, depth, output_color](RDG::RenderGraphBuilder &builder)
        {
            builder.Read(input_color, EResourceUsage::kWriteRTV);
            builder.Read(depth, EResourceUsage::kDSV);
            *output_color = builder.Write(input_color);
        }, [this, output_color, depth, matrix](RDG::RenderGraph &pass_graph, CommandBuffer *cmd, const RenderingData &)
        {
            auto *color_target = pass_graph.Resolve<RenderTexture>(*output_color);
            auto *depth_target = pass_graph.Resolve<RenderTexture>(depth);
            cmd->SetRenderTargetLoadAction(color_target, ELoadStoreAction::kLoad);
            cmd->SetRenderTargetLoadAction(depth_target, ELoadStoreAction::kLoad);
            cmd->SetRenderTarget(color_target, depth_target);
            cmd->DrawMesh(_mesh.get(), _material.get(), matrix);
        });
        data._rg_handles._color_target = *output_color;
    }
    Ref<Mesh> _mesh;
    Ref<Material> _material;
    Matrix4x4f _matrix;
    bool _enabled = false;
};

// Production passes and Renderer-owned TAA in an unsaved scene. No substitute transparent raster or TAA shader.
class TaaTransparentAcceptance final : public RenderFeature, public RenderPass
{
public:
    TaaTransparentAcceptance(const WString &output, Camera &camera, Scene &scene, Renderer &renderer)
        : RenderFeature("M5Acceptance"), RenderPass("M5Capture"), _camera(camera), _scene(scene), _output(output)
    {
        _event = static_cast<ERenderPassEvent>(551u);
        _observations.open(std::filesystem::path(output) / "m5_observations.csv");
        _observations << "sample,kind,phase,engine_frame,jitter_x,jitter_y,"
                         "previous_jitter_x,previous_jitter_y,x,factor\n";
        for (auto *feature : renderer.GetFeatures())
        {
            if (feature->Name() == "TemporalAA")
            {
                _taa = dynamic_cast<TemporalAA *>(feature);
                feature->SetActive(true);
            }
            if (feature->Name() == "SpriteRenderFeature")
                feature->SetActive(true);
        }
        auto dark = Material::CreateStandard("M5Background");
        dark->SetCullMode(ECullMode::kNone);
        dark->SetVector("_AlbedoValue", Vector4f(0.f, 0.f, 0.f, 1.f));
        dark->SetVector("_EmissionValue", Vector4f(.04f, .04f, .04f, 1.f));
        auto bright = Material::CreateStandard("M5BackgroundRails");
        bright->SetCullMode(ECullMode::kNone);
        bright->SetVector("_AlbedoValue", Vector4f(0.f, 0.f, 0.f, 1.f));
        bright->SetVector("_EmissionValue", Vector4f(.8f, .8f, .8f, 1.f));
        scene.AddObject(CreateTaaSceneMesh(true), dark);
        scene.AddObject(CreateTaaSceneMesh(false), bright);

        auto texture = Texture2D::Create(16u, 16u, ETextureFormat::kRGBA32);
        Vector<u8> pixels(16u * 16u * 4u, 255u);
        for (u32 y = 0u; y < 16u; ++y)
            for (u32 x = 0u; x < 16u; ++x)
                pixels[(y * 16u + x) * 4u + 3u] = ((x / 4u + y / 4u) % 2u) ? 0u : 255u;
        texture->SetPixelData(pixels.data(), 0u, 0u);
        texture->ApplySync();
        _texture = texture;
        auto &assets = ResourceMgr::Get().GetMutableAssetRegistry();
        _texture_guid = Guid::Generate();
        const auto texture_handle = assets.GetOrCreateHandle<Texture2D>(_texture_guid);
        _failed |= !assets.Publish(texture_handle, texture, {});
        auto sprite = MakeRef<Sprite>("M5RuntimeSprite");
        sprite->_texture.SetGuid(_texture_guid);
        sprite->_size = 1.3f;
        _sprite_guid = Guid::Generate();
        const auto sprite_handle = assets.GetOrCreateHandle<Sprite>(_sprite_guid);
        _failed |= !assets.Publish(sprite_handle, sprite, {});

        const auto mesh = CreateQuad();
        auto forward = MakeRef<Material>(ResourceMgr::Get().Get<Shader>(L"Shaders/hlsl/forwardlit.alasset"), "M5Blend");
        forward->SetCullMode(ECullMode::kNone);
        forward->SetVector("_AlbedoValue", Vector4f(0.f, 0.f, 0.f, .3f));
        forward->SetVector("_EmissionValue", Vector4f(.7f, .15f, .08f, 1.f));
        _forward_material = forward;
        _entities[0] = scene.AddObject(mesh, forward);
        _entities[1] = scene.AddObject("M5Sprite");
        auto &component = scene.GetRegister().AddComponent<ECS::SpriteRendererComponent>(_entities[1]);
        component._sprite_guid = component._sprite_handle_guid = _sprite_guid;
        component._sprite_handle = sprite_handle;
        component._color = Color(.9f, .1f, .05f, .65f);
        auto billboard_shader = ResourceMgr::Get().Get<Shader>(L"Shaders/hlsl/billboard.alasset");
        auto billboard = MakeRef<Material>(billboard_shader, "M5Billboard");
        billboard->SetTexture("_MainTex", texture.get());
        _entities[2] = scene.AddObject("M5BillboardTransform");
        _billboard_pass._mesh = mesh;
        _billboard_pass._material = billboard;
        auto cutout = Material::CreateStandard("M5Cutout");
        cutout->SetCullMode(ECullMode::kNone);
        cutout->EnableKeyword("ALPHA_TEST");
        cutout->SetTexture("_AlbedoTex", texture.get());
        cutout->SetInt("_SamplerMask", 1);
        cutout->SetFloat("_AlphaCulloff", .5f);
        cutout->SetVector("_AlbedoValue", Vector4f(1.f, 1.f, 1.f, 1.f));
        cutout->SetVector("_EmissionValue", Vector4f(.7f, .15f, .08f, 1.f));
        _entities[3] = scene.AddObject(mesh, cutout);
        _failed |= _taa == nullptr || sprite->_texture.Handle().Resolve() == nullptr;
        for (const auto entity : _entities)
            scene.SetEntityEnabled(entity, false);
    }

    void AddRenderPasses(Renderer &renderer, const RenderingData &) override
    {
        renderer.EnqueuePass(&_billboard_pass);
        renderer.EnqueuePass(this);
    }

    void Prepare(u32 sample)
    {
        _sample = sample;
        if (sample >= kSampleCount)
            return;
        const u32 kind = sample / 112u;
        const u32 local = sample % 112u;
        const u32 entity_kinds[]{0u, 1u, 2u, 3u, 0u, 1u, 2u};
        if (local == 0u)
        {
            _camera.Type(kind == 6u ? ECameraType::kOrthographic : ECameraType::kPerspective);
            _camera.Size(8.32f);
            _camera.MarkTemporalHistoryReset();
            _forward_material->SetVector("_EmissionValue", kind == 4u ? Vector4f(.7f, .7f, .7f, 1.f)
                                                                     : Vector4f(.7f, .15f, .08f, 1.f));
            auto *sprite = _scene.GetRegister().GetComponent<ECS::SpriteRendererComponent>(_entities[1]);
            sprite->_color = kind == 5u ? Color(.9f, .9f, .9f, .65f) : Color(.9f, .1f, .05f, .65f);
        }
        // 16 background control frames, 32 static, 32 at 3 pixels/frame, then 32 disappearance frames.
        _x = (-48.f + (local >= 48u ? 3.f * static_cast<f32>(std::min(local - 47u, 32u)) : 0.f)) /
             (180.f * .866025403784f / 4.f);
        for (u32 index = 0u; index < 4u; ++index)
        {
            _scene.SetEntityEnabled(_entities[index], index == entity_kinds[kind] && local >= 16u && local < 80u);
            auto *transform = _scene.GetRegister().GetComponent<ECS::TransformComponent>(_entities[index]);
            transform->SetLocalPosition({_x, 0.f, -1.f});
        }
        _billboard_pass._enabled = entity_kinds[kind] == 2u && local >= 16u && local < 80u;
        _billboard_pass._matrix = MatrixTranslation(Vector3f(_x, 0.f, -1.f));
        _camera.MarkDirty();
        _camera.RecalculateMatrix();
    }

    void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &data) override
    {
        if (_sample >= kSampleCount)
            return;
        const auto current = data._rg_handles._color_tex;
        const auto output = data._rg_handles._taa_color;
        const auto motion = data._rg_handles._motion_vector_tex;
        const auto reactive = data._rg_handles._taa_reactive;
        const auto jitter = _camera.GetJitter();
        const auto previous = _previous_jitter;
        const auto sample = _sample;
        _previous_jitter = jitter;
        _failed |= !output.IsValid() || !reactive.IsValid();
        graph.AddPass("M5Capture", {}, [current, output, motion, reactive](RDG::RenderGraphBuilder &builder)
        {
            builder.Read(current);
            builder.Read(output);
            builder.Read(motion);
            builder.Read(reactive);
        }, [this, current, output, motion, reactive, sample, jitter, previous]
           (RDG::RenderGraph &pass_graph, CommandBuffer *cmd, const RenderingData &)
        {
            Capture(*cmd, pass_graph.Resolve<Texture>(current), sample, "current");
            Capture(*cmd, pass_graph.Resolve<Texture>(output), sample, "resolved");
            Capture(*cmd, pass_graph.Resolve<Texture>(motion), sample, "motion");
            Capture(*cmd, pass_graph.Resolve<Texture>(reactive), sample, "reactive");
            const u32 local = sample % 112u;
            const String phase = local < 16u ? "background" : local < 48u ? "static" : local < 80u ? "motion" : "gone";
            const char *names[]{"blend", "sprite", "billboard", "cutout", "grayblend", "graysprite", "orthobillboard"};
            _observations << sample << ',' << names[sample / 112u] << ',' << phase << ','
                          << g_pGfxContext->GetFrameCount() << ',' << jitter.z << ',' << jitter.w << ',' << previous.z
                          << ',' << previous.w << ',' << _x << ',' << _taa->_history_factor << '\n';
            _observations.flush();
            if (sample % 16u == 0u)
                std::cout << "M5 " << sample << ' ' << names[sample / 112u] << ' ' << phase << '\n' << std::flush;
        });
    }

    bool Finished()
    {
        if (_sample < kSampleCount)
            return false;
        bool completed = true;
        for (const auto session : _sessions)
        {
            const auto status = ImageCaptureService::GetStatus(session);
            _failed |= status._state == EImageCaptureState::kFailed;
            completed &= status._state == EImageCaptureState::kCompleted;
        }
        return completed || _failed;
    }

    ~TaaTransparentAcceptance()
    {
        auto &assets = ResourceMgr::Get().GetMutableAssetRegistry();
        assets.Unregister(_sprite_guid);
        assets.Unregister(_texture_guid);
    }

    bool _failed = false;
    static constexpr u32 kSampleCount = 784u;

private:
    static Ref<Mesh> CreateQuad()
    {
        auto mesh = MakeRef<Mesh>("M5Quad");
        mesh->SetVertices({{-.65f, -.65f, 0.f}, {-.65f, .65f, 0.f}, {.65f, .65f, 0.f}, {.65f, -.65f, 0.f}});
        mesh->SetNormals(Vector<Vector3f>(4u, Vector3f(0.f, 0.f, -1.f)));
        mesh->SetTangents(Vector<Vector4f>(4u, Vector4f(1.f, 0.f, 0.f, 1.f)));
        mesh->SetUVs({{0.f, 1.f}, {0.f, 0.f}, {1.f, 0.f}, {1.f, 1.f}});
        mesh->SetVerticesCount(4u);
        mesh->AddSubmesh({0u, 1u, 2u, 0u, 2u, 3u});
        mesh->SetBounds(Vector<AABB>(2u, AABB(Vector3f(-.65f, -.65f, -.01f), Vector3f(.65f, .65f, .01f))));
        mesh->Apply();
        return mesh;
    }

    void Capture(CommandBuffer &cmd, Texture *texture, u32 sample, const String &kind)
    {
        ImageCaptureSequenceDesc sequence;
        sequence._output_directory = _output;
        sequence._file_prefix = std::to_string(sample) + "_" + kind;
        ImageCaptureTextureDesc source;
        source._debug_name = sequence._file_prefix;
        _sessions.push_back(ImageCaptureService::CaptureOnce(cmd, texture, source, sequence));
    }

    Camera &_camera;
    Scene &_scene;
    WString _output;
    TemporalAA *_taa = nullptr;
    Ref<Texture2D> _texture;
    Ref<Material> _forward_material;
    TaaM5BillboardPass _billboard_pass;
    Guid _texture_guid;
    Guid _sprite_guid;
    ECS::Entity _entities[4]{};
    std::ofstream _observations;
    Vector<ImageCaptureSessionId> _sessions;
    Vector4f _previous_jitter = Vector4f::kZero;
    u32 _sample = kSampleCount;
    f32 _x = 0.f;
};
