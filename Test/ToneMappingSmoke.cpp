#include "Framework/Common/Application.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/CrashHandler.h"
#include "Platform/WinWindow.h"
#include "Render/GraphicsContext.h"
#include "Render/Texture.h"
#include "Render/CommandBuffer.h"
#include "Render/Shader.h"
#include "Render/Material.h"
#include "Render/ShaderInterop.h"
#include "RHI/DX12/D3DContext.h"
#include <d3d12sdklayers.h>
#include "Scene/Scene.h"
#include "Assets/PrefabAsset.h"
#include "Render/PostprocessSettings.h"
#include "Render/ImageCapture/ImageCaptureService.h"
#include "Objects/JsonArchive.h"
#include "Objects/SerializeSpecializations.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace Ailu;
using namespace Ailu::Render;

static void Require(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}

static void CheckD3DDebugErrors()
{
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> info_queue;
    auto *device = static_cast<Ailu::RHI::DX12::D3DContext &>(GraphicsContext::Get()).GetDevice();
    Require(SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&info_queue))), "D3D12 debug info queue unavailable");
    for (u64 index = 0u; index < info_queue->GetNumStoredMessagesAllowedByRetrievalFilter(); ++index)
    {
        SIZE_T size = 0u;
        info_queue->GetMessage(index, nullptr, &size);
        Vector<u8> bytes(size);
        auto *message = reinterpret_cast<D3D12_MESSAGE *>(bytes.data());
        Require(SUCCEEDED(info_queue->GetMessage(index, message, &size)), "D3D12 message read failed");
        if (message->Severity == D3D12_MESSAGE_SEVERITY_ERROR ||
            message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION)
            throw std::runtime_error(message->pDescription);
    }
}

class ToneMappingApplication final : public Application
{
public:
    using Application::BeginFrame;
    using Application::EndFrame;
    void Flush()
    {
        BeginFrame();
        RenderFrame();
        RenderEditor();
        PresentFrame();
        EndFrame();
        GraphicsContext::Get().WaitForGpu();
    }
};

static void CheckPreExposure(ToneMappingApplication &app, const std::filesystem::path &directory)
{
    TextureDesc history_desc(1u, 1u, ERenderTargetFormat::kRFloat);
    history_desc._is_random_access = true;
    auto history = RenderTexture::Create(history_desc, "PreExposureHistorySmoke");
    auto target = RenderTexture::Create(TextureDesc(1u, 1u, ERenderTargetFormat::kRGBAFloat), "PreExposureResultSmoke");
    auto white = Texture2D::Create(1u, 1u, ETextureFormat::kRGBAFloat, false);
    white->SetPixel(0u, 0u, Color(1, 1, 1, 1), 0u);
    white->Apply();
    auto shader = Shader::Create((std::filesystem::path(AILU_REPO_ROOT) /
                                 "Engine/Res/Shaders/hlsl/PostProcess/tone_mapping.hlsl").wstring());
    Require(shader->Compile(EShaderCompilePolicy::kForceCompile), "PreExposure shader compile failed");
    auto material = MakeRef<Material>(shader.get(), "PreExposureSmoke");
    auto bloom_shader = Shader::Create((std::filesystem::path(AILU_REPO_ROOT) /
                                       "Engine/Res/Shaders/hlsl/PostProcess/bloom.hlsl").wstring());
    Require(bloom_shader->Compile(EShaderCompilePolicy::kForceCompile), "Bloom bindless shader compile failed");
    auto bloom_material = MakeRef<Material>(bloom_shader.get(), "BloomBindlessSmoke");
    bloom_material->SetTexture("_SourceTex", white.get());
    material->SetTexture("_SourceTex", white.get());
    material->SetFloat("_Exposure", 1.0f);
    material->SetFloat("_Gamma", 1.0f);
    material->SetInt("_ToneMappingMode", 0);
    material->SetInt("_UseAutoExposureEV", 0);
    for (const f32 ev : {-3.0f, 2.0f})
    {
        auto value = Texture2D::Create(1u, 1u, ETextureFormat::kRGBAFloat, false);
        value->SetPixel(0u, 0u, Color(ev, ev, ev, 1), 0u);
        value->Apply();
        app.Flush();
        Require(history->GetBindlessSRVIndex() >= 0, "PreExposure history has no bindless SRV");
        app.BeginFrame();
        auto cmd = CommandBufferPool::Get("PreExposureSmoke");
        cmd->Blit(value.get(), history.get());
        // This read also transitions history from RTV to SRV before the bindless read.
        cmd->Blit(history.get(), target.get());
        CBufferPerCameraData camera_data{};
        camera_data._pre_exposure = 1.0f;
        camera_data._pre_exposure_ev_texture_index = static_cast<u32>(history->GetBindlessSRVIndex());
        cmd->SetGlobalBuffer(RenderConstants::kCBufNamePerCamera, &camera_data, sizeof(camera_data));
        cmd->SetRenderTarget(target.get());
        cmd->DrawFullScreenQuad(bloom_material.get(), 0u);
        cmd->DrawFullScreenQuad(bloom_material.get(), 1u);
        cmd->DrawFullScreenQuad(material.get());
        ImageCaptureSequenceDesc sequence;
        const auto output = directory / (ev < 0.0f ? "preexposure_negative" : "preexposure_positive");
        sequence._output_directory = output.wstring();
        const auto session = ImageCaptureService::CaptureOnce(*cmd, target.get(), {}, sequence);
        GraphicsContext::Get().ExecuteCommandBufferSync(cmd);
        GraphicsContext::Get().WaitForGpu();
        CheckD3DDebugErrors();
        CommandBufferPool::Release(cmd);
        for (u32 attempt = 0u;; ++attempt)
        {
            ImageCaptureService::Tick();
            const auto status = ImageCaptureService::GetStatus(session);
            Require(status._state != EImageCaptureState::kFailed, "PreExposure capture failed");
            if (status._state == EImageCaptureState::kCompleted)
                break;
            Require(attempt < 1000u, "PreExposure capture timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        app.EndFrame();
        bool checked = false;
        for (const auto &file : std::filesystem::recursive_directory_iterator(output))
        {
            if (file.path().extension() != ".dds")
                continue;
            std::ifstream stream(file.path(), std::ios::binary);
            stream.seekg(-static_cast<std::streamoff>(sizeof(f32) * 4u), std::ios::end);
            f32 pixel[4]{};
            stream.read(reinterpret_cast<char *>(pixel), sizeof(pixel));
            std::cout << "PreExposure EV=" << ev << " pixel=" << pixel[0] << '\n' << std::flush;
            Require(stream.good() && std::isfinite(pixel[0]) && std::abs(pixel[0] - std::exp2(-ev)) < 1e-4f,
                    "Graphics bindless pre-exposure sample is incorrect");
            checked = true;
        }
        Require(checked, "PreExposure capture DDS missing");
    }
}

static void CheckToneMapping(ToneMappingApplication &app, const std::filesystem::path &directory)
{
    TextureDesc history_desc(1u, 1u, ERenderTargetFormat::kRFloat);
    history_desc._is_color_target = false;
    history_desc._is_random_access = true;
    history_desc._is_linear = true;
    history_desc._fixed_size = true;
    auto exposure_history = RenderTexture::Create(history_desc, "ExposureBindlessSmoke");
    app.Flush();
    const i32 exposure_index = exposure_history->GetBindlessSRVIndex();
    Require(exposure_index >= 0, "Exposure history must have a bindless Texture2D SRV");
    exposure_history->Apply();
    app.Flush();
    Require(exposure_history->GetBindlessSRVIndex() == exposure_index,
            "Exposure history re-upload must preserve its bindless SRV index");
    const Color samples[]{Color(0, 0, 0, 1), Color(.001f, .001f, .001f, 1), Color(.18f, .18f, .18f, 1),
        Color(1, 1, 1, 1),         Color(4, 4, 4, 1), Color(4, 0, 0, 1), Color(0, 4, 0, 1), Color(0, 0, 4, 1)};
    auto input = Texture2D::Create(8, 1, ETextureFormat::kRGBAFloat, false);
    input->sRGB(false);
    for (u16 index = 0; index < 8; ++index)
        input->SetPixel(index, 0, samples[index], 0);
    input->Apply();
    auto shader = Shader::Create((std::filesystem::path(AILU_REPO_ROOT) /
        "Engine/Res/Shaders/hlsl/PostProcess/tone_mapping.hlsl").wstring());
    Require(shader != nullptr && shader->Compile(EShaderCompilePolicy::kForceCompile),
            "ToneMapping shader compile failed");
    auto material = MakeRef<Material>(shader.get(), "ToneMappingSmoke");
    material->SetTexture("_SourceTex", input.get());
    std::cerr << "Input uploaded, shader compiled; flushing\n" << std::flush;
    app.Flush();
    std::cerr << "Initial flush completed\n" << std::flush;
    for (i32 mode = 0; mode < 4; ++mode)
    {
        for (const f32 exposure : {0.0f, 0.5f, 1.0f, 2.0f})
        {
            for (const f32 gamma : {1.0f, 0.4100580513f})
            for (const bool packed : {false, true})
            {
                if (mode != 3 && gamma != 1.0f)
                    continue;
                std::cerr << "Capture " << mode << ":" << exposure << ":" << packed << "\n" << std::flush;
                TextureDesc desc(8u, 1u, packed ? ERenderTargetFormat::kDefaultHDR : ERenderTargetFormat::kRGBAFloat);
                auto target = RenderTexture::Create(desc, "ToneMappingSmoke");
                app.Flush();
                const String name = std::format("mode{}_exposure{}_gamma{}_{}",
                                                mode, exposure, gamma, packed ? "packed" : "float");
                app.BeginFrame();
                material->SetInt("_ToneMappingMode", mode);
                material->SetFloat("_Exposure", exposure);
                material->SetFloat("_Gamma", gamma);
                auto cmd = CommandBufferPool::Get(name);
                cmd->SetRenderTarget(target.get());
                cmd->DrawFullScreenQuad(material.get());
                ImageCaptureSequenceDesc sequence;
                sequence._output_directory = directory.wstring();
                sequence._file_prefix = name;
                ImageCaptureTextureDesc source;
                source._debug_name = name;
                const auto session = ImageCaptureService::CaptureOnce(*cmd, target.get(), source, sequence);
                auto &context = GraphicsContext::Get();
                context.ExecuteCommandBufferSync(cmd);
                context.WaitForGpu();
                CommandBufferPool::Release(cmd);
                for (u32 attempt = 0; ; ++attempt)
                {
                    ImageCaptureService::Tick();
                    const auto status = ImageCaptureService::GetStatus(session);
                    Require(status._state != EImageCaptureState::kFailed, "ToneMapping capture failed");
                    if (status._state == EImageCaptureState::kCompleted)
                        break;
                    Require(attempt < 1000, "ToneMapping capture timed out");
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                app.EndFrame();
            }
        }
        PostprocessSettings authored;
        Require(authored._tone_mapping._mode == EToneMappingMode::kAces && authored._tone_mapping._exposure == 1.0f,
                "Authored default must remain ACES with unit exposure");
        authored._tone_mapping._mode = static_cast<EToneMappingMode>(mode);
        authored._tone_mapping._exposure = 0.5f;
        authored._tone_mapping._gamma = 0.410058f;
        const String name = "postprocess";
        JsonArchive writer;
        SerializerWrapper<PostprocessSettings>::Serialize(&authored, writer, &name);
        const String serialized = writer.SaveToString();
        std::ofstream(directory / std::format("settings_mode{}.json", mode)) << serialized;
        JsonArchive reader;
        Require(reader.LoadFromString(serialized), "Postprocess settings JSON parse failed");
        PostprocessSettings loaded;
        SerializerWrapper<PostprocessSettings>::Deserialize(&loaded, reader, &name);
        Require(loaded._tone_mapping._mode == authored._tone_mapping._mode &&
            loaded._tone_mapping._exposure == 0.5f &&
            std::abs(loaded._tone_mapping._gamma - 0.410058f) < 0.0001f, "ToneMapping enum/exposure roundtrip failed");
        PrefabEntityDocument prefab;
        prefab._entity._has_postprocess_volume_component = true;
        prefab._entity._postprocess_volume_component._settings = authored;
        JsonArchive prefab_writer;
        SerializerWrapper<PrefabEntityDocument>::Serialize(&prefab, prefab_writer, &name);
        const String prefab_json = prefab_writer.SaveToString();
        std::ofstream(directory / std::format("prefab_mode{}.json", mode)) << prefab_json;
        JsonArchive prefab_reader;
        Require(prefab_reader.LoadFromString(prefab_json), "Prefab entity JSON parse failed");
        PrefabEntityDocument reopened;
        SerializerWrapper<PrefabEntityDocument>::Deserialize(&reopened, prefab_reader, &name);
        Require(reopened._entity._has_postprocess_volume_component &&
            reopened._entity._postprocess_volume_component._settings._tone_mapping._mode ==
                authored._tone_mapping._mode,
            "Prefab Postprocess Volume mode roundtrip failed");
    }
}

int wmain(int argc, wchar_t **argv)
{
    if (argc != 2)
        return 2;
    const auto directory = std::filesystem::absolute(argv[1]);
    std::filesystem::create_directories(directory);
    const auto project = directory / "ToneMapping.ailuproject", config = directory / "EngineConfig.json";
    if (std::filesystem::exists(project) || std::filesystem::exists(config))
        return 2;
    std::filesystem::current_path(directory);
    std::ofstream(project) << R"({"_file_version":1,"_project_name":"ToneMapping Smoke",
        "_project_guid":"1549bab1-fc22-4c9d-aa32-4644f2c0758a"})";
    std::ofstream(config) << R"({"None":0,"Default":1,"ShadowCaster":2,"All":4294967295,
        "isMultiThreadRender":false,"enable_graphics_job":false,"_enable_d3d12_debug_layer":true,
        "_enable_rdc":false,"_enable_pix":false})";
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    LogMgr::Init();
    InstallCrashHandler();
    Application::SetEngineConfigPath(config.wstring());
    ToneMappingApplication app;
    ApplicationInitContext init;
    init._project_file_path = project.wstring();
    init._enable_audio = false;
    ApplicationDesc window;
    window._window_width = window._gameview_width = 64;
    window._window_height = window._gameview_height = 64;
    if (app.Initialize(window, init) != 0)
        return 3;
    ShowWindow(static_cast<HWND>(app.GetWindow().GetNativeWindowPtr()), SW_HIDE);
    app.SetMultiThreadRendering(false);
    Camera *camera = Camera::GetDefaultCamera();
    camera->OutputSize(64u, 64u);
    auto camera_target = RenderTexture::Create(64u, 64u, "ToneMappingScene", ERenderTargetFormat::kDefaultHDR);
    camera->TargetTexture(camera_target.get());
    auto pipeline = MakeScope<RenderPipeline>();
    RenderPipeline::Register(pipeline.get());
    SceneManagement::SceneMgr::Get().OpenTemporaryScene(SceneManagement::SceneMgr::Get().Create("ToneMappingSmoke"));
    (void) Camera::GetDefaultCamera();
    int result = 0;
    try
    {
        Microsoft::WRL::ComPtr<ID3D12InfoQueue> info_queue;
        auto *device = static_cast<Ailu::RHI::DX12::D3DContext &>(GraphicsContext::Get()).GetDevice();
        Require(SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&info_queue))), "D3D12 debug info queue unavailable");
        info_queue->ClearStoredMessages();
        CheckPreExposure(app, directory);
        CheckToneMapping(app, directory);
        CheckD3DDebugErrors();
        std::cout << "D3D12 debug layer: no errors\n";
        std::cout << "ToneMapping GPU capture and serialization PASS\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    GraphicsContext::Get().WaitForGpu();
    SceneManagement::SceneMgr::Get().CloseTemporaryScene();
    RenderPipeline::Register(nullptr);
    pipeline.reset();
    camera->TargetTexture(nullptr);
    camera_target.reset();
    app.Finalize();
    return result;
}
