#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace Ailu::RHI::DX12
{
    class FsrRuntime
    {
    public:
        FsrRuntime();
        ~FsrRuntime();

        FsrRuntime(const FsrRuntime&) = delete;
        FsrRuntime& operator=(const FsrRuntime&) = delete;

        bool Init(void* native_device);
        void Shutdown();
        bool IsSupported() const;
        const std::string& GetError() const;
        const std::string& GetProviderVersion() const;
        std::uint64_t GetProviderId() const;
        bool GetRenderSize(std::uint32_t output_width, std::uint32_t output_height, std::uint32_t quality_mode,
                                  std::uint32_t& render_width, std::uint32_t& render_height) const;
        bool Evaluate(void* command_list, void* color, void* depth, void* motion_vectors, void* output,
                      std::uint64_t camera_id, std::uint64_t reset_revision,
                      std::uint32_t render_width, std::uint32_t render_height,
                      std::uint32_t output_width, std::uint32_t output_height,
                      float jitter_x, float jitter_y, float frame_time_ms,
                      float camera_near, float camera_far, float camera_fov_vertical);

    private:
        struct Impl;
        std::unique_ptr<Impl> _impl;
    };
}
