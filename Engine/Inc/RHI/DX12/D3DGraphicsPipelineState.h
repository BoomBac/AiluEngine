#pragma once
#ifndef __D3DGFX_PIPELINE_STATE_H__
#define __D3DGFX_PIPELINE_STATE_H__
#include <d3dx12.h>

#include "Render/GraphicsPipelineState.h"

using Microsoft::WRL::ComPtr;
namespace Ailu
{
    static DXGI_FORMAT ShaderDataTypeToDGXIFormat(EShaderDateType type)
    {
        switch (type)
        {
        case Ailu::EShaderDateType::kFloat:   return DXGI_FORMAT_R32_FLOAT;
        case Ailu::EShaderDateType::kFloat2:  return DXGI_FORMAT_R32G32_FLOAT;
        case Ailu::EShaderDateType::kFloat3:  return DXGI_FORMAT_R32G32B32_FLOAT;
        case Ailu::EShaderDateType::kFloat4:  return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case Ailu::EShaderDateType::kMat3:    return DXGI_FORMAT_UNKNOWN;
        case Ailu::EShaderDateType::kMat4:    return DXGI_FORMAT_UNKNOWN;
        case Ailu::EShaderDateType::kInt:     return DXGI_FORMAT_R32_SINT;
        case Ailu::EShaderDateType::kInt2:    return DXGI_FORMAT_R32G32_SINT;
        case Ailu::EShaderDateType::kInt3:    return DXGI_FORMAT_R32G32B32_SINT;
        case Ailu::EShaderDateType::kInt4:    return DXGI_FORMAT_R32G32B32A32_SINT;
        case Ailu::EShaderDateType::kuInt:     return DXGI_FORMAT_R32_UINT;
        case Ailu::EShaderDateType::kuInt2:    return DXGI_FORMAT_R32G32_UINT;
        case Ailu::EShaderDateType::kuInt3:    return DXGI_FORMAT_R32G32B32_UINT;
        case Ailu::EShaderDateType::kuInt4:    return DXGI_FORMAT_R32G32B32A32_UINT;
        case Ailu::EShaderDateType::kBool:    return DXGI_FORMAT_R8_UINT;
        }
        AL_ASSERT(true, "Unknown ShaderDateType or DGXI format");
        return DXGI_FORMAT_UNKNOWN;
    }

    static std::tuple<D3D12_INPUT_ELEMENT_DESC*, uint32_t> ConvertToD3DInputLayout(const VertexBufferLayout& layout)
    {
        static D3D12_INPUT_ELEMENT_DESC cache_desc[10]{};
        if (layout.GetDescCount() > 10)
        {
            AL_ASSERT(true, "LayoutDesc count must less than 10");
            return std::make_tuple<D3D12_INPUT_ELEMENT_DESC*, uint32_t>(nullptr, 0);
        }
        uint32_t desc_count = 0u;
        for (const auto& desc : layout)
        {
            cache_desc[desc_count++] = { desc.Name.c_str(), 0, ShaderDataTypeToDGXIFormat(desc.Type), desc.Stream, desc.Offset, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
        }
        return std::make_tuple<D3D12_INPUT_ELEMENT_DESC*, uint32_t>(&cache_desc[0], std::move(desc_count));
    }

	class D3DGraphicsPipelineState : public GraphicsPipelineState
	{
	public:
        D3DGraphicsPipelineState() {};
		void Build() override;
	private:
		ComPtr<ID3D12PipelineState> m_pipelineState;
	};
}


#endif // !D3DGFX_PIPELINE_STATE_H__
