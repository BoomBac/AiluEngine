#pragma once
#ifndef __D3D_UTILS_H__
#define __D3D_UTILS_H__
#include "d3dx12.h"
namespace Ailu
{
    struct D3DResourceStateGuard
    {
        void MakesureResourceState(ID3D12GraphicsCommandList *cmd, ID3D12Resource *p_res, D3D12_RESOURCE_STATES target_state)
        {
            if (_cur_res_state == target_state)
                return;
            auto old_state = _cur_res_state;
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(p_res, old_state, target_state);
            cmd->ResourceBarrier(1, &barrier);
            _cur_res_state = target_state;
        }
        D3D12_RESOURCE_STATES CurState() const { return _cur_res_state; }
        D3DResourceStateGuard() = default;
        D3DResourceStateGuard(D3D12_RESOURCE_STATES initial_state) : _cur_res_state(initial_state){};

    private:
        D3D12_RESOURCE_STATES _cur_res_state;
    };
}

#endif// !D3D_UTILS_H__
