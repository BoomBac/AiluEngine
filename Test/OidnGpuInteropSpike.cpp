// Independent M2 DX12/CUDA test. No Engine runtime, scene, RenderGraph or HDR CPU transfer.
#include "RHI/DX12/OidnGpuInteropD3D12.h"
#include <Windows.h>
#include <chrono>
#include <cstring>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <wrl/client.h>

using Ailu::RHI::DX12::OidnGpuInteropD3D12;
using Microsoft::WRL::ComPtr;
namespace
{
    void Check(HRESULT result)
    {
        if (FAILED(result)) throw std::runtime_error("HRESULT=" + std::to_string(result));
    }
    LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
    {
        if (message == WM_CLOSE)
        {
            DestroyWindow(window);
            return 0;
        }
        if (message == WM_DESTROY)
        {
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }
    void Transition(ID3D12GraphicsCommandList *cmd, ID3D12Resource *resource, D3D12_RESOURCE_STATES before,
                    D3D12_RESOURCE_STATES after)
    {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
        cmd->ResourceBarrier(1, &barrier);
    }
    ComPtr<ID3DBlob> Compile(const char *source, const char *entry)
    {
        ComPtr<ID3DBlob> bytecode, errors;
        const auto result = D3DCompile(source, std::strlen(source), "OIDN spike", nullptr, nullptr, entry, "cs_5_1",
                                       D3DCOMPILE_ENABLE_STRICTNESS, 0, &bytecode, &errors);
        if (FAILED(result) && errors) std::cerr << static_cast<const char *>(errors->GetBufferPointer());
        Check(result);
        return bytecode;
    }
    void SharedAllocationSmoke(ID3D12Device *device, UINT width, UINT height)
    {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = uint64_t(width) * height * 16u;
        desc.Height = 1;
        desc.DepthOrArraySize = desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        for (UINT iteration = 0; iteration < 32; ++iteration)
        {
            ComPtr<ID3D12Resource> resource;
            Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_SHARED, &desc, D3D12_RESOURCE_STATE_COMMON,
                                                  nullptr, IID_PPV_ARGS(&resource)));
            ComPtr<ID3D12Fence> fence;
            Check(device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fence)));
            HANDLE resource_handle = nullptr, fence_handle = nullptr;
            Check(device->CreateSharedHandle(resource.Get(), nullptr, GENERIC_ALL, nullptr, &resource_handle));
            const HRESULT result =
                    device->CreateSharedHandle(fence.Get(), nullptr, GENERIC_ALL, nullptr, &fence_handle);
            CloseHandle(resource_handle);
            if (fence_handle) CloseHandle(fence_handle);
            Check(result);
        }
        std::cout << "Shared resource/fence create/export/close passed: 32 iterations, bytes=" << desc.Width << '\n';
    }
    constexpr const char *kShader = R"(
RWStructuredBuffer<float4> input_buffer : register(u0);
RWStructuredBuffer<float4> output_buffer : register(u1);
RWTexture2D<float4> preview : register(u2);
RWTexture2D<float4> hdr_result : register(u3);
cbuffer Params : register(b0) { uint width; uint height; uint request; uint unused; }
[numthreads(8,8,1)] void Pack(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= width || id.y >= height) return;
    uint seed = (id.x + id.y * width) * 1664525u + request * 1013904223u;
    seed ^= seed >> 13;
    float noise = (float(seed & 1023u) / 1023.0 - 0.5) * 0.8;
    float3 color = float3(0.3 + 3.0 * id.x / width, 0.5 + 2.0 * id.y / height, 0.8);
    color += 0.15 * (request % 4u);
    if (id.x > width/3 && id.x < width/2 && id.y > height/3 && id.y < height/2) color += 4.0;
    input_buffer[id.x + id.y * width] = float4(max(color + noise, 0.0), 0.37);
}
[numthreads(8,8,1)] void Unpack(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= width || id.y >= height) return;
    uint index = id.x + id.y * width;
    float3 filtered = output_buffer[index].rgb;
    hdr_result[id.xy] = float4(filtered, 1.0);
    float3 color = id.x < width/2 ? input_buffer[index].rgb : filtered;
    // A magenta pixel exposes invalid output entirely on GPU. HDR data above remains linear.
    if (any(isnan(color)) || any(isinf(color)) || any(color < 0.0)) color = float3(100, 0, 100);
    preview[id.xy] = float4(pow(max(color, 0.0) / (1.0 + max(color, 0.0)), 1.0/2.2), 1.0);
})";
}// namespace
int wmain(int argc, wchar_t **argv)
{
    try
    {
        if (argc < 2)
        {
            std::cout << "OidnGpuInteropSpike <absolute OIDN-2.5.1 bin> [width=1023] [height=513] [requests=32]\n"
                         "--shared-only [width] [height]: DX12 handles only, no OIDN.\n"
                         "--disabled: verify no-SDK fallback without GPU initialization.\n"
                         "Left: noisy HDR; right: OIDN. Close during filtering tests shutdown. Exit 2=Unsupported.\n";
            return 2;
        }
        if (std::wstring(argv[1]) == L"--shader-only")
        {
            Compile(kShader, "Pack");
            Compile(kShader, "Unpack");
            std::cout << "Pack/Unpack HLSL compile passed (no GPU initialization)\n";
            return 0;
        }
        if (std::wstring(argv[1]) == L"--shader-file" && argc == 3)
        {
            for (const char *entry : {"PackHDR", "UnpackHDR", "ClearInvalidCounter"})
            {
                ComPtr<ID3DBlob> code, errors;
                const auto result = D3DCompileFromFile(argv[2], nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                    entry, "cs_5_1", D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &errors);
                if (FAILED(result) && errors) std::cerr << static_cast<const char *>(errors->GetBufferPointer());
                Check(result);
            }
            std::cout << "M3 PackHDR/UnpackHDR/ClearInvalidCounter compile passed\n";
            return 0;
        }
        if (std::wstring(argv[1]) == L"--disabled")
        {
            OidnGpuInteropD3D12 disabled;
            if (disabled.Initialize(nullptr, nullptr, 1, 1, L"")) return 1;
            std::cout << disabled.Reason() << '\n';
            return disabled.Reason().find("AILU_WITH_OIDN_GPU=0") != std::string::npos ? 0 : 1;
        }
        const UINT width = argc > 2 ? std::stoul(argv[2]) : 1023u;
        const UINT height = argc > 3 ? std::stoul(argv[3]) : 513u;
        const UINT requests = argc > 4 ? std::stoul(argv[4]) : 32u;
        if (!width || !height || width > 16384 || height > 16384 || !requests)
            throw std::runtime_error("Invalid dimensions/request count");
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
        ComPtr<IDXGIFactory6> factory;
        Check(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)));
        ComPtr<IDXGIAdapter1> adapter;
        ComPtr<ID3D12Device> device;
        for (UINT index = 0; factory->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                                 IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
             ++index)
        {
            DXGI_ADAPTER_DESC1 desc{};
            Check(adapter->GetDesc1(&desc));
            if (desc.VendorId == 0x10de && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
                SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
            {
                std::wcout << L"DX12 adapter: " << desc.Description << L'\n';
                LARGE_INTEGER driver{};
                if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driver)))
                    std::cout << "Driver version raw=" << driver.QuadPart << '\n';
                break;
            }
            adapter.Reset();
        }
        if (!device)
        {
            std::cerr << "Unsupported: no NVIDIA DX12 adapter\n";
            return 2;
        }
        ComPtr<ID3D12CommandQueue> queue;
        D3D12_COMMAND_QUEUE_DESC queue_desc{};
        queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        Check(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)));
        if (std::wstring(argv[1]) == L"--shared-only")
        {
            SharedAllocationSmoke(device.Get(), width, height);
            return 0;
        }
        // Interop is declared last relative to resources it references and drained explicitly before exit.
        OidnGpuInteropD3D12 interop;
        if (!interop.Initialize(device.Get(), queue.Get(), width, height, argv[1]))
        {
            std::cerr << "Unsupported: " << interop.Reason() << '\n';
            return 2;
        }
        WNDCLASSW window_class{};
        window_class.lpfnWndProc = WindowProc;
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.lpszClassName = L"AiluOidnGpuSpike";
        RegisterClassW(&window_class);
        const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        RECT rect{0, 0, LONG(width), LONG(height)};
        AdjustWindowRect(&rect, style, FALSE);
        HWND window = CreateWindowW(window_class.lpszClassName, L"OIDN GPU M2 - noisy | filtered", style, CW_USEDEFAULT,
                                    CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr,
                                    window_class.hInstance, nullptr);
        if (!window) throw std::runtime_error("Window creation failed");
        DXGI_SWAP_CHAIN_DESC1 swap_desc{};
        swap_desc.Width = width;
        swap_desc.Height = height;
        swap_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swap_desc.SampleDesc.Count = 1;
        swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap_desc.BufferCount = 2;
        swap_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> swap_base;
        Check(factory->CreateSwapChainForHwnd(queue.Get(), window, &swap_desc, nullptr, nullptr, &swap_base));
        ComPtr<IDXGISwapChain3> swap;
        Check(swap_base.As(&swap));
        Check(factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER));
        ComPtr<ID3D12Resource> backbuffers[2], preview, hdr;
        for (UINT i = 0; i < 2; ++i) Check(swap->GetBuffer(i, IID_PPV_ARGS(&backbuffers[i])));
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC texture{};
        texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texture.Width = width;
        texture.Height = height;
        texture.DepthOrArraySize = texture.MipLevels = 1;
        texture.SampleDesc.Count = 1;
        texture.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &texture,
                                              D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&preview)));
        texture.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &texture,
                                              D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&hdr)));
        hdr->SetName(L"OIDN Linear HDR Result (alpha=1)");
        D3D12_DESCRIPTOR_HEAP_DESC descriptor_desc{};
        descriptor_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        descriptor_desc.NumDescriptors = 2;
        descriptor_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ComPtr<ID3D12DescriptorHeap> descriptors;
        Check(device->CreateDescriptorHeap(&descriptor_desc, IID_PPV_ARGS(&descriptors)));
        auto cpu_handle = descriptors->GetCPUDescriptorHandleForHeapStart();
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uav.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        device->CreateUnorderedAccessView(preview.Get(), nullptr, &uav, cpu_handle);
        cpu_handle.ptr += device->GetDescriptorHandleIncrementSize(descriptor_desc.Type);
        uav.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        device->CreateUnorderedAccessView(hdr.Get(), nullptr, &uav, cpu_handle);
        D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 2, 0, 0};
        D3D12_ROOT_PARAMETER params[4]{};
        params[0].ParameterType = params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[0].Descriptor.ShaderRegister = 0;
        params[1].Descriptor.ShaderRegister = 1;
        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[2].DescriptorTable = {1, &range};
        params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[3].Constants = {0, 0, 4};
        D3D12_ROOT_SIGNATURE_DESC root_desc{4, params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        ComPtr<ID3DBlob> signature, errors;
        Check(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors));
        ComPtr<ID3D12RootSignature> root;
        Check(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                          IID_PPV_ARGS(&root)));
        ComPtr<ID3D12PipelineState> pack_pso, unpack_pso;
        D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = root.Get();
        auto pack_code = Compile(kShader, "Pack");
        pso.CS = {pack_code->GetBufferPointer(), pack_code->GetBufferSize()};
        Check(device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&pack_pso)));
        auto unpack_code = Compile(kShader, "Unpack");
        pso.CS = {unpack_code->GetBufferPointer(), unpack_code->GetBufferSize()};
        Check(device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&unpack_pso)));
        ComPtr<ID3D12CommandAllocator> allocator;
        Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
        ComPtr<ID3D12GraphicsCommandList> cmd;
        Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                        IID_PPV_ARGS(&cmd)));
        Check(cmd->Close());
        struct DrainOnExit
        {
            OidnGpuInteropD3D12 &_interop;
            ~DrainOnExit() { _interop.Shutdown(); }
        } drain{interop};
        UINT completed = 0;
        bool filtering = false, quit = false;
        auto submit_time = std::chrono::steady_clock::now();
        ShowWindow(window, SW_SHOW);
        while (!quit)
        {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                if (message.message == WM_QUIT) quit = true;
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if (quit) break;
            if ((!filtering && interop.Idle() && completed < requests) || (filtering && interop.FilterComplete()))
            {
                // Idle includes DX12 consume completion. FilterComplete includes pack completion.
                Check(allocator->Reset());
                Check(cmd->Reset(allocator.Get(), filtering ? unpack_pso.Get() : pack_pso.Get()));
                cmd->SetComputeRootSignature(root.Get());
                cmd->SetComputeRootUnorderedAccessView(0, interop.Input()->GetGPUVirtualAddress());
                cmd->SetComputeRootUnorderedAccessView(1, interop.Output()->GetGPUVirtualAddress());
                ID3D12DescriptorHeap *heaps[] = {descriptors.Get()};
                cmd->SetDescriptorHeaps(1, heaps);
                cmd->SetComputeRootDescriptorTable(2, descriptors->GetGPUDescriptorHandleForHeapStart());
                const UINT constants[] = {width, height, completed + 1u, 0};
                cmd->SetComputeRoot32BitConstants(3, 4, constants, 0);
                Transition(cmd.Get(), interop.Input(), D3D12_RESOURCE_STATE_COMMON,
                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                if (filtering)
                    Transition(cmd.Get(), interop.Output(), D3D12_RESOURCE_STATE_COMMON,
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                cmd->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
                // UAV -> COMMON transitions order all buffer writes before external access.
                Transition(cmd.Get(), interop.Input(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                           D3D12_RESOURCE_STATE_COMMON);
                if (filtering)
                {
                    Transition(cmd.Get(), interop.Output(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_COMMON);
                    D3D12_RESOURCE_BARRIER hdr_barrier{};
                    hdr_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                    hdr_barrier.UAV.pResource = hdr.Get();
                    cmd->ResourceBarrier(1, &hdr_barrier);
                    auto *back = backbuffers[swap->GetCurrentBackBufferIndex()].Get();
                    Transition(cmd.Get(), preview.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
                    Transition(cmd.Get(), back, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
                    cmd->CopyResource(back, preview.Get());
                    Transition(cmd.Get(), back, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
                    Transition(cmd.Get(), preview.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    Check(cmd->Close());
                    if (!interop.SubmitConsume(cmd.Get())) throw std::runtime_error(interop.Reason());
                    Check(swap->Present(1, 0));
                    ++completed;
                    std::cout << "request=" << interop.RequestId() << " fence_latency_ms="
                              << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                                           submit_time)
                                         .count()
                              << '\n';
                    filtering = false;
                }
                else
                {
                    Check(cmd->Close());
                    submit_time = std::chrono::steady_clock::now();
                    if (!interop.SubmitPack(cmd.Get())) throw std::runtime_error(interop.Reason());
                    filtering = true;
                }
            }
            if (filtering && std::chrono::steady_clock::now() - submit_time > std::chrono::seconds(60))
                throw std::runtime_error("GPU completion timed out; inspect OIDN/driver/debug-layer logs");
            // Pump window messages while GPU work runs; no CPU fence/event wait in the request loop.
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 1, QS_ALLINPUT);
        }
        interop.Shutdown();
        std::cout << "Completed requests=" << completed << "; visual/capture acceptance remains manual\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
