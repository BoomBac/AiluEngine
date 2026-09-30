#include "Render/GpuResource.h"
#include "Render/GraphicsContext.h"
#include "pch.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/Profiler.h"
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

        std::lock_guard lock(_pending_mutation_mutex);
        GpuResourceHandle handle;
        if (!_available_handles.empty())
        {
            handle = _available_handles.back();
            _available_handles.pop_back();
        }
        else
        {
            handle = {_next_slot_index++, 1u};
        }
        resource->SetHandle(handle);
        _pending_adds.emplace_back(PendingGpuResourceAdd{handle, std::move(resource)});
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
        if (!handle.IsValid())
            return;
        std::lock_guard lock(_pending_mutation_mutex);
        _pending_retires.emplace_back(PendingGpuResourceRetire{handle, fence});
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

    GpuResourcePendingStats GpuResourceRegistry::GetPendingStats() const
    {
        GpuResourcePendingStats stats;
#if defined(_DEBUG)
        std::lock_guard lock(_pending_mutation_mutex);
        stats._pending_add_count = static_cast<u32>(_pending_adds.size());
        stats._pending_retire_count = static_cast<u32>(_pending_retires.size());
#endif
        return stats;
    }

    void GpuResourceRegistry::Sync(u64 completed_fence)
    {
        PROFILE_BLOCK_CPU("GpuResourceRegistry::Sync")
        // 阶段 2B 的过渡保护：保留独占锁，与 Resolve 的 shared lock 配对。
        std::unique_lock lock(_resource_mutex);
        Vector<PendingGpuResourceAdd> pending_adds;
        Vector<PendingGpuResourceRetire> pending_retires;
        {
            std::lock_guard pending_lock(_pending_mutation_mutex);
            pending_adds.swap(_pending_adds);
            pending_retires.swap(_pending_retires);
        }
        // 先 publish，确保同一批 Add -> Retire（发布前就释放的资源）不会丢失 retire 请求。
        for (auto &pending : pending_adds)
        {
            const auto handle = pending._handle;
            if (handle._index >= _slots.size())
                _slots.resize(static_cast<size_t>(handle._index) + 1u);
            auto &slot = _slots[handle._index];
            AL_ASSERT(slot._state == EGpuResourceSlotState::kFree && slot._generation == handle._generation);
            slot._resource = std::move(pending._resource);
            slot._state = EGpuResourceSlotState::kAlive;
        }
        _slot_count.store(static_cast<u32>(_slots.size()), std::memory_order_release);

        Vector<GpuResourceHandle> available_handles;
        for (const auto &pending : pending_retires)
        {
            const auto handle = pending._handle;
            if (handle._index >= _slots.size())
                continue;
            auto &slot = _slots[handle._index];
            if (slot._state != EGpuResourceSlotState::kAlive || slot._generation != handle._generation)
                continue;
            _retired_resources.emplace_back(RetiredGpuResource{pending._fence, std::move(slot._resource)});
            slot._state = EGpuResourceSlotState::kFree;
            if (++slot._generation == 0u)
                slot._generation = 1u;
            available_handles.emplace_back(GpuResourceHandle{handle._index, slot._generation});
        }
        // 包括 fence 0 在内，统一只在 Sync 中析构，不持有 pending mutex。
        while (!_retired_resources.empty() && _retired_resources.front()._fence <= completed_fence)
            _retired_resources.pop_front();
        if (!available_handles.empty())
        {
            std::lock_guard pending_lock(_pending_mutation_mutex);
            for (const auto handle : available_handles)
                _available_handles.emplace_back(handle);
        }
    }

    void GpuResourceRegistry::Clear()
    {
        // 仅用于 worker 停止后的 shutdown；pending 资源析构不能持有 pending mutex。
        Vector<PendingGpuResourceAdd> pending_adds;
        {
            std::lock_guard lock(_pending_mutation_mutex);
            pending_adds.swap(_pending_adds);
            _pending_retires.clear();
            _available_handles.clear();
            _next_slot_index = 0u;
        }
        pending_adds.clear();
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
