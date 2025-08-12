#pragma once
#ifndef __D3DGFX_PIPELINE_STATE_H__
#define __D3DGFX_PIPELINE_STATE_H__
#include <d3dx12.h>

#include "Render/GraphicsPipelineStateObject.h"

using Microsoft::WRL::ComPtr;

namespace Ailu::Math::ALHash
{
    template<>
    static u32 HashFunc(const D3D12_PRIMITIVE_TOPOLOGY_TYPE &obj)
    {
        switch (obj)
        {
            case D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED:
                return 0;
            case D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT:
                return 0;
            case D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE:
                return 1;
            case D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE:
                return 2;
            case D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH:
                return 3;
        }
        return 0;
    }
}// namespace Ailu::Math::ALHash
using Ailu::Render::EShaderDateType;
using Ailu::Render::VertexBufferLayout;
using Ailu::Render::GraphicsPipelineStateObject;
using Ailu::Render::GraphicsPipelineStateInitializer;
using Ailu::Render::GraphicsContext;
using Ailu::Render::BindParams;
using Ailu::Render::PipelineResource;
using Ailu::Render::ETopology;
using Ailu::Render::UploadParams;

namespace Ailu::RHI::DX12
{
    static DXGI_FORMAT ShaderDataTypeToDGXIFormat(EShaderDateType type)
    {
        switch (type)
        {
            case EShaderDateType::kFloat:
                return DXGI_FORMAT_R32_FLOAT;
            case EShaderDateType::kFloat2:
                return DXGI_FORMAT_R32G32_FLOAT;
            case EShaderDateType::kFloat3:
                return DXGI_FORMAT_R32G32B32_FLOAT;
            case EShaderDateType::kFloat4:
                return DXGI_FORMAT_R32G32B32A32_FLOAT;
            case EShaderDateType::kMat3:
                return DXGI_FORMAT_UNKNOWN;
            case EShaderDateType::kMat4:
                return DXGI_FORMAT_UNKNOWN;
            case EShaderDateType::kInt:
                return DXGI_FORMAT_R32_SINT;
            case EShaderDateType::kInt2:
                return DXGI_FORMAT_R32G32_SINT;
            case EShaderDateType::kInt3:
                return DXGI_FORMAT_R32G32B32_SINT;
            case EShaderDateType::kInt4:
                return DXGI_FORMAT_R32G32B32A32_SINT;
            case EShaderDateType::kuInt:
                return DXGI_FORMAT_R32_UINT;
            case EShaderDateType::kuInt2:
                return DXGI_FORMAT_R32G32_UINT;
            case EShaderDateType::kuInt3:
                return DXGI_FORMAT_R32G32B32_UINT;
            case EShaderDateType::kuInt4:
                return DXGI_FORMAT_R32G32B32A32_UINT;
            case EShaderDateType::kBool:
                return DXGI_FORMAT_R8_UINT;
        }
        AL_ASSERT_MSG(false, "Unknown ShaderDateType or DGXI format");
        return DXGI_FORMAT_UNKNOWN;
    }

    static std::tuple<D3D12_INPUT_ELEMENT_DESC *, u32> ConvertToD3DInputLayout(const VertexBufferLayout &layout)
    {
        static D3D12_INPUT_ELEMENT_DESC cache_desc[10]{};
        if (layout.GetDescCount() > 10)
        {
            AL_ASSERT_MSG(false, "LayoutDesc count must less than 10");
            return std::make_tuple<D3D12_INPUT_ELEMENT_DESC *, u32>(nullptr, 0);
        }
        u32 desc_count = 0u;
        for (const auto &desc: layout)
        {
            cache_desc[desc_count++] = {desc.Name.c_str(), 0, ShaderDataTypeToDGXIFormat(desc.Type), desc.Stream, desc.Offset, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0};
        }
        return std::make_tuple<D3D12_INPUT_ELEMENT_DESC *, u32>(&cache_desc[0], std::move(desc_count));
    }

    class D3DGraphicsPipelineState : public GraphicsPipelineStateObject
    {
    public:
        D3DGraphicsPipelineState(const GraphicsPipelineStateInitializer &initializer);
        ~D3DGraphicsPipelineState();
        void SetTopology(ETopology topology) final;

    private:
        void BindImpl(RHICommandBuffer *rhi_cmd, const BindParams& params) final;
        void UploadImpl(GraphicsContext *ctx, RHICommandBuffer *rhi_cmd, UploadParams *params) final;
        void BindResource(RHICommandBuffer *cmd, const PipelineResource &res);

    private:
        D3D12_GRAPHICS_PIPELINE_STATE_DESC _d3d_pso_desc;
        D3D_PRIMITIVE_TOPOLOGY _d3d_topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        ComPtr<ID3D12PipelineState> _p_plstate;
        ComPtr<ID3D12RootSignature> _p_sig;
        ID3D12GraphicsCommandList *_p_cmd;
    };
}// namespace Ailu::RHI::DX12


#endif// !D3DGFX_PIPELINE_STATE_H__
