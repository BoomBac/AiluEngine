#include "Framework/Common/Application.h"
#include "Framework/Common/Log.h"
#include "RHI/DX12/D3DContext.h"
#include "RHI/DX12/DescriptorManager.h"
#include <d3d12sdklayers.h>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <windows.h>

using namespace Ailu;
using namespace Ailu::Render;
using namespace Ailu::RHI::DX12;

static bool ExercisePageGrowth(ID3D12Device *device, std::ostream &report)
{
    GPUVisibleDescriptorAllocator allocator;
    auto anchor = allocator.Allocate();
    auto *anchor_page = anchor.Page();
    const auto anchor_cpu = std::get<0>(anchor.At(0));
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(nullptr, &srv, anchor_cpu);

    std::atomic<bool> copying = true;
    std::atomic<bool> failed = false;
    std::atomic<u32> copies = 0;
    std::thread worker([&]()
    {
        while (copying.load())
        {
            if (allocator.GetBindGpuHandle(anchor).ptr == 0)
                failed.store(true);
            ++copies;
        }
    });
    Vector<GPUVisibleDescriptorAllocation> allocations;
    allocations.reserve(16);
    for (u32 page_index = 0; page_index < 16; ++page_index)
    {
        auto allocation = allocator.Allocate(1023);
        for (u16 index = 0; index < allocation.DescriptorNum(); ++index)
            device->CreateShaderResourceView(nullptr, &srv, std::get<0>(allocation.At(index)));
        if (anchor.Page() != anchor_page || std::get<0>(anchor.At(0)).ptr != anchor_cpu.ptr
            || allocator.GetBindGpuHandle(allocation).ptr == 0)
            failed.store(true);
        allocations.push_back(std::move(allocation));
    }
    copying.store(false);
    worker.join();
    u32 total = 0, available = 0;
    allocator.AllocationInfo(total, available);
    const auto used = 1u + 16u * 1023u;
    if (total <= 10u * 1024u || total - available != used || copies.load() == 0)
        failed.store(true);
    const auto generation = anchor.Generation();
    allocator.Free(std::move(anchor));
    for (auto &allocation : allocations)
        allocator.Free(std::move(allocation));
    const auto released = allocator.ReleaseSpace();
    auto reused = allocator.Allocate();
    if (released != used || reused.Page() != anchor_page || reused.Generation() == generation)
        failed.store(true);
    device->CreateShaderResourceView(nullptr, &srv, std::get<0>(reused.At(0)));
    if (allocator.GetBindGpuHandle(reused).ptr == 0)
        failed.store(true);
    allocator.Free(std::move(reused));
    allocator.ReleaseSpace();
    report << "pages=" << total / 1024u << " descriptors=" << used << " concurrent_copies=" << copies
              << " released=" << released << " failed=" << failed << '\n';
    return !failed.load();
}

int wmain(int argc, wchar_t **argv)
{
    if (argc != 4)
        return 2;
    pow2::Assert::SetLoggingHandler();
    LogMgr::Init();
    Application::SetEngineConfigPath(argv[3]);
    ComPtr<ID3D12Debug> debug;
    if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
        return 3;
    debug->EnableDebugLayer();
    Application app;
    ApplicationInitContext init;
    init._project_file_path = argv[1];
    init._enable_audio = false;
    ApplicationDesc desc;
    desc._window_width = desc._gameview_width = 320;
    desc._window_height = desc._gameview_height = 180;
    if (app.Initialize(desc, init) != 0)
        return 3;
    ShowWindow(static_cast<HWND>(app.GetWindow().GetNativeWindowPtr()), SW_HIDE);
    auto *device = static_cast<D3DContext &>(GraphicsContext::Get()).GetDevice();
    ComPtr<ID3D12InfoQueue> queue;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&queue))))
        return 3;
    queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, false);
    queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, false);
    std::filesystem::create_directories(argv[2]);
    std::ofstream report(std::filesystem::path(argv[2]) / "validation.txt");
    const bool first = ExercisePageGrowth(device, report);
    const bool second = ExercisePageGrowth(device, report);
    u32 gpu_errors = 0;
    for (u64 index = 0; index < queue->GetNumStoredMessagesAllowedByRetrievalFilter(); ++index)
    {
        SIZE_T size = 0;
        queue->GetMessage(index, nullptr, &size);
        Vector<u8> bytes(size);
        auto *message = reinterpret_cast<D3D12_MESSAGE *>(bytes.data());
        if (SUCCEEDED(queue->GetMessage(index, message, &size))
            && message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR)
        {
            ++gpu_errors;
            std::cerr << "D3D12 " << message->ID << ": " << message->pDescription << '\n';
        }
    }
    report << "first_allocator=" << first << "\nsecond_allocator=" << second
           << "\nD3D12_ERROR_CORRUPTION=" << gpu_errors << '\n';
    report.close();
    app.Finalize();
    return first && second && gpu_errors == 0 ? 0 : 1;
}
