#include "RHI/DX12/StreamlineDx12Bridge.h"
#include "RHI/DX12/StreamlineRuntime.h"
#include "RHI/DX12/D3DGPUTimer.h"
#include "Framework/Common/Log.h"
#include "Render/GpuResource.h"
#include <wrl/client.h>
#include <iostream>
#include <memory>

using Microsoft::WRL::ComPtr;
using namespace Ailu::RHI::DX12;

namespace
{
    class NativeResourceStub final : public Ailu::Render::GpuResource
    {
    public:
        NativeResourceStub(Ailu::Render::RendererAPI::ERenderAPI api, ID3D12Resource* resource)
            : _api(api), _resource(resource)
        {
        }

        Ailu::Render::NativeHandle NativeResource() override { return {_api, _resource}; }

    private:
        Ailu::Render::RendererAPI::ERenderAPI _api;
        ID3D12Resource* _resource;
    };
}

int main()
{
    Ailu::LogMgr::Init();
    ID3D12Resource* native_resource = nullptr;
    if (StreamlineDx12Bridge::GetNativeResource(nullptr, &native_resource) || native_resource ||
        StreamlineDx12Bridge::GetNativeResource(nullptr, nullptr))
        return 9;

    ComPtr<IDXGIFactory6> factory;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<ID3D12Device> device;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))) ||
        FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))) ||
        FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
        return 1;

    D3D12_HEAP_PROPERTIES heap_properties{};
    heap_properties.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC resource_desc{};
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Width = 256u;
    resource_desc.Height = 1u;
    resource_desc.DepthOrArraySize = 1u;
    resource_desc.MipLevels = 1u;
    resource_desc.SampleDesc.Count = 1u;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> native_resource_owner;
    if (FAILED(device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&native_resource_owner))))
        return 10;
    NativeResourceStub valid_resource(Ailu::Render::RendererAPI::ERenderAPI::kDirectX12,
                                      native_resource_owner.Get());
    ComPtr<ID3D12Resource> bridged_resource;
    if (!StreamlineDx12Bridge::GetNativeResource(&valid_resource, bridged_resource.GetAddressOf()) ||
        bridged_resource.Get() != native_resource_owner.Get())
        return 11;
    NativeResourceStub wrong_backend(Ailu::Render::RendererAPI::ERenderAPI::kNone, native_resource_owner.Get());
    bridged_resource.Reset();
    if (StreamlineDx12Bridge::GetNativeResource(&wrong_backend, bridged_resource.GetAddressOf()) || bridged_resource)
        return 12;

    StreamlineRuntime runtime; // Deliberately uninitialized: upgrade must fail without acquiring ownership.
    const ULONG initial_ref_count = device->AddRef();
    device->Release();
    for (int attempt = 0; attempt < 32; ++attempt)
    {
        ComPtr<ID3D12Device> proxy_device;
        if (StreamlineDx12Bridge::UpgradeDevice(runtime, device.Get(), proxy_device.GetAddressOf()) || proxy_device)
        {
            // The buggy implementation publishes a borrowed pointer; do not Release it on this failure path.
            proxy_device.Detach();
            std::cerr << "FAIL: failed upgrade published a device reference\n";
            return 2;
        }
        const ULONG current_ref_count = device->AddRef();
        device->Release();
        if (current_ref_count != initial_ref_count)
            return 3;
    }
    ComPtr<IDXGIFactory> base_factory;
    if (FAILED(factory.As(&base_factory)))
        return 4;
    ComPtr<IDXGIFactory6> proxy_factory;
    if (StreamlineDx12Bridge::UpgradeFactory(runtime, base_factory.Get(), proxy_factory.GetAddressOf()) ||
        proxy_factory)
        return 5;
    ComPtr<ID3D12Device> null_device;
    if (StreamlineDx12Bridge::UpgradeDevice(runtime, nullptr, null_device.GetAddressOf()) || null_device ||
        StreamlineDx12Bridge::UpgradeDevice(runtime, device.Get(), nullptr))
        return 6;
    runtime.Shutdown();
    runtime.Shutdown();
    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))))
        return 7;
    const ULONG before_timer = device->AddRef();
    device->Release();
    std::unique_ptr<Ailu::IGPUTimer> timer = std::make_unique<D3DGPUTimer>(device.Get(), queue.Get(), 3);
    timer.reset();
    const ULONG after_timer = device->AddRef();
    device->Release();
    if (before_timer != after_timer)
    {
        std::cerr << "FAIL: polymorphic timer destruction retained device references\n";
        return 8;
    }
    std::cout << "PASS: 32 failed upgrades leave output empty and preserve native device reference count\n";
    std::cout << "PASS: native resource bridge validates backend and returns an owning COM reference\n";
    std::cout << "PASS: polymorphic GPU timer destruction releases its native resources\n";
    Ailu::LogMgr::Shutdown();
    return 0;
}
