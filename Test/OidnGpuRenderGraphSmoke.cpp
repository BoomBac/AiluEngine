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
#include <chrono>
#include <d3d12sdklayers.h>
#include <filesystem>
#include <fstream>
#include <iostream>

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
    // All writable project/config/log state belongs to a fresh test directory, never to the user's scene.
    const auto selected_directory = argc > 1 ? std::filesystem::path(argv[1])
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
    bool failed = false, unsupported = false;
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> queue;
    static_cast<RHI::DX12::D3DContext *>(g_pGfxContext)->GetDevice()->QueryInterface(IID_PPV_ARGS(&queue));
    if (queue)
    {
        queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, false);
        queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, false);
    }
    {
        PathTracingDenoiserHost host;
        Ref<RenderTexture> source, display;
        u32 spp = 0u, ready_count = 0u;
        u64 last_request = 0u;
        const auto started = std::chrono::steady_clock::now();
        for (u32 frame = 0u; frame < 90000u && ready_count < 100u; ++frame)
        {
            app.BeginFrame();
            app.UpdateResources(1.0f / 60.0f);
            const u32 width = ready_count < 50u ? 63u : 127u, height = ready_count < 50u ? 33u : 65u;
            if (!source || source->Width() != width)
            {
                TextureDesc texture(width, height, ERenderTargetFormat::kRGBAFloat);
                source = RenderTexture::Create(texture, "OIDN smoke source");
                display = RenderTexture::Create(texture, "OIDN smoke display");
                host.Reset();
                spp = 0u;
                last_request = 0u;
            }
            host.Configure(EPathTracingDenoiser::kOidnGpu, true, false);
            if (source->IsReady() && display->IsReady())
            {
                RDG::RenderGraph graph;
                graph.CreateResource(TextureDesc(1, 1, ERenderTargetFormat::kRGBAFloat), "Reserved Handle");
                auto radiance = graph.Import(source.get());
                auto raw_executed = std::make_shared<bool>(false);
                graph.AddPass(
                        "OIDN smoke radiance", RDG::PassDesc(),
                        [&](RDG::RenderGraphBuilder &builder) { radiance = builder.Write(radiance); },
                        [source, raw_executed](RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &)
                        {
                            cmd->SetRenderTarget(source.get());
                            cmd->ClearRenderTarget(Color(2.0f, 0.5f, 0.25f, 0.37f));
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
                input._quality =
                        ready_count < 75u ? EPathTracingDenoiserQuality::kBalanced : EPathTracingDenoiserQuality::kHigh;
                input._sample_interval = 4u;
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
                              << " SPP=" << metrics._last_sample_count << " latency_ms=" << metrics._latency_ms
                              << " shared_mb=" << metrics._shared_mb << '\n';
                    if (ready_count % 10u == 0u)
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
            if (std::chrono::steady_clock::now() - started > std::chrono::seconds(90))
            {
                failed = true;
                break;
            }
        }
        failed |= !unsupported && ready_count < 100u;
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
            if (SUCCEEDED(queue->GetMessage(i, message, &size)) && message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR)
            {
                std::cerr << message->pDescription << '\n';
                failed = true;
            }
        }
    }
    app.Finalize();
    std::cout << "OIDN RG smoke " << (failed ? "failed" : unsupported ? "unsupported" : "passed") << '\n';
    return failed ? 1 : unsupported ? 2 : 0;
}
