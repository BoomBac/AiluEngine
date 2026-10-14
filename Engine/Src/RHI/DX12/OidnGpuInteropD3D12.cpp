#include "OidnGpuInteropD3D12.h"
#include <Windows.h>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <wrl/client.h>

#ifndef AILU_WITH_OIDN_GPU
#define AILU_WITH_OIDN_GPU 0
#endif

namespace Ailu::RHI::DX12
{
    using Microsoft::WRL::ComPtr;
    // Private C ABI subset, pinned to OIDN 2.5.1 oidn.h. No SDK headers, import library or CPU backend.
    // Opaque pointers and enum/int signatures mirror the exported C ABI exactly.
    struct OidnGpuInteropD3D12::Impl
    {
        ComPtr<ID3D12Device> _device;
        ComPtr<ID3D12CommandQueue> _queue;
        ComPtr<ID3D12Resource> _input, _output;
        ComPtr<ID3D12Fence> _ready, _done, _consumed;
        HANDLE _handles[4]{};
        HMODULE _module = nullptr;
        void *_oidn_device = nullptr, *_filter = nullptr, *_buffers[2]{}, *_semaphores[2]{};
        uint64_t _request = 0u, _consume_value = 0u;
        bool _filtering = false, _initialized = false, _failed = false;
        std::string _reason = "Not initialized";
#define OIDN_PROC(result, name, ...) result (*name)(__VA_ARGS__) = nullptr
        OIDN_PROC(int, oidnGetNumPhysicalDevices);
        OIDN_PROC(int, oidnGetPhysicalDeviceInt, int, const char *);
        OIDN_PROC(bool, oidnGetPhysicalDeviceBool, int, const char *);
        OIDN_PROC(const char *, oidnGetPhysicalDeviceString, int, const char *);
        OIDN_PROC(const void *, oidnGetPhysicalDeviceData, int, const char *, size_t *);
        OIDN_PROC(void *, oidnNewDeviceByID, int);
        OIDN_PROC(void, oidnCommitDevice, void *);
        OIDN_PROC(int, oidnGetDeviceInt, void *, const char *);
        OIDN_PROC(int, oidnGetDeviceError, void *, const char **);
        OIDN_PROC(void, oidnSyncDevice, void *);
        OIDN_PROC(void, oidnReleaseDevice, void *);
        OIDN_PROC(void *, oidnNewSharedBufferFromWin32Handle, void *, unsigned, void *, const void *, size_t);
        OIDN_PROC(void, oidnReleaseBuffer, void *);
        OIDN_PROC(void *, oidnNewSharedSemaphoreFromWin32Handle, void *, unsigned, void *, const void *);
        OIDN_PROC(void, oidnReleaseSemaphore, void *);
        OIDN_PROC(void, oidnWaitSemaphoresAsync, void *, void *const *, const uint64_t *, const uint32_t *, int);
        OIDN_PROC(void, oidnSignalSemaphoresAsync, void *, void *const *, const uint64_t *, int);
        OIDN_PROC(void *, oidnNewFilter, void *, const char *);
        OIDN_PROC(void, oidnSetFilterImage, void *, const char *, void *, int, size_t, size_t, size_t, size_t, size_t);
        OIDN_PROC(void, oidnSetFilterBool, void *, const char *, bool);
        OIDN_PROC(void, oidnSetFilterInt, void *, const char *, int);
        OIDN_PROC(void, oidnCommitFilter, void *);
        OIDN_PROC(void, oidnExecuteFilterAsync, void *);
        OIDN_PROC(void, oidnReleaseFilter, void *);
#undef OIDN_PROC
        void Check(const char *operation)
        {
            const char *message = nullptr;
            if (oidnGetDeviceError(_oidn_device, &message) != 0)
                throw std::runtime_error(std::string(operation) + ": " + (message ? message : "OIDN error"));
        }
        static void CheckHr(HRESULT result)
        {
            if (FAILED(result)) throw std::runtime_error("DX12 failure HRESULT=" + std::to_string(result));
        }
        bool Fail(const std::exception &error)
        {
            _failed = true;
            _reason = error.what();
            std::cerr << "OIDN GPU Unsupported/Failed: " << _reason << '\n';
            return false;
        }
    };
    OidnGpuInteropD3D12::OidnGpuInteropD3D12() : _impl(std::make_unique<Impl>()) {}
    OidnGpuInteropD3D12::~OidnGpuInteropD3D12() { Shutdown(); }
    bool OidnGpuInteropD3D12::Initialize(ID3D12Device *device, ID3D12CommandQueue *exclusive_queue, uint32_t width,
                                         uint32_t height, const std::wstring &runtime_directory,
                                         bool external_queue, int quality)
    {
        Shutdown();
        _impl = std::make_unique<Impl>();
        auto &p = *_impl;
#if !AILU_WITH_OIDN_GPU
        p._reason = "AILU_WITH_OIDN_GPU=0 (no SDK/runtime required)";
        return false;
#else
        try
        {
            if (!device || (!exclusive_queue && !external_queue) || !width || !height ||
                width > 16384u || height > 16384u || quality < 4 || quality > 6)
                throw std::runtime_error("Invalid device/queue/dimensions; maximum dimension is 16384");
            const uint64_t bytes = uint64_t(width) * height * 16u;
            if (bytes > std::numeric_limits<uint32_t>::max()) throw std::runtime_error("Shared buffer exceeds 4 GiB");
            if (runtime_directory.empty()) throw std::runtime_error("Specify an absolute OIDN 2.5.1 bin directory");
            const std::wstring dll = runtime_directory + L"\\OpenImageDenoise.dll";
            if (dll.size() < 3 || (dll[1] != L':' && dll.substr(0, 2) != L"\\\\"))
                throw std::runtime_error("OIDN runtime directory must be absolute");
            p._module = LoadLibraryExW(dll.c_str(), nullptr,
                                       LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if (!p._module)
                throw std::runtime_error("Cannot load OIDN runtime/dependencies, Win32=" +
                                         std::to_string(GetLastError()));
#define LOAD_OIDN(name)                                                                                                \
    p.name = reinterpret_cast<decltype(p.name)>(GetProcAddress(p._module, #name));                                     \
    if (!p.name) throw std::runtime_error("Missing OIDN 2.5.1 export: " #name)
            LOAD_OIDN(oidnGetNumPhysicalDevices);
            LOAD_OIDN(oidnGetPhysicalDeviceInt);
            LOAD_OIDN(oidnGetPhysicalDeviceBool);
            LOAD_OIDN(oidnGetPhysicalDeviceString);
            LOAD_OIDN(oidnGetPhysicalDeviceData);
            LOAD_OIDN(oidnNewDeviceByID);
            LOAD_OIDN(oidnCommitDevice);
            LOAD_OIDN(oidnGetDeviceInt);
            LOAD_OIDN(oidnGetDeviceError);
            LOAD_OIDN(oidnSyncDevice);
            LOAD_OIDN(oidnReleaseDevice);
            LOAD_OIDN(oidnNewSharedBufferFromWin32Handle);
            LOAD_OIDN(oidnReleaseBuffer);
            LOAD_OIDN(oidnNewSharedSemaphoreFromWin32Handle);
            LOAD_OIDN(oidnReleaseSemaphore);
            LOAD_OIDN(oidnWaitSemaphoresAsync);
            LOAD_OIDN(oidnSignalSemaphoresAsync);
            LOAD_OIDN(oidnNewFilter);
            LOAD_OIDN(oidnSetFilterImage);
            LOAD_OIDN(oidnSetFilterBool);
            LOAD_OIDN(oidnSetFilterInt);
            LOAD_OIDN(oidnCommitFilter);
            LOAD_OIDN(oidnExecuteFilterAsync);
            LOAD_OIDN(oidnReleaseFilter);
#undef LOAD_OIDN
            const LUID luid = device->GetAdapterLuid();
            int selected = -1;
            for (int i = 0; i < p.oidnGetNumPhysicalDevices(); ++i)
            {
                if (p.oidnGetPhysicalDeviceInt(i, "type") != 3) continue;// CUDA only
                if (!p.oidnGetPhysicalDeviceBool(i, "luidSupported")) continue;
                size_t luid_size = 0u;
                const void *oidn_luid = p.oidnGetPhysicalDeviceData(i, "luid", &luid_size);
                p.Check("query CUDA LUID");
                if (oidn_luid && luid_size == sizeof(luid) && std::memcmp(oidn_luid, &luid, sizeof(luid)) == 0)
                {
                    selected = i;
                    break;
                }
            }
            if (selected < 0)
                throw std::runtime_error("No supported CUDA GPU with matching DX12 LUID (Turing+ required)");
            const int node_mask = p.oidnGetPhysicalDeviceInt(selected, "nodeMask");
            p.Check("query nodeMask");
            if (device->GetNodeCount() != 1 || node_mask != 1)
                throw std::runtime_error("M2 supports only one GPU node with nodeMask=1");
            p._oidn_device = p.oidnNewDeviceByID(selected);
            p.Check("create CUDA device");
            if (!p._oidn_device) throw std::runtime_error("CUDA device creation returned null");
            p.oidnCommitDevice(p._oidn_device);
            p.Check("commit CUDA device");
            const int version = p.oidnGetDeviceInt(p._oidn_device, "version");
            p.Check("query version");
            if (version != 20501) throw std::runtime_error("OIDN runtime must be exactly 2.5.1");
            constexpr unsigned kResource = 1u << 9u, kDedicated = 1u << 30u, kFence = 1u << 4u;
            const unsigned memory = unsigned(p.oidnGetDeviceInt(p._oidn_device, "externalMemoryTypes"));
            const unsigned semaphore = unsigned(p.oidnGetDeviceInt(p._oidn_device, "externalSemaphoreTypes"));
            p.Check("query external capabilities");
            std::cout << "OIDN 2.5.1 CUDA " << p.oidnGetPhysicalDeviceString(selected, "name")
                      << " LUID=" << luid.HighPart << ':' << luid.LowPart << " nodeMask=" << node_mask
                      << " memory=" << memory << " semaphore=" << semaphore << " logical_bytes=" << bytes * 2u
                      << " HDR_CPU_bytes=0\n";
            if ((memory & (kResource | kDedicated)) != (kResource | kDedicated) || !(semaphore & kFence))
                throw std::runtime_error("Missing D3D12_RESOURCE/DEDICATED or D3D12_FENCE import capability");
            p._device = device;
            p._queue = exclusive_queue;
            ComPtr<ID3D12Device> queue_device;
            if (exclusive_queue) Impl::CheckHr(exclusive_queue->GetDevice(IID_PPV_ARGS(&queue_device)));
            if (exclusive_queue && (queue_device.Get() != device ||
                exclusive_queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT))
                throw std::runtime_error("Exclusive DIRECT queue must belong to the same DX12 device");
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_DEFAULT;
            heap.CreationNodeMask = heap.VisibleNodeMask = 1u;
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            // CUDA imports the dedicated allocation, whose size is aligned, rather than the pixel payload alone.
            constexpr uint64_t kAllocationAlignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
            desc.Width = (bytes + (external_queue ? 16u : 0u) + kAllocationAlignment - 1u) /
                         kAllocationAlignment * kAllocationAlignment;
            desc.Height = 1u;
            desc.DepthOrArraySize = desc.MipLevels = 1u;
            desc.SampleDesc.Count = 1u;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            const auto allocation = device->GetResourceAllocationInfo(0u, 1u, &desc);
            if (allocation.SizeInBytes == UINT64_MAX || allocation.SizeInBytes < desc.Width)
                throw std::runtime_error("Invalid shared resource allocation size");
            std::cout << "GPU shared allocation bytes=" << allocation.SizeInBytes * 2u << '\n';
            ID3D12Resource **resources[] = {p._input.GetAddressOf(), p._output.GetAddressOf()};
            for (int i = 0; i < 2; ++i)
            {
                Impl::CheckHr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_SHARED, &desc,
                                                              D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                              IID_PPV_ARGS(resources[i])));
                Impl::CheckHr(device->CreateSharedHandle(*resources[i], nullptr, GENERIC_ALL, nullptr, &p._handles[i]));
                p._buffers[i] = p.oidnNewSharedBufferFromWin32Handle(
                        p._oidn_device, kResource | kDedicated, p._handles[i], nullptr, size_t(allocation.SizeInBytes));
                p.Check("import dedicated committed buffer");
                if (!p._buffers[i]) throw std::runtime_error("Buffer import returned null");
            }
            ID3D12Fence **fences[] = {p._ready.GetAddressOf(), p._done.GetAddressOf()};
            for (int i = 0; i < 2; ++i)
            {
                Impl::CheckHr(device->CreateFence(0u, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(fences[i])));
                Impl::CheckHr(
                        device->CreateSharedHandle(*fences[i], nullptr, GENERIC_ALL, nullptr, &p._handles[i + 2]));
                p._semaphores[i] =
                        p.oidnNewSharedSemaphoreFromWin32Handle(p._oidn_device, kFence, p._handles[i + 2], nullptr);
                p.Check("import timeline fence");
                if (!p._semaphores[i]) throw std::runtime_error("Fence import returned null");
            }
            Impl::CheckHr(device->CreateFence(0u, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&p._consumed)));
            p._filter = p.oidnNewFilter(p._oidn_device, "RT");
            p.Check("create RT filter");
            if (!p._filter) throw std::runtime_error("Filter creation returned null");
            p.oidnSetFilterImage(p._filter, "color", p._buffers[0], 3, width, height, 0, 16, size_t(width) * 16);
            p.oidnSetFilterImage(p._filter, "output", p._buffers[1], 3, width, height, 0, 16, size_t(width) * 16);
            p.oidnSetFilterBool(p._filter, "hdr", true);
            p.oidnSetFilterInt(p._filter, "quality", quality);
            p.oidnCommitFilter(p._filter);
            p.Check("commit HDR Float3/16-byte-stride filter");
            p._initialized = true;
            p._reason = "Idle";
            return true;
        }
        catch (const std::exception &error)
        {
            return p.Fail(error);
        }
#endif
    }
    bool OidnGpuInteropD3D12::Idle() const
    {
        const auto &p = *_impl;
        return p._initialized && !p._failed && !p._filtering &&
               (p._consume_value == 0u || p._consumed->GetCompletedValue() >= p._consume_value);
    }
    ID3D12Fence *OidnGpuInteropD3D12::ReadyFence() const { return _impl->_ready.Get(); }
    ID3D12Fence *OidnGpuInteropD3D12::DoneFence() const { return _impl->_done.Get(); }
    bool OidnGpuInteropD3D12::QueueExternalFilter(uint64_t request)
    {
        auto &p = *_impl;
        if (!p._initialized || p._failed || request <= p._request || p._queue) return false;
        try
        {
            p._request = request;
            p.oidnWaitSemaphoresAsync(p._oidn_device, &p._semaphores[0], &request, nullptr, 1);
            p.Check("external GPU wait");
            p.oidnExecuteFilterAsync(p._filter);
            p.Check("external GPU filter");
            p.oidnSignalSemaphoresAsync(p._oidn_device, &p._semaphores[1], &request, 1);
            p.Check("external GPU signal");
            return true;
        }
        catch (const std::exception &error) { return p.Fail(error); }
    }
    bool OidnGpuInteropD3D12::SubmitPack(ID3D12CommandList *closed_pack)
    {
        auto &p = *_impl;
        if (!closed_pack || !Idle() || p._request >= UINT64_MAX - 2u) return false;
        try
        {
            p._queue->ExecuteCommandLists(1u, &closed_pack);
            ++p._request;
            Impl::CheckHr(p._queue->Signal(p._ready.Get(), p._request));
            p._filtering = true;
            p.oidnWaitSemaphoresAsync(p._oidn_device, &p._semaphores[0], &p._request, nullptr, 1);
            p.Check("GPU wait ready");
            p.oidnExecuteFilterAsync(p._filter);
            p.Check("GPU filter enqueue");
            p.oidnSignalSemaphoresAsync(p._oidn_device, &p._semaphores[1], &p._request, 1);
            p.Check("GPU signal done");
            p._reason = "Filtering";
            return true;
        }
        catch (const std::exception &error)
        {
            return p.Fail(error);
        }
    }
    bool OidnGpuInteropD3D12::FilterComplete()
    {
        auto &p = *_impl;
        if (!p._filtering || p._failed) return false;
        try
        {
            p.Check("asynchronous filter status");
            const auto completed = p._done->GetCompletedValue();
            if (completed == UINT64_MAX) throw std::runtime_error("DX12 device removed");
            return completed >= p._request;
        }
        catch (const std::exception &error)
        {
            return p.Fail(error);
        }
    }
    bool OidnGpuInteropD3D12::SubmitConsume(ID3D12CommandList *closed_consume)
    {
        auto &p = *_impl;
        if (!closed_consume || !FilterComplete()) return false;
        try
        {
            Impl::CheckHr(p._queue->Wait(p._done.Get(), p._request));// Already completed, never stalls preview.
            p._queue->ExecuteCommandLists(1u, &closed_consume);
            p._consume_value = p._request;
            Impl::CheckHr(p._queue->Signal(p._consumed.Get(), p._consume_value));
            p._filtering = false;
            p._reason = "Consumed";
            return true;
        }
        catch (const std::exception &error)
        {
            return p.Fail(error);
        }
    }
    void OidnGpuInteropD3D12::Shutdown()
    {
        auto &p = *_impl;
        // Order: drain CUDA, drain exclusive DX12 queue, release imports, close handles, release resources/DLL.
        if (p._oidn_device && p.oidnSyncDevice) p.oidnSyncDevice(p._oidn_device);
        if (p._queue && p._device && p._request > 0u && p._device->GetDeviceRemovedReason() == S_OK)
        {
            // This fence already exists whenever SubmitPack is possible. No new allocation during retirement.
            const uint64_t drain_value = ++p._consume_value;
            HRESULT signal = p._queue->Signal(p._consumed.Get(), drain_value);
            while (FAILED(signal) && p._device->GetDeviceRemovedReason() == S_OK)
            {
                Sleep(1);
                signal = p._queue->Signal(p._consumed.Get(), drain_value);
            }
            HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (SUCCEEDED(signal) && event && SUCCEEDED(p._consumed->SetEventOnCompletion(drain_value, event)))
                WaitForSingleObject(event, INFINITE);
            else
                while (p._consumed->GetCompletedValue() < drain_value && p._device->GetDeviceRemovedReason() == S_OK)
                    Sleep(1);
            if (event) CloseHandle(event);
        }
        if (p._filter) p.oidnReleaseFilter(p._filter);
        for (void *buffer: p._buffers)
            if (buffer) p.oidnReleaseBuffer(buffer);
        for (void *semaphore: p._semaphores)
            if (semaphore) p.oidnReleaseSemaphore(semaphore);
        if (p._oidn_device) p.oidnReleaseDevice(p._oidn_device);
        for (HANDLE &handle: p._handles)
        {
            if (handle) CloseHandle(handle);
            handle = nullptr;
        }
        p._filter = p._oidn_device = nullptr;
        for (auto &buffer: p._buffers) buffer = nullptr;
        for (auto &semaphore: p._semaphores) semaphore = nullptr;
        p._input.Reset();
        p._output.Reset();
        p._ready.Reset();
        p._done.Reset();
        p._consumed.Reset();
        p._queue.Reset();
        p._device.Reset();
        if (p._module) FreeLibrary(p._module);
        p._module = nullptr;
        p._initialized = p._filtering = false;
    }
    ID3D12Resource *OidnGpuInteropD3D12::Input() const { return _impl->_input.Get(); }
    ID3D12Resource *OidnGpuInteropD3D12::Output() const { return _impl->_output.Get(); }
    const std::string &OidnGpuInteropD3D12::Reason() const { return _impl->_reason; }
    uint64_t OidnGpuInteropD3D12::RequestId() const { return _impl->_request; }
}// namespace Ailu::RHI::DX12
