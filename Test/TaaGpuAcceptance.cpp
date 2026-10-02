#include "Framework/Common/Application.h"
#include "Framework/Common/EngineConfig.h"
#include "Render/Features/TemporalAA.h"
#include <fstream>
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
#include "../Engine/Ext/renderdoc_app.h"

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
                (message->Severity == D3D12_MESSAGE_SEVERITY_ERROR ||
                 message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION))
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
    std::function<void()> _after_render;
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
            if (_after_render)
                _after_render();
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


class TaaAcceptancePass final : public RenderPass
{
public:
    TaaAcceptancePass(const WString &output, Camera &camera, bool render_graph = false)
        : RenderPass("M3Isolated"), _camera(camera), _output(output), _render_graph(render_graph)
    {
        _event = ERenderPassEvent::kBeforePostprocess;
        _observations.open(std::filesystem::path(output) / "observations.csv");
        _observations << "index,phase,engine_frame,width,height,jitter_x,jitter_y,output_id\n";
    }
    bool _done = false;
    bool _failed = false;
    void Execute(GraphicsContext *context, RenderingData &data) override
    {
        if (_done)
            return;
        if (_index >= 86u)
        {
            bool complete = true;
            for (auto session : _sessions)
            {
                const auto status = ImageCaptureService::GetStatus(session);
                if (status._state == EImageCaptureState::kFailed)
                {
                    std::cerr << status._error << '\n';
                    _failed = true;
                    _done = true;
                }
                complete &= status._state == EImageCaptureState::kCompleted;
            }
            _done |= complete;
            return;
        }
        // Exercise production Setup/Execute/EndPass without implementing native RG scheduling.
        Camera &camera = _camera;
        String phase = _index == 0u ? "first" : _index < 65u ? "static" : "factor0";
        u16 width = 320u, height = 180u;
        f32 factor = _index == 65u ? 0.f : .95f;
        if (_index == 66u) { phase = "toggle"; _taa.InvalidateHistory(camera.HashCode()); }
        if (_index == 67u)
        {
            phase = "cut"; camera.Position({5, 0, -5}); camera.MarkDirty(); camera.RecalculateMatrix();
        }
        if (_index == 68u) { phase = "gap"; ++_index; return; }
        if (_index == 69u) phase = "gap_restart";
        if (_index == 70u || _index == 72u || _index == 74u)
        {
            phase = "resize"; width = 257u; height = 137u;
        }
        if (_index == 71u || _index == 73u || _index == 75u) phase = "resize_back";
        if (_index == 76u)
        {
            phase = "fov"; camera.SetLens(70.f, 16.f / 9.f, .1f, 100.f);
            camera.MarkDirty(); camera.RecalculateMatrix();
        }
        if (_index == 77u)
        {
            phase = "rotation"; camera.LookTo({1, 0, 0}, {0, 1, 0});
        }
        if (_index >= 78u) { phase = "factor0_series"; factor = 0.f; }
        const Vector4f jitter = camera.GetJitter();
        auto source = Texture2D::Create(width, height, ETextureFormat::kRGBA32);
        Vector<u8> pixels(width * height * 4u, 255u);
        for (u32 y = 0; y < height; ++y)
            for (u32 x = 0; x < width; ++x)
            {
                // Analytic coverage of fixed diagonal geometry, displaced by actual Camera UV jitter.
                const f32 edge = (float(x) + .5f - jitter.z * width) -
                    .61f * (float(y) + .5f - jitter.w * height) - float(width) * .27f;
                f32 value = std::clamp(edge + .5f, 0.f, 1.f) * .7f + .1f;
                if (_index >= 65u) value = 1.f - value;
                const u8 channel = static_cast<u8>(value * 255.f);
                const u32 offset = (y * width + x) * 4u;
                pixels[offset] = pixels[offset + 1] = pixels[offset + 2] = channel;
            }
        source->SetPixelData(pixels.data(), 0u, 0u);
        source->ApplySync();
        _sources.push_back(source);
        const auto color = RenderTexture::GetTempRT(width, height, "M3Color", ERenderTargetFormat::kDefaultHDR);
        const auto depth = RenderTexture::GetTempRT(width, height, "M3Depth", ERenderTargetFormat::kRFloat);
        const auto motion = RenderTexture::GetTempRT(width, height, "M3Motion", ERenderTargetFormat::kDefaultHDR);
        auto cmd = CommandBufferPool::Get("M3Inputs");
        cmd->Blit(source.get(), color);
        cmd->SetRenderTarget(depth); cmd->ClearRenderTarget(Color(.5f, .5f, .5f, 1.f));
        cmd->SetRenderTarget(motion); cmd->ClearRenderTarget(Color(0.f, 0.f, 0.f, 0.f));
        context->ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
        if (_warmup++ < 8u)
        {
            RenderTexture::ReleaseTempRT(color);
            RenderTexture::ReleaseTempRT(depth);
            RenderTexture::ReleaseTempRT(motion);
            return;
        }
        RenderingData isolated = data;
        isolated._width = width; isolated._height = height;
        isolated._camera_opaque_tex_handle = color;
        isolated._camera_depth_tex_handle = depth;
        isolated._motion_vector_tex_handle = motion;
        isolated._postprocess_input = nullptr;
        _taa.Setup(camera, context->GetFrameCount(), width, height, {jitter.z, jitter.w},
                   {1.f, 1.f, factor, 0.f}, {2.f, 2.f, 0.f, 0.f});
        if (_render_graph)
        {
            RDG::RenderGraph graph;
            graph._is_debug = true;
            // Renderer normally allocates graph resources before TAA inputs; handle zero is reserved as invalid.
            graph.CreateResource(TextureDesc(1u, 1u, ERenderTargetFormat::kDefaultHDR), "M4ReservedHandle");
            isolated._camera = &camera;
            isolated._rg_handles._color_tex = graph.Import(g_pRenderTexturePool->Get(color));
            isolated._rg_handles._depth_tex = graph.Import(g_pRenderTexturePool->Get(depth));
            isolated._rg_handles._motion_vector_tex = graph.Import(g_pRenderTexturePool->Get(motion));
            const auto current_color = isolated._rg_handles._color_tex;
            _taa.OnRecordRenderGraph(graph, isolated);
            // The core/history oracle compares jittered accumulation. Presentation has a separate Editor oracle.
            const auto output = isolated._rg_handles._taa_history_color;
            if (!output.IsValid() || isolated._rg_handles._color_tex != current_color)
            {
                std::cerr << "RG TAA registration/output failed: color=" << current_color._id
                          << " depth=" << isolated._rg_handles._depth_tex._id
                          << " motion=" << isolated._rg_handles._motion_vector_tex._id << " output=" << output._id
                          << " changed_color=" << (isolated._rg_handles._color_tex != current_color) << '\n';
                _failed = _done = true;
                return;
            }
            graph.AddPass("M4Capture", RDG::PassDesc(), [current_color, output](RDG::RenderGraphBuilder &builder)
            {
                builder.Read(current_color);
                builder.Read(output);
            }, [this, current_color, output, phase](RDG::RenderGraph &pass_graph, CommandBuffer *capture_cmd,
                                                   const RenderingData &)
            {
                Capture(*capture_cmd, pass_graph.Resolve<Texture>(current_color), phase + "_current");
                Capture(*capture_cmd, pass_graph.Resolve<Texture>(output), phase + "_resolved");
            });
            if (!graph.Compile())
            {
                std::cerr << "RG compile failed\n";
                _failed = _done = true;
                return;
            }
            graph.Execute(*context, isolated);
            isolated._postprocess_input = graph.Resolve<RenderTexture>(output);
            for (const auto &pass : graph._debug_passes)
            {
                if (pass._name != "TemporalAA")
                    continue;
                if (pass._input_access_records.size() != 5u || pass._output_access_records.size() != 2u ||
                    pass._input_access_records[0]._handle != current_color ||
                    pass._input_access_records[1]._handle != isolated._rg_handles._depth_tex ||
                    pass._input_access_records[2]._handle != isolated._rg_handles._motion_vector_tex ||
                    pass._output_access_records[0]._access._usage != EResourceUsage::kWriteUAV)
                    _failed = true;
                auto *previous = graph.Resolve<Texture>(pass._input_access_records[3]._handle);
                if (previous == isolated._postprocess_input || !previous)
                    _failed = true;
                std::cout << "M4_BINDINGS " << _index << " color=" << current_color._id << ':'
                          << current_color._version << " history=" << previous->ID() << " output="
                          << isolated._postprocess_input->ID() << '\n';
            }
            graph.EndFrame();
        }
        else
        {
            _taa.Execute(context, isolated);
            _taa.EndPass(context);
        }
        if (isolated._postprocess_input == nullptr)
        {
            std::cerr << "TAA did not execute\n";
            _failed = _done = true;
            return;
        }
        if (!_render_graph)
        {
            cmd = CommandBufferPool::Get("M3Capture");
            Capture(*cmd, g_pRenderTexturePool->Get(color), phase + "_current");
            Capture(*cmd, isolated._postprocess_input, phase + "_resolved");
            context->ExecuteCommandBuffer(cmd);
            CommandBufferPool::Release(cmd);
        }
        _observations << _index << ',' << phase << ',' << context->GetFrameCount() << ',' << width << ',' << height
            << ',' << jitter.z << ',' << jitter.w << ',' << isolated._postprocess_input->ID() << '\n';
        _observations.flush();
        std::cout << "M3 " << _index << ' ' << phase << '\n' << std::flush;
        RenderTexture::ReleaseTempRT(color); RenderTexture::ReleaseTempRT(depth); RenderTexture::ReleaseTempRT(motion);
        ++_index;
    }
private:
    void Capture(CommandBuffer &cmd, Texture *texture, const String &suffix)
    {
        ImageCaptureSequenceDesc sequence;
        sequence._output_directory = _output;
        sequence._file_prefix = std::to_string(_index) + "_" + suffix;
        ImageCaptureTextureDesc source;
        source._debug_name = sequence._file_prefix;
        _sessions.push_back(ImageCaptureService::CaptureOnce(cmd, texture, source, sequence));
    }
    Camera &_camera;
    WString _output;
    u32 _index = 0u;
    u32 _warmup = 0u;
    TAAExecutePass _taa;
    Vector<Ref<Texture2D>> _sources;
    Vector<ImageCaptureSessionId> _sessions;
    std::ofstream _observations;
    bool _render_graph = false;
};

class TaaAcceptanceFeature final : public RenderFeature
{
public:
    TaaAcceptanceFeature(const WString &output, Camera &camera, bool render_graph = false)
        : RenderFeature("M3Acceptance"), _pass(output, camera, render_graph) {}
    TaaAcceptancePass _pass;
    void AddRenderPasses(Renderer &renderer, const RenderingData &) override { renderer.EnqueuePass(&_pass); }
};

#include "TaaSceneAcceptance.h"
#include "TaaTransparentAcceptance.h"
#include "TaaCloudAcceptance.h"
#include "TaaRtgiAcceptance.h"
#include "TaaM8Acceptance.h"

int wmain(int argc, wchar_t **argv)
{
    if (argc != 4 && argc != 5) return 2;
    const bool render_graph = argc == 5 && WString(argv[4]) == L"--rg";
    const bool control_mode = argc == 5 && WString(argv[4]) == L"--scene-controls";
    const bool transparent_mode = argc == 5 && WString(argv[4]) == L"--transparent";
    const bool cloud_mode = argc == 5 && WString(argv[4]) == L"--cloud";
    const bool rtgi_mode = argc == 5 && WString(argv[4]) == L"--rtgi";
    const bool m8_long_run = argc == 5 && WString(argv[4]) == L"--m8-longrun";
    const bool m8_performance = argc == 5 && WString(argv[4]) == L"--m8-performance";
    const bool m8_mode = argc == 5 && (WString(argv[4]) == L"--m8" || m8_long_run || m8_performance);
    const bool scene_mode = argc == 5 && (WString(argv[4]) == L"--scene" || control_mode || transparent_mode ||
                                        cloud_mode || rtgi_mode || m8_mode);
    RENDERDOC_API_1_1_2 *renderdoc_api = nullptr;
    wchar_t renderdoc_path[MAX_PATH]{};
    if ((render_graph || m8_performance) &&
        GetEnvironmentVariableW(L"AILU_TAA_RENDERDOC_DLL", renderdoc_path, MAX_PATH) > 0u)
    {
        const HMODULE module = LoadLibraryW(renderdoc_path);
        const auto get_api = module ? reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(module, "RENDERDOC_GetAPI")) : nullptr;
        if (!get_api || get_api(eRENDERDOC_API_Version_1_1_2, reinterpret_cast<void **>(&renderdoc_api)) != 1)
            return 4;
    }
    pow2::Assert::SetLoggingHandler();
    InstallCrashHandler();
    AddVectoredExceptionHandler(1, RecordValidationFailure);
    LogMgr::Init();
    std::filesystem::create_directories(argv[2]);
    Application::SetEngineConfigPath(argv[3]);
    Microsoft::WRL::ComPtr<ID3D12Debug> debug;
    if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) return 3;
    debug->EnableDebugLayer();
    AcceptanceApplication app;
    ApplicationInitContext init;
    init._project_file_path = argv[1]; init._enable_audio = false;
    ApplicationDesc desc;
    desc._window_width = desc._gameview_width = 320;
    desc._window_height = desc._gameview_height = 180;
    if (app.Initialize(desc, init) != 0) return 3;
    if (renderdoc_api)
    {
        const String capture_prefix = (std::filesystem::path(argv[2]) / "TAA_RG").string();
        renderdoc_api->SetCaptureFilePathTemplate(capture_prefix.c_str());
        renderdoc_api->SetActiveWindow(static_cast<RHI::DX12::D3DContext *>(g_pGfxContext)->GetDevice(),
                                      app.GetWindow().GetNativeWindowPtr());
    }
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> queue;
    auto *device = static_cast<RHI::DX12::D3DContext *>(g_pGfxContext)->GetDevice();
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&queue)))) return 3;
    queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, false);
    queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, false);
    s_debug_queue = queue.Get();
    app.SetMultiThreadRendering(false);
    ShowWindow(static_cast<HWND>(app.GetWindow().GetNativeWindowPtr()), SW_HIDE);
    Camera camera(16.f / 9.f, .1f, 100.f);
    camera.SetLens(60.f, 16.f / 9.f, .1f, 100.f);
    camera.Position({0, 0, -5}); camera.OutputSize(320, 180);
    camera._anti_aliasing = EAntiAliasing::kTAA;
    camera.MarkDirty(); camera.RecalculateMatrix();
    Camera::sScene = Camera::sMain = Camera::sCurrent = &camera;
    auto pipeline = MakeScope<RenderPipeline>();
    RenderPipeline::Register(pipeline.get());
    auto scene = MakeRef<Scene>("M3Temporary");
    SceneMgr::Get().OpenTemporaryScene(scene);
    Camera::sCurrent = &camera;
    auto *renderer = pipeline->GetRenderer();
    for (auto *feature : renderer->GetFeatures()) feature->SetActive(false);
    renderer->_is_use_render_graph = scene_mode;
    TemporalAA *scene_taa = nullptr;
    Scope<TaaSceneAcceptanceFeature> scene_feature;
    Scope<TaaTransparentAcceptance> transparent_feature;
    Scope<TaaCloudAcceptance> cloud_feature;
    Scope<TaaRtgiAcceptance> rtgi_feature;
    Scope<TaaM8Acceptance> m8_feature;
    Ref<Material> bright_material;
    ECS::Entity rails_entity = ECS::kInvalidEntity;
    ECS::Entity occluder_entity = ECS::kInvalidEntity;
    if (transparent_mode)
    {
        transparent_feature = MakeScope<TaaTransparentAcceptance>(argv[2], camera, *scene, *renderer);
        renderer->AddFeature(transparent_feature.get());
    }
    if (cloud_mode)
    {
        const auto environment = scene->AddObject("M6ProceduralEnvironment");
        scene->GetRegister().AddComponent<ECS::EnvironmentComponent>(environment);
        const auto sun_entity = scene->AddObject("M6Sun");
        auto &sun = scene->GetRegister().AddComponent<ECS::LightComponent>(sun_entity);
        sun._type = ECS::ELightType::kDirectional;
        sun._light = {};
        sun._light._light_color = Color(1.f, 1.f, 1.f, 1.f);
        sun._light._light_dir = Vector4f(0.f, -1.f, 0.f, 0.f);
        sun._light._light_param = Vector4f(1.f, 0.f, 0.f, 0.f);
        sun._shadow = {};
        scene->GetRegister().GetComponent<ECS::TransformComponent>(sun_entity)->SetLocalRotation(
            Quaternion::EulerAngles(90.f, 0.f, 0.f));
        cloud_feature = MakeScope<TaaCloudAcceptance>(argv[2], camera, *renderer);
        renderer->AddFeature(cloud_feature.get());
    }
    if (rtgi_mode)
    {
        renderer->_is_use_raytracing = true;
        const auto environment = scene->AddObject("M7Environment");
        scene->GetRegister().AddComponent<ECS::EnvironmentComponent>(environment);
        rtgi_feature = MakeScope<TaaRtgiAcceptance>(argv[2], camera, *renderer);
        renderer->AddFeature(rtgi_feature.get());
    }
    if (m8_mode)
    {
        m8_feature = MakeScope<TaaM8Acceptance>(argv[2], camera, *scene, *renderer, m8_long_run, m8_performance);
        renderer->AddFeature(m8_feature.get());
        app._after_render = [&]() { m8_feature->AfterRender(); };
    }
    if (scene_mode && !transparent_mode && !cloud_mode && !m8_mode)
    {
        for (auto *existing_feature : renderer->GetFeatures())
            if (existing_feature->Name() == "TemporalAA")
                scene_taa = dynamic_cast<TemporalAA *>(existing_feature);
        if (!scene_taa)
            return 4;
        bright_material = Material::CreateStandard("M3BrightGeometry");
        auto dark_material = Material::CreateStandard("M3DarkGeometry");
        bright_material->SetCullMode(ECullMode::kNone);
        dark_material->SetCullMode(ECullMode::kNone);
        bright_material->SetVector("_AlbedoValue", Vector4f(0.f, 0.f, 0.f, 1.f));
        dark_material->SetVector("_AlbedoValue", Vector4f(0.f, 0.f, 0.f, 1.f));
        bright_material->SetVector("_EmissionValue", Vector4f(.8f, .8f, .8f, 1.f));
        dark_material->SetVector("_EmissionValue", Vector4f(.04f, .04f, .04f, 1.f));
        scene->AddObject(CreateTaaSceneMesh(true), dark_material);
        rails_entity = scene->AddObject(CreateTaaSceneMesh(false), bright_material);
        if (control_mode)
        {
            occluder_entity = scene->AddObject(CreateTaaSceneMesh(true), dark_material);
            scene->GetRegister().GetComponent<ECS::TransformComponent>(occluder_entity)->SetLocalPosition({0.f, 0.f, 20.f});
        }
    }
    Scope<TaaAcceptanceFeature> feature;
    u32 frame = 0u;
    bool failed = false;
    app._before_update += [&]()
    {
        ++frame;
        if (renderdoc_api && (frame == 140u || (m8_performance && frame == 540u)))
            renderdoc_api->TriggerMultiFrameCapture(2u);
        if (frame == 90u && !transparent_mode && !cloud_mode && !rtgi_mode && !m8_mode)
        {
            if (scene_mode)
            {
                scene_feature = MakeScope<TaaSceneAcceptanceFeature>(argv[2], camera);
                scene_feature->_pass._taa = scene_taa;
                scene_feature->_pass._bright_material = bright_material;
                if (control_mode)
                {
                    scene_feature->_pass._total_samples = 334u;
                    scene_feature->_pass._rails = scene->GetRegister().GetComponent<ECS::TransformComponent>(rails_entity);
                    scene_feature->_pass._occluder = scene->GetRegister().GetComponent<ECS::TransformComponent>(occluder_entity);
                }
                renderer->AddFeature(scene_feature.get());
            }
            else
            {
                feature = MakeScope<TaaAcceptanceFeature>(argv[2], camera, render_graph);
                renderer->AddFeature(feature.get());
            }
        }
        if (scene_feature)
            scene_feature->_pass.Prepare(frame - 90u);
        if (m8_feature && frame >= 90u)
        {
            m8_feature->Prepare(frame - 90u);
            if (m8_feature->Finished())
            {
                failed = m8_feature->_failed;
                app.Stop();
            }
        }
        if (transparent_feature && frame >= 90u)
        {
            transparent_feature->Prepare(frame - 90u);
            if (transparent_feature->Finished())
            {
                failed = transparent_feature->_failed;
                app.Stop();
            }
        }
        if (cloud_feature && frame >= 90u)
        {
            cloud_feature->Prepare(frame - 90u);
            if (cloud_feature->Finished())
            {
                failed = cloud_feature->_failed;
                app.Stop();
            }
        }
        if (rtgi_feature && frame >= 90u)
        {
            rtgi_feature->Prepare(frame - 90u);
            if (rtgi_feature->Finished())
            {
                failed = rtgi_feature->_failed;
                app.Stop();
            }
        }
        if (scene_feature && scene_feature->_pass.Finished())
        {
            failed = scene_feature->_pass._failed;
            app.Stop();
        }
        const u32 frame_limit = m8_long_run ? 30000u : m8_mode ? 1600u : 900u;
        if ((feature && feature->_pass._done) || frame > frame_limit)
        {
            failed = !feature || feature->_pass._failed || frame > frame_limit;
            app.Stop();
        }
    };
    app.Run();
    CheckValidationMessages();
    std::cout << "D3D12 errors=" << s_gpu_error_count << '\n';
    failed |= s_gpu_error_count != 0u;
    std::ofstream status(std::filesystem::path(argv[2]) / "validation.txt");
    status << "D3D12_debug_layer=enabled\nERROR_CORRUPTION=" << s_gpu_error_count
           << "\nfailed=" << failed << "\n";
    status.close();
    if (feature) feature->SetActive(false);
    if (scene_feature) scene_feature->SetActive(false);
    if (transparent_feature) transparent_feature->RenderFeature::SetActive(false);
    if (cloud_feature) cloud_feature->RenderFeature::SetActive(false);
    if (rtgi_feature) rtgi_feature->RenderFeature::SetActive(false);
    if (m8_feature) m8_feature->RenderFeature::SetActive(false);
    app._after_render = {};
    m8_feature.reset();
    rtgi_feature.reset();
    cloud_feature.reset();
    transparent_feature.reset();
    scene_feature.reset();
    bright_material.reset();
    feature.reset(); scene.reset(); pipeline.reset();
    RenderPipeline::Register(nullptr);
    Camera::sCurrent = Camera::sScene = Camera::sMain = nullptr;
    s_debug_queue = nullptr;
    app.Finalize();
    return failed ? 1 : 0;
}
