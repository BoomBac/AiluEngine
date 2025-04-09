#pragma once
#ifndef __D3D_UTILS_H__
#define __D3D_UTILS_H__
#include "GlobalMarco.h"
#include "d3dx12.h"
#include "Render/GpuResource.h"
namespace Ailu
{
    struct D3DResourceStateGuard
    {
        void MakesureResourceState(ID3D12GraphicsCommandList *cmd, ID3D12Resource *p_res, D3D12_RESOURCE_STATES target_state,u32 sub_res = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
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
                            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(p_res, state, target_state, sub_res_id);
                            cmd->ResourceBarrier(1, &barrier);
                            _cur_res_state[sub_res_id] = target_state;
                        }
                    }
                }
                return;
            }
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(p_res, _cur_res_state[sub_res], target_state,sub_res);
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
        explicit D3DResourceStateGuard(D3D12_RESOURCE_STATES initial_state)
        {
            _cur_res_state[D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES] =  initial_state;
        };
    private:
        Map<u32,D3D12_RESOURCE_STATES> _cur_res_state;
    };

    namespace D3DConvertUtils
    {
        static EResourceState ToALResState(D3D12_RESOURCE_STATES state)
        {
            return (EResourceState)state;
        };
        static D3D12_RESOURCE_STATES FromALResState(EResourceState state)
        {
            return (D3D12_RESOURCE_STATES)state;
        };
    }
}

#endif// !D3D_UTILS_H__
