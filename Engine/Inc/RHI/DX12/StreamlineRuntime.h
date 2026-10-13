#pragma once

#include <cstdint>
#include <memory>
#include <mutex>

namespace Ailu::RHI::DX12
{
    std::mutex& GetStreamlineApiMutex();

    class StreamlineRuntime
    {
    public:
        StreamlineRuntime();
        ~StreamlineRuntime();

        StreamlineRuntime(const StreamlineRuntime&) = delete;
        StreamlineRuntime& operator=(const StreamlineRuntime&) = delete;

        bool Init();
        bool AttachD3D12Device(void* native_device, const void* adapter_luid, std::uint32_t luid_size);
        void Shutdown();
        bool IsLoaded() const;
        bool IsDlssSupported() const;
        bool GetDlssOptimalRenderSize(std::uint32_t mode, std::uint32_t output_width, std::uint32_t output_height,
                                      std::uint32_t& render_width, std::uint32_t& render_height) const;
        void* GetCoreFunction(const char* name) const;
        void* GetFeatureFunction(const char* name) const;
        bool UpgradeInterface(void** native_interface) const;
        bool GetNativeInterface(void* proxy_interface, void** native_interface) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> _impl;
    };
}
