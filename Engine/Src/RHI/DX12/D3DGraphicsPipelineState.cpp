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
        case EALGFormat::kALGFormatR8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case EALGFormat::kALGFormatR24G8_TYPELESS: return DXGI_FORMAT_R24G8_TYPELESS;
        case EALGFormat::kALGFormatR32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
        case EALGFormat::kALGFormatD32_FLOAT: return DXGI_FORMAT_D32_FLOAT;
        }
        return DXGI_FORMAT_UNKNOWN;
    }

    static D3D12_PRIMITIVE_TOPOLOGY_TYPE ConvertToDXTopologyType(const ETopology& Topology)
    {
        switch (Topology)
        {
        case ETopology::kPoint: return D3D12_PRIMITIVE_TOPOLOGY_TYPE::D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
        case ETopology::kLine: return D3D12_PRIMITIVE_TOPOLOGY_TYPE::D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        case ETopology::kTriangle: return D3D12_PRIMITIVE_TOPOLOGY_TYPE::D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        case ETopology::kPatch: return D3D12_PRIMITIVE_TOPOLOGY_TYPE::D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;       
        }
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE::D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    }

    static ID3D12RootSignature* GenerateRootSignature(std::initializer_list<Ref<Shader>> shaders)
    {
        bool b_render_object_scene = true;
        if (b_render_object_scene)
        {
            ID3D12RootSignature* p_signature = nullptr;
            D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
            auto device = D3DContext::GetInstance()->GetDevice();
            if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
            {
                featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
            }
            static CD3DX12_DESCRIPTOR_RANGE1 ranges[32]{};
            static CD3DX12_ROOT_PARAMETER1 rootParameters[32]{};
            rootParameters[0].InitAsConstantBufferView(0u);
            rootParameters[1].InitAsConstantBufferView(1u);
            rootParameters[2].InitAsConstantBufferView(2u);

            for (auto it = shaders.begin(); it != shaders.end(); it++)
            {             
                auto reflection = static_cast<D3DShader*>(it->get())->GetD3DReflectionInfo();
                if (reflection == nullptr)
                {
                    LOG_WARNING(L"未获取到着色器反射信息！");
                    continue;
                }
                D3D12_SHADER_DESC desc{};
                reflection->GetDesc(&desc);
                std::vector<D3D12_SHADER_INPUT_BIND_DESC> bind_desc(desc.BoundResources);
                for (uint32_t i = 0u; i < desc.BoundResources; i++)
                {
                    auto resc_desc = reflection->GetResourceBindingDesc(i, &bind_desc[i]);
                    if (bind_desc[i].Type == D3D_SHADER_INPUT_TYPE::D3D_SIT_CBUFFER)
                    {

                    }
                }
            }
            D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
                D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
            CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
            uint16_t root_param_count = 3u;
            rootSignatureDesc.Init_1_1(root_param_count, rootParameters, 0, nullptr, rootSignatureFlags);
            ComPtr<ID3DBlob> signature;
            ComPtr<ID3DBlob> error;
            ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
            ThrowIfFailed(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&p_signature)));
            return p_signature;
        }
        else
        {
            return nullptr;
        }
    }

    D3DGraphicsPipelineState::D3DGraphicsPipelineState(const GraphicsPipelineStateInitializer& initializer) : _state_desc(initializer)
    {
    }

    void D3DGraphicsPipelineState::Build()
	{
        auto [desc, count] = ConvertToD3DInputLayout(_state_desc._input_layout);
        _p_sig = GenerateRootSignature({ _state_desc._p_vertex_shader,_state_desc._p_pixel_shader });
        _d3d_pso_desc.InputLayout = { desc, count };
        _d3d_pso_desc.pRootSignature = _p_sig.Get();
        _d3d_pso_desc.VS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<ID3DBlob*>(_state_desc._p_vertex_shader->GetByteCode(EShaderType::kVertex)));
        _d3d_pso_desc.PS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<ID3DBlob*>(_state_desc._p_pixel_shader->GetByteCode(EShaderType::kPixel)));
        _d3d_pso_desc.RasterizerState = ConvertToD3D12RasterizerDesc(_state_desc._raster_state);
        _d3d_pso_desc.BlendState = ConvertToD3D12BlendDesc(_state_desc._blend_state);
        _d3d_pso_desc.DepthStencilState = ConvertToD3D12DepthStencilDesc(_state_desc._depth_stencil_state);
        _d3d_pso_desc.SampleMask = UINT_MAX;
        _d3d_pso_desc.PrimitiveTopologyType = ConvertToDXTopologyType(_state_desc._topology);
        _d3d_pso_desc.NumRenderTargets = _state_desc._rt_nums;
        if (_state_desc._depth_stencil_state._b_depth_write) _d3d_pso_desc.DSVFormat = ConvertToDXGIFormat(_state_desc._ds_format);
        for (int i = 0; i < _state_desc._rt_nums; i++)
        {
            _d3d_pso_desc.RTVFormats[i] = ConvertToDXGIFormat(_state_desc._rt_formats[i]);
        }
        _d3d_pso_desc.SampleDesc.Count = 1;
        ThrowIfFailed(D3DContext::GetInstance()->GetDevice()->CreateGraphicsPipelineState(&_d3d_pso_desc, IID_PPV_ARGS(&_p_plstate)));
        _b_build = true;
	}
    void D3DGraphicsPipelineState::Bind()
    {
        if (!_b_build)
        {
            LOG_ERROR("PipelineState must be build before it been bind!");
            return;
        }
        D3DContext::GetInstance()->GetCmdList()->SetGraphicsRootSignature(_p_sig.Get());
        D3DContext::GetInstance()->GetCmdList()->SetPipelineState(_p_plstate.Get());
        D3DContext::GetInstance()->GetCmdList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }
    void D3DGraphicsPipelineState::CommitBindResource(uint16_t slot, void* res, EBindResourceType res_type)
    {
        static auto context = D3DContext::GetInstance();
        switch (res_type)
        {
        case Ailu::EBindResourceType::kConstBuffer:
        {
            D3D12_CONSTANT_BUFFER_VIEW_DESC view = *reinterpret_cast<D3D12_CONSTANT_BUFFER_VIEW_DESC*>(res);
            context->GetCmdList()->SetGraphicsRootConstantBufferView(slot, view.BufferLocation);
            return;
        }
        case Ailu::EBindResourceType::kTexture: return;
        }
        return;
    }
}
