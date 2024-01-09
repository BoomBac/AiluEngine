#include "pch.h"
#include <iosfwd>
#include "Render/Material.h"
#include "Framework/Common/LogMgr.h"
#include "Render/GraphicsPipelineStateObject.h"

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

	static bool ParseShaderProperty(const String& input, std::vector<String>& result)
	{
		if (input.empty())
		{
			return false;
		}
		std::istringstream ss(input);
		String token;
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

	Material::Material(Ref<Shader> shader, String name) : _p_shader(shader)
	{
		_name = name;
		_cbuf_index = s_current_cbuf_offset++;
		_b_internal = false;
		_p_cbuf = _p_shader->GetCBufferPtr(_cbuf_index);
		Construct(true);
		shader->AddMaterialRef(this);
		auto it = _p_shader->GetBindResInfo().find("SamplerMask");
		if (it != _p_shader->GetBindResInfo().end())
		{
			_sampler_mask_offset = ShaderBindResourceInfo::GetVariableOffset(it->second);
		}
	}

	Material::~Material()
	{
		DESTORY_PTRARR(_p_cbuf_cpu)
	}

	void Material::ChangeShader(Ref<Shader> shader)
	{
		_p_shader->RemoveMaterialRef(this);
		_p_shader = shader;
		_p_shader->AddMaterialRef(this);
		Construct(false);
	}

	void Material::MarkTextureUsed(std::initializer_list<ETextureUsage> use_infos, bool b_use)
	{
		//40 根据shader中MaterialBuf计算，可能会有变动
		uint32_t* sampler_mask = reinterpret_cast<uint32_t*>(_p_cbuf_cpu + _sampler_mask_offset);
		//*sampler_mask = 0;
		for (auto& usage : use_infos)
		{
			MarkTextureUsedHelper(*sampler_mask, usage, b_use);
		}
	}

	bool Material::IsTextureUsed(ETextureUsage use_info)
	{
		uint32_t sampler_mask = *reinterpret_cast<uint32_t*>(_p_cbuf_cpu + _sampler_mask_offset);
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

	void Material::SetFloat(const String& name, const float& f)
	{
		auto& res_info = _p_shader->GetBindResInfo();
		auto it = res_info.find(name);
		if (it != res_info.end())
		{
			memcpy(_p_cbuf_cpu + ShaderBindResourceInfo::GetVariableOffset(it->second), &f, sizeof(f));
			LOG_WARNING("float value{}", *reinterpret_cast<float*>(_p_cbuf_cpu + ShaderBindResourceInfo::GetVariableOffset(it->second)));
		}
		else
		{
			LOG_WARNING("Material: {} set float with name {} failed!", _name, name);
		}
	}

	void Material::SetVector(const String& name, const Vector4f& vector)
	{
		auto& res_info = _p_shader->GetBindResInfo();
		auto it = res_info.find(name);
		if (it != res_info.end())
		{
			memcpy(_p_cbuf_cpu + ShaderBindResourceInfo::GetVariableOffset(it->second), &vector, sizeof(vector));
		}
		else
		{
			LOG_WARNING("Material: {} set vector with name {} failed!", _name, name);
		}
	}

	float Material::GetFloat(const String& name)
	{
		auto& res_info = _p_shader->GetBindResInfo();
		auto it = res_info.find(name);
		if (it != res_info.end())
			return *reinterpret_cast<float*>(_p_cbuf_cpu + ShaderBindResourceInfo::GetVariableOffset(it->second));
		return 0.0f;
	}

	Vector4f Material::GetVector(const String& name)
	{
		auto& res_info = _p_shader->GetBindResInfo();
		auto it = res_info.find(name);
		if (it != res_info.end())
			return *reinterpret_cast<Vector4f*>(_p_cbuf_cpu + ShaderBindResourceInfo::GetVariableOffset(it->second));
		return 0.0f;
		return Vector4f::Zero;
	}

	void Material::SetTexture(const String& name, Ref<Texture> texture)
	{
		auto it = _textures.find(name);
		if (it == _textures.end())
		{
			return;
		}
		std::get<1>(_textures[name]) = texture;
		if (_properties.find(name) != _properties.end())
		{
			_properties[name]._value_ptr = reinterpret_cast<void*>(&_textures[name]);
		}
		//else
		//	LOG_WARNING("Cann't find texture prop with value name: {} when set material {} texture!", name, _name);
	}

	void Material::SetTexture(const String& name, const String& texture_path)
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
			g_pLogMgr->LogErrorFormat("Cann't find texture: {} when set material {} texture{}!", texture_path, _name, name);
			return;
		}
		auto it = _textures.find(name);
		if (it == _textures.end())
		{
			return;
		}
		std::get<1>(_textures[name]) = texture;
		if (_properties.find(name) != _properties.end())
		{
			_properties[name]._value_ptr = reinterpret_cast<void*>(&_textures[name]);
		}
		else
			LOG_WARNING("Cann't find texture prop with value name: {} when set material {} texture!", name, _name);
	}

	void Material::EnableKeyword(const String& keyword)
	{
	}

	void Material::DisableKeyword(const String& keyword)
	{
	}

	void Material::RemoveTexture(const String& name)
	{
		auto it = _textures.find(name);
		if (it == _textures.end())
		{
			return;
		}
		std::get<1>(_textures[name]) = nullptr;
	}

	void Material::Bind()
	{
		memcpy(_p_cbuf, _p_cbuf_cpu, _mat_cbuf_size);
		_p_shader->Bind(_cbuf_index);
		for (auto it = _textures.begin(); it != _textures.end(); it++)
		{
			auto& [slot, texture] = it->second;
			if (texture != nullptr)
			{
				//texture->Bind(slot);
				GraphicsPipelineStateMgr::SubmitBindResource(texture.get(), slot);
			}
			//else
			//{
			//	LOG_WARNING("Material: {} haven't set texture on bind slot {}",_name,(short)slot);
			//}
		}
	}
	Shader* Material::GetShader() const
	{
		return _p_shader.get();
	}

	List<std::tuple<String, float>> Material::GetAllFloatValue()
	{
		List<std::tuple<String, float>> ret{};
		for (auto& [name, bind_info] : _p_shader->GetBindResInfo())
		{
			if (!ShaderBindResourceInfo::s_reversed_res_name.contains(name) && bind_info._res_type == EBindResDescType::kCBufferAttribute
				&& ShaderBindResourceInfo::GetVariableSize(bind_info) == 4)
			{
				ret.emplace_back(std::make_tuple(name, *reinterpret_cast<float*>(_p_cbuf_cpu + ShaderBindResourceInfo::GetVariableOffset(bind_info))));
			}
		}
		return ret;
	}

	List<std::tuple<String, Vector4f>> Material::GetAllVectorValue()
	{
		List<std::tuple<String, Vector4f>> ret{};
		for (auto& [name, bind_info] : _p_shader->GetBindResInfo())
		{
			if (!ShaderBindResourceInfo::s_reversed_res_name.contains(name) && bind_info._res_type == EBindResDescType::kCBufferAttribute
				&& ShaderBindResourceInfo::GetVariableSize(bind_info) == 16)
			{
				ret.emplace_back(std::make_tuple(name, *reinterpret_cast<Vector4f*>(_p_cbuf_cpu + ShaderBindResourceInfo::GetVariableOffset(bind_info))));
			}
		}
		return ret;
	}

	void Material::Construct(bool first_time)
	{
		static u8 s_unused_shader_prop_buf[256]{ 0 };
		u16 unused_shader_prop_buf_offset = 0u;
		u16 cur_shader_cbuf_size = 0;
		if (!first_time)
		{
			_properties.clear();
		}
		std::map<String, std::tuple<uint8_t, Ref<Texture>>> _tmp_textures{};
		for (auto& bind_info : _p_shader->GetBindResInfo())
		{
			if (bind_info.second._res_type == EBindResDescType::kTexture2D)
			{
				auto tex_info = std::make_tuple(bind_info.second._bind_slot, nullptr);
				auto& [slot, tex_ptr] = tex_info;
				first_time ? _textures.insert(std::make_pair(bind_info.second._name, tex_info)) : _tmp_textures.insert(std::make_pair(bind_info.second._name, tex_info));
			}
			else if (bind_info.second._res_type == EBindResDescType::kCBufferAttribute && !ShaderBindResourceInfo::s_reversed_res_name.contains(bind_info.first))
			{
				auto byte_size = ShaderBindResourceInfo::GetVariableSize(bind_info.second);
				cur_shader_cbuf_size += byte_size;
			}
		}
		if (!first_time)
		{
			for (auto it = _tmp_textures.begin(); it != _tmp_textures.end(); it++)
			{
				if (_textures.contains(it->first))
				{
					std::get<1>(it->second) = std::get<1>(_textures[it->first]);
				}
			}
			_textures = std::move(_tmp_textures);
		}
		cur_shader_cbuf_size = ALIGN_TO_256(cur_shader_cbuf_size);
		if (first_time)
		{
			_mat_cbuf_size = cur_shader_cbuf_size;
			_p_cbuf_cpu = new uint8_t[_mat_cbuf_size];
			memset(_p_cbuf_cpu, 0, _mat_cbuf_size);
		}
		else if (_mat_cbuf_size != cur_shader_cbuf_size)
		{
			u8* new_cbuf_data = new uint8_t[cur_shader_cbuf_size];
			memcpy(new_cbuf_data, _p_cbuf_cpu, _mat_cbuf_size);
			DESTORY_PTRARR(_p_cbuf_cpu);
			_p_cbuf_cpu = new_cbuf_data;
		}
		auto& bind_info = _p_shader->GetBindResInfo();
		for (auto& prop_info : _p_shader->GetShaderPropertyInfos())
		{
			if (prop_info._prop_type == ESerializablePropertyType::kTexture2D)
			{
				SerializableProperty prop{ nullptr,prop_info._prop_name,prop_info._value_name,ESerializablePropertyType::kTexture2D };
				_properties.insert(std::make_pair(prop_info._value_name, prop));
				if (!first_time)
				{
					auto it = _textures.find(prop_info._value_name);
					if (it != _textures.end())
					{
						_properties[prop_info._value_name]._value_ptr = &(it->second);
					}
				}
			}
			else
			{
				auto it = bind_info.find(prop_info._value_name);
				SerializableProperty prop{};
				if (it != bind_info.end())
				{
					SerializableProperty p{ _p_cbuf_cpu + ShaderBindResourceInfo::GetVariableOffset(bind_info.find(prop_info._value_name)->second),
prop_info._prop_name,prop_info._value_name,prop_info._prop_type };
					prop = std::move(p);
				}
				else
				{
					SerializableProperty p{ s_unused_shader_prop_buf + unused_shader_prop_buf_offset,prop_info._prop_name,prop_info._value_name,prop_info._prop_type };
					prop = std::move(p);
					unused_shader_prop_buf_offset += SerializableProperty::GetValueSize(prop_info._prop_type);
					g_pLogMgr->LogWarningFormat("Unused shader prop {} been added;", prop_info._prop_name);
				}
				memcpy(prop._param, prop_info._prop_param, sizeof(Vector4f));
				if (prop._type == ESerializablePropertyType::kFloat || prop._type == ESerializablePropertyType::kRange)
				{
					memcpy(prop._value_ptr, &prop._param[2], sizeof(float));
				}
				else if (prop._type == ESerializablePropertyType::kColor || prop._type == ESerializablePropertyType::kVector4f)
				{
					memcpy(prop._value_ptr, prop._param, sizeof(Vector4f));
				}
				_properties.insert(std::make_pair(prop_info._value_name, prop));
			}
		}
	}
	//-------------------------------------------StandardMaterial--------------------------------------------------------
	StandardMaterial::StandardMaterial(Ref<Shader> shader, String name) : Material(shader, name)
	{

	}
	StandardMaterial::~StandardMaterial()
	{
	}
	//-------------------------------------------StandardMaterial--------------------------------------------------------
}
