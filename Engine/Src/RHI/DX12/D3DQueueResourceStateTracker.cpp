#include "RHI/DX12/D3DCommandBuffer.h"
#include "RHI/DX12/D3DQueueResourceStateTracker.h"

namespace Ailu::RHI::DX12
{
    namespace
    {
        bool NeedsTransition(Render::EResourceState current_state, Render::EResourceState required_state)
        {
            return !Render::IsResourceStateCompatible(current_state, required_state);
        }

        void SetState(ResourceState &state, u32 subresource_count, u32 subresource, Render::EResourceState value)
        {
            if (subresource == Render::kTotalSubRes || subresource_count == 1u)
            {
                SetAllSubresourceStates(state, value);
                return;
            }
            ExpandSubresourceStates(state, subresource_count);
            state._subresource_states[subresource] = value;
            CollapseSubresourceStates(state);
        }
    }

    bool D3DQueueResourceStateTracker::ResolveInitialBarriers(
        D3DCommandBuffer &command_buffer, const D3DResourceStateTracker &command_state_tracker)
    {
        bool has_barriers = false;
        for (const auto &[state_id, local_state]: command_state_tracker.States())
        {
            auto [it, inserted] = _states.try_emplace(state_id);
            auto &queue_state = it->second;
            if (inserted)
                queue_state._state = Render::EResourceState::kCommon;

            const auto &first_state = local_state._first_state;
            if (first_state._subresource_states.empty())
            {
                if (queue_state._subresource_states.empty())
                {
                    if (NeedsTransition(queue_state._state, first_state._state))
                    {
                        has_barriers = true;
                        command_buffer.RecordQueueTransition(local_state._resource, queue_state._state,
                                                             first_state._state, Render::kTotalSubRes);
                    }
                }
                else
                {
                    for (u32 subresource = 0u; subresource < local_state._resource._subresource_count; ++subresource)
                    {
                        const auto current_state = GetSubresourceState(queue_state, subresource);
                        if (NeedsTransition(current_state, first_state._state))
                        {
                            has_barriers = true;
                            command_buffer.RecordQueueTransition(local_state._resource, current_state,
                                                                 first_state._state, subresource);
                        }
                    }
                }
                continue;
            }

            for (u32 subresource = 0u; subresource < local_state._resource._subresource_count; ++subresource)
            {
                if (!local_state.IsInitialized(subresource))
                    continue;
                const auto current_state = GetSubresourceState(queue_state, subresource);
                const auto required_state = first_state._subresource_states[subresource];
                if (NeedsTransition(current_state, required_state))
                {
                    has_barriers = true;
                    command_buffer.RecordQueueTransition(local_state._resource, current_state, required_state, subresource);
                }
            }
        }
        return has_barriers;
    }

    void D3DQueueResourceStateTracker::CommitFinalStates(const D3DResourceStateTracker &command_state_tracker)
    {
        for (const auto &[state_id, local_state]: command_state_tracker.States())
        {
            auto [it, inserted] = _states.try_emplace(state_id);
            auto &queue_state = it->second;
            if (inserted)
                queue_state._state = Render::EResourceState::kCommon;

            const auto &current_state = local_state._current_state;
            if (current_state._subresource_states.empty())
            {
                SetAllSubresourceStates(queue_state, current_state._state);
                continue;
            }

            for (u32 subresource = 0u; subresource < local_state._resource._subresource_count; ++subresource)
            {
                if (local_state.IsInitialized(subresource))
                    SetState(queue_state, local_state._resource._subresource_count, subresource,
                             current_state._subresource_states[subresource]);
            }
        }
    }

    void D3DQueueResourceStateTracker::SetExternalState(const D3DResource &resource,
                                                        Render::EResourceState state, u32 subresource)
    {
        if (resource._resource == nullptr)
            return;
        if (subresource != Render::kTotalSubRes && subresource >= resource._subresource_count)
            return;
        auto [it, inserted] = _states.try_emplace(resource._state_id);
        auto &queue_state = it->second;
        if (inserted)
            queue_state._state = Render::EResourceState::kCommon;
        SetState(queue_state, resource._subresource_count, subresource, state);
    }
}
