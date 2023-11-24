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
	public:
		D3DShader(const std::string& file_name, const std::string_view shader_name, const uint32_t& id ,EShaderType type);
		D3DShader(const std::string& file_name, const std::string_view shader_name,const uint32_t& id);
		~D3DShader();
		void Bind() override;
		void Bind(uint32_t index) override;
		void Compile() final;

		inline std::string GetName() const override;
		inline uint32_t GetID() const override;
		inline const std::unordered_map<std::string, ShaderBindResourceInfo>& GetBindResInfo() const final;
		void SetGlobalVector(const std::string& name, const Vector4f& vector) override;
		void SetGlobalVector(const std::string& name, const Vector3f& vector) override;
		void SetGlobalVector(const std::string& name, const Vector2f& vector) override;
		void SetGlobalMatrix(const std::string& name, const Matrix4x4f& mat) override;
		void SetGlobalMatrix(const std::string& name, const Matrix3x3f& mat) override;
		const Vector<String>& GetVSInputSemanticSeqences() const final;
		Vector4f GetVectorValue(const std::string& name);
		float GetFloatValue(const std::string& name);
		void* GetByteCode(EShaderType type) override;
		const String& GetSrcPath() const final;
		ComPtr<ID3D12RootSignature> GetSignature() const;
		ID3D12ShaderReflection* GetD3DReflectionInfo() const;
		ID3D12ShaderReflection* GetD3DReflectionInfo(const EShaderType& type) const;
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC& GetD3DPSODesc() const { return _pso_desc; }
		std::pair<D3D12_INPUT_ELEMENT_DESC*,uint8_t> GetVertexInputLayout();
	private:
		void AddMaterialRef(Material* mat) final;
		void RemoveMaterialRef(Material* mat) final;
		static void ParserShaderProperty(String& line, List<ShaderPropertyInfo>& props);
		void PreProcessShader() final;
		void Reset();
		const List<ShaderPropertyInfo>& GetShaderPropertyInfos() const final;
		uint8_t* GetCBufferPtr(uint32_t index) override;
		void LoadShaderRelfection(ID3D12ShaderReflection* reflection,const EShaderType& type);
		void LoadAdditionalShaderRelfection(const String& sys_path);
		uint16_t GetVariableOffset(const std::string& name) const override;
		void GenerateInternalPSO();

	private:
		String _vert_entry, _pixel_entry;
		D3D12_INPUT_ELEMENT_DESC _vertex_input_layout[RenderConstants::kMaxVertexAttrNum];
		uint8_t _vertex_input_num = 0u;
		String _src_file_path;
		short _per_mat_buf_bind_slot = -1;
		i8 _per_frame_buf_bind_slot = -1;
		std::unordered_map<std::string,ShaderBindResourceInfo> _bind_res_infos{};

		std::string _name = "DefaultShader";
		Vector<String> _semantic_seq;
		uint32_t _id;
		inline static bool _b_init_buffer = false;
		uint32_t _desc_size = 0;
		ComPtr<ID3DBlob> _p_vblob = nullptr;
		ComPtr<ID3DBlob> _p_pblob = nullptr;
		uint8_t* _p_cbuffer = nullptr;
		inline static ComPtr<ID3D12DescriptorHeap> m_cbvHeap = nullptr;
		ComPtr<ID3D12RootSignature> _p_sig;
		ComPtr<ID3D12ShaderReflection> _p_v_reflection;
		ComPtr<ID3D12ShaderReflection> _p_p_reflection;
		std::map<std::string, uint16_t> _variable_offset;
		inline static ComPtr<ID3D12RootSignature> s_active_sig;
		List<ShaderPropertyInfo> _shader_prop_infos;
		ComPtr<ID3D12PipelineState> _p_pso;
		D3D12_GRAPHICS_PIPELINE_STATE_DESC _pso_desc;
		std::set<Material*> _reference_mats;
	};
}


#endif // !__D3DSHADER_H__


