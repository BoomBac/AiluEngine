#include "pch.h"
#include "RHI/DX12/D3DGraphicsPipelineState.h"
#include "RHI/DX12/D3DShader.h"
#include "RHI/DX12/dxhelper.h"
#include "RHI/DX12/D3DContext.h"

namespace Ailu
{
    static D3D12_RASTERIZER_DESC ConvertToD3D12RasterizerDesc(const RasterizerState& state)
    {
        auto raster_state = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        if (state._cull_mode == ECullMode::kFront) raster_state.CullMode = D3D12_CULL_MODE_FRONT;
        else if (state._cull_mode == ECullMode::kNone) raster_state.CullMode = D3D12_CULL_MODE_NONE;
        else raster_state.CullMode = D3D12_CULL_MODE_BACK;
        if (state._fill_mode == EFillMode::kWireframe) raster_state.FillMode = D3D12_FILL_MODE_WIREFRAME;
        else raster_state.FillMode = D3D12_FILL_MODE_SOLID;
        return raster_state;
    }

    static D3D12_BLEND_DESC ConvertToD3D12BlendDesc(const BlendState& state)
    {
        auto blend_state = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        for (size_t i = 0; i < 8; i++)
        {
            blend_state.RenderTarget[i].BlendEnable = state._b_enable;
        }
        return blend_state;
    }

    static D3D12_DEPTH_STENCIL_DESC ConvertToD3D12DepthStencilDesc(const DepthStencilState& state)
    {
        auto ds_state = CD3DX12_DEPTH_STENCIL_DESC(CD3DX12_DEFAULT{});
        ds_state.DepthEnable = state._b_depth_write;
        ds_state.StencilEnable = state._b_front_stencil;
        return ds_state;
    }

    static DXGI_FORMAT ConvertToDXGIFormat(const EALGFormat& format)
    {
        switch (format)
        {
        case EALGFormat::kALGFormatR8G8B8A8_UNORM:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case EALGFormat::kALGFormatR24G8_TYPELESS:
            return DXGI_FORMAT_R24G8_TYPELESS;
        }
        return DXGI_FORMAT_UNKNOWN;
    }

    void D3DGraphicsPipelineState::Build()
	{
        auto [desc, count] = ConvertToD3DInputLayout(_input_layout);
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { desc, count };
        psoDesc.pRootSignature = static_cast<D3DShader*>(_p_pixel_shader.get())->GetSignature().Get();
        psoDesc.VS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<ID3DBlob*>(_p_vertex_shader->GetByteCode(EShaderType::kVertex)));
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<ID3DBlob*>(_p_pixel_shader->GetByteCode(EShaderType::kPixel)));
        psoDesc.RasterizerState = ConvertToD3D12RasterizerDesc(_raster_state);
        psoDesc.BlendState = ConvertToD3D12BlendDesc(_blend_state);
        psoDesc.DepthStencilState = ConvertToD3D12DepthStencilDesc(_depth_stencil_state);
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = _rt_nums;
        for (int i = 0; i < _rt_nums; i++)
        {
            psoDesc.RTVFormats[i] = ConvertToDXGIFormat(_rt_formats[i]);
        }
        psoDesc.SampleDesc.Count = 1;    
        ThrowIfFailed(D3DContext::GetInstance()->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));
	}
}
