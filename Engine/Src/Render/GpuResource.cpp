#include "Render/GpuResource.h"
#include "Render/GraphicsContext.h"
#include "pch.h"
#include "Framework/Common/Log.h"
#include <utility>

namespace Ailu::Render
{
    GpuResourceRegistry &GpuResourceRegistry::Get()
    {
        static GpuResourceRegistry s_registry;
        return s_registry;
    }

    GpuResourceHandle GpuResourceRegistry::Add(std::unique_ptr<GpuResource> resource)
    {
        if (resource == nullptr)
            return {};

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

        auto &slot = _slots[index];
        slot._state = EGpuResourceSlotState::kAlive;
        slot._resource = std::move(resource);
        const GpuResourceHandle handle{index, slot._generation};
        slot._resource->SetHandle(handle);
        return handle;
    }

    GpuResource *GpuResourceRegistry::Resolve(GpuResourceHandle handle)
    {
        if (!handle.IsValid() || handle._index >= _slots.size())
            return nullptr;
        auto &slot = _slots[handle._index];
        if (slot._state != EGpuResourceSlotState::kAlive || slot._generation != handle._generation)
            return nullptr;
        return slot._resource.get();
    }

    const GpuResource *GpuResourceRegistry::Resolve(GpuResourceHandle handle) const
    {
        if (!handle.IsValid() || handle._index >= _slots.size())
            return nullptr;
        const auto &slot = _slots[handle._index];
        if (slot._state != EGpuResourceSlotState::kAlive || slot._generation != handle._generation)
            return nullptr;
        return slot._resource.get();
    }

    void GpuResourceRegistry::Retire(GpuResourceHandle handle, u64 fence)
    {
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
        while (!_retired_resources.empty() && _retired_resources.front()._fence <= completed_fence)
            _retired_resources.pop_front();
    }

    void GpuResourceRegistry::Clear()
    {
        std::lock_guard lock(_pending_release_mutex);
        _pending_releases.clear();
        _retired_resources.clear();
        _slots.clear();
        _free_indices.clear();
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
