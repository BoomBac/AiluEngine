#include "Framework/Common/Application.h"
#include "Framework/Common/EngineConfig.h"
#include "Animation/SkinningSystem.h"
#include "Framework/Common/CrashHandler.h"
#include "Framework/Common/Log.h"
#include "Render/Camera.h"
#include "Render/CommandBuffer.h"
#include "Render/GraphicsContext.h"
#include "Render/ImageCapture/ImageCaptureService.h"
#include "Render/Material.h"
#include "Render/Mesh.h"
#include "Render/RenderGraph/RenderGraph.h"
#include "Render/Renderer.h"
#include "Scene/Scene.h"
#include "RHI/DX12/D3DContext.h"
#include <d3d12sdklayers.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <windows.h>

using namespace Ailu;
using namespace Ailu::Render;
using namespace Ailu::SceneManagement;

static ID3D12InfoQueue *s_debug_queue = nullptr;

static LONG WINAPI RecordValidationFailure(EXCEPTION_POINTERS *exception)
{
    const auto code = exception->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != 0x87a)
        return EXCEPTION_CONTINUE_SEARCH;
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(exception->ExceptionRecord->ExceptionAddress), &module))
    {
        wchar_t module_path[MAX_PATH]{};
        GetModuleFileNameW(module, module_path, MAX_PATH);
        std::wcerr << L"Exception module: " << module_path << L'\n' << std::flush;
    }
    if (s_debug_queue != nullptr)
    {
        const auto count = s_debug_queue->GetNumStoredMessagesAllowedByRetrievalFilter();
        for (u64 index = count > 20 ? count - 20 : 0; index < count; ++index)
        {
            SIZE_T size = 0;
            s_debug_queue->GetMessage(index, nullptr, &size);
            Vector<u8> bytes(size);
            auto *message = reinterpret_cast<D3D12_MESSAGE *>(bytes.data());
            if (SUCCEEDED(s_debug_queue->GetMessage(index, message, &size)))
                std::cerr << "D3D12 " << message->ID << ": " << message->pDescription << '\n' << std::flush;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static u64 s_gpu_error_count = 0u;

static void CheckValidationMessages()
{
    if (s_debug_queue != nullptr)
    {
        for (u64 index = 0u; index < s_debug_queue->GetNumStoredMessagesAllowedByRetrievalFilter(); ++index)
        {
            SIZE_T size = 0u;
            s_debug_queue->GetMessage(index, nullptr, &size);
            Vector<u8> bytes(size);
            auto *message = reinterpret_cast<D3D12_MESSAGE *>(bytes.data());
            if (SUCCEEDED(s_debug_queue->GetMessage(index, message, &size)) &&
                (message->Severity == D3D12_MESSAGE_SEVERITY_ERROR || message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION))
            {
                ++s_gpu_error_count;
                std::cerr << "D3D12 " << message->ID << ": " << message->pDescription << '\n';
            }
        }
    }
}

// This executable changes only runtime objects. Its config and captures belong to the supplied output directory.
class AcceptanceApplication final : public Application
{
public:
    std::function<void()> _prepare_render;
    void Stop() { _state = EApplicationState::EApplicationState_Exit; }
    void Run()
    {
        bool first = true;
        while (State() != EApplicationState::EApplicationState_Exit)
        {
            GetWindow().OnUpdate();
            if (State() == EApplicationState::EApplicationState_Exit)
                break;
            _state = EApplicationState::EApplicationState_Running;
            BeginFrame();
            if (State() == EApplicationState::EApplicationState_Exit)
                break;
            if (first)
                std::cerr << "Updating resources\n" << std::flush;
            UpdateResources(1.f / 60.f);
            if (first)
                std::cerr << "Updating scene\n" << std::flush;
            UpdateScenes(1.f / 60.f);
            if (first)
                std::cerr << "Rendering frame\n" << std::flush;
            if (_prepare_render)
                _prepare_render();
            RenderFrame();
            RenderEditor();
            if (first)
                std::cerr << "Presenting frame\n" << std::flush;
            CheckValidationMessages();
            PresentFrame();
            EndFrame();
            first = false;
        }
    }
};

struct AcceptanceState
{
    WString _output;
    String _phase;
    ECS::SkinningSystem *_skinning = nullptr;
    bool _capture = false;
    Vector<ImageCaptureSessionId> _sessions;

    void Capture(CommandBuffer &cmd, Texture *texture, const String &suffix)
    {
        ImageCaptureSequenceDesc sequence;
        sequence._output_directory = _output;
        sequence._file_prefix = _phase + "_" + suffix;
        ImageCaptureTextureDesc source;
        source._debug_name = sequence._file_prefix;
        _sessions.push_back(ImageCaptureService::CaptureOnce(cmd, texture, source, sequence));
    }
};

class AcceptanceSkinningPass final : public RenderPass
{
public:
    explicit AcceptanceSkinningPass(AcceptanceState &state) : RenderPass("M2Skinning"), _state(state)
    {
        _event = static_cast<ERenderPassEvent>(1);
    }
    void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &data) override
    {
        if (_state._skinning != nullptr)
            _state._skinning->RecordRenderGraph(graph, data);
    }
private:
    AcceptanceState &_state;
};

class AcceptanceCapturePass final : public RenderPass
{
public:
    AcceptanceCapturePass(AcceptanceState &state, bool before)
        : RenderPass(before ? "M2DepthBefore" : "M2CaptureAfter"), _state(state), _before(before)
    {
        _event = static_cast<ERenderPassEvent>(before ? 449 : 451);
        _depth = RenderTexture::Create(320, 180, _name, ERenderTargetFormat::kRFloat);
    }

    void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &data) override
    {
        if (!_before && _state._skinning != nullptr)
            graph.AddVertexBufferReads(_state._skinning->GetRenderGraphOutputHandles());
        if (!_state._capture)
            return;
        const auto depth = data._rg_handles._depth_target;
        const auto motion = data._rg_handles._motion_vector_tex;
        const auto copy = graph.Import(_depth.get());
        graph.AddPass(_name, {}, [=](RDG::RenderGraphBuilder &builder)
        {
            builder.Read(depth);
            (void)builder.Write(copy);
            if (!_before)
                builder.Read(motion);
        }, [=](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &)
        {
            cmd->Blit(depth, copy);
            _state.Capture(*cmd, graph.Resolve<Texture>(copy), _before ? "depth_before" : "depth_after");
            if (!_before)
                _state.Capture(*cmd, graph.Resolve<Texture>(motion), "motion");
        });
    }

    void Execute(GraphicsContext *context, RenderingData &data) override
    {
        if (!_state._capture)
            return;
        auto cmd = CommandBufferPool::Get(_name);
        cmd->Blit(data._camera_depth_target_handle, _depth.get());
        _state.Capture(*cmd, _depth.get(), _before ? "depth_before" : "depth_after");
        if (!_before)
        {
            const auto snapshot = Shader::CaptureGlobalResourceSnapshot();
            const auto found = snapshot._resources.find(Shader::PropertyID("_MotionVectorTexture"));
            _state.Capture(*cmd, found == snapshot._resources.end() ? nullptr :
                dynamic_cast<Texture *>(found->second._resource), "motion");
        }
        context->ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
    }

private:
    AcceptanceState &_state;
    bool _before;
    Ref<RenderTexture> _depth;
};

class AcceptanceFeature final : public RenderFeature
{
public:
    explicit AcceptanceFeature(AcceptanceState &state)
        : RenderFeature("M2Acceptance"), _skinning(state), _before(state, true), _after(state, false) {}
    void AddRenderPasses(Renderer &renderer, const RenderingData &) override
    {
        renderer.EnqueuePass(&_skinning);
        renderer.EnqueuePass(&_before);
        renderer.EnqueuePass(&_after);
    }
private:
    AcceptanceSkinningPass _skinning;
    AcceptanceCapturePass _before;
    AcceptanceCapturePass _after;
};

static Ref<Mesh> CreateQuad(bool deform = false)
{
    Ref<Mesh> mesh = deform ? MakeRef<SkeletonMesh>("M2DeformQuad") : MakeRef<Mesh>("M2AcceptanceQuad");
    mesh->SetVertices(Vector<Vector3f>{{-1, -1, 0}, {-1, 1, 0}, {1, 1, 0}, {1, -1, 0}});
    mesh->SetNormals(Vector<Vector3f>(4, Vector3f(0, 0, -1)));
    mesh->SetTangents(Vector<Vector4f>(4, Vector4f(1, 0, 0, 1)));
    mesh->SetUVs(Vector<Vector2f>{{0, 1}, {0, 0}, {1, 0}, {1, 1}});
    mesh->SetVerticesCount(4);
    mesh->AddSubmesh(Vector<u32>{0, 1, 2, 0, 2, 3});
    const Vector<AABB> bounds(2, AABB(Vector3f(-1, -1, -.01f), Vector3f(1.2f, 1, .01f)));
    mesh->SetBounds(bounds);
    if (deform)
    {
        auto *skeleton_mesh = static_cast<SkeletonMesh *>(mesh.get());
        skeleton_mesh->SetBoneWeights(Vector<Vector4f>(4, Vector4f(1, 0, 0, 0)));
        skeleton_mesh->SetBoneIndices(Vector<Vector4D<u32>>(4, Vector4D<u32>(0, 0, 0, 0)));
    }
    mesh->Apply();
    return mesh;
}

int wmain(int argc, wchar_t **argv)
{
    std::cerr << "Acceptance entry\n" << std::flush;
    pow2::Assert::SetLoggingHandler();
    InstallCrashHandler();
    AddVectoredExceptionHandler(1, RecordValidationFailure);
    _set_invalid_parameter_handler([](const wchar_t *, const wchar_t *, const wchar_t *, unsigned int, uintptr_t)
    {
        RaiseException(EXCEPTION_BREAKPOINT, 0, 0, nullptr);
    });
    LogMgr::Init();
    if (argc != 4 && argc != 5)
    {
        std::cerr << "Usage: MotionVectorGpuAcceptance project.json output_directory engine_config.json [phase]\n";
        return 2;
    }
    std::filesystem::create_directories(argv[2]);
    Application::SetEngineConfigPath(argv[3]);
    std::cerr << "Config set\n" << std::flush;
    Microsoft::WRL::ComPtr<ID3D12Debug> debug_controller;
    if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller))))
    {
        std::cerr << "D3D12 debug layer unavailable\n";
        return 3;
    }
    debug_controller->EnableDebugLayer();
    AcceptanceApplication app;
    ApplicationInitContext init;
    init._project_file_path = argv[1];
    init._enable_audio = false;
    ApplicationDesc desc;
    desc._window_width = desc._gameview_width = 320;
    desc._window_height = desc._gameview_height = 180;
    std::cerr << "Initializing application\n" << std::flush;
    if (app.Initialize(desc, init) != 0)
        return 3;
    std::cerr << "Application initialized\n" << std::flush;
    // Keep validation enabled, but allow unsupported input streams to reach capture instead of breaking the process.
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> debug_queue;
    auto *device = static_cast<RHI::DX12::D3DContext *>(g_pGfxContext)->GetDevice();
    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&debug_queue))))
    {
        debug_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, false);
        debug_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, false);
        s_debug_queue = debug_queue.Get();
        std::cerr << "D3D12 validation enabled\n";
    }
    else
    {
        std::cerr << "D3D12 InfoQueue unavailable\n";
        return 3;
    }
    app.SetMultiThreadRendering(false);
    ShowWindow(static_cast<HWND>(app.GetWindow().GetNativeWindowPtr()), SW_HIDE);

    Camera camera(16.f / 9.f, .1f, 100.f);
    camera.SetLens(60.f, 16.f / 9.f, .1f, 100.f);
    camera.Position({0, 0, -5});
    camera.MarkDirty();
    camera.RecalculateMatrix();
    camera.OutputSize(320, 180);
    Camera::sScene = Camera::sMain = Camera::sCurrent = &camera;
    auto pipeline = MakeScope<RenderPipeline>();
    RenderPipeline::Register(pipeline.get());
    std::cerr << "Pipeline initialized\n" << std::flush;
    auto scene = MakeRef<Scene>("M2AcceptanceTemporary");
    auto material = Material::CreateStandard("M2AcceptanceMaterial");
    material->SetCullMode(ECullMode::kNone);
    material->SetVector("_AlbedoValue", Vector4f(1, 1, 1, 1));
    material->SetFloat("_AlphaCulloff", .5f);
    auto texture = Texture2D::Create(16, 16, ETextureFormat::kRGBA32);
    Vector<u8> alpha_pixels(16u * 16u * 4u, 255u);
    for (u16 y = 0; y < 16; ++y)
        for (u16 x = 0; x < 16; ++x)
            alpha_pixels[(y * 16u + x) * 4u + 3u] = x < 8 ? 255u : 0u;
    texture->SetPixelData(alpha_pixels.data(), 0u, 0u);
    texture->Apply();
    auto rigid_mesh = CreateQuad();
    std::cerr << "Rigid mesh initialized\n" << std::flush;
    auto deform_mesh = CreateQuad(true);
    auto alpha_mesh = CreateQuad();
    std::cerr << "Deform mesh initialized\n" << std::flush;
    const auto entity = scene->AddObject(rigid_mesh, material);
    std::cerr << "Entity created\n" << std::flush;
    SceneMgr::Get().OpenTemporaryScene(scene);
    std::cerr << "Temporary scene opened\n" << std::flush;
    Camera::sCurrent = &camera;
    auto renderer = RenderPipeline::Get().GetRenderer();
    for (auto feature : renderer->GetFeatures())
        feature->SetActive(false);
    AcceptanceState state;
    state._output = argv[2];
    auto feature = MakeScope<AcceptanceFeature>(state);
    renderer->AddFeature(feature.get());
    std::cerr << "Capture feature registered\n" << std::flush;

    const Vector<String> phases{"rg_first", "rg_static", "rg_teleport_control", "rg_reset", "rg_rigid",
        "rg_alpha_value", "rg_alpha_texture", "rg_alpha_tinted", "rg_cpu_deform", "legacy_static", "legacy_rigid",
        "legacy_alpha_texture", "rg_cpu_skinning_move", "rg_cpu_skinning_hold", "rg_gpu_skinning_move", "rg_gpu_skinning_hold"};
    u32 frame = 0, phase_index = 0, phase_frame = 0;
    const String selected_phase = argc == 5 ? std::filesystem::path(argv[4]).string() : "";
    if (!selected_phase.empty())
    {
        const auto found = std::find(phases.begin(), phases.end(), selected_phase);
        if (found == phases.end())
            return 2;
        phase_index = static_cast<u32>(std::distance(phases.begin(), found));
    }
    bool failed = false;
    // Resource warm-up uses a separate camera, so rg_first is the first submission of the test camera.
    Camera warmup = camera;
    Camera::sCurrent = &warmup;
    auto skinning = MakeScope<ECS::SkinningSystem>();
    const auto pose = skinning->CreatePoseHandle();
    const bool compute_skinning = g_engine_config._enable_compute_skinning;
    g_engine_config._enable_compute_skinning = false;
    app._prepare_render = [&]()
    {
        state._skinning = phase_index >= 12 ? skinning.get() : nullptr;
        if (phase_index < 12)
            return;
        g_engine_config._enable_compute_skinning = phase_index >= 14;
        skinning->BeginFrame();
        Vector<Matrix4x4f> palette(1, Matrix4x4f::Identity());
        palette[0][3][0] = .005f * float(phase_index == 12 || phase_index == 14 ? phase_frame : 90u);
        if (phase_index >= 14)
        {
            skinning->UploadPose(pose, std::span<const Matrix4x4f>(palette.data(), palette.size()));
            skinning->SetEntityPose(entity, pose);
            skinning->Acquire(static_cast<SkeletonMesh *>(deform_mesh.get()), pose, entity);
        }
        else
        {
            skinning->Submit(static_cast<SkeletonMesh *>(deform_mesh.get()), palette, 4u, entity);
            skinning->WaitFor();
        }
    };
    app._before_update += [&]()
    {
        ++frame;
        if (frame == 1 || frame % 90 == 0)
            std::cerr << "Frame " << frame << '\n' << std::flush;
        state._capture = false;
        if (frame > 1800)
        {
            std::cerr << "Acceptance timeout\n";
            failed = true;
            app.Stop();
            return;
        }
        if (frame < 90)
            return;
        if (!state._sessions.empty())
        {
            bool complete = true;
            for (auto session : state._sessions)
            {
                const auto status = ImageCaptureService::GetStatus(session);
                if (status._state == EImageCaptureState::kFailed)
                {
                    std::cerr << state._phase << ": " << status._error << '\n';
                    failed = true;
                    app.Stop();
                    return;
                }
                complete &= status._state == EImageCaptureState::kCompleted;
            }
            if (!complete)
                return;
            std::cout << state._phase << " captured\n" << std::flush;
            state._sessions.clear();
            ++phase_index;
            phase_frame = 0;
            if (phase_index == phases.size() || !selected_phase.empty())
            {
                app.Stop();
                return;
            }
        }
        state._phase = phases[phase_index];
        Camera::sCurrent = &camera;
        renderer->_is_use_render_graph = phase_index < 9 || phase_index >= 12;
        if (phase_frame++ == 0)
        {
            material->DisableKeyword("ALPHA_TEST");
            material->DisableKeyword("CPU_DEFORM");
            if (auto *component = scene->GetRegister().GetComponent<ECS::StaticMeshComponent>(entity))
                component->_p_mesh = rigid_mesh;
            material->SetVector("_AlbedoValue", Vector4f(1, 1, 1, 1));
            material->SetInt("_SamplerMask", 0);
            if (phase_index == 2 || phase_index == 3)
            {
                camera.Position({phase_index == 2 ? 3.f : -3.f, 0, -5});
                camera.MarkDirty();
                camera.RecalculateMatrix();
                if (phase_index == 3)
                    camera.MarkTemporalHistoryReset();
            }
            if (phase_index >= 4)
            {
                camera.Position({0, 0, -5});
                camera.MarkDirty();
                camera.RecalculateMatrix();
                camera.MarkTemporalHistoryReset();
            }
            if ((phase_index >= 5 && phase_index <= 7) || phase_index == 11)
            {
                // Exercise the ordinary production mesh with UV0 at TEXCOORD0.
                scene->GetRegister().GetComponent<ECS::StaticMeshComponent>(entity)->_p_mesh = alpha_mesh;
                material->EnableKeyword("ALPHA_TEST");
                if (phase_index != 5)
                {
                    material->SetTexture("_AlbedoTex", texture.get());
                    material->SetInt("_SamplerMask", 1);
                }
                if (phase_index == 7)
                    material->SetVector("_AlbedoValue", Vector4f(1, 1, 1, .25f));
            }
            if (phase_index >= 12)
            {
                if (scene->GetRegister().GetComponent<ECS::CSkeletonMesh>(entity) == nullptr)
                {
                    scene->GetRegister().RemoveComponent<ECS::StaticMeshComponent>(entity);
                    auto &component = scene->GetRegister().AddComponent<ECS::CSkeletonMesh>(entity);
                    component._p_mesh = std::static_pointer_cast<SkeletonMesh>(deform_mesh);
                    component._p_mats = {material};
                    component._transformed_aabbs.resize(deform_mesh->SubmeshCount() + 1u);
                }
                scene->GetRegister().GetComponent<ECS::TransformComponent>(entity)->SetLocalPosition({0, 0, 0});
            }
            if (phase_index == 8)
            {
                // Use the production SkeletonMesh layout: UV at TEXCOORD0, previous positions at TEXCOORD1.
                // The pass selects CPU_DEFORM from the vertex layout, without changing shared material keywords.
                scene->GetRegister().GetComponent<ECS::StaticMeshComponent>(entity)->_p_mesh = deform_mesh;
                const Vector<Vector3f> deformed{{-.8f, -1, 0}, {-.8f, 1, 0}, {1.2f, 1, 0}, {1.2f, -1, 0}};
                std::memcpy(deform_mesh->GetVertexBuffer()->GetStream(0), deformed.data(),
                    deformed.size() * sizeof(Vector3f));
                scene->GetRegister().GetComponent<ECS::TransformComponent>(entity)->SetLocalPosition({0, 0, 0});
            }
        }
        if ((phase_index >= 4 && phase_index <= 7) || (phase_index >= 10 && phase_index <= 11))
        {
            auto *transform = scene->GetRegister().GetComponent<ECS::TransformComponent>(entity);
            transform->SetLocalPosition({.005f * float(phase_frame), 0, 0});
        }
        if (phase_frame == 89)
        {
            const auto pass = material->GetActiveShader()->FindPass("MotionVector");
            Mesh *mesh = phase_index >= 12 ? deform_mesh.get()
                : scene->GetRegister().GetComponent<ECS::StaticMeshComponent>(entity)->_p_mesh.get();
            auto *vertex_buffer = ECS::SkinningSystem::ResolveVertexBuffer(mesh, entity);
            if (vertex_buffer == nullptr)
                vertex_buffer = mesh->GetVertexBuffer().get();
            const auto variant = material->GetMotionVectorVariantHash(pass, vertex_buffer);
            std::cerr << state._phase << " variant=" << variant << " cutoff=" << material->GetFloat("_AlphaCulloff")
                << " mask=" << material->GetUint("_SamplerMask") << " alpha=" << material->GetVector("_AlbedoValue").w << '\n';
            for (const auto &element : material->GetActiveShader()->PipelineInputLayout(pass, variant).GetBufferDesc())
                std::cerr << "Input semantic=" << unsigned(element._semantic) << " stream=" << unsigned(element.Stream)
                    << " type=" << unsigned(element.Type) << '\n';
        }
        // First/reset/cut captures are immediate. Other phases allow asynchronous shader variants to become ready.
        state._capture = phase_index == 0 || phase_index == 2 || phase_index == 3 || phase_frame >= 90;
    };
    app.Run();
    CheckValidationMessages();
    std::cout << "D3D12 errors=" << s_gpu_error_count << '\n' << std::flush;
    failed |= s_gpu_error_count != 0u;
    feature->SetActive(false);
    feature.reset();
    scene.reset();
    material.reset();
    texture.reset();
    rigid_mesh.reset();
    deform_mesh.reset();
    app._prepare_render = {};
    skinning.reset();
    g_engine_config._enable_compute_skinning = compute_skinning;
    alpha_mesh.reset();
    pipeline.reset();
    RenderPipeline::Register(nullptr);
    s_debug_queue = nullptr;
    Camera::sCurrent = Camera::sScene = Camera::sMain = nullptr;
    app.Finalize();
    return failed ? 1 : 0;
}
