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
		uint16_t GetVariableOffset(const std::string& name) const override;
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
	};
}


#endif // !__D3DSHADER_H__


