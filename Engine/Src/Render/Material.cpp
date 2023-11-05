#include "pch.h"
#include <iosfwd>
#include "Render/Material.h"
#include "Framework/Common/LogMgr.h"

namespace Ailu
{
#define ALIGN_TO_256(x) (((x) + 255) & ~255)

	static void MarkTextureUsedHelper(uint32_t& mask, const ETextureUsage& usage, const bool& b_use)
	{
		switch (usage)
		{
		case Ailu::ETextureUsage::kAlbedo: mask = b_use ? mask | 0x01 : mask & (~0x01);
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

	Material::Material(Ref<Shader> shader, std::string name) : _p_shader(shader)
	{
		_name = name;
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
		//_texture_paths.reserve(10);	//提前分配内存，否则vector扩容时，prop记录的string地址就不对了
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
						//_texture_paths.emplace_back("none");
						auto tex_info = std::make_tuple(prop_info._bind_slot, nullptr, "none");
						auto& [slot, tex_ptr, tex_path] = tex_info;
						SerializableProperty prop{nullptr,prop_name,ESerializablePropertyType::kTexture2D};
						_textures.insert(std::make_pair(prop_name, tex_info));
						properties.insert(std::make_pair(prop_name, prop));
					}
					else if(type_name == ShaderPropertyType::Float)
					{
						SerializableProperty prop{ _p_data + ShaderBindResourceInfo::GetVariableOffset(prop_info),prop_name,ESerializablePropertyType::kFloat };
						properties.insert(std::make_pair(prop_name, prop));
					}
					else if (type_name == ShaderPropertyType::Color)
					{
						SerializableProperty prop{ _p_data + ShaderBindResourceInfo::GetVariableOffset(prop_info),prop_name,ESerializablePropertyType::kColor };
						properties.insert(std::make_pair(prop_name, prop));
					}
				}
			}
			else
			{
				LOG_ERROR("Material: {} with shader {},parser property {} failed!", _p_shader->GetName(), _name, prop_info._name);
			}		
		}
		for (auto& prop : properties)
		{

		}
	}

	Material::~Material()
	{
		DESTORY_PTRARR(_p_data)
	}

	void Material::MarkTextureUsed(std::initializer_list<ETextureUsage> use_infos, bool b_use)
	{
		//40 根据shader中MaterialBuf计算，可能会有变动
		uint32_t* sampler_mask = reinterpret_cast<uint32_t*>(_p_data + 56);
		//*sampler_mask = 0;
		for (auto& usage : use_infos)
		{
			MarkTextureUsedHelper(*sampler_mask, usage, b_use);
		}
	}

	bool Material::IsTextureUsed(ETextureUsage use_info)
	{
		uint32_t sampler_mask = *reinterpret_cast<uint32_t*>(_p_data + 56);
		switch (use_info)
		{
		case Ailu::ETextureUsage::kAlbedo: return sampler_mask & 1;
		case Ailu::ETextureUsage::kNormal: return sampler_mask & 2;
		case Ailu::ETextureUsage::kEmssive: return sampler_mask & 4;
		case Ailu::ETextureUsage::kRoughness: return sampler_mask & 8;
		case Ailu::ETextureUsage::kSpecular: return sampler_mask & 16;
		case Ailu::ETextureUsage::kMetallic: return sampler_mask & 32;
		}
		return false;
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

	void Material::RemoveTexture(const std::string& name)
	{
		auto it = _textures.find(name);
		if (it == _textures.end())
		{
			return;
		}
		std::get<1>(_textures[name]) = nullptr;
		std::get<2>(_textures[name]) = "null";
		properties.find(name)->second._value_ptr = nullptr;
	}

	void Material::SetTexture(const std::string& name, const String& texture_path)
	{
		if (name == InternalStandardMaterialTexture::kAlbedo) MarkTextureUsed({ ETextureUsage::kAlbedo }, true);
		else if (name == InternalStandardMaterialTexture::kEmssive) MarkTextureUsed({ ETextureUsage::kEmssive }, true);
		else if (name == InternalStandardMaterialTexture::kMetallic) MarkTextureUsed({ ETextureUsage::kMetallic }, true);
		else if (name == InternalStandardMaterialTexture::kRoughness) MarkTextureUsed({ ETextureUsage::kRoughness }, true);
		else if (name == InternalStandardMaterialTexture::kSpecular) MarkTextureUsed({ ETextureUsage::kSpecular }, true);
		else if (name == InternalStandardMaterialTexture::kNormal) MarkTextureUsed({ ETextureUsage::kNormal }, true);
		else {};
		auto texture = TexturePool::Get(texture_path);
		if (texture == nullptr) 
		{
			g_pLogMgr->LogErrorFormat("Cann't find texture: {} when set material {} texture{}!",texture_path,_name,name);
			return;
		}
		auto it = _textures.find(name);
		if (it == _textures.end())
		{
			return;
		}
		std::get<1>(_textures[name]) = texture;
		std::get<2>(_textures[name]) = texture_path;
		properties.find(name)->second._value_ptr = &std::get<2>(_textures[name]);
	}

	void Material::Bind()
	{
		for (auto it = _textures.begin(); it != _textures.end(); it++)
		{
			auto& [slot, texture,tex_path] = it->second;
			if(texture != nullptr)
				texture->Bind(slot);
			//else
			//{
			//	LOG_WARNING("Material: {} haven't set texture on bind slot {}",_name,(short)slot);
			//}
		}
		memcpy(_p_cbuf,_p_data,_mat_cbuf_size);
		_p_shader->Bind(_cbuf_index);
	}
	Shader* Material::GetShader() const
	{
		return _p_shader.get();
	}
}
