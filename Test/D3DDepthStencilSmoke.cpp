#include <wrl/client.h>
#include "RHI/DX12/D3DShader.h"
#include <iostream>

int main()
{
    using namespace Ailu::Render;
    namespace D3DConvertUtils = Ailu::RHI::DX12::D3DConvertUtils;
    DepthStencilState state{};
    state._depth_test_func = ECompareFunc::kAlways;
    state._b_depth_write = true;
    auto desc = D3DConvertUtils::ConvertToD3D12DepthStencilDesc(state);
    if (!desc.DepthEnable || desc.DepthWriteMask != D3D12_DEPTH_WRITE_MASK_ALL || desc.DepthFunc != D3D12_COMPARISON_FUNC_ALWAYS)
        return 1;
    state._b_depth_write = false;
    desc = D3DConvertUtils::ConvertToD3D12DepthStencilDesc(state);
    if (desc.DepthEnable || desc.DepthWriteMask != D3D12_DEPTH_WRITE_MASK_ZERO)
        return 2;
    state._depth_test_func = ECompareFunc::kLess;
    desc = D3DConvertUtils::ConvertToD3D12DepthStencilDesc(state);
    if (!desc.DepthEnable || desc.DepthWriteMask != D3D12_DEPTH_WRITE_MASK_ZERO || desc.DepthFunc != D3D12_COMPARISON_FUNC_LESS)
        return 3;
    state._depth_test_func = ECompareFunc::kNever;
    state._b_depth_write = true;
    desc = D3DConvertUtils::ConvertToD3D12DepthStencilDesc(state);
    if (!desc.DepthEnable || desc.DepthFunc != D3D12_COMPARISON_FUNC_NEVER)
        return 4;
    std::cout << "PASS: unconditional depth writes, read-only tests and depth-free draws retain their DX12 semantics.\n";
    return 0;
}
