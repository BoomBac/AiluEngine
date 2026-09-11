#pragma once
#ifndef __D3D_QUEUE_RESOURCE_STATE_TRACKER_H__
#define __D3D_QUEUE_RESOURCE_STATE_TRACKER_H__

#include "D3DResourceStateTracker.h"
#include <unordered_map>

namespace Ailu::RHI::DX12
{
    class D3DCommandBuffer;

    class D3DQueueResourceStateTracker
    {
    public:
        using StateMap = std::unordered_map<ResourceStateId, ResourceState, ResourceStateIdHash>;

        bool ResolveInitialBarriers(D3DCommandBuffer &command_buffer,
                                     const D3DResourceStateTracker &command_state_tracker);
        void CommitFinalStates(const D3DResourceStateTracker &command_state_tracker);
        void SetExternalState(const D3DResource &resource, Render::EResourceState state,
                               u32 subresource = Render::kTotalSubRes);

    private:
        StateMap _states;
    };
}

#endif // !__D3D_QUEUE_RESOURCE_STATE_TRACKER_H__
