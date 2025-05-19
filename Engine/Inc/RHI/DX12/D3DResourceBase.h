#pragma once
#ifndef __D3D_UTILS_H__
#define __D3D_UTILS_H__
#include "GlobalMarco.h"
#include "d3dx12.h"
#include "Render/GpuResource.h"
namespace Ailu::RHI::DX12
{
    struct D3DResourceStateGuard
    {
        void MakesureResourceState(ID3D12GraphicsCommandList *cmd,D3D12_RESOURCE_STATES target_state,u32 sub_res = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
        {
            if (!_cur_res_state.contains(sub_res))
                _cur_res_state[sub_res] = _cur_res_state[D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES];
            if (_cur_res_state[sub_res] == target_state)
            {
                if (sub_res == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
                {
                    for(auto&[sub_res_id, state]: _cur_res_state)
                    {
                        if (state != target_state)
                        {
                            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(_resource, state, target_state, sub_res_id);
                            cmd->ResourceBarrier(1, &barrier);
                            _cur_res_state[sub_res_id] = target_state;
                        }
                    }
                }
                return;
            }
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(_resource, _cur_res_state[sub_res], target_state,sub_res);
            cmd->ResourceBarrier(1, &barrier);
            _cur_res_state[sub_res] = target_state;
            if (sub_res ==  D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
            {
                for(auto&[sub_res_id, state]: _cur_res_state)
                {
                    _cur_res_state[sub_res_id] = target_state;
                }
            }
        }
        [[nodiscard]] D3D12_RESOURCE_STATES CurState(u32 sub_res = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) const
        {
            if (_cur_res_state.contains(sub_res))
                return _cur_res_state.at(sub_res);
            return _cur_res_state.at(D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
        }
        D3DResourceStateGuard() = default;
        D3DResourceStateGuard(ID3D12Resource* resource,D3D12_RESOURCE_STATES initial_state) : _resource(resource)
        {
            _cur_res_state[D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES] =  initial_state;
        };
    private:
        Map<u32,D3D12_RESOURCE_STATES> _cur_res_state;
        ID3D12Resource* _resource = nullptr;
    };

    namespace D3DConvertUtils
    {
        static Ailu::Render::EResourceState ToALResState(D3D12_RESOURCE_STATES state)
        {
            return (Ailu::Render::EResourceState)state;
        };
        static D3D12_RESOURCE_STATES FromALResState(Ailu::Render::EResourceState state)
        {
            return (D3D12_RESOURCE_STATES)state;
        };
    }
}

#endif// !D3D_UTILS_H__
