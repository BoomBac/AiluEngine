#pragma once
#ifndef __D3DSHADER_H__
#define __D3DSHADER_H__
#include <d3dx12.h>
#include <d3d12shader.h>
#include <map>
#include <unordered_map>

#include "Render/RenderConstants.h"
#include "Render/Shader.h"
#include "RHI/DX12/D3DGraphicsPipelineState.h"


using Microsoft::WRL::ComPtr;

namespace Ailu
{
	static DXGI_FORMAT GetFormatBySemanticName(const char* semantic)
	{
		if (!std::strcmp(semantic, RenderConstants::kSemanticPosition)) return DXGI_FORMAT_R32G32B32_FLOAT;
		else if (!std::strcmp(semantic, RenderConstants::kSemanticNormal)) return DXGI_FORMAT_R32G32B32_FLOAT;
		else if (!std::strcmp(semantic, RenderConstants::kSemanticTangent)) return DXGI_FORMAT_R32G32B32A32_FLOAT;
		else if (!std::strcmp(semantic, RenderConstants::kSemanticColor)) return DXGI_FORMAT_R32G32B32A32_FLOAT;
		else if (!std::strcmp(semantic, RenderConstants::kSemanticTexcoord)) return DXGI_FORMAT_R32G32_FLOAT;
		else
		{
			LOG_ERROR("Unsupported vertex shader semantic!");
			return DXGI_FORMAT_R32G32B32_FLOAT;
		}
	}

	namespace D3DConvertUtils
	{
		static std::tuple<D3D12_INPUT_ELEMENT_DESC*, u8> ConvertToD3D12InputLayout(const VertexInputLayout& layout)
		{
			static D3D12_INPUT_ELEMENT_DESC desc_arr[RenderConstants::kMaxVertexAttrNum];
			u8 layout_index = 0;
			for (auto& it : layout)
			{
				desc_arr[layout_index++] = D3D12_INPUT_ELEMENT_DESC{ it.Name.c_str(), 0,
					GetFormatBySemanticName(it.Name.c_str()), it.Stream, it.Offset, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
			}
			return std::make_tuple(desc_arr,layout_index + 1);
		}

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

		static D3D12_BLEND ConvertToD3D12BlendFactor(const EBlendFactor& factor)
		{
			switch (factor)
			{
			case EBlendFactor::kZero:                   return D3D12_BLEND_ZERO;
			case EBlendFactor::kOne:                    return D3D12_BLEND_ONE;
			case EBlendFactor::kSrcColor:               return D3D12_BLEND_SRC_COLOR;
			case EBlendFactor::kOneMinusSrcColor:       return D3D12_BLEND_INV_SRC_COLOR;
			case EBlendFactor::kDstColor:               return D3D12_BLEND_DEST_COLOR;
			case EBlendFactor::kOneMinusDstColor:       return D3D12_BLEND_INV_DEST_COLOR;
			case EBlendFactor::kSrcAlpha:               return D3D12_BLEND_SRC_ALPHA;
			case EBlendFactor::kOneMinusSrcAlpha:       return D3D12_BLEND_INV_SRC_ALPHA;
			case EBlendFactor::kDstAlpha:               return D3D12_BLEND_DEST_ALPHA;
			case EBlendFactor::kOneMinusDstAlpha:       return D3D12_BLEND_INV_DEST_ALPHA;
				//case EBlendFactor::kConstantColor:          return
				//case EBlendFactor::kOneMinusConstantColor:  return
				//case EBlendFactor::kConstantAlpha:          return
				//case EBlendFactor::kOneMinusConstantAlpha:  return
				//case EBlendFactor::kSrcAlphaSaturate:       return
			case EBlendFactor::kSrc1Color:              return D3D12_BLEND_SRC1_COLOR;
			case EBlendFactor::kOneMinusSrc1Color:      return D3D12_BLEND_INV_SRC1_COLOR;
			case EBlendFactor::kSrc1Alpha:              return D3D12_BLEND_SRC1_ALPHA;
			case EBlendFactor::kOneMinusSrc1Alpha:      return D3D12_BLEND_INV_SRC1_ALPHA;
			}
			return D3D12_BLEND_ZERO;
		}

		static D3D12_BLEND_OP ConvertToD3D12BlendOp(const EBlendOp& op)
		{
			switch (op)
			{
			case EBlendOp::kAdd:             return D3D12_BLEND_OP_ADD;
			case EBlendOp::kMax:             return D3D12_BLEND_OP_MAX;
			case EBlendOp::kMin:             return D3D12_BLEND_OP_MIN;
			case EBlendOp::kReverseSubtract: return D3D12_BLEND_OP_REV_SUBTRACT;
			case EBlendOp::kSubtract:        return D3D12_BLEND_OP_SUBTRACT;
			}
			return D3D12_BLEND_OP_ADD;
		}

		static uint8_t ConvertToD3DBlendColorMask(const EColorMask& mask)
		{
			switch (mask)
			{
			case Ailu::EColorMask::kRED:    return D3D12_COLOR_WRITE_ENABLE_RED;
			case Ailu::EColorMask::kGREEN:  return D3D12_COLOR_WRITE_ENABLE_GREEN;
			case Ailu::EColorMask::kBLUE:   return D3D12_COLOR_WRITE_ENABLE_BLUE;
			case Ailu::EColorMask::kALPHA:  return D3D12_COLOR_WRITE_ENABLE_ALPHA;
			case Ailu::EColorMask::k_NONE:  return 0u;
			case Ailu::EColorMask::k_RGB:   return (D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN | D3D12_COLOR_WRITE_ENABLE_BLUE);
			case Ailu::EColorMask::k_RGBA:  return D3D12_COLOR_WRITE_ENABLE_ALL;
			case Ailu::EColorMask::k_RG:    return (D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN);
			case Ailu::EColorMask::k_BA:    return (D3D12_COLOR_WRITE_ENABLE_BLUE | D3D12_COLOR_WRITE_ENABLE_ALPHA);
			}
			return D3D12_COLOR_WRITE_ENABLE_ALL;
		}

		static D3D12_BLEND_DESC ConvertToD3D12BlendDesc(const BlendState& state, const uint8_t& rt_num)
		{
			auto blend_state = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
			for (size_t i = 0; i < rt_num; i++)
			{
				blend_state.RenderTarget[i].BlendEnable = state._b_enable;
				blend_state.RenderTarget[i].BlendOp = ConvertToD3D12BlendOp(state._color_op);
				blend_state.RenderTarget[i].BlendOpAlpha = ConvertToD3D12BlendOp(state._alpha_op);
				blend_state.RenderTarget[i].SrcBlend = ConvertToD3D12BlendFactor(state._src_color);
				blend_state.RenderTarget[i].SrcBlendAlpha = ConvertToD3D12BlendFactor(state._src_alpha);
				blend_state.RenderTarget[i].DestBlend = ConvertToD3D12BlendFactor(state._dst_color);
				blend_state.RenderTarget[i].DestBlendAlpha = ConvertToD3D12BlendFactor(state._dst_alpha);
				blend_state.RenderTarget[i].RenderTargetWriteMask = ConvertToD3DBlendColorMask(state._color_mask);
			}
			return blend_state;
		}

		static D3D12_BLEND_DESC ConvertToD3D12BlendDesc(const BlendState& state)
		{
			return ConvertToD3D12BlendDesc(state, 1);
		}

		static D3D12_DEPTH_STENCIL_DESC ConvertToD3D12DepthStencilDesc(const DepthStencilState& state)
		{
			auto ds_state = CD3DX12_DEPTH_STENCIL_DESC(CD3DX12_DEFAULT{});
			ds_state.DepthEnable = state._b_depth_write && state._depth_test_func != ECompareFunc::kAlways;
			switch (state._depth_test_func)
			{
			case ECompareFunc::kLess: ds_state.DepthFunc = D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_LESS; break;
			case ECompareFunc::kLessEqual: ds_state.DepthFunc = D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_LESS_EQUAL; break;
			case ECompareFunc::kGreater: ds_state.DepthFunc = D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_GREATER; break;
			case ECompareFunc::kGreaterEqual: ds_state.DepthFunc = D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_GREATER_EQUAL; break;
			case ECompareFunc::kAlways: ds_state.DepthFunc = D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_ALWAYS; break;
			case ECompareFunc::kNever: ds_state.DepthFunc = D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_NEVER; break;
			case ECompareFunc::kEqual: ds_state.DepthFunc = D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_EQUAL; break;
			case ECompareFunc::kNotEqual: ds_state.DepthFunc = D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_NOT_EQUAL; break;
			}
			ds_state.StencilEnable = state._b_front_stencil;
			return ds_state;
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

		static D3D_PRIMITIVE_TOPOLOGY ConvertToDXTopology(const ETopology& Topology)
		{
			switch (Topology)
			{
			case ETopology::kPoint: return D3D12_PRIMITIVE_TOPOLOGY::D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
			case ETopology::kLine: return D3D12_PRIMITIVE_TOPOLOGY::D3D_PRIMITIVE_TOPOLOGY_LINELIST;
			case ETopology::kTriangle: return D3D12_PRIMITIVE_TOPOLOGY::D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			case ETopology::kPatch: return D3D12_PRIMITIVE_TOPOLOGY::D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST;
			}
			return D3D12_PRIMITIVE_TOPOLOGY::D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
		}
	}

	class D3DShader : public Shader
	{
		struct PSOSystem
		{
			inline std::tuple<ComPtr<ID3D12RootSignature>&, ComPtr<ID3D12PipelineState>&> GetFront()
			{
				return _b_current_is_a ? std::tie(_p_sig_a, _p_pso_a) : std::tie(_p_sig_b, _p_pso_b);
			}
			inline std::tuple<ComPtr<ID3D12RootSignature>&, ComPtr<ID3D12PipelineState>&> GetBack()
			{
				auto& sig = !_b_current_is_a ? _p_sig_a : _p_sig_b;
				auto& pso = !_b_current_is_a ? _p_pso_a : _p_pso_b;
				sig.Reset();
				pso.Reset();
				return std::tie(sig, pso);
			}
			void Bind(ID3D12GraphicsCommandList* cmd)
			{
				auto [sig, pso] = GetFront();
				cmd->SetPipelineState(pso.Get());
				cmd->SetGraphicsRootSignature(sig.Get());
			}
			inline void Swap()
			{
				_b_current_is_a = !_b_current_is_a;
			}
		private:
			bool _b_current_is_a = true;
			ComPtr<ID3D12RootSignature> _p_sig_a;
			ComPtr<ID3D12PipelineState> _p_pso_a;
			ComPtr<ID3D12RootSignature> _p_sig_b;
			ComPtr<ID3D12PipelineState> _p_pso_b;
		};
		struct ShaderVariantData
		{
			std::unordered_map<std::string, ShaderBindResourceInfo> _bind_res_infos{};
			PSOSystem _pso_sys;
			bool _is_error;
			ComPtr<ID3DBlob> _p_vblob = nullptr;
			ComPtr<ID3DBlob> _p_pblob = nullptr;
			ComPtr<ID3D12ShaderReflection> _p_v_reflection;
			ComPtr<ID3D12ShaderReflection> _p_p_reflection;
			List<ShaderPropertyInfo> _shader_prop_infos;
			D3D12_GRAPHICS_PIPELINE_STATE_DESC _pso_desc;
			std::set<Material*> _reference_mats;
		};
	public:
		D3DShader(const std::string& sys_path, const std::string_view shader_name, const uint32_t& id ,EShaderType type);
		D3DShader(const std::string& sys_path, const std::string_view shader_name,const uint32_t& id);
		~D3DShader();
		void Bind() override;
		void Bind(uint32_t index) override;
		bool Compile() final;

		inline std::string GetName() const override;
		inline uint32_t GetID() const override;
		inline const std::unordered_map<std::string, ShaderBindResourceInfo>& GetBindResInfo() const final;
		void SetGlobalVector(const std::string& name, const Vector4f& vector) override;
		void SetGlobalVector(const std::string& name, const Vector3f& vector) override;
		void SetGlobalVector(const std::string& name, const Vector2f& vector) override;
		void SetGlobalMatrix(const std::string& name, const Matrix4x4f& mat) override;
		void SetGlobalMatrix(const std::string& name, const Matrix3x3f& mat) override;
		const Vector<String>& GetVSInputSemanticSeqences() const final;
		Vector<Material*> GetAllReferencedMaterials() final;
		const std::set<String>& GetSourceFiles() const final;
		Vector4f GetVectorValue(const std::string& name);
		float GetFloatValue(const std::string& name);
		void* GetByteCode(EShaderType type) override;
		const String& GetSrcPath() const final;

		const VertexInputLayout& PipelineInputLayout() const final { return _pipeline_input_layout; };
		const RasterizerState& PipelineRasterizerState() const final { return _pipeline_raster_state; };
		const DepthStencilState& PipelineDepthStencilState() const final { return _pipeline_ds_state; };
		const ETopology& PipelineTopology() const final { return _pipeline_topology; };
		const BlendState& PipelineBlendState() const final { return _pipeline_blend_state; };

		ID3D12RootSignature* GetSignature();
		ID3D12ShaderReflection* GetD3DReflectionInfo() const;
		ID3D12ShaderReflection* GetD3DReflectionInfo(const EShaderType& type) const;
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC& GetD3DPSODesc() const { return _pso_desc; }
		std::pair<D3D12_INPUT_ELEMENT_DESC*,uint8_t> GetVertexInputLayout();
		const std::map<String, Vector<String>> GetKeywordGroups() const final { return _keywords; };
	private:
		void AddMaterialRef(Material* mat) final;
		void RemoveMaterialRef(Material* mat) final;
		void ParserShaderProperty(String& line, List<ShaderPropertyInfo>& props);
		void PreProcessShader() final;
		void LoadAdditionInfo();
		void Reset();
		const List<ShaderPropertyInfo>& GetShaderPropertyInfos() const final;
		uint8_t* GetCBufferPtr(uint32_t index) override;
		void LoadShaderReflection(ID3D12ShaderReflection* reflection,const EShaderType& type);
		void LoadAdditionalShaderReflection(const String& sys_path);
		void GenerateInternalPSO();

	private:
		std::string _name = "DefaultShader";
		uint32_t _id;
		String _vert_entry, _pixel_entry;
		uint8_t _vertex_input_num = 0u;
		String _src_file_path;
		short _per_mat_buf_bind_slot = -1;
		i8 _per_frame_buf_bind_slot = -1;
		D3D12_INPUT_ELEMENT_DESC _vertex_input_layout[RenderConstants::kMaxVertexAttrNum];
		std::unordered_map<std::string,ShaderBindResourceInfo> _bind_res_infos{};
		PSOSystem _pso_sys;
		Vector<String> _semantic_seq;
		std::map<String, Vector<String>> _keywords;
		List<D3D_SHADER_MACRO*> _keyword_defines;
		//keyword,<kwgroup_id,kw_inner_group_id>
		std::map<String, std::tuple<u8, u8>> _keywords_ids;
		std::set<uint64_t> _shader_variant;
		inline static bool _b_init_buffer = false;
		bool is_error;
		uint32_t _desc_size = 0;
		ComPtr<ID3DBlob> _p_vblob = nullptr;
		ComPtr<ID3DBlob> _p_pblob = nullptr;
		inline static u8* _p_cbuffer = nullptr;
		inline static ComPtr<ID3D12DescriptorHeap> m_cbvHeap = nullptr;
		ComPtr<ID3D12ShaderReflection> _p_v_reflection;
		ComPtr<ID3D12ShaderReflection> _p_p_reflection;
		D3D12_PRIMITIVE_TOPOLOGY _topology;
		std::map<std::string, uint16_t> _variable_offset;
		List<ShaderPropertyInfo> _shader_prop_infos;
		D3D12_GRAPHICS_PIPELINE_STATE_DESC _pso_desc;
		std::set<Material*> _reference_mats;
		std::set<String> _source_files;

		VertexInputLayout _pipeline_input_layout;
		RasterizerState _pipeline_raster_state;
		DepthStencilState _pipeline_ds_state;
		ETopology _pipeline_topology;
		BlendState _pipeline_blend_state;
	};
}


#endif // !__D3DSHADER_H__


