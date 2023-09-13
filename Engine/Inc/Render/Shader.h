#pragma once
#ifndef __SHADER_H__
#define __SHADER_H__
#include <string>
#include "Framework/Math/ALMath.hpp"

namespace Ailu
{
	enum class EShaderType : uint8_t
	{
		kVertex,kPixel
	};
	class Shader
	{
		friend class Material;
	public:
		virtual ~Shader() = default;
		virtual void Bind() = 0;
		virtual void SetGlobalVector(const std::string& name, const Vector4f& vector) = 0;
		virtual void SetGlobalVector(const std::string& name, const Vector3f& vector) = 0;
		virtual void SetGlobalVector(const std::string& name, const Vector2f& vector) = 0;
		virtual void SetGlobalMatrix(const std::string& name, const Matrix4x4f& mat) = 0;
		virtual void SetGlobalMatrix(const std::string& name, const Matrix3x3f& mat) = 0;
		virtual void* GetByteCode() = 0;
		static Shader* Create(const std::string_view file_name, EShaderType type);
	protected:
		virtual void Bind(uint32_t index) = 0;
		virtual uint8_t* GetCBufferPtr(uint32_t index) = 0;
		virtual uint16_t GetVariableOffset(const std::string& name) const = 0;
	};
}


#endif // !SHADER_H__

