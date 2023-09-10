#pragma once
#ifndef __D3DSHADER_H__
#define __D3DSHADER_H__
#include "Render/Shader.h"
#include "D3DConstants.h"
#include <d3dx12.h>

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
	private:
		bool _b_init_buffer = false;
		uint32_t _desc_size = 0;
		uint8_t* _p_cbuffer = nullptr;
		ComPtr<ID3D12DescriptorHeap> m_cbvHeap;
		ComPtr<ID3D12Resource> m_constantBuffer;
	};
}


#endif // !__D3DSHADER_H__


