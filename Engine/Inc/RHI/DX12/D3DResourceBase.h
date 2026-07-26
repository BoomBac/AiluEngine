#pragma once
#ifndef __D3D_UTILS_H__
#define __D3D_UTILS_H__
#include "Framework/Common/Assert.h"
#include "Framework/Common/Log.h"
#include "Framework/Core/Containers/Array.h"
#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/String.h"
#include "Render/GpuResource.h"
#include "d3dx12.h"
#include <mutex>
#include <vector>

namespace Ailu::RHI::DX12
{
    struct D3DResourceStateGuard
    {
    public:
        D3DResourceStateGuard() = default;

        D3DResourceStateGuard(ID3D12Resource *resource, D3D12_RESOURCE_STATES initial_state, u32 subres_num)
            : _resource(resource), _uniform_state(initial_state), _sub_res_num(subres_num), _is_state_uniform(true)
        {
            AL_ASSERT(_resource != nullptr);
            AL_ASSERT(_sub_res_num > 0u);
        }

        D3DResourceStateGuard(const D3DResourceStateGuard &) = delete;
        D3DResourceStateGuard &operator=(const D3DResourceStateGuard &) = delete;

        D3DResourceStateGuard(D3DResourceStateGuard &&other) noexcept
        {
            std::scoped_lock lock(other._mutex);
            MoveFrom(other);
        }

        D3DResourceStateGuard &operator=(D3DResourceStateGuard &&other) noexcept
        {
            if (this == &other) return *this;

            std::scoped_lock lock(_mutex, other._mutex);
            MoveFrom(other);
            return *this;
        }

        [[nodiscard]] D3D12_RESOURCE_STATES CurState(u32 sub_res = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) const
        {
            std::scoped_lock lock(_mutex);

            if (sub_res == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
            {
                AL_ASSERT(_is_state_uniform);
                return _uniform_state;
            }

            AL_ASSERT(sub_res < _sub_res_num);
            return GetSubresourceState(sub_res);
        }

        [[nodiscard]] bool IsStateUniform() const
        {
            std::scoped_lock lock(_mutex);
            return _is_state_uniform;
        }

        [[nodiscard]] u32 SubresourceCount() const
        {
            std::scoped_lock lock(_mutex);
            return _sub_res_num;
        }

        static String DebugObjectName(ID3D12Resource *resource)
        {
            if (resource == nullptr) return "null";

            UINT name_len = 0u;
            HRESULT result = resource->GetPrivateData(WKPDID_D3DDebugObjectNameW, &name_len, nullptr);

            if (SUCCEEDED(result) && name_len >= sizeof(wchar_t))
            {
                const size_t wchar_count = name_len / sizeof(wchar_t);
                std::vector<wchar_t> name_buf(wchar_count, L'\0');

                result = resource->GetPrivateData(WKPDID_D3DDebugObjectNameW, &name_len, name_buf.data());
                if (SUCCEEDED(result)) return ToChar(WString(name_buf.data()));
            }

            name_len = 0u;
            result = resource->GetPrivateData(WKPDID_D3DDebugObjectName, &name_len, nullptr);

            if (FAILED(result) || name_len == 0u) return "Unnamed";

            std::vector<char> name_buf(name_len, '\0');
            result = resource->GetPrivateData(WKPDID_D3DDebugObjectName, &name_len, name_buf.data());

            if (FAILED(result)) return "Unnamed";

            const size_t string_length = name_len > 0u && name_buf[name_len - 1u] == '\0' ? name_len - 1u : name_len;
            return String(name_buf.data(), string_length);
        }

        static void LogBarrier(ID3D12Resource *resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after, u32 sub_res)
        {
            LOG_WARNING("D3D12 barrier: resource={}, ptr={}, subRes={}, before=0x{:X}, after=0x{:X}", DebugObjectName(resource),
                        static_cast<const void *>(resource), sub_res, static_cast<u32>(before), static_cast<u32>(after));
        }

        static void LogUAVBarrier(ID3D12Resource *resource)
        { LOG_WARNING("D3D12 UAV barrier: resource={}, ptr={}", DebugObjectName(resource), static_cast<const void *>(resource)); }

        static void InsertUAVBarrier(ID3D12GraphicsCommandList *cmd, ID3D12Resource *resource = nullptr)
        {
            AL_ASSERT(cmd != nullptr);

            const D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(resource);
            cmd->ResourceBarrier(1u, &barrier);
        }

        void InsertTrackedUAVBarrier(ID3D12GraphicsCommandList *cmd)
        {
            AL_ASSERT(cmd != nullptr);

            std::scoped_lock lock(_mutex);
            InsertUAVBarrier(cmd, _resource);
        }

        void MakesureResourceState(ID3D12GraphicsCommandList *cmd, D3D12_RESOURCE_STATES target_state,
                                   u32 sub_res = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
        {
            AL_ASSERT(cmd != nullptr);

            std::scoped_lock lock(_mutex);

            AL_ASSERT(_resource != nullptr);
            AL_ASSERT(_sub_res_num > 0u);

            if (sub_res == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
            {
                MakesureAllSubresourcesState(cmd, target_state);
                return;
            }

            AL_ASSERT(sub_res < _sub_res_num);
            MakesureSingleSubresourceState(cmd, target_state, sub_res);
        }

    private:
        [[nodiscard]] D3D12_RESOURCE_STATES GetSubresourceState(u32 sub_res) const
        {
            AL_ASSERT(sub_res < _sub_res_num);

            if (_is_state_uniform) return _uniform_state;

            AL_ASSERT(_subresource_states.size() == _sub_res_num);
            return _subresource_states[sub_res];
        }

        void MakesureAllSubresourcesState(ID3D12GraphicsCommandList *cmd, D3D12_RESOURCE_STATES target_state)
        {
            if (_is_state_uniform)
            {
                if (_uniform_state == target_state) return;

                const D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(_resource, _uniform_state, target_state,
                                                                                            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

                cmd->ResourceBarrier(1u, &barrier);
                SetUniformState(target_state);
                return;
            }

            Vector<D3D12_RESOURCE_BARRIER> barriers;
            barriers.reserve(_sub_res_num);

            for (u32 i = 0u; i < _sub_res_num; ++i)
            {
                const D3D12_RESOURCE_STATES current_state = _subresource_states[i];

                if (current_state == target_state) continue;

                barriers.emplace_back(CD3DX12_RESOURCE_BARRIER::Transition(_resource, current_state, target_state, i));
            }

            if (!barriers.empty()) cmd->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());

            SetUniformState(target_state);
        }

        void MakesureSingleSubresourceState(ID3D12GraphicsCommandList *cmd, D3D12_RESOURCE_STATES target_state, u32 sub_res)
        {
            if (_is_state_uniform)
            {
                if (_uniform_state == target_state) return;

                if (_sub_res_num == 1u)
                {
                    const D3D12_RESOURCE_BARRIER barrier =
                            CD3DX12_RESOURCE_BARRIER::Transition(_resource, _uniform_state, target_state, sub_res);

                    cmd->ResourceBarrier(1u, &barrier);
                    SetUniformState(target_state);
                    return;
                }

                ExpandUniformStates();
            }

            const D3D12_RESOURCE_STATES current_state = _subresource_states[sub_res];

            if (current_state == target_state) return;

            const D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(_resource, current_state, target_state, sub_res);

            cmd->ResourceBarrier(1u, &barrier);
            _subresource_states[sub_res] = target_state;

            TryCollapseUniformStates();
        }

        void ExpandUniformStates()
        {
            AL_ASSERT(_is_state_uniform);
            AL_ASSERT(_sub_res_num > 0u);

            _subresource_states.assign(_sub_res_num, _uniform_state);
            _is_state_uniform = false;
        }

        void TryCollapseUniformStates()
        {
            AL_ASSERT(!_is_state_uniform);
            AL_ASSERT(_subresource_states.size() == _sub_res_num);
            AL_ASSERT(!_subresource_states.empty());

            const D3D12_RESOURCE_STATES candidate_state = _subresource_states[0u];

            for (u32 i = 1u; i < _sub_res_num; ++i)
            {
                if (_subresource_states[i] != candidate_state) return;
            }

            SetUniformState(candidate_state);
        }

        void SetUniformState(D3D12_RESOURCE_STATES state)
        {
            _uniform_state = state;
            _is_state_uniform = true;
            _subresource_states.clear();
        }

        void MoveFrom(D3DResourceStateGuard &other)
        {
            _subresource_states = std::move(other._subresource_states);
            _resource = other._resource;
            _uniform_state = other._uniform_state;
            _sub_res_num = other._sub_res_num;
            _is_state_uniform = other._is_state_uniform;

            other._subresource_states.clear();
            other._resource = nullptr;
            other._uniform_state = D3D12_RESOURCE_STATE_COMMON;
            other._sub_res_num = 0u;
            other._is_state_uniform = true;
        }

    private:
        Vector<D3D12_RESOURCE_STATES> _subresource_states;
        ID3D12Resource *_resource = nullptr;
        D3D12_RESOURCE_STATES _uniform_state = D3D12_RESOURCE_STATE_COMMON;
        mutable std::mutex _mutex;
        u32 _sub_res_num = 0u;
        bool _is_state_uniform = true;
    };
    namespace D3DConvertUtils
    {
        static ::Ailu::Render::EResourceState ToALResState(D3D12_RESOURCE_STATES state) { return (::Ailu::Render::EResourceState) state; };
        static D3D12_RESOURCE_STATES FromALResState(::Ailu::Render::EResourceState state) { return (D3D12_RESOURCE_STATES) state; };
    }// namespace D3DConvertUtils
}// namespace Ailu::RHI::DX12

#endif// !D3D_UTILS_H__
