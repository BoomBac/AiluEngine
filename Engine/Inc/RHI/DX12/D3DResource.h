#pragma once
#ifndef __D3D_RESOURCE_H__
#define __D3D_RESOURCE_H__

#include "Framework/Core/CoreMinimal.h"
#include "Render/CoreType.h"
#include <algorithm>
#include <atomic>
#include <d3d12.h>
#include <wrl/client.h>

namespace Ailu::RHI::DX12
{
    using Microsoft::WRL::ComPtr;

    struct ResourceStateId
    {
        u32 _index = 0u;
        u32 _generation = 0u;

        bool operator==(const ResourceStateId &) const = default;
    };

    struct ResourceStateIdHash
    {
        size_t operator()(const ResourceStateId &id) const noexcept
        {
            return std::hash<u64>{}((static_cast<u64>(id._index) << 32u) | id._generation);
        }
    };

    struct ResourceState
    {
        Render::EResourceState _state = Render::EResourceState::kCommon;
        Vector<Render::EResourceState> _subresource_states;
    };

    struct D3DResource
    {
        ComPtr<ID3D12Resource> _resource;
        ResourceStateId _state_id;
        u32 _subresource_count = 1u;

        D3DResource() = default;

        D3DResource(ID3D12Resource *resource, u32 subresource_count = 1u)
        {
            Set(resource, subresource_count);
        }

        D3DResource(ComPtr<ID3D12Resource> resource, u32 subresource_count = 1u)
            : _resource(std::move(resource)), _subresource_count(std::max(1u, subresource_count))
        {
            ResetStateId();
        }

        void Set(ID3D12Resource *resource, u32 subresource_count = 1u)
        {
            _resource = resource;
            _subresource_count = std::max(1u, subresource_count);
            if (_resource != nullptr)
                ResetStateId();
            else
                _state_id = {};
        }

        void Reset()
        {
            _resource.Reset();
            _state_id = {};
            _subresource_count = 1u;
        }

        void ResetStateId()
        {
            static std::atomic<u32> s_next_index = 1u;
            _state_id = {s_next_index.fetch_add(1u, std::memory_order_relaxed), 1u};
        }

        ID3D12Resource **GetAddressOf()
        {
            _state_id = {};
            return _resource.ReleaseAndGetAddressOf();
        }
        ID3D12Resource *Get() const { return _resource.Get(); }
        ID3D12Resource *operator->() const { return _resource.Get(); }
        explicit operator bool() const { return _resource != nullptr; }
    };

    inline Render::EResourceState GetSubresourceState(const ResourceState &state, u32 subresource)
    {
        if (state._subresource_states.empty())
            return state._state;
        return state._subresource_states[subresource];
    }

    inline void SetAllSubresourceStates(ResourceState &state, Render::EResourceState value)
    {
        state._state = value;
        state._subresource_states.clear();
    }

    inline void ExpandSubresourceStates(ResourceState &state, u32 subresource_count)
    {
        if (state._subresource_states.empty())
            state._subresource_states.assign(subresource_count, state._state);
    }

    inline void CollapseSubresourceStates(ResourceState &state)
    {
        if (state._subresource_states.empty())
            return;
        const auto value = state._subresource_states.front();
        for (const auto subresource_state: state._subresource_states)
        {
            if (subresource_state != value)
                return;
        }
        SetAllSubresourceStates(state, value);
    }

    namespace D3DConvertUtils
    {
        inline Render::EResourceState ToALResState(D3D12_RESOURCE_STATES state)
        {
            return static_cast<Render::EResourceState>(state);
        }

        inline D3D12_RESOURCE_STATES FromALResState(Render::EResourceState state)
        {
            return static_cast<D3D12_RESOURCE_STATES>(state);
        }

        inline bool IsStateCompatible(D3D12_RESOURCE_STATES current_state, D3D12_RESOURCE_STATES required_state)
        {
            return Render::IsResourceStateCompatible(ToALResState(current_state), ToALResState(required_state));
        }
    }
}

#endif // !__D3D_RESOURCE_H__
