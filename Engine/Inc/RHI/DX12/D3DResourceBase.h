#pragma once
#ifndef __D3D_UTILS_H__
#define __D3D_UTILS_H__
#include "Framework/Common/Log.h"
#include "GlobalMarco.h"
#include "Render/GpuResource.h"
#include "d3dx12.h"
#include <mutex>

namespace Ailu::RHI::DX12
{
    struct D3DResourceStateGuard
    {
        static constexpr u16 kMaxSubresources = 64u;
        D3DResourceStateGuard() = default;

        D3DResourceStateGuard(ID3D12Resource *resource, D3D12_RESOURCE_STATES initial_state, u16 subres_num)
            : _resource(resource),_sub_res_num(subres_num)
        {
            AL_ASSERT(_sub_res_num < kMaxSubresources);
            for (u16 i = 0; i < _sub_res_num; i++)
            {
                _cur_states[i] = initial_state;
                _new_states[i] = initial_state;
            }
        }

        D3DResourceStateGuard &operator=(D3DResourceStateGuard &&other) noexcept
        {
            if (this != &other)
            {
                std::scoped_lock lock(_mutex, other._mutex);
                _cur_states = std::move(other._cur_states);
                _new_states = std::move(other._new_states);
                _resource = other._resource;
                _sub_res_num = other._sub_res_num;
                other._resource = nullptr;
            }
            return *this;
        }

        [[nodiscard]] D3D12_RESOURCE_STATES CurState(u32 sub_res = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
        {
            std::unique_lock lock(_mutex);
            if (sub_res == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
                return _cur_states[0u];
            AL_ASSERT(sub_res < kMaxSubresources);
            return _cur_states[sub_res];
        }

        void MakesureResourceState(ID3D12GraphicsCommandList *cmd, D3D12_RESOURCE_STATES target_state, u32 sub_res = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
        {
            std::unique_lock lock(_mutex);
            if (sub_res == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
            {
                for(u16 i = 0; i < _sub_res_num; i++)
                {
                    if (_cur_states[i] != target_state)
                    {
                        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(_resource, _cur_states[i], target_state, i);
                        cmd->ResourceBarrier(1, &barrier);
                        _cur_states[i] = target_state;
                    }
                }
            }
            else
            {
                AL_ASSERT(sub_res < kMaxSubresources);
                if (_cur_states[sub_res] != target_state)
                {
                    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(_resource, _cur_states[sub_res], target_state, sub_res);
                    cmd->ResourceBarrier(1, &barrier);
                    _cur_states[sub_res] = target_state;
                }
            }
        }

    private:
        Array<D3D12_RESOURCE_STATES, kMaxSubresources> _cur_states;
        Array<D3D12_RESOURCE_STATES, kMaxSubresources> _new_states;
        ID3D12Resource *_resource = nullptr;
        std::mutex _mutex;
        u16 _sub_res_num;
    };


    namespace D3DConvertUtils
    {
        static Ailu::Render::EResourceState ToALResState(D3D12_RESOURCE_STATES state)
        {
            return (Ailu::Render::EResourceState) state;
        };
        static D3D12_RESOURCE_STATES FromALResState(Ailu::Render::EResourceState state)
        {
            return (D3D12_RESOURCE_STATES) state;
        };
    }// namespace D3DConvertUtils
}// namespace Ailu::RHI::DX12

#endif// !D3D_UTILS_H__
