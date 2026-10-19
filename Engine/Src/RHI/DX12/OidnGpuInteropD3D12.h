#pragma once
#include <cstdint>
#include <d3d12.h>
#include <memory>
#include <string>

namespace Ailu::RHI::DX12
{
    // Isolated M2 owner. The caller owns an exclusive queue and serializes all calls on one thread.
    // Command lists must release input/output buffers to COMMON before crossing API boundaries.
    class OidnGpuInteropD3D12
    {
    public:
        OidnGpuInteropD3D12();
        ~OidnGpuInteropD3D12();
        OidnGpuInteropD3D12(const OidnGpuInteropD3D12 &) = delete;
        OidnGpuInteropD3D12 &operator=(const OidnGpuInteropD3D12 &) = delete;
        bool Initialize(ID3D12Device *device, ID3D12CommandQueue *exclusive_queue, uint32_t width, uint32_t height,
                        const std::wstring &runtime_directory, bool external_queue = false, int quality = 5,
                        bool use_guides = false);
        ID3D12Fence *ReadyFence() const;
        ID3D12Fence *DoneFence() const;
        bool QueueExternalFilter(uint64_t request);
        bool SubmitPack(ID3D12CommandList *closed_pack);
        bool FilterComplete();
        bool SubmitConsume(ID3D12CommandList *closed_consume);
        bool Idle() const;
        // Controlled standalone shutdown only; may synchronize. Never call from a production frame.
        void Shutdown();
        ID3D12Resource *Input() const;
        ID3D12Resource *Output() const;
        const std::string &Reason() const;
        uint64_t RequestId() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> _impl;
    };
}// namespace Ailu::RHI::DX12
