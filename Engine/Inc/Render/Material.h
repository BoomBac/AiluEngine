#pragma once
#ifndef __MATERIAL_H__
#define __MATERIAL_H__
#include <map>
#include "Shader.h"
#include "GlobalMarco.h"
#include "Texture.h"

namespace Ailu
{
	class Material
	{
	public:
		Material(Ref<Shader> shader,std::string name);
		void SetVector(const std::string& name, const Vector4f& vector);
		void SetTexture(const std::string& name, Ref<Texture> texture);
		void Bind();
		Shader* GetShader() const;
	private:
		std::string _name;
		uint8_t _base_texture_slot_offset = 0u;
		std::map<std::string, std::tuple<uint8_t, Ref<Texture>>> _textures{};
		Ref<Shader> _p_shader;
		uint8_t* _p_cbuf = nullptr;
		uint32_t _cbuf_index;
		inline static uint32_t s_current_cbuf_offset = 0u;
	};
}

#endif // !MATERIAL_H__
