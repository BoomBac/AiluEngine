#include "Render/GpuResource.h"
#include "Render/GraphicsContext.h"
#include "pch.h"
#include "Framework/Common/Log.h"
#include <utility>

namespace Ailu::Render
{
    namespace
    {
#if defined(_DEBUG)
        std::atomic<u64> s_gpu_resolve_count{0u};
        std::atomic<u64> s_gpu_resolve_invalid_handle_count{0u};
        std::atomic<u64> s_gpu_resolve_locked_count{0u};
#endif
        // 最近一次帧边界快照，供自动化/UI 读取（与 RenderingStates::DisplayData 同语义）。
        GpuResourceResolveStats s_resolve_snapshot;
    }

    GpuResourceRegistry &GpuResourceRegistry::Get()
    {
        static GpuResourceRegistry s_registry;
        return s_registry;
    }

    GpuResourceResolveStats GpuResourceRegistry::TakeResolveStats()
    {
        GpuResourceResolveStats stats;
#if defined(_DEBUG)
        stats._total_resolve_count = s_gpu_resolve_count.exchange(0u, std::memory_order_acq_rel);
        stats._invalid_handle_resolve_count = s_gpu_resolve_invalid_handle_count.exchange(0u, std::memory_order_acq_rel);
        stats._locked_resolve_count = s_gpu_resolve_locked_count.exchange(0u, std::memory_order_acq_rel);
#endif
        s_resolve_snapshot = stats;
        return stats;
    }

    GpuResourceResolveStats GpuResourceRegistry::GetResolveStats()
    {
        return s_resolve_snapshot;
    }

    void GpuResourceRegistry::ResetResolveStats()
    {
#if defined(_DEBUG)
        s_gpu_resolve_count.store(0u, std::memory_order_relaxed);
        s_gpu_resolve_invalid_handle_count.store(0u, std::memory_order_relaxed);
        s_gpu_resolve_locked_count.store(0u, std::memory_order_relaxed);
#endif
    }

    GpuResourceHandle GpuResourceRegistry::Add(std::unique_ptr<GpuResource> resource)
    {
        if (resource == nullptr)
            return {};

        std::unique_lock lock(_resource_mutex);
        u32 index = kInvalidGpuHandleIndex;
        if (!_free_indices.empty())
        {
            index = _free_indices.back();
            _free_indices.pop_back();
        }
        else
        {
            index = static_cast<u32>(_slots.size());
            _slots.emplace_back();
        }
        _slot_count.store(static_cast<u32>(_slots.size()), std::memory_order_release);

        auto &slot = _slots[index];
        slot._state = EGpuResourceSlotState::kAlive;
        slot._resource = std::move(resource);
        const GpuResourceHandle handle{index, slot._generation};
        slot._resource->SetHandle(handle);
        return handle;
    }

    GpuResource *GpuResourceRegistry::Resolve(GpuResourceHandle handle)
    {
        // 无效句柄在加锁前直接拒绝：普通 draw 的 per-object cbuffer / indirect arg buffer
        // 大多为空句柄，不应为此获取 shared_mutex。
#if defined(_DEBUG)
        s_gpu_resolve_count.fetch_add(1u, std::memory_order_relaxed);
#endif
        if (!handle.IsValid() || handle._index >= _slot_count.load(std::memory_order_acquire))
        {
#if defined(_DEBUG)
            s_gpu_resolve_invalid_handle_count.fetch_add(1u, std::memory_order_relaxed);
#endif
            return nullptr;
        }
#if defined(_DEBUG)
        s_gpu_resolve_locked_count.fetch_add(1u, std::memory_order_relaxed);
#endif
        std::shared_lock lock(_resource_mutex);
        if (handle._index >= _slots.size())
            return nullptr;
        auto &slot = _slots[handle._index];
        if (slot._state != EGpuResourceSlotState::kAlive || slot._generation != handle._generation)
            return nullptr;
        return slot._resource.get();
    }

    const GpuResource *GpuResourceRegistry::Resolve(GpuResourceHandle handle) const
    {
#if defined(_DEBUG)
        s_gpu_resolve_count.fetch_add(1u, std::memory_order_relaxed);
#endif
        if (!handle.IsValid() || handle._index >= _slot_count.load(std::memory_order_acquire))
        {
#if defined(_DEBUG)
            s_gpu_resolve_invalid_handle_count.fetch_add(1u, std::memory_order_relaxed);
#endif
            return nullptr;
        }
#if defined(_DEBUG)
        s_gpu_resolve_locked_count.fetch_add(1u, std::memory_order_relaxed);
#endif
        std::shared_lock lock(_resource_mutex);
        if (handle._index >= _slots.size())
            return nullptr;
        const auto &slot = _slots[handle._index];
        if (slot._state != EGpuResourceSlotState::kAlive || slot._generation != handle._generation)
            return nullptr;
        return slot._resource.get();
    }

    void GpuResourceRegistry::Retire(GpuResourceHandle handle, u64 fence)
    {
        std::unique_lock lock(_resource_mutex);
        if (!handle.IsValid() || handle._index >= _slots.size())
            return;
        auto &slot = _slots[handle._index];
        if (slot._state != EGpuResourceSlotState::kAlive || slot._generation != handle._generation)
            return;

        RetiredGpuResource retired;
        retired._fence = fence;
        retired._resource = std::move(slot._resource);
        slot._state = EGpuResourceSlotState::kFree;
        ++slot._generation;
        if (slot._generation == 0u)
            slot._generation = 1u;
        _free_indices.emplace_back(handle._index);

        if (retired._fence == 0u)
            return;
        _retired_resources.emplace_back(std::move(retired));
    }

    void GpuResourceRegistry::Release(GpuResourceHandle handle)
    {
        if (!handle.IsValid())
            return;
        std::lock_guard lock(_pending_release_mutex);
        _pending_releases.emplace_back(handle);
    }

    Vector<GpuResourceHandle> GpuResourceRegistry::TakePendingReleases()
    {
        std::lock_guard lock(_pending_release_mutex);
        Vector<GpuResourceHandle> pending = std::move(_pending_releases);
        _pending_releases.clear();
        return pending;
    }

    void GpuResourceRegistry::Collect(u64 completed_fence)
    {
        std::unique_lock lock(_resource_mutex);
        while (!_retired_resources.empty() && _retired_resources.front()._fence <= completed_fence)
            _retired_resources.pop_front();
    }

    void GpuResourceRegistry::Clear()
    {
        {
            std::lock_guard lock(_pending_release_mutex);
            _pending_releases.clear();
        }

        // Resource destructors may release nested Ref objects, which re-enter Release().
        // Do not hold _pending_release_mutex while unique_ptr destructors are running.
        {
            std::unique_lock lock(_resource_mutex);
            _retired_resources.clear();
            _slots.clear();
            _free_indices.clear();
            _slot_count.store(0u, std::memory_order_release);
        }

        {
            std::lock_guard lock(_pending_release_mutex);
            _pending_releases.clear();
        }
    }

    #pragma region GpuResource
    GpuResource::GpuResource()
    {
    }
    GpuResource::~GpuResource()
    {
        s_total_mem_size -= _mem_size;
        //LOG_INFO("GpuResource::~GpuResource: {} released,mem size {}", _name, _mem_size);
    }
    void GpuResource::Apply()
    {
        // Async resource creation may be checked by UI/render code before the upload command runs.
        // Keep readiness independent from the per-queue resource state tracker.
        SetCreatedFence(0xFFFFFFFFFFFFFFFFu);
        GraphicsContext::Get().CreateResource(this);
    }
    void GpuResource::ApplySync()
    {
        GraphicsContext::Get().CreateResourceSync(this);
    }
    void GpuResource::UploadImpl(GraphicsContext* ctx,RHICommandBuffer* rhi_cmd,UploadParams* params)
    {
        s_total_mem_size += _mem_size;
    }
    void GpuResource::Bind(RHICommandBuffer *rhi_cmd, const BindParams& params)
    {
        if (IsReady())
            BindImpl(rhi_cmd, params);
        else
        {
            LOG_WARNING("GpuResource::Bind: {} not ready yet,forget call `GraphicsContext::CreateResource`?", _name);
        }
    }
    void GpuResource::Upload(GraphicsContext* ctx,RHICommandBuffer* rhi_cmd,UploadParams* params)
    {
        UploadImpl(ctx,rhi_cmd,params);
    }

    bool GpuResource::IsReady()
    {
        if (!_is_ready_for_rendering)
        {
            const u64 gpu_fence_value = GraphicsContext::Get().GetFenceValueGPU();
            _is_ready_for_rendering = gpu_fence_value >= _created_fence;
        }
        return _is_ready_for_rendering;
    }
}// namespace Ailu
