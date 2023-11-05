#pragma once
#ifndef __SHADER_H__
#define __SHADER_H__
#include <string>
#include <set>
#include <unordered_map>
#include "Framework/Math/ALMath.hpp"
#include "Framework/Common/Path.h"

namespace Ailu
{
	enum class EShaderType : uint8_t
	{
		kVertex,kPixel
	};

	enum class EBindResDescType : uint8_t
	{
		kConstBuffer = 0, kTexture2D, kSampler, kCBufferAttribute
	};

	struct ShaderPropertyType
	{
		inline static std::string Vector = "Vector";
		inline static std::string Float = "Float";
		inline static std::string Color = "Color";
		inline static std::string Texture2D = "Texture2D";
	};

	struct ShaderBindResourceInfo
	{
		inline const static std::set<std::string> s_reversed_res_name
		{
			"SceneObjectBuffer",
			"_MatrixWorld",
			"SceneMaterialBuffer",
			"SceneStatetBuffer",
			"_MatrixV",
			"_MatrixP",
			"_MatrixVP",
			"_CameraPos",
			"_DirectionalLights",
			"_PointLights",
			"_SpotLights",
			"padding",
			"padding0"
		};
		inline static uint8_t GetVariableSize(const ShaderBindResourceInfo& info){ return info._cbuf_member_offset & 0XFF;}
		inline static uint8_t GetVariableOffset(const ShaderBindResourceInfo& info){ return info._cbuf_member_offset >> 8;}
		EBindResDescType _res_type;
		union
		{
			uint16_t _res_slot;
			uint16_t _cbuf_member_offset;
		};
		uint8_t _bind_slot;
		std::string _name;
		ShaderBindResourceInfo() = default;
		ShaderBindResourceInfo(EBindResDescType res_type, uint16_t slot_or_offset, uint8_t bind_slot, const std::string& name)
			: _res_type(res_type), _bind_slot(bind_slot), _name(name) 
		{
			if (res_type == EBindResDescType::kCBufferAttribute) _cbuf_member_offset = slot_or_offset;	
			else _res_slot = slot_or_offset;
		}
		bool operator==(const ShaderBindResourceInfo& other) const
		{
			return _name == other._name;
		}
	};

	// Hash function for ShaderBindResourceInfo
	struct ShaderBindResourceInfoHash
	{
		std::size_t operator()(const ShaderBindResourceInfo& info) const
		{
			return std::hash<std::string>{}(info._name);
		}
	};

	// Equality function for ShaderBindResourceInfo
	struct ShaderBindResourceInfoEqual
	{
		bool operator()(const ShaderBindResourceInfo& lhs, const ShaderBindResourceInfo& rhs) const
		{
			return lhs._name == rhs._name;
		}
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
		static Ref<Shader> Create(const std::string_view file_name);
	protected:
		virtual void Bind(uint32_t index) = 0;
		virtual uint8_t* GetCBufferPtr(uint32_t index) = 0;
		virtual uint16_t GetVariableOffset(const std::string& name) const = 0;
		virtual const std::unordered_map<std::string, ShaderBindResourceInfo>& GetBindResInfo() const = 0;
	};

	class ShaderLibrary
	{
#define VAILD_SHADER_ID(id) id != ShaderLibrary::s_error_id
		friend class Shader;
	public:
		static Ref<Shader> Get(std::string shader_name)
		{
			uint32_t shader_id = NameToId(shader_name);
			if (VAILD_SHADER_ID(shader_id)) return s_shader_library.find(shader_id)->second;
			else
			{
				LOG_ERROR("Shader: {} dont's exist in ShaderLibrary!", shader_name);
				return nullptr;
			}
		}
		static bool Exist(const std::string& name)
		{
			return NameToId(name) != s_error_id;
		}
		static std::string GetShaderPath(const std::string& name)
		{
			auto it = s_shader_path.find(name);
			return it == s_shader_path.end() ? "" : it->second;
		}
		static Ref<Shader> Add(const std::string& res_path)
		{
			auto abs_path = GetResPath(res_path);
			auto name = GetFileName(abs_path);
			if (Exist(name)) return s_shader_library.find(NameToId(name))->second;
			else
			{
				s_shader_path.insert(std::make_pair(name, res_path));
				return Shader::Create(abs_path);
			}
		}
		inline static uint32_t NameToId(const std::string& name)
		{
			auto it = s_shader_name.find(name);
			if (it != s_shader_name.end()) return it->second;
			else return s_error_id;
		}
	private:
		inline static uint32_t s_shader_id = 0u;
		inline static uint32_t s_error_id = 9999u;
		inline static std::unordered_map<uint32_t, Ref<Shader>> s_shader_library;
		inline static std::unordered_map<std::string, uint32_t> s_shader_name;
		inline static std::unordered_map<std::string, std::string> s_shader_path;
	};
}


#endif // !SHADER_H__

