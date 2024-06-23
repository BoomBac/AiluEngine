#include "pch.h"
#include <iosfwd>
#include "Render/Material.h"
#include "Framework/Common/LogMgr.h"
#include "Render/GraphicsPipelineStateObject.h"
#include "Framework/Common/Asset.h"
#include "Framework/Common/ResourceMgr.h"

namespace Ailu
{
	static void MarkTextureUsedHelper(u32& mask, const ETextureUsage& usage, const bool& b_use)
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

	Material::Material(Shader* shader, String name) : _p_shader(shader)
	{
		AL_ASSERT(s_total_material_num > RenderConstants::kMaxMaterialDataCount);
		_name = name;
		_b_internal = false;
		_p_active_shader = _p_shader;
		_surface = ESurfaceType::kOpaque;
		Construct(true);
		shader->AddMaterialRef(this);
		//只有首个pass支持默认着色
		for (int i = 0; i < shader->PassCount(); i++)
		{
			auto& cur_variant_bind_infos = _p_shader->GetBindResInfo(i, _pass_variants[i]._variant_hash);
			auto it = cur_variant_bind_infos.find("SamplerMask");
			if (it != cur_variant_bind_infos.end())
			{
				_b_internal = true;
				_sampler_mask_offset = ShaderBindResourceInfo::GetVariableOffset(it->second);
				_standard_pass_index = i;
			}
			it = cur_variant_bind_infos.find("MaterialID");
			if (it != cur_variant_bind_infos.end())
			{
				_b_internal = true;
				_material_id_offset = ShaderBindResourceInfo::GetVariableOffset(it->second);
			}
			if (_standard_pass_index != -1)
				break;
		}
		_material_id = EMaterialID::kStandard;
		++s_total_material_num;
	}

	Material::Material(const Material& other)
	{
		_b_internal = other._b_internal;
		_material_id = other._material_id;
		_sampler_mask_offset = other._sampler_mask_offset;
		_material_id_offset = other._material_id_offset;
		_surface = other._surface;
		_p_active_shader = other._p_active_shader;
		//标准pass才会使用上面两个变量
		_standard_pass_index = other._standard_pass_index;
		_mat_cbuf_per_pass_size = other._mat_cbuf_per_pass_size;
		_p_shader = other._p_shader;
		for (auto& cbuf : other._p_cbufs)
		{
			u32 buffer_size = cbuf->GetBufferSize();
			_p_cbufs.emplace_back(ConstantBuffer::Create(buffer_size));
			memcpy(_p_cbufs.back()->GetData(), cbuf->GetData(), buffer_size);
		}
		_textures_all_passes = other._textures_all_passes;
	}

	Material::Material(Material&& other) noexcept
	{
		_b_internal = other._b_internal;
		_material_id = other._material_id;
		_sampler_mask_offset = other._sampler_mask_offset;
		_material_id_offset = other._material_id_offset;
		_surface = other._surface;
		//标准pass才会使用上面两个变量
		_standard_pass_index = other._standard_pass_index;
		_mat_cbuf_per_pass_size = other._mat_cbuf_per_pass_size;
		_p_shader = other._p_shader;
		_p_active_shader = other._p_active_shader;
		_p_cbufs = std::move(other._p_cbufs);
		other._p_cbufs.clear();
		_textures_all_passes = std::move(other._textures_all_passes);
		other._textures_all_passes.clear();
	}

	Material& Material::operator=(const Material& other)
	{
		_b_internal = other._b_internal;
		_material_id = other._material_id;
		_sampler_mask_offset = other._sampler_mask_offset;
		_material_id_offset = other._material_id_offset;
		_surface = other._surface;
		//标准pass才会使用上面两个变量
		_standard_pass_index = other._standard_pass_index;
		_mat_cbuf_per_pass_size = other._mat_cbuf_per_pass_size;
		_p_shader = other._p_shader;
		_p_active_shader = other._p_active_shader;
		for (auto& cbuf : other._p_cbufs)
		{
			u32 buffer_size = cbuf->GetBufferSize();
			_p_cbufs.emplace_back(ConstantBuffer::Create(buffer_size));
			memcpy(_p_cbufs.back()->GetData(), cbuf->GetData(), buffer_size);
		}
		_textures_all_passes = other._textures_all_passes;
		return *this;
	}

	Material& Material::operator=(Material&& other) noexcept
	{
		_b_internal = other._b_internal;
		_material_id = other._material_id;
		_sampler_mask_offset = other._sampler_mask_offset;
		_material_id_offset = other._material_id_offset;
		_surface = other._surface;
		//标准pass才会使用上面两个变量
		_standard_pass_index = other._standard_pass_index;
		_mat_cbuf_per_pass_size = other._mat_cbuf_per_pass_size;
		_p_shader = other._p_shader;
		_p_active_shader = other._p_active_shader;
		_p_cbufs = std::move(other._p_cbufs);
		other._p_cbufs.clear();
		_textures_all_passes = std::move(other._textures_all_passes);
		other._textures_all_passes.clear();
		return *this;
	}

	Material::~Material()
	{
		--s_total_material_num;
	}
	void Material::Bind(u16 pass_index)
	{
		auto& cur_pass_variant_hash = _pass_variants[pass_index]._variant_hash;
		auto variant_state = _p_active_shader->GetVariantState(pass_index, cur_pass_variant_hash);
		if (variant_state != EShaderVariantState::kReady)
			return;
		if (_material_id_offset != 0)
		{
			memset(_p_cbufs[_standard_pass_index]->GetData() + _material_id_offset, (u32)_material_id, sizeof(u32));
		}
		_p_active_shader->Bind(pass_index, cur_pass_variant_hash);

		i8 cbuf_bind_slot = _p_active_shader->_passes[pass_index]._variants[cur_pass_variant_hash]._per_mat_buf_bind_slot;
		if (cbuf_bind_slot != -1)
		{
			GraphicsPipelineStateMgr::SubmitBindResource(_p_cbufs[pass_index].get(), EBindResDescType::kConstBuffer, (u8)cbuf_bind_slot);
		}
		auto& bind_textures = _textures_all_passes[pass_index];
		auto& bind_infos = _p_active_shader->_passes[pass_index]._variants[cur_pass_variant_hash]._bind_res_infos;
		for (auto it = bind_textures.begin(); it != bind_textures.end(); it++)
		{
			if (bind_infos.find(it->first) != bind_infos.end())
			{
				auto& [slot, texture] = it->second;
				if (texture != nullptr)
				{
					//texture->Bind(slot);
					GraphicsPipelineStateMgr::SubmitBindResource(texture, slot);
				}
			}
			//else
			//{
			//	LOG_WARNING("Material: {} haven't set texture on bind slot {}",_name,(short)slot);
			//}
		}
	}

	void Material::ChangeShader(Shader* shader)
	{
		_p_shader->RemoveMaterialRef(this);
		_p_shader = shader;
		_p_shader->AddMaterialRef(this);
		Construct(false);
	}
	void Material::SurfaceType(const ESurfaceType::ESurfaceType & value)
	{
		if (_surface == value)
			return;
		if (value == ESurfaceType::kOpaque)
			_p_active_shader = _p_shader;
		else if (value == ESurfaceType::kTransparent)
		{
			_p_active_shader = g_pResourceMgr->Get<Shader>(L"Shaders/forwardlit.alasset");
		}
		_surface = value;
	}

	bool Material::IsReadyForDraw() const
	{
		u16 pass_index = 0;
		for (auto& pass : _pass_variants)
		{
			if (_p_active_shader->GetVariantState(pass_index, pass._variant_hash) != EShaderVariantState::kReady)
				return false;
			++pass_index;
		}
		return true;
	}

	void Material::MarkTextureUsed(std::initializer_list<ETextureUsage> use_infos, bool b_use)
	{
		//40 根据shader中MaterialBuf计算，可能会有变动
		u32* sampler_mask = reinterpret_cast<u32*>(_p_cbufs[_standard_pass_index]->GetData() + _sampler_mask_offset);
		//*sampler_mask = 0;
		for (auto& usage : use_infos)
		{
			MarkTextureUsedHelper(*sampler_mask, usage, b_use);
		}
	}

	bool Material::IsTextureUsed(ETextureUsage use_info)
	{
		u32 sampler_mask = *reinterpret_cast<u32*>(_p_cbufs[_standard_pass_index]->GetData() + _sampler_mask_offset);
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
		u16 pass_index = 0;
		for (auto& pass : _p_shader->_passes)
		{
			auto& res_info = pass._variants[_pass_variants[pass_index]._variant_hash]._bind_res_infos;
			auto it = res_info.find(name);
			if (it != res_info.end())
			{
				memcpy(_p_cbufs[pass._index]->GetData() + ShaderBindResourceInfo::GetVariableOffset(it->second), &f, sizeof(f));
				//LOG_WARNING("float value{}", *reinterpret_cast<float*>(_p_cbuf->GetData() + ShaderBindResourceInfo::GetVariableOffset(it->second)));
			}
			else
			{
				LOG_WARNING("Material: {} set float with name {} failed!", _name, name);
			}
			++pass_index;
		}
	}

	void Material::SetUint(const String& name, const u32& value)
	{
		u16 pass_index = 0;
		for (auto& pass : _p_shader->_passes)
		{
			auto& res_info = pass._variants[_pass_variants[pass_index]._variant_hash]._bind_res_infos;
			auto it = res_info.find(name);
			if (it != res_info.end())
			{
				memcpy(_p_cbufs[pass._index]->GetData() + ShaderBindResourceInfo::GetVariableOffset(it->second), &value, sizeof(value));
				//LOG_WARNING("uint value{}", *reinterpret_cast<u32*>(_p_cbuf->GetData() + ShaderBindResourceInfo::GetVariableOffset(it->second)));
			}
			else
			{
				LOG_WARNING("Material: {} set uint with name {} failed!", _name, name);
			}
			++pass_index;
		}
	}

	void Material::SetVector(const String& name, const Vector4f& vector)
	{
		u16 pass_index = 0;
		for (auto& pass : _p_shader->_passes)
		{
			auto& res_info = pass._variants[_pass_variants[pass_index]._variant_hash]._bind_res_infos;
			auto it = res_info.find(name);
			if (it != res_info.end())
			{
				memcpy(_p_cbufs[pass._index]->GetData() + ShaderBindResourceInfo::GetVariableOffset(it->second), &vector, sizeof(vector));
			}
			else
			{
				LOG_WARNING("Material: {} set vector with name {} failed!", _name, name);
			}
			++pass_index;
		}
	}

	float Material::GetFloat(const String& name)
	{
		u16 pass_index = 0;
		for (auto& pass : _p_shader->_passes)
		{
			auto& res_info = pass._variants[_pass_variants[pass_index]._variant_hash]._bind_res_infos;
			auto it = res_info.find(name);
			if (it != res_info.end())
				return *reinterpret_cast<float*>(_p_cbufs[pass._index]->GetData() + ShaderBindResourceInfo::GetVariableOffset(it->second));
			++pass_index;
		}
		return 0.0f;
	}

	ShaderVariantHash Material::ActiveVariantHash(u16 pass_index) const
	{
		AL_ASSERT(pass_index >= _pass_variants.size());
		return _pass_variants[pass_index]._variant_hash;
	};

	u32 Material::GetUint(const String& name)
	{
		u16 pass_index = 0;
		for (auto& pass : _p_shader->_passes)
		{
			auto& res_info = pass._variants[_pass_variants[pass_index]._variant_hash]._bind_res_infos;
			auto it = res_info.find(name);
			if (it != res_info.end())
				return *reinterpret_cast<u32*>(_p_cbufs[pass._index]->GetData() + ShaderBindResourceInfo::GetVariableOffset(it->second));
			++pass_index;
		}
		return -1;
	}

	Vector4f Material::GetVector(const String& name)
	{
		u16 pass_index = 0;
		for (auto& pass : _p_shader->_passes)
		{
			auto& res_info = pass._variants[_pass_variants[pass_index]._variant_hash]._bind_res_infos;
			auto it = res_info.find(name);
			if (it != res_info.end())
				return *reinterpret_cast<Vector4f*>(_p_cbufs[pass._index]->GetData() + ShaderBindResourceInfo::GetVariableOffset(it->second));
			++pass_index;
		}
		return Vector4f::kZero;
	}

	void Material::SetTexture(const String& name, Texture* texture)
	{
		if (name == InternalStandardMaterialTexture::kAlbedo) MarkTextureUsed({ ETextureUsage::kAlbedo }, true);
		else if (name == InternalStandardMaterialTexture::kEmssive) MarkTextureUsed({ ETextureUsage::kEmssive }, true);
		else if (name == InternalStandardMaterialTexture::kMetallic) MarkTextureUsed({ ETextureUsage::kMetallic }, true);
		else if (name == InternalStandardMaterialTexture::kRoughness) MarkTextureUsed({ ETextureUsage::kRoughness }, true);
		else if (name == InternalStandardMaterialTexture::kSpecular) MarkTextureUsed({ ETextureUsage::kSpecular }, true);
		else if (name == InternalStandardMaterialTexture::kNormal) MarkTextureUsed({ ETextureUsage::kNormal }, true);
		else {};
		for (auto& pass_tex : _textures_all_passes)
		{
			auto it = pass_tex.find(name);
			if (it == pass_tex.end())
			{
				return;
			}
			std::get<1>(pass_tex[name]) = texture;
			if (_properties.find(name) != _properties.end())
			{
				_properties[name]._value_ptr = reinterpret_cast<void*>(&pass_tex[name]);
			}
		}

		//else
		//	LOG_WARNING("Cann't find texture prop with value name: {} when set material {} texture!", name, _name);
	}

	void Material::SetTexture(const String& name, const WString& texture_path)
	{
		if (name == InternalStandardMaterialTexture::kAlbedo) MarkTextureUsed({ ETextureUsage::kAlbedo }, true);
		else if (name == InternalStandardMaterialTexture::kEmssive) MarkTextureUsed({ ETextureUsage::kEmssive }, true);
		else if (name == InternalStandardMaterialTexture::kMetallic) MarkTextureUsed({ ETextureUsage::kMetallic }, true);
		else if (name == InternalStandardMaterialTexture::kRoughness) MarkTextureUsed({ ETextureUsage::kRoughness }, true);
		else if (name == InternalStandardMaterialTexture::kSpecular) MarkTextureUsed({ ETextureUsage::kSpecular }, true);
		else if (name == InternalStandardMaterialTexture::kNormal) MarkTextureUsed({ ETextureUsage::kNormal }, true);
		else {};
		auto texture = g_pResourceMgr->Get<Texture2D>(texture_path);
		if (texture == nullptr)
		{
			g_pLogMgr->LogErrorFormat("Cann't find texture: {} when set material {} texture{}!", ToChar(texture_path), _name, name);
			return;
		}
		for (auto& pass_tex : _textures_all_passes)
		{
			auto it = pass_tex.find(name);
			if (it == pass_tex.end())
			{
				continue;
			}
			std::get<1>(pass_tex[name]) = texture;
			if (_properties.find(name) != _properties.end())
			{
				_properties[name]._value_ptr = reinterpret_cast<void*>(&pass_tex[name]);
			}
		}
	}

	void Material::SetTexture(const String& name, RTHandle texture)
	{
		if (name == InternalStandardMaterialTexture::kAlbedo) MarkTextureUsed({ ETextureUsage::kAlbedo }, true);
		else if (name == InternalStandardMaterialTexture::kEmssive) MarkTextureUsed({ ETextureUsage::kEmssive }, true);
		else if (name == InternalStandardMaterialTexture::kMetallic) MarkTextureUsed({ ETextureUsage::kMetallic }, true);
		else if (name == InternalStandardMaterialTexture::kRoughness) MarkTextureUsed({ ETextureUsage::kRoughness }, true);
		else if (name == InternalStandardMaterialTexture::kSpecular) MarkTextureUsed({ ETextureUsage::kSpecular }, true);
		else if (name == InternalStandardMaterialTexture::kNormal) MarkTextureUsed({ ETextureUsage::kNormal }, true);
		else {};
		for (auto& pass_tex : _textures_all_passes)
		{
			auto it = pass_tex.find(name);
			if (it == pass_tex.end())
			{
				continue;
			}
			std::get<1>(pass_tex[name]) = g_pRenderTexturePool->Get(texture);
			if (_properties.find(name) != _properties.end())
			{
				_properties[name]._value_ptr = reinterpret_cast<void*>(&pass_tex[name]);
			}
		}
	}

	void Material::EnableKeyword(const String& keyword)
	{
		u16 pass_index = 0;
		for (auto& p : _pass_variants)
		{
			if (_p_active_shader->IsKeywordValid(pass_index, keyword))
			{
				auto group_kws = _p_active_shader->KeywordsSameGroup(pass_index, keyword);
				for (auto& kw : group_kws)
				{
					_pass_variants[pass_index]._keywords.erase(kw);
				}
				_pass_variants[pass_index]._keywords.insert(keyword);
				_pass_variants[pass_index]._variant_hash = _p_active_shader->ConstructVariantHash(pass_index, _pass_variants[pass_index]._keywords);
				_all_keywords.insert(keyword);
				UpdateBindTexture(pass_index,_pass_variants[pass_index]._variant_hash);
			}
			++pass_index;
		}
	}

	void Material::DisableKeyword(const String& keyword)
	{
		u16 pass_index = 0;
		for (auto& p : _pass_variants)
		{
			if (_p_active_shader->IsKeywordValid(pass_index, keyword))
			{
				if (_pass_variants[pass_index]._keywords.contains(keyword))
				{
					_pass_variants[pass_index]._keywords.erase(keyword);
					_pass_variants[pass_index]._variant_hash = _p_active_shader->ConstructVariantHash(pass_index, _pass_variants[pass_index]._keywords);
					_all_keywords.erase(keyword);
					UpdateBindTexture(pass_index, _pass_variants[pass_index]._variant_hash);
				}
			}
			++pass_index;
		}
	}

	void Material::RemoveTexture(const String& name)
	{
		for (auto& pass_tex : _textures_all_passes)
		{
			auto it = pass_tex.find(name);
			if (it == pass_tex.end())
			{
				return;
			}
			std::get<1>(pass_tex[name]) = nullptr;
		}
	}

	List<std::tuple<String, float>> Material::GetAllFloatValue()
	{
		List<std::tuple<String, float>> ret{};
		u16 pass_index = 0;
		for (auto& pass : _pass_variants)
		{
			for (auto& [name, bind_info] : _p_shader->GetBindResInfo(pass_index, _pass_variants[pass_index]._variant_hash))
			{
				if (!ShaderBindResourceInfo::s_reversed_res_name.contains(name) && bind_info._res_type & EBindResDescType::kCBufferFloat
					&& ShaderBindResourceInfo::GetVariableSize(bind_info) == 4)
				{
					ret.emplace_back(std::make_tuple(name, *reinterpret_cast<float*>(_p_cbufs[pass_index]->GetData() + ShaderBindResourceInfo::GetVariableOffset(bind_info))));
				}
			}
			++pass_index;
		}

		return ret;
	}

	List<std::tuple<String, Vector4f>> Material::GetAllVectorValue()
	{
		List<std::tuple<String, Vector4f>> ret{};
		u16 pass_index = 0;
		for (auto& pass : _pass_variants)
		{
			for (auto& [name, bind_info] : _p_shader->GetBindResInfo(pass_index, _pass_variants[pass_index]._variant_hash))
			{
				if (!ShaderBindResourceInfo::s_reversed_res_name.contains(name) && bind_info._res_type & EBindResDescType::kCBufferFloat4
					&& ShaderBindResourceInfo::GetVariableSize(bind_info) == 16)
				{
					ret.emplace_back(std::make_tuple(name, *reinterpret_cast<Vector4f*>(_p_cbufs[pass_index]->GetData() + ShaderBindResourceInfo::GetVariableOffset(bind_info))));
				}
			}
			++pass_index;
		}

		return ret;
	}

	List<std::tuple<String, u32>> Material::GetAllUintValue()
	{
		List<std::tuple<String, u32>> ret{};
		u16 pass_index = 0;
		for (auto& pass : _pass_variants)
		{
			for (auto& [name, bind_info] : _p_shader->GetBindResInfo(pass_index, _pass_variants[pass_index]._variant_hash))
			{
				if (!ShaderBindResourceInfo::s_reversed_res_name.contains(name) && bind_info._res_type & EBindResDescType::kCBufferUint)
				{
					ret.emplace_back(std::make_tuple(name, *reinterpret_cast<u32*>(_p_cbufs[pass_index]->GetData() + ShaderBindResourceInfo::GetVariableOffset(bind_info))));
				}
			}
			++pass_index;
		}

		return ret;
	}

	//List<std::tuple<String, Texture*>> Material::GetAllTexture()
	//{
	//	List<std::tuple<String, Texture*>> ret{};
	//	for (auto& [name, bind_info] : _p_shader->GetBindResInfo())
	//	{
	//		if (!ShaderBindResourceInfo::s_reversed_res_name.contains(name) && bind_info._res_type & EBindResDescType::kTexture2D)
	//		{
	//			ret.emplace_back(std::make_tuple(name, bind_info._p_res));
	//		}
	//	}
	//	return ret;
	//}

	void Material::Construct(bool first_time)
	{
		ConstructKeywords(_p_shader);
		static u8 s_unused_shader_prop_buf[256]{ 0 };
		u16 unused_shader_prop_buf_offset = 0u;
		u16 pass_count = _p_shader->PassCount();
		//u16 cur_shader_cbuf_size = 0; //每个passcbuffer大小一致。
		Vector<u16> cbuf_size_per_passes(pass_count);
		if (!first_time)
		{
			_properties.clear();
		}
		else
		{
			_textures_all_passes.resize(pass_count);
			_mat_cbuf_per_pass_size.resize(pass_count);
			_p_cbufs.resize(pass_count);
		}
		Vector<Map<String, std::tuple<u8, Texture*>>> _tmp_textures_all_passes(pass_count);
		for (int i = 0; i < pass_count; i++)
		{
			for (auto& bind_info : _p_shader->GetBindResInfo(i, _pass_variants[i]._variant_hash))
			{
				if (bind_info.second._res_type == EBindResDescType::kTexture2D)
				{
					auto tex_info = std::make_tuple(bind_info.second._bind_slot, nullptr);
					auto& [slot, tex_ptr] = tex_info;
					if (first_time)
					{
						_textures_all_passes[i].insert(std::make_pair(bind_info.second._name, tex_info));
					}
					else
					{
						_tmp_textures_all_passes[i].insert(std::make_pair(bind_info.second._name, tex_info));
					}
				}
				else if (bind_info.second._res_type & EBindResDescType::kCBufferAttribute && bind_info.second._bind_flag & ShaderBindResourceInfo::kBindFlagPerMaterial)
				{
					auto byte_size = ShaderBindResourceInfo::GetVariableSize(bind_info.second);
					cbuf_size_per_passes[i] += byte_size;
				}
			}
		}

		if (!first_time)
		{
			for (int i = 0; i < pass_count; i++)
			{
				auto& tmp_textures = _tmp_textures_all_passes[i];
				auto& textures = _textures_all_passes[i];
				for (auto it = tmp_textures.begin(); it != tmp_textures.end(); it++)
				{
					if (textures.contains(it->first))
					{
						std::get<1>(it->second) = std::get<1>(textures[it->first]);
					}
				}
				_textures_all_passes[i] = std::move(_tmp_textures_all_passes[i]);
			}
		}
		for (int i = 0; i < pass_count; i++)
		{
			//处理cbuffer
			cbuf_size_per_passes[i] = ALIGN_TO_256(cbuf_size_per_passes[i]);
			AL_ASSERT(cbuf_size_per_passes[i] > 256);
			if (first_time)
			{
				_mat_cbuf_per_pass_size[i] = cbuf_size_per_passes[i];
				_p_cbufs[i].reset(ConstantBuffer::Create(_mat_cbuf_per_pass_size[i]));
				memset(_p_cbufs[i]->GetData(), 0, _mat_cbuf_per_pass_size[i]);
			}
			else if (_mat_cbuf_per_pass_size[i] != cbuf_size_per_passes[i])
			{
				throw std::runtime_error("Material: " + _name + " shader cbuf size not equal!");
				LOG_ERROR("Material: " + _name + " shader cbuf size not equal!");
				//u8* new_cbuf_data = new u8[cur_shader_cbuf_size];
				//memcpy(new_cbuf_data, _p_cbuf_cpu, _mat_cbuf_size);
				//DESTORY_PTRARR(_p_cbuf_cpu);
				//_p_cbuf_cpu = new_cbuf_data;
			}
			else {}
			//处理纹理和属性
			auto& bind_info = _p_shader->GetBindResInfo(i, _pass_variants[i]._variant_hash);
			auto& textures = _textures_all_passes[i];
			for (auto& prop_info : _p_shader->GetShaderPropertyInfos(i))
			{
				if (prop_info._prop_type == ESerializablePropertyType::kTexture2D)
				{
					SerializableProperty prop{ nullptr,prop_info._prop_name,prop_info._value_name,ESerializablePropertyType::kTexture2D };
					_properties.insert(std::make_pair(prop_info._value_name, prop));
					if (!first_time)
					{
						auto it = textures.find(prop_info._value_name);
						if (it != textures.end())
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
						SerializableProperty p{ _p_cbufs[i]->GetData() + ShaderBindResourceInfo::GetVariableOffset(bind_info.find(prop_info._value_name)->second),
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
						if (prop.GetProppertyValue<float>().value() == 0.0f)
							memcpy(prop._value_ptr, &prop._param[2], sizeof(float));
					}
					else if (prop._type == ESerializablePropertyType::kColor || prop._type == ESerializablePropertyType::kVector4f)
					{
						if (prop.GetProppertyValue<Vector4f>().value() == Vector4f::kZero)
							memcpy(prop._value_ptr, prop._param, sizeof(Vector4f));
					}
					_properties.insert(std::make_pair(prop_info._value_name, prop));
				}
			}
		}
	}
	
	void Material::ConstructKeywords(Shader* shader)
	{
		_pass_variants.clear();
		for (u16 i = 0; i < shader->_passes.size(); i++)
		{
			auto& new_shader_pass = shader->_passes[i];
			ShaderVariantHash new_variant_hash;
			std::set<String> active_kws;
			new_variant_hash = shader->ConstructVariantHash(i, _all_keywords, active_kws);
			PassVariantInfo info;
			info._pass_name = new_shader_pass._name;
			info._keywords = std::move(active_kws);
			info._variant_hash = new_variant_hash;
			_pass_variants.emplace_back(info);
		}
	}

	void Material::UpdateBindTexture(u16 pass_index, ShaderVariantHash new_hash)
	{
		auto& bind_textures = _textures_all_passes[pass_index];
		auto& bind_infos = _p_active_shader->_passes[pass_index]._variants[_pass_variants[pass_index]._variant_hash]._bind_res_infos;
		for (auto it = bind_textures.begin(); it != bind_textures.end(); it++)
		{
			auto shader_bind_info_it = bind_infos.find(it->first);
			if (shader_bind_info_it != bind_infos.end())
			{
				auto& [slot, texture] = it->second;
				u8 new_slot = shader_bind_info_it->second._bind_slot;
				it->second = std::make_pair(new_slot, texture);
			}
		}
	}
	//-------------------------------------------StandardMaterial--------------------------------------------------------
	StandardMaterial::StandardMaterial(Shader* shader, String name) : Material(shader, name)
	{

	}
	StandardMaterial::~StandardMaterial()
	{
	}
	//-------------------------------------------StandardMaterial--------------------------------------------------------
}
