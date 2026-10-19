#include "Framework/Common/Application.h"
#include "Framework/Common/CrashHandler.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/Profiler.h"
#include "Platform/WinWindow.h"
#include "RHI/DX12/D3DCommandBuffer.h"
#include "RHI/DX12/D3DContext.h"
#include "Render/CommandBuffer.h"
#include "Render/Denoiser/PathTracingDenoiser.h"
#include "Render/GraphicsContext.h"
#include "Render/RenderGraph/RenderGraph.h"
#include "Render/RenderingData.h"
#include <atomic>
#include <chrono>
#include <d3d12sdklayers.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

using namespace Ailu;
using namespace Ailu::Render;
class SmokeApplication final : public Application
{
public:
    using Application::BeginFrame;
    using Application::EndFrame;
    using Application::PresentFrame;
    using Application::RenderEditor;
    using Application::RenderFrame;
    using Application::UpdateResources;
    using Application::UpdateScenes;
};
int wmain(int argc, wchar_t **argv)
{
    bool full_hd = false, guides = false;
    int directory_argument = 1;
    for (; directory_argument < argc; ++directory_argument)
    {
        const std::wstring_view option(argv[directory_argument]);
        if (option == L"--full-hd") full_hd = true;
        else if (option == L"--guides") guides = true;
        else break;
    }
    if (argc > directory_argument + 1)
    {
        std::cerr << "Usage: OidnGpuRenderGraphSmoke [--full-hd] [--guides] [NEW_OUTPUT_DIRECTORY]\n";
        return 2;
    }
    const u32 target_results = full_hd ? 12u : 100u;
    const u32 resize_after = target_results / 2u;
    const u32 high_after = target_results * 3u / 4u;
    const u32 toggle_interval = full_hd ? 3u : 10u;
    // All writable project/config/log state belongs to a fresh test directory, never to the user's scene.
    const auto selected_directory = argc > directory_argument ? std::filesystem::path(argv[directory_argument])
                                             : std::filesystem::path(Application::GetWorkingPath()) /
                                                       (L"OidnRG_" + std::to_wstring(GetTickCount64()));
    const auto directory = std::filesystem::absolute(selected_directory);
    std::filesystem::create_directories(directory);
    const auto project = directory / L"Smoke.ailuproject", config = directory / L"EngineConfig.json";
    if (std::filesystem::exists(project) || std::filesystem::exists(config)) return 2;
    std::filesystem::current_path(directory);
    std::ofstream(project) << R"({"_file_version":1,"_project_name":"OIDN RG Smoke",
        "_project_guid":"e95e3ca7-1551-4c0d-b03e-b060f83c828c"})";
    std::ofstream(config) << R"({"None":0,"Default":1,"ShadowCaster":2,"All":4294967295,
        "MaxRenderObjectPerTask":512,"isMultiThreadRender":false,"enable_graphics_job":false,
        "EnableCpuStateBatchedSubmission":true,"EnableIncrementalGraphicsBinding":true,
        "_enable_d3d12_debug_layer":true,"_enable_rdc":false,"_enable_pix":false})";
    LogMgr::Init();
    pow2::Assert::SetLoggingHandler();
    InstallCrashHandler();
    Application::SetEngineConfigPath(config.wstring());
    Microsoft::WRL::ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
    SmokeApplication app;
    ApplicationInitContext init;
    init._project_file_path = project.wstring();
    init._enable_audio = false;
    ApplicationDesc desc;
    desc._window_width = desc._gameview_width = 64u;
    desc._window_height = desc._gameview_height = 64u;
    if (app.Initialize(desc, init) != 0) return 3;
    Profiler::Initialize();
    app.SetMultiThreadRendering(false);
    ShowWindow(static_cast<HWND>(app.GetWindow().GetNativeWindowPtr()), SW_HIDE);
    std::cerr << "OIDN smoke initialized\n" << std::flush;
    std::cout << "Mode=" << (full_hd ? "720p/1080p" : "odd-size")
              << " target_results=" << target_results << '\n';
    bool failed = false, unsupported = false;
    std::atomic<bool> validation_failed{false};
    Microsoft::WRL::ComPtr<ID3D12InfoQueue1> callback_queue;
    DWORD callback_cookie = 0u;
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> queue;
    static_cast<RHI::DX12::D3DContext *>(g_pGfxContext)->GetDevice()->QueryInterface(IID_PPV_ARGS(&queue));
    if (queue)
    {
        queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, false);
        queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, false);
    }
    // The context drains the message store each frame; an exit-only scan can miss earlier failures.
    if (!queue || FAILED(queue.As(&callback_queue)) ||
        FAILED(callback_queue->RegisterMessageCallback(
                [](D3D12_MESSAGE_CATEGORY, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id,
                   LPCSTR, void *context)
                {
                    if (severity <= D3D12_MESSAGE_SEVERITY_ERROR ||
                        id == D3D12_MESSAGE_ID_PIPELINE_STATE_TYPE_MISMATCH)
                        static_cast<std::atomic<bool> *>(context)->store(true);
                },
                D3D12_MESSAGE_CALLBACK_FLAG_NONE, &validation_failed, &callback_cookie)))
    {
        std::cerr << "D3D12 validation callback unavailable\n";
        app.Finalize();
        return 3;
    }
    {
        PathTracingDenoiserHost host;
        Ref<RenderTexture> source, display, albedo, normal;
        u32 spp = 0u, ready_count = 0u;
        u64 last_request = 0u;
        const auto started = std::chrono::steady_clock::now();
        for (u32 frame = 0u; frame < 90000u && ready_count < target_results; ++frame)
        {
            app.BeginFrame();
            app.UpdateResources(1.0f / 60.0f);
            const bool first_size = ready_count < resize_after;
            const u32 width = full_hd ? (first_size ? 1280u : 1920u) : (first_size ? 63u : 127u);
            const u32 height = full_hd ? (first_size ? 720u : 1080u) : (first_size ? 33u : 65u);
            if (!source || source->Width() != width)
            {
                TextureDesc texture(width, height, ERenderTargetFormat::kRGBAFloat);
                source = RenderTexture::Create(texture, "OIDN smoke source");
                display = RenderTexture::Create(texture, "OIDN smoke display");
                if (guides)
                {
                    albedo = RenderTexture::Create(texture, "OIDN smoke albedo");
                    normal = RenderTexture::Create(texture, "OIDN smoke normal");
                }
                host.Reset();
                spp = 0u;
                last_request = 0u;
            }
            host.Configure(EPathTracingDenoiser::kOidnGpu, true, false);
            if (source->IsReady() && display->IsReady() && (!guides || (albedo->IsReady() && normal->IsReady())))
            {
                RDG::RenderGraph graph;
                graph.CreateResource(TextureDesc(1, 1, ERenderTargetFormat::kRGBAFloat), "Reserved Handle");
                auto radiance = graph.Import(source.get());
                RDG::RGHandle albedo_handle{}, normal_handle{};
                if (guides)
                {
                    albedo_handle = graph.Import(albedo.get());
                    normal_handle = graph.Import(normal.get());
                }
                auto raw_executed = std::make_shared<bool>(false);
                graph.AddPass(
                        "OIDN smoke radiance", RDG::PassDesc(),
                        [&](RDG::RenderGraphBuilder &builder)
                        {
                            radiance = builder.Write(radiance);
                            if (guides)
                            {
                                albedo_handle = builder.Write(albedo_handle);
                                normal_handle = builder.Write(normal_handle);
                            }
                        },
                        [source, albedo, normal, raw_executed](RDG::RenderGraph &, CommandBuffer *cmd,
                                                             const RenderingData &)
                        {
                            cmd->SetRenderTarget(source.get());
                            cmd->ClearRenderTarget(Color(2.0f, 0.5f, 0.25f, 0.37f));
                            if (albedo && normal)
                            {
                                cmd->SetRenderTarget(albedo.get());
                                cmd->ClearRenderTarget(Color(0.5f, 0.25f, 0.1f, 1.0f));
                                cmd->SetRenderTarget(normal.get());
                                cmd->ClearRenderTarget(Color(0.0f, 0.0f, 1.0f, 1.0f));
                            }
                            *raw_executed = true;
                        });
                PathTracingDenoiserInput input;
                input._radiance = radiance;
                input._radiance_owner = source;
                input._raw_executed = raw_executed;
                input._width = width;
                input._height = height;
                input._sample_count = spp;
                input._valid_radiance = spp > 0u;
                input._quality = ready_count < high_after ? EPathTracingDenoiserQuality::kBalanced
                                                         : EPathTracingDenoiserQuality::kHigh;
                input._sample_interval = 4u;
                if (guides)
                {
                    input._use_guides = ready_count != resize_after; // Exercise guides -> color -> guides retirement.
                    input._albedo = albedo_handle;
                    input._normal = normal_handle;
                    input._albedo_owner = albedo;
                    input._normal_owner = normal;
                    if (ready_count < target_results / 4u) input._quality = EPathTracingDenoiserQuality::kFast;
                }
                const auto result = host.Record(graph, input);
                const auto selected = result._valid ? result._display_texture : radiance;
                const auto target = graph.Import(display.get());
                graph.AddPass(
                        "OIDN GPU consumer smoke", RDG::PassDesc(RDG::EPassType::kCopy),
                        [selected, target](RDG::RenderGraphBuilder &builder)
                        {
                            builder.Read(selected, EResourceUsage::kCopySrc);
                            (void) builder.Write(target, EResourceUsage::kCopyDst);
                        },
                        [selected, target](RDG::RenderGraph &pass_graph, CommandBuffer *cmd, const RenderingData &)
                        {
                            auto *src = pass_graph.Resolve<Texture>(selected);
                            auto *dst = pass_graph.Resolve<Texture>(target);
                            cmd->RecordNativeCallback(
                                    [src, dst](RHICommandBuffer *rhi)
                                    {
                                        static_cast<RHI::DX12::D3DCommandBuffer *>(rhi)->NativeCmdList()->CopyResource(
                                                dst->NativeResource().As<ID3D12Resource>(),
                                                src->NativeResource().As<ID3D12Resource>());
                                    });
                        });
                if (!graph.Compile())
                {
                    failed = true;
                    break;
                }
                RenderingData data;
                data._width = width;
                data._height = height;
                graph.Execute(*g_pGfxContext, data);
                graph.EndFrame();
                ++spp;
                if (result._valid && result._request_id != last_request)
                {
                    last_request = result._request_id;
                    ++ready_count;
                    const auto metrics = host.Diagnostics();
                    std::cout << "Ready=" << ready_count << " request=" << last_request
                              << " size=" << width << 'x' << height << " quality=" << static_cast<int>(input._quality)
                              << " guides=" << input._use_guides
                              << " SPP=" << metrics._last_sample_count << " latency_ms=" << metrics._latency_ms
                              << " shared_mb=" << metrics._shared_mb << '\n';
                    if (ready_count % toggle_interval == 0u)
                    {
                        host.Configure(EPathTracingDenoiser::kNone, true, false);
                        host.Configure(EPathTracingDenoiser::kOidnGpu, true, false);
                        spp = 0u;
                        last_request = 0u;
                    }
                }
                if (host.Status() == EDenoiserStatus::kUnsupported)
                {
                    std::cout << "Unsupported: " << host.Reason() << '\n';
                    unsupported = true;
                    break;
                }
                if (host.Status() == EDenoiserStatus::kFailed)
                {
                    std::cerr << host.Reason() << '\n';
                    failed = true;
                    break;
                }
            }
            app.RenderEditor();
            app.PresentFrame();
            app.EndFrame();
            Sleep(1);
            if (std::chrono::steady_clock::now() - started > std::chrono::seconds(full_hd ? 180 : 90))
            {
                failed = true;
                break;
            }
        }
        failed |= !unsupported && ready_count < target_results;
        host.Shutdown();
        // Draining is deliberate at test exit; the production Record path never waits on CUDA.
        g_pGfxContext->WaitForGpu();
    }
    if (queue)
    {
        for (u64 i = 0; i < queue->GetNumStoredMessagesAllowedByRetrievalFilter(); ++i)
        {
            SIZE_T size = 0u;
            queue->GetMessage(i, nullptr, &size);
            std::vector<u8> bytes(size);
            auto *message = reinterpret_cast<D3D12_MESSAGE *>(bytes.data());
            if (SUCCEEDED(queue->GetMessage(i, message, &size)) &&
                (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR ||
                 message->ID == D3D12_MESSAGE_ID_PIPELINE_STATE_TYPE_MISMATCH))
            {
                std::cerr << message->pDescription << '\n';
                failed = true;
            }
        }
    }
    app.Finalize();
    callback_queue->UnregisterMessageCallback(callback_cookie);
    failed |= validation_failed.load();
    std::cout << "OIDN RG smoke " << (failed ? "failed" : unsupported ? "unsupported" : "passed") << '\n';
    return failed ? 1 : unsupported ? 2 : 0;
}
