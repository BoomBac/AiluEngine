#pragma once
#ifndef __D3DGFX_PIPELINE_STATE_H__
#define __D3DGFX_PIPELINE_STATE_H__
#include <d3dx12.h>

#include "Render/GraphicsPipelineStateObject.h"

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

	static std::tuple<D3D12_INPUT_ELEMENT_DESC*, u32> ConvertToD3DInputLayout(const VertexBufferLayout& layout)
	{
		static D3D12_INPUT_ELEMENT_DESC cache_desc[10]{};
		if (layout.GetDescCount() > 10)
		{
			AL_ASSERT(true, "LayoutDesc count must less than 10");
			return std::make_tuple<D3D12_INPUT_ELEMENT_DESC*, u32>(nullptr, 0);
		}
		u32 desc_count = 0u;
		for (const auto& desc : layout)
		{
			cache_desc[desc_count++] = { desc.Name.c_str(), 0, ShaderDataTypeToDGXIFormat(desc.Type), desc.Stream, desc.Offset, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
		}
		return std::make_tuple<D3D12_INPUT_ELEMENT_DESC*, u32>(&cache_desc[0], std::move(desc_count));
	}

	namespace ALHash
	{
		template<>
		static u32 Hasher(const D3D12_PRIMITIVE_TOPOLOGY_TYPE& obj)
		{
			switch (obj)
			{
			case D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED: return 0;
			case D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT:  return 0;
			case D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE:  return 1;
			case D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE:  return 2;
			case D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH:  return 3;
			}
			return 0;
		}
	}


	class D3DGraphicsPipelineState : public GraphicsPipelineStateObject
	{
	public:
		D3DGraphicsPipelineState(const GraphicsPipelineStateInitializer& initializer);
		void Build() final;
		void Bind(CommandBuffer* cmd) final;
		void SetPipelineResource(CommandBuffer* cmd,void* res, const EBindResDescType& res_type, u8 slot = 255) final;
		void SetPipelineResource(CommandBuffer* cmd,void* res, const EBindResDescType& res_type, const String& name) final;
		//just compare res type is same by slot,maybe cause some error
		bool IsValidPipelineResource(const EBindResDescType& res_type, u8 slot) const final;
		const ALHash::Hash<64>& Hash() final { return _hash; };
		const String& Name() const final { return _name; };
	private:
		void BindResource(CommandBuffer* cmd,void* res, const EBindResDescType& res_type, u8 slot = 255) final;
	private:
		String _name;
		u8 _per_frame_cbuf_bind_slot = 255u;
		D3D12_GRAPHICS_PIPELINE_STATE_DESC _d3d_pso_desc;
		GraphicsPipelineStateInitializer _state_desc;
		bool _b_build = false;
		ComPtr<ID3D12PipelineState> _p_plstate;
		ComPtr<ID3D12RootSignature>_p_sig;
		ID3D12GraphicsCommandList* _p_cmd;
		std::unordered_map<std::string, ShaderBindResourceInfo>* _p_bind_res_desc_infos = nullptr;
		std::unordered_multimap<u8,EBindResDescType> _bind_res_desc_type_lut;
		ALHash::Hash<64> _hash;
	};
}


#endif // !D3DGFX_PIPELINE_STATE_H__

