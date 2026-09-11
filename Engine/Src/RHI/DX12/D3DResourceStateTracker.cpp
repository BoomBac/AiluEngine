#include "RHI/DX12/D3DResourceStateTracker.h"

namespace Ailu::RHI::DX12
{
    namespace
    {
        bool NeedsTransition(Render::EResourceState current_state, Render::EResourceState required_state)
        {
            return !Render::IsResourceStateCompatible(current_state, required_state);
        }
    }

    void D3DResourceStateTracker::Clear()
    {
        _states.clear();
        _transitions.clear();
    }

    void D3DResourceStateTracker::InitializeWholeResource(LocalResourceState &local_state,
                                                           Render::EResourceState state)
    {
        SetAllSubresourceStates(local_state._first_state, state);
        SetAllSubresourceStates(local_state._current_state, state);
        local_state._first_initialized.clear();
        local_state._all_subresources_initialized = true;
    }

    void D3DResourceStateTracker::ExpandForPartialUse(LocalResourceState &local_state)
    {
        const u32 subresource_count = local_state._resource._subresource_count;
        ExpandSubresourceStates(local_state._first_state, subresource_count);
        ExpandSubresourceStates(local_state._current_state, subresource_count);
        if (local_state._first_initialized.empty())
        {
            local_state._first_initialized.assign(subresource_count,
                                                  local_state._all_subresources_initialized ? 1u : 0u);
        }
        local_state._all_subresources_initialized = false;
    }

    void D3DResourceStateTracker::CollapseIfPossible(LocalResourceState &local_state)
    {
        if (!local_state._all_subresources_initialized)
            return;
        CollapseSubresourceStates(local_state._first_state);
        CollapseSubresourceStates(local_state._current_state);
        local_state._first_initialized.clear();
    }

    void D3DResourceStateTracker::RequireState(const D3DResource &resource, Render::EResourceState state,
                                               u32 subresource)
    {
        _transitions.clear();
        if (resource._resource == nullptr)
            return;
        if (subresource != Render::kTotalSubRes && subresource >= resource._subresource_count)
            return;

        auto [it, inserted] = _states.try_emplace(resource._state_id);
        auto &local_state = it->second;
        if (inserted)
        {
            local_state._resource = resource;
            if (subresource == Render::kTotalSubRes || resource._subresource_count == 1u)
            {
                InitializeWholeResource(local_state, state);
                return;
            }
            ExpandForPartialUse(local_state);
            local_state._first_state._subresource_states[subresource] = state;
            local_state._current_state._subresource_states[subresource] = state;
            local_state._first_initialized[subresource] = 1u;
            return;
        }

        if (subresource == Render::kTotalSubRes || resource._subresource_count == 1u)
        {
            if (local_state._resource._subresource_count == 1u)
            {
                if (NeedsTransition(local_state._current_state._state, state))
                {
                    _transitions.push_back({local_state._current_state._state, state, Render::kTotalSubRes});
                    local_state._current_state._state = state;
                }
                return;
            }

            if (local_state._all_subresources_initialized && local_state._current_state._subresource_states.empty() &&
                !NeedsTransition(local_state._current_state._state, state))
                return;

            ExpandForPartialUse(local_state);
            for (u32 index = 0u; index < resource._subresource_count; ++index)
            {
                if (local_state._first_initialized[index] == 0u)
                {
                    local_state._first_state._subresource_states[index] = state;
                    local_state._current_state._subresource_states[index] = state;
                    local_state._first_initialized[index] = 1u;
                    continue;
                }
                auto &current_state = local_state._current_state._subresource_states[index];
                if (NeedsTransition(current_state, state))
                {
                    _transitions.push_back({current_state, state, index});
                    current_state = state;
                }
            }
            local_state._all_subresources_initialized = true;
            CollapseIfPossible(local_state);
            return;
        }

        if (subresource >= resource._subresource_count)
            return;
        if (local_state._all_subresources_initialized || local_state._first_initialized.empty())
            ExpandForPartialUse(local_state);
        if (local_state._first_initialized[subresource] == 0u)
        {
            local_state._first_state._subresource_states[subresource] = state;
            local_state._current_state._subresource_states[subresource] = state;
            local_state._first_initialized[subresource] = 1u;
            return;
        }

        auto &current_state = local_state._current_state._subresource_states[subresource];
        if (NeedsTransition(current_state, state))
        {
            _transitions.push_back({current_state, state, subresource});
            current_state = state;
        }
    }
}
