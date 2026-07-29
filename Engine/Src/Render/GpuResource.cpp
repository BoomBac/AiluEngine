#include "Render/GpuResource.h"
#include "Render/GraphicsContext.h"
#include "pch.h"
#include "Framework/Common/Log.h"

namespace Ailu::Render
{
    namespace
    {
        constexpr u32 kMaxTrackedSubResources = 64u;

        bool IsValidTrackedSubResource(u32 sub_res)
        {
            return sub_res == kTotalSubRes || sub_res < kMaxTrackedSubResources;
        }

        EResourceState AggregateState(const Array<EResourceState, kMaxTrackedSubResources>& states)
        {
            const auto first_state = states[0];
            for (u32 index = 1u; index < kMaxTrackedSubResources; ++index)
            {
                if (states[index] != first_state)
                    return EResourceState::kCommon;
            }
            return first_state;
        }
    }

    #pragma region GpuResource
    void GpuResource::Track(u64 fence)
    {
        _fence_value = fence == 0u? GraphicsContext::Get().GetFenceValueCPU() + 1 : fence;
    }
    bool GpuResource::MarkUsedByCommand(u64 command_epoch)
    {
        if (_last_marked_command_epoch == command_epoch)
            return false;
        _last_marked_command_epoch = command_epoch;
        return true;
    }
    GpuResource::GpuResource()
    {
    }
    GpuResource::~GpuResource()
    {
        s_total_mem_size -= _mem_size;
        LOG_INFO("GpuResource::~GpuResource: {} released,mem size {}", _name, _mem_size);
        ResourceStateTracker::Get().RemoveResource(this);
    }
    void GpuResource::Apply()
    {
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
    void GpuResource::TrackResourceState(EResourceState new_state, u32 sub_res)
    {
        _state = new_state;
        ResourceStateTracker::Get().UpdateResourceState(this, new_state, sub_res);
    }
    EResourceState GpuResource::CurrentResourceState(u32 sub_res) const
    {
        return ResourceStateTracker::Get().GetResourceState(const_cast<GpuResource *>(this), sub_res);
    }
    bool GpuResource::TryCurrentResourceState(EResourceState &out_state, u32 sub_res) const
    {
        out_state = CurrentResourceState(sub_res);
        return true;
    }
    bool GpuResource::IsReferenceByGpu() const
    {
        u64 fence_value = GraphicsContext::Get().GetFenceValueGPU();
        return fence_value < _fence_value;
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
            const u64 created_fence = ResourceStateTracker::Get().GetCreatedFence(this);
            const u64 gpu_fence_value = GraphicsContext::Get().GetFenceValueGPU();
            _is_ready_for_rendering = gpu_fence_value >= created_fence;
        }
        return _is_ready_for_rendering;
    }
    #pragma endregion

    #pragma region ResourceStateTracker
    
    ResourceStateTracker& ResourceStateTracker::Get()
    {
        static ResourceStateTracker tracker;
        return tracker;
    }

    void ResourceStateTracker::AddResource(GpuResource* res, u64 created_fence)
    {
        if (res == nullptr)
            return;

        std::scoped_lock lock(_mutex);
        States tracked_states{};
        tracked_states._created_fence = created_fence;
        tracked_states._cur_states.fill(res->_state);
        tracked_states._new_states.fill(res->_state);
        _res_state_map.insert_or_assign(res, tracked_states);
        LOG_INFO("ResourceStateTracker::AddResource({}) {},num is {}", res->Name(),static_cast<const void*>(res), _res_state_map.size());
    }

    void ResourceStateTracker::RemoveResource(GpuResource* res)
    {
        if (res == nullptr)
            return;

        std::scoped_lock lock(_mutex);
        _res_state_map.erase(res);
        LOG_INFO("ResourceStateTracker::RemoveResource({}) {},num is {}", res->Name(),static_cast<const void*>(res), _res_state_map.size());
    }

    EResourceState ResourceStateTracker::GetResourceState(GpuResource* res, u32 sub_res) const
    {
        if (res == nullptr || !IsValidTrackedSubResource(sub_res))
            return EResourceState::kCommon;

        std::scoped_lock lock(_mutex);
        if (auto it = _res_state_map.find(res); it != _res_state_map.end())
        {
            return sub_res == kTotalSubRes ? AggregateState(it->second._cur_states) : it->second._cur_states[sub_res];
        }
        return res->_state;
    }

    void ResourceStateTracker::UpdateResourceState(GpuResource* res, EResourceState new_state, u32 sub_res)
    {
        if (res == nullptr || !IsValidTrackedSubResource(sub_res))
            return;

        std::scoped_lock lock(_mutex);
        auto [it, inserted] = _res_state_map.try_emplace(res);
        auto& tracked_states = it->second;
        if (inserted)
        {
            tracked_states._created_fence = 0u;
            tracked_states._cur_states.fill(res->_state);
            tracked_states._new_states.fill(res->_state);
        }
        if (sub_res == kTotalSubRes)
        {
            tracked_states._cur_states.fill(new_state);
            tracked_states._new_states.fill(new_state);
        }
        else
        {
            tracked_states._cur_states[sub_res] = new_state;
            tracked_states._new_states[sub_res] = new_state;
        }
        res->_state = AggregateState(tracked_states._cur_states);
    }

    u64 ResourceStateTracker::GetCreatedFence(GpuResource* res) const
    {
        AL_ASSERT(res != nullptr);
        std::scoped_lock lock(_mutex);
        auto it = _res_state_map.find(res); 
        if (it == _res_state_map.end())
        {
            LOG_WARNING("ResourceStateTracker::GetCreatedFence: resource {} {} not found in tracker, maybe it's not tracked or already removed?", res->Name(), static_cast<const void*>(res));
            return 0xFFFFFFFFFFFFFFFFu;
        }
        AL_ASSERT(it != _res_state_map.end());
        AL_ASSERT(it->first->ID() == res->ID());
        return it->second._created_fence;
    }
}// namespace Ailu
