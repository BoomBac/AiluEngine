#pragma once
#ifndef __SHADER_H__
#define __SHADER_H__
#include <string>
#include <map>
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
		virtual std::string GetName() const = 0;
		virtual uint32_t GetID() const = 0;
		virtual void SetGlobalVector(const std::string& name, const Vector4f& vector) = 0;
		virtual void SetGlobalVector(const std::string& name, const Vector3f& vector) = 0;
		virtual void SetGlobalVector(const std::string& name, const Vector2f& vector) = 0;
		virtual void SetGlobalMatrix(const std::string& name, const Matrix4x4f& mat) = 0;
		virtual void SetGlobalMatrix(const std::string& name, const Matrix3x3f& mat) = 0;
		virtual void* GetByteCode(EShaderType type) = 0;
		[[deprecated("combine vertex and pixel shader")]] static Shader* Create(const std::string_view file_name,const std::string& shader_name,EShaderType type);
		static Shader* Create(const std::string_view file_name, const std::string& shader_name = "DefaultShader");
	protected:
		virtual void Bind(uint32_t index) = 0;
		virtual uint8_t* GetCBufferPtr(uint32_t index) = 0;
		virtual uint16_t GetVariableOffset(const std::string& name) const = 0;
	};
	class ShaderLibrary
	{
		friend class Shader;
	public:
		static Ref<Shader> GetShader(std::string& shader_name)
		{
			auto it = s_shader_name.find(shader_name);
			if (it == s_shader_name.end())
			{
				AL_ASSERT(true, std::format("Shader: {} dont's exist in ShaderLibrary!",shader_name ));
				return nullptr;
			}
			return s_shader_library.find(it->second)->second;
		}
	private:
		inline static uint32_t s_shader_id = 0u;
		inline static std::map<uint32_t, Ref<Shader>> s_shader_library;
		inline static std::map<std::string, uint32_t> s_shader_name;
	};
}


#endif // !SHADER_H__

