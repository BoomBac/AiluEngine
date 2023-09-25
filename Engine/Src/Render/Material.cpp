#include "pch.h"
#include "Render/Material.h"

namespace Ailu
{
	Material::Material(Ref<Shader> shader, std::string name) : _p_shader(shader),_name(name)
	{
		_cbuf_index = s_current_cbuf_offset++;
		_p_cbuf = _p_shader->GetCBufferPtr(_cbuf_index);
		for (auto& bind_info : _p_shader->GetBindResInfo())
		{
			if (bind_info.second._res_type == EBindResDescType::kTexture2D)
			{
				_textures.insert(std::make_pair(bind_info.first, std::make_tuple(bind_info.second._bind_slot, nullptr)));
			}
		}
	}

	void Material::SetVector(const std::string& name, const Vector4f& vector)
	{
		memcpy(_p_cbuf + _p_shader->GetVariableOffset(name), &vector, sizeof(vector));
	}

	void Material::SetTexture(const std::string& name, Ref<Texture> texture)
	{
		auto it = _textures.find(name);
		if (it == _textures.end())
		{
			return;
		}
		std::get<1>(_textures[name])= texture;
	}

	void Material::Bind()
	{
		for (auto it = _textures.begin(); it != _textures.end(); it++)
		{
			auto [slot, texture] = it->second;
			if(texture != nullptr)
				texture->Bind(slot);
			else
			{
				LOG_WARNING("Material: {} haven't set texture on bind slot {}",_name,(short)slot);
			}
		}
		_p_shader->Bind(_cbuf_index);
	}
	Shader* Material::GetShader() const
	{
		return _p_shader.get();
	}
}
