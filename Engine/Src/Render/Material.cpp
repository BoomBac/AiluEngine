#include "pch.h"
#include <iosfwd>
#include "Render/Material.h"

namespace Ailu
{
#define ALIGN_TO_256(x) (((x) + 255) & ~255)

	static bool ParseShaderProperty(const std::string& input, std::vector<std::string>& result)
	{
		if (input.empty())
		{
			return false;
		}
		std::istringstream ss(input);
		std::string token;
		while (std::getline(ss, token, '_'))
		{
			if (!token.empty())
			{
				result.push_back(token);
			}
		}
		if (result.empty())
		{
			return false;
		}
		return true;
	}

	static void MarkTextureUsedHelper(uint32_t& mask,const ETextureUsage& usage,const bool& b_use)
	{
		switch (usage)
		{
		case Ailu::ETextureUsage::kAlbedo: mask =b_use? mask | 0x01 : mask & (~0x01);
			break;
		case Ailu::ETextureUsage::kNormal: mask = b_use ? mask | 0x02 : mask & (~0x02);
			break;
		case Ailu::ETextureUsage::kEmssive: mask = b_use ? mask | 0x04 : mask & (~0x04);
			break;
		case Ailu::ETextureUsage::kRoughness: mask = b_use ? mask | 0x08 : mask & (~0x08);
			break;
		case Ailu::ETextureUsage::kSpecular: mask = b_use ? mask | 0x10 : mask & (~0x10);
			break;
		case Ailu::ETextureUsage::kMetallic: mask = b_use ? mask | 0x20 : mask & (~0x20);
			break;
		}
	}

	Material::Material(Ref<Shader> shader, std::string name) : Asset("run_time","run_time"), _p_shader(shader), _name(name)
	{
		_cbuf_index = s_current_cbuf_offset++;
		_p_cbuf = _p_shader->GetCBufferPtr(_cbuf_index);
		for (auto& bind_info : _p_shader->GetBindResInfo())
		{
			if (bind_info.second._res_type == EBindResDescType::kTexture2D)
			{
				//_textures.insert(std::make_pair(bind_info.first, std::make_tuple(bind_info.second._bind_slot, nullptr, "none")));
				_mat_props.insert(bind_info.second);
			}
			else if (bind_info.second._res_type == EBindResDescType::kCBufferAttribute && !ShaderBindResourceInfo::s_reversed_res_name.contains(bind_info.first))
			{
				auto byte_size = ShaderBindResourceInfo::GetVariableSize(bind_info.second);
				LOG_INFO("{} with byte size {}", bind_info.second._name, (int)byte_size);
				_mat_cbuf_size += byte_size;
				_mat_props.insert(bind_info.second);
			}
		}
		_mat_cbuf_size = ALIGN_TO_256(_mat_cbuf_size);
		_p_data = new uint8_t[_mat_cbuf_size];
		memset(_p_data, 0, _mat_cbuf_size);
		for (auto& prop_info : _mat_props)
		{
			std::vector<std::string> shader_prop{};
			if (ParseShaderProperty(prop_info._name, shader_prop))
			{
				bool b_hide = shader_prop[0] == "H";
				if (!b_hide)
				{
					std::string	type_name = shader_prop[0], prop_name = shader_prop[1];
					//LOG_INFO("{} with byte offset {}", prop_info._name, (int)ShaderBindResourceInfo::GetVariableOffset(prop_info));
					if (type_name == ShaderPropertyType::Texture2D)
					{
						_texture_paths.emplace_back("none");
						auto tex_info = std::make_tuple(prop_info._bind_slot, nullptr, &_texture_paths.back());
						auto& [slot, tex_ptr, tex_path] = tex_info;
						SerializableProperty prop{reinterpret_cast<void*>(tex_path),prop_name,type_name};
						_textures.insert(std::make_pair(prop_name, tex_info));
						properties.insert(std::make_pair(prop_name, prop));
					}
					else
					{
						SerializableProperty prop{ _p_data + ShaderBindResourceInfo::GetVariableOffset(prop_info),prop_name,type_name };
						properties.insert(std::make_pair(prop_name, prop));
					}
				}
			}
			else
			{
				LOG_ERROR("Material: {} with shader {},parser property {} failed!", _p_shader->GetName(), _name, prop_info._name);
			}
		}
	}

	Material::~Material()
	{
		DESTORY_PTRARR(_p_data)
	}

	void Material::MarkTextureUsed(std::initializer_list<ETextureUsage> use_infos, bool b_use)
	{
		//40 根据shader中MaterialBuf计算，可能会有变动
		uint32_t* sampler_mask = reinterpret_cast<uint32_t*>(_p_data + 40);
		*sampler_mask = 0;
		for (auto& usage : use_infos)
		{
			MarkTextureUsedHelper(*sampler_mask, usage,b_use);
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
		//for (auto it = _textures.begin(); it != _textures.end(); it++)
		//{
		//	auto [slot, texture] = it->second;
		//	if(texture != nullptr)
		//		texture->Bind(slot);
		//	else
		//	{
		//		LOG_WARNING("Material: {} haven't set texture on bind slot {}",_name,(short)slot);
		//	}
		//}
		memcpy(_p_cbuf,_p_data,_mat_cbuf_size);
		_p_shader->Bind(_cbuf_index);
	}
	Shader* Material::GetShader() const
	{
		return _p_shader.get();
	}
}
