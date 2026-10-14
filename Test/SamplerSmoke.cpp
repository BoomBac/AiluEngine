#include "Framework/Common/Application.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/CrashHandler.h"
#include "Platform/WinWindow.h"
#include "Render/GraphicsContext.h"
#include "Render/Texture.h"
#include "Render/Buffer.h"
#include "Render/CommandBuffer.h"
#include "Render/Shader.h"
#include "Render/Material.h"
#include "Render/Camera.h"
#include "Render/ImageCapture/ImageCaptureService.h"
#include <thread>
#include <chrono>
#include "Assets/AssetDocument.h"
#include <cmath>
#include "RHI/DX12/D3DContext.h"
#include <d3d12sdklayers.h>
#include "Objects/JsonArchive.h"
#include "Objects/SerializeSpecializations.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace Ailu;
using namespace Ailu::Render;
static void Require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
class SmokeApplication final : public Application
{
public:
    void Flush() { RenderEditor(); PresentFrame(); GraphicsContext::Get().WaitForGpu(); }
};
static void CheckRasterSampling(SmokeApplication &app, Texture2D *input, u32 sampler_index, const std::filesystem::path &directory)
{
    auto &context = GraphicsContext::Get();
    auto shader = Shader::Create((std::filesystem::path(__FILE__).parent_path() / "SamplerRasterSmoke.hlsl").wstring());
    Require(shader != nullptr && shader->Compile(EShaderCompilePolicy::kForceCompile), "raster sampler shader must compile");
    auto material = MakeRef<Material>(shader.get(), "SamplerRasterSmoke");
    material->SetTexture("_AlbedoTex", input);
    material->SetInt("_AlbedoTexSamplerIndex", sampler_index);
    TextureDesc target_desc(4u, 4u, ERenderTargetFormat::kRGBAFloat);
    auto target = RenderTexture::Create(target_desc, "SamplerRasterSmoke");
    app.Flush();
    struct Phase { const char *_name; f32 _manual; f32 _automatic; bool _apply_auto; };
    const Phase phases[]{ {"raster_base",0,0,true}, {"raster_auto",0,1,true},
        {"raster_opt_out",0,1,false}, {"raster_combined",1,1,true}, {"raster_negative",-1,0,true} };
    for (const auto &phase : phases)
    {
        material->SetFloat("_TestManualBias", phase._manual);
        material->SetFloat("_TestEffectiveBias", phase._manual + phase._automatic);
        material->SetInt("_TextureViewMipBiasMask", phase._apply_auto ? 1 : 0);
        auto cmd = CommandBufferPool::Get(phase._name);
        cmd->SetRenderTarget(target.get());
        cmd->DrawFullScreenQuad(material.get());
        ImageCaptureSequenceDesc sequence;
        sequence._output_directory = directory.wstring();
        sequence._file_prefix = phase._name;
        ImageCaptureTextureDesc source;
        source._debug_name = phase._name;
        const auto dds_session = ImageCaptureService::CaptureOnce(*cmd, target.get(), source, sequence);
        sequence._output_format = EImageCaptureOutputFormat::kPng;
        sequence._file_prefix += "_preview";
        const auto png_session = ImageCaptureService::CaptureOnce(*cmd, target.get(), source, sequence);
        context.ExecuteCommandBufferSync(cmd);
        context.WaitForGpu();
        CommandBufferPool::Release(cmd);
        for (u32 attempt = 0; attempt < 1000u; ++attempt)
        {
            ImageCaptureService::Tick();
            const auto dds = ImageCaptureService::GetStatus(dds_session), png = ImageCaptureService::GetStatus(png_session);
            Require(dds._state != EImageCaptureState::kFailed && png._state != EImageCaptureState::kFailed, "raster capture must not fail");
            if (dds._state == EImageCaptureState::kCompleted && png._state == EImageCaptureState::kCompleted) break;
            Require(attempt < 999u, "raster capture timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
}

static void CheckGpuSampling(SmokeApplication &app, const std::filesystem::path &directory)
{
    auto &context = GraphicsContext::Get();
    auto input = Texture2D::Create(4, 4, ETextureFormat::kRGBAFloat, true);
    input->sRGB(false);
    for (u16 mip = 0; mip < input->MipmapLevel(); ++mip)
    {
        const u16 size = std::max<u16>(1u, 4u >> mip);
        for (u16 y = 0; y < size; ++y)
            for (u16 x = 0; x < size; ++x)
                input->SetPixel(x, y, mip == 0 ? (x % 2 ? Color(0,1,0,1) : Color(1,0,0,1)) :
                    mip == 1 ? Color(0,0,1,1) : Color(1,1,1,1), mip);
    }
    input->Apply();
    BufferDesc output_desc;
    output_desc._size = 256u;
    output_desc._element_size = 16u;
    output_desc._element_num = 16u;
    output_desc._target = kStructured;
    output_desc._is_random_write = output_desc._is_create_uav = true;
    auto output = GPUBuffer::CreateSync(output_desc, "SamplerSmokeOutput");
    auto shader = ComputeShader::Create((std::filesystem::path(__FILE__).parent_path() / "SamplerSmoke.hlsl").wstring());
    Require(shader->Compile(EShaderCompilePolicy::kForceCompile), "sampler compute shader must compile");
    app.Flush();
    shader->SetTexture("_Input", input.get());
    shader->SetBuffer("_Output", output.get());
    SamplerDesc sampler;
    sampler._filter = EFilterMode::kPoint;
    const auto repeat = context.GetSamplerIndex(sampler);
    shader->SetInt("_Repeat", repeat);
    shader->SetInt("_Point", repeat);
    sampler._address_u = EWrapMode::kClamp;
    shader->SetInt("_Clamp", context.GetSamplerIndex(sampler));
    sampler._filter = EFilterMode::kTrilinear;
    shader->SetInt("_Linear", context.GetSamplerIndex(sampler));
    sampler._mip_bias = 1;
    shader->SetInt("_LodSampler", context.GetSamplerIndex(sampler));
    auto cmd = CommandBufferPool::Get("SamplerSmoke");
    cmd->Dispatch(shader.get(), shader->FindKernel("Main"), 1, 1, 1);
    context.ExecuteCommandBufferSync(cmd);
    context.WaitForGpu();
    Vector4f values[16]{};
    context.ReadBack(output.get(), reinterpret_cast<u8 *>(values), sizeof(values));
    CommandBufferPool::Release(cmd);
    const Vector4f expected[]{Vector4f(1,0,0,1), Vector4f(0,1,0,1), Vector4f(1,0,0,1),
        Vector4f(.5f,.5f,0,1), Vector4f(0,0,1,1), Vector4f(1,1,1,1)};
    std::ofstream report(directory / "gpu_samples.txt");
    for (u32 i = 0; i < 6u; ++i)
    {
        report << i << ": " << values[i].x << ',' << values[i].y << ',' << values[i].z << ',' << values[i].w << '\n';
        const auto delta = values[i] - expected[i];
        Require(std::abs(delta.x) + std::abs(delta.y) + std::abs(delta.z) + std::abs(delta.w) < .02f,
            "GPU sampler address/filter/mip result differs from expected color");
    }
    CheckRasterSampling(app, input.get(), repeat, directory);
    auto material = Material::CreateStandard("SamplerPolicy");
    Require(material != nullptr, "standard material must be available");
    const auto property = ShaderPropertyRegistry::Get().Intern("_AlbedoTex");
    material->SetTexture(property, input.get());
    SamplerDesc texture_default;
    texture_default._address_v = EWrapMode::kMirror;
    input->SetSamplerDesc(texture_default);
    Require(material->GetTextureSamplerDesc(property)._address_v == EWrapMode::kMirror, "material must inherit texture defaults");
    SamplerDesc override_desc;
    override_desc._address_u = EWrapMode::kMirror;
    material->SetSamplerOverride(property, override_desc);
    Require(material->GetTextureSamplerDesc(property)._address_u == EWrapMode::kMirror, "slot override must win");
    auto clone = material->CreateInstance();
    Require(clone->GetTextureSamplerDesc(property)._address_u == EWrapMode::kMirror, "material instance must copy overrides");
    material->ClearSamplerOverride(property);
    Require(!material->GetTextureSamplingSettings(property)._override_sampler, "clearing override restores inheritance");
    AssetTextureBinding original;
    original._name = "_AlbedoTex";
    original._sampling._override_sampler = true;
    original._sampling._sampler_desc = override_desc;
    original._sampling._apply_view_mip_bias = false;
    JsonArchive writer;
    const String binding_name = "binding";
    SerializerWrapper<AssetTextureBinding>::Serialize(&original, writer, &binding_name);
    JsonArchive reader;
    Require(reader.LoadFromString(writer.SaveToString()), "binding JSON must parse");
    AssetTextureBinding loaded;
    SerializerWrapper<AssetTextureBinding>::Deserialize(&loaded, reader, &binding_name);
    JsonArchive old_reader;
    Require(old_reader.LoadFromString(R"({"binding":{"_name":"_AlbedoTex","_texture_guid":""}})"), "legacy binding must parse");
    AssetTextureBinding old_binding;
    SerializerWrapper<AssetTextureBinding>::Deserialize(&old_binding, old_reader, &binding_name);
    Require(!old_binding._sampling._override_sampler && old_binding._sampling._apply_view_mip_bias,
        "legacy materials must inherit defaults and opt into view bias");
    Require(loaded._sampling._override_sampler && !loaded._sampling._apply_view_mip_bias &&
        loaded._sampling._sampler_desc._address_u == EWrapMode::kMirror, "slot sampling must persist");
}

static LONG WINAPI ReportDebugException(EXCEPTION_POINTERS *exception)
{
    if (exception->ExceptionRecord->ExceptionCode != 0x87a || g_pGfxContext == nullptr) return EXCEPTION_CONTINUE_SEARCH;
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> queue;
    auto *device = static_cast<RHI::DX12::D3DContext *>(g_pGfxContext)->GetDevice();
    if (device != nullptr && SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&queue))))
    {
        std::ofstream report("gpu_errors.txt", std::ios::app);
        for (u64 index = 0; index < queue->GetNumStoredMessagesAllowedByRetrievalFilter(); ++index)
        {
            SIZE_T size = 0;
            queue->GetMessage(index, nullptr, &size);
            Vector<u8> bytes(size);
            auto *message = reinterpret_cast<D3D12_MESSAGE *>(bytes.data());
            if (SUCCEEDED(queue->GetMessage(index, message, &size))) report << message->ID << ": " << message->pDescription << '\n';
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

int wmain(int argc, wchar_t **argv)
{
    if (argc != 2) return 2;
    const auto directory = std::filesystem::absolute(argv[1]);
    std::filesystem::create_directories(directory);
    const auto project = directory / L"Sampler.ailuproject", config = directory / L"EngineConfig.json";
    if (std::filesystem::exists(project) || std::filesystem::exists(config)) return 2;
    std::filesystem::current_path(directory);
    std::ofstream(project) << R"({"_file_version":1,"_project_name":"Sampler Smoke","_project_guid":"df51b817-5ae0-4a80-b975-616ff482c18e"})";
    std::ofstream(config) << R"({"None":0,"Default":1,"ShadowCaster":2,"All":4294967295,"isMultiThreadRender":false,"enable_graphics_job":false,"_enable_d3d12_debug_layer":true,"_enable_rdc":false,"_enable_pix":false})";
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    LogMgr::Init();
    InstallCrashHandler();
    AddVectoredExceptionHandler(1, ReportDebugException);
    Application::SetEngineConfigPath(config.wstring());
    SmokeApplication app;
    ApplicationInitContext init;
    init._project_file_path = project.wstring();
    init._enable_audio = false;
    ApplicationDesc window;
    window._window_width = window._gameview_width = 64;
    window._window_height = window._gameview_height = 64;
    if (app.Initialize(window, init) != 0) return 3;
    ShowWindow(static_cast<HWND>(app.GetWindow().GetNativeWindowPtr()), SW_HIDE);
    app.SetMultiThreadRendering(false);
    int result = 0;
    try
    {
        auto &context = GraphicsContext::Get();
        SamplerDesc desc;
        const u32 initial = context.GetSamplerIndex(desc);
        Require(context.GetSamplerIndex(desc) == initial, "identical sampler must share descriptor");
        desc._address_u = EWrapMode::kClamp;
        const u32 clamp = context.GetSamplerIndex(desc);
        Require(clamp != initial, "different address mode must select another descriptor");
        desc._mip_bias = 1.0f;
        Require(context.GetSamplerIndex(desc) != clamp, "bias must participate in sampler identity");
        desc._mip_bias = 0.0f;
        Require(context.GetSamplerIndex(desc) == clamp, "changing defaults must preserve existing descriptors");
        desc._max_anisotropy = 0u;
        const auto normalized = context.GetSamplerIndex(desc);
        desc._max_anisotropy = 1u;
        Require(context.GetSamplerIndex(desc) == normalized, "native normalized values must deduplicate");
        JsonArchive writer;
        const String name = "sampler";
        SerializerWrapper<SamplerDesc>::Serialize(&desc, writer, &name);
        JsonArchive reader;
        Require(reader.LoadFromString(writer.SaveToString()), "sampler JSON must parse");
        SamplerDesc loaded;
        SerializerWrapper<SamplerDesc>::Deserialize(&loaded, reader, &name);
        Require(context.GetSamplerIndex(loaded) == normalized, "sampler roundtrip must retain GPU state");
        Camera camera;
        camera.OutputSize(1920u, 1080u);
        camera.ResolveTemporalMode(EResolvedTemporalMode::kDlssSR, {1280u,720u}, true);
        Require(std::abs(camera.GetAutoTextureMipBias() - std::log2(2.0f / 3.0f)) < .0001f, "DLSS bias must use actual render/output ratio");
        camera.ResolveTemporalMode(EResolvedTemporalMode::kFsrSR, {1280u,720u}, false, true);
        Require(std::abs(camera.GetAutoTextureMipBias() - (std::log2(2.0f / 3.0f) - 1.0f)) < .0001f, "FSR bias must include provider offset");
        camera.SetAutoTextureMipBiasEnabled(false);
        Require(camera.GetAutoTextureMipBias() == 0.0f, "automatic bias must be switchable");
        camera.SetAutoTextureMipBiasEnabled(true);
        camera.ResolveTemporalMode(EResolvedTemporalMode::kTAA, {1920u,1080u}, false);
        Require(camera.GetAutoTextureMipBias() == 0.0f, "unavailable SR fallback must not keep SR bias");
        camera.PrepareRenderFrame(1u, 0.0f);
        const auto revision = camera.GetTemporalHistoryResetRevision();
        camera.PrepareRenderFrame(2u, 1.0f);
        Require(camera.GetTemporalHistoryResetRevision() > revision, "manual bias changes must reset camera history");
        CheckGpuSampling(app, directory);
        std::ofstream(directory / "checks.txt") << "Sampler cache, persistence and GPU sampling PASS\n";
    }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; result = 1; }
    app.SetMultiThreadRendering(false);
    app.Flush();
    app.Finalize();
    std::ofstream(directory / "shutdown.txt") << "clean shutdown\n";
    return result;
}
