#pragma once
#ifndef __D3D_RESOURCE_STATE_TRACKER_H__
#define __D3D_RESOURCE_STATE_TRACKER_H__

#include "D3DResource.h"
#include <unordered_map>

namespace Ailu::RHI::DX12
{
    struct ResourceTransition
    {
        Render::EResourceState _before = Render::EResourceState::kCommon;
        Render::EResourceState _after = Render::EResourceState::kCommon;
        u32 _subresource = Render::kTotalSubRes;
    };

    struct LocalResourceState
    {
        D3DResource _resource;
        ResourceState _first_state;
        ResourceState _current_state;
        Vector<u8> _first_initialized;
        bool _all_subresources_initialized = false;

        bool IsInitialized(u32 subresource) const
        {
            return _all_subresources_initialized ||
                   (!_first_initialized.empty() && _first_initialized[subresource] != 0u);
        }
    };

    class D3DResourceStateTracker
    {
    public:
        using StateMap = std::unordered_map<ResourceStateId, LocalResourceState, ResourceStateIdHash>;

        void RequireState(const D3DResource &resource, Render::EResourceState state, u32 subresource);
        void Clear();

        const StateMap &States() const { return _states; }
        const Vector<ResourceTransition> &Transitions() const { return _transitions; }

    private:
        void InitializeWholeResource(LocalResourceState &local_state, Render::EResourceState state);
        void ExpandForPartialUse(LocalResourceState &local_state);
        void CollapseIfPossible(LocalResourceState &local_state);

        StateMap _states;
        Vector<ResourceTransition> _transitions;
    };
}

#endif // !__D3D_RESOURCE_STATE_TRACKER_H__
