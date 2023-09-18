#pragma once
#ifndef __D3DSHADER_H__
#define __D3DSHADER_H__
#include <d3dx12.h>
#include <d3d12shader.h>
#include <map>

#include "Render/Shader.h"
#include "D3DConstants.h"



using Microsoft::WRL::ComPtr;

namespace Ailu
{
	class D3DShader : public Shader
	{
	public:
		D3DShader(const std::string_view file_name, const std::string_view shader_name, const uint32_t& id ,EShaderType type);
		D3DShader(const std::string_view file_name, const std::string_view shader_name,const uint32_t& id);
		~D3DShader();
		void Bind() override;
		void Bind(uint32_t index) override;

		inline std::string GetName() const override;
		inline uint32_t GetID() const override;

		void SetGlobalVector(const std::string& name, const Vector4f& vector) override;
		void SetGlobalVector(const std::string& name, const Vector3f& vector) override;
		void SetGlobalVector(const std::string& name, const Vector2f& vector) override;
		void SetGlobalMatrix(const std::string& name, const Matrix4x4f& mat) override;
		void SetGlobalMatrix(const std::string& name, const Matrix3x3f& mat) override;
		void* GetByteCode(EShaderType type) override;
		ComPtr<ID3D12RootSignature> GetSignature() const;
		ID3D12ShaderReflection* GetD3DReflectionInfo() const;
		ID3D12ShaderReflection* GetD3DReflectionInfo(const EShaderType& type) const;
		static ComPtr<ID3D12RootSignature> GetCurrentActiveSignature();
	private:
		uint8_t* GetCBufferPtr(uint32_t index) override;
		void LoadShaderRelfection(ID3D12ShaderReflection* reflection);
		uint16_t GetVariableOffset(const std::string& name) const override;

	private:
		std::string _name = "DefaultShader";
		uint32_t _id;
		bool _b_init_buffer = false;
		uint32_t _desc_size = 0;
		ComPtr<ID3DBlob> _p_vblob = nullptr;
		ComPtr<ID3DBlob> _p_pblob = nullptr;
		uint8_t* _p_cbuffer = nullptr;
		inline static ComPtr<ID3D12DescriptorHeap> m_cbvHeap = nullptr;
		inline static ComPtr<ID3D12Resource> m_constantBuffer = nullptr;
		ComPtr<ID3D12RootSignature> _p_sig;
		ComPtr<ID3D12ShaderReflection> _p_v_reflection;
		ComPtr<ID3D12ShaderReflection> _p_p_reflection;
		std::map<std::string, uint16_t> _variable_offset;
		inline static ComPtr<ID3D12RootSignature> s_active_sig;
	};
}


#endif // !__D3DSHADER_H__


