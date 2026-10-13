#pragma once

#include <dxgi1_6.h>
#include <d3d12.h>

namespace Ailu::Render
{
    class GpuResource;
}

namespace Ailu::RHI::DX12
{
    class StreamlineRuntime;

    class StreamlineDx12Bridge
    {
    public:
        // Returns an AddRef'd native resource. The graph/RHI usage declaration remains responsible for
        // resource state tracking and fence-based lifetime.
        static bool GetNativeResource(Render::GpuResource* resource, ID3D12Resource** native_resource);
        static bool UpgradeDevice(StreamlineRuntime& runtime, ID3D12Device* native_device, ID3D12Device** proxy_device);
        static bool UpgradeFactory(StreamlineRuntime& runtime, IDXGIFactory* native_factory,
                                   IDXGIFactory6** proxy_factory);
        static bool UpgradeSwapchain(StreamlineRuntime& runtime, IDXGISwapChain1* proxy_swapchain,
                                     IDXGISwapChain1** native_swapchain);
    };
}
