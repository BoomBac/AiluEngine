#pragma once
#ifndef __MATERIAL_H__
#define __MATERIAL_H__
#include <map>
#include <unordered_set>
#include "Shader.h"
#include "GlobalMarco.h"
#include "Texture.h"
#include "Framework/Common/Reflect.h"

namespace Ailu
{
	enum class ETextureUsage : uint8_t
	{
		kAlbedo = 0,kNormal,kEmssive,kRoughness,kSpecular,kMetallic
	};
	class Material
	{
		DECLARE_REFLECT_FIELD(Material)
	public:
		Material(Ref<Shader> shader,std::string name);
		~Material();
		void MarkTextureUsed(std::initializer_list<ETextureUsage> use_infos,bool b_use);
		void SetVector(const std::string& name, const Vector4f& vector);
		void SetTexture(const std::string& name, Ref<Texture> texture);
		void Bind();
		Shader* GetShader() const;
	private:
		uint16_t _mat_cbuf_size = 0u;
		//std::set<ShaderBindResourceInfo, ShaderBindResourceInfoComparer> _mat_props{};
		std::unordered_set<ShaderBindResourceInfo,ShaderBindResourceInfoHash,ShaderBindResourceInfoEqual> _mat_props{};
		uint8_t* _p_data;
		std::string _name;
		std::map<std::string, std::tuple<uint8_t, Ref<Texture>>> _textures{};
		Ref<Shader> _p_shader;
		uint8_t* _p_cbuf = nullptr;
		uint32_t _cbuf_index;
		inline static uint32_t s_current_cbuf_offset = 0u;
	};
}

#endif // !MATERIAL_H__
