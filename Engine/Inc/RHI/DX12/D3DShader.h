#pragma once
#ifndef __D3DSHADER_H__
#define __D3DSHADER_H__
#include "Render/Shader.h"
#include "D3DConstants.h"
#include <d3dx12.h>
#include <d3d12shader.h>

using Microsoft::WRL::ComPtr;

namespace Ailu
{
	class D3DShader : public Shader
	{
	public:
		D3DShader(const std::string_view file_name, EShaderType type);
		~D3DShader();
		void Bind() override;


		void SetGlobalVector(const std::string_view name, const Vector4f& vector) override;
		void SetGlobalVector(const std::string_view name, const Vector3f& vector) override;
		void SetGlobalVector(const std::string_view name, const Vector2f& vector) override;
		void SetGlobalMatrix(const std::string_view name, const Matrix4x4f& mat) override;
		void SetGlobalMatrix(const std::string_view name, const Matrix3x3f& mat) override;
		void* GetByteCode() override;
		ComPtr<ID3D12RootSignature> GetSignature() const;
	private:
		void Bind(uint32_t index) override;
		uint8_t* GetCBufferPtr(uint32_t index) override;

		bool _b_init_buffer = false;
		uint32_t _desc_size = 0;
		ComPtr<ID3DBlob> _p_blob = nullptr;
		uint8_t* _p_cbuffer = nullptr;
		inline static ComPtr<ID3D12DescriptorHeap> m_cbvHeap = nullptr;
		inline static ComPtr<ID3D12Resource> m_constantBuffer = nullptr;
		ComPtr<ID3D12RootSignature> _p_sig;
		ComPtr<ID3D12ShaderReflection> _p_reflection;

	};
}


#endif // !__D3DSHADER_H__


