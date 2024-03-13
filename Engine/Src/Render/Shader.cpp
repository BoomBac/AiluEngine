#include "pch.h"
#include "Render/Shader.h"
#include "Render/Renderer.h"
#include "RHI/DX12/D3DShader.h"
#include "Render/GraphicsPipelineStateObject.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/Utils.h"
#include "Render/GraphicsContext.h"



namespace Ailu
{
	Ref<Shader> Shader::Create(const std::string& file_name, String vert_entry, String pixel_entry)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT(false, "None render api used!");
			return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
		{
			auto shader = MakeRef<D3DShader>(file_name, vert_entry, pixel_entry);
			ShaderLibrary::Add(shader->Name(), shader);
			return shader;
		}
		}
		AL_ASSERT(false, "Unsupported render api!")
			return nullptr;
	}

	Shader::Shader(const String& sys_path) : _src_file_path(sys_path), _id(_s_global_shader_id), _name(std::format("AnonymityShader_{}", _s_global_shader_id++))
	{
		_actual_id = _id;
	}

	void Shader::Bind(uint32_t index)
	{
		if (index > RenderConstants::kMaxMaterialDataCount)
		{
			AL_ASSERT(true, "Material num more than MaxMaterialDataCount!");
			return;
		}
		if (_actual_id != _id)
		{
			ShaderLibrary::Get("error")->Bind(index);
			return;
		}
		GraphicsPipelineStateMgr::ConfigureVertexInputLayout(_pipeline_input_layout.Hash());
		GraphicsPipelineStateMgr::ConfigureRasterizerState(_pipeline_raster_state.Hash());
		GraphicsPipelineStateMgr::ConfigureDepthStencilState(_pipeline_ds_state.Hash());
		GraphicsPipelineStateMgr::ConfigureTopology(static_cast<u8>(ALHash::Hasher(_pipeline_topology)));
		GraphicsPipelineStateMgr::ConfigureBlendState(_pipeline_blend_state.Hash());
		GraphicsPipelineStateMgr::ConfigureShader(_actual_id);
		for (auto& it : s_global_textures_bind_info)
		{
			auto tex_it = _bind_res_infos.find(it.first);
			if (tex_it != _bind_res_infos.end() && (tex_it->second._res_type == EBindResDescType::kCubeMap || tex_it->second._res_type == EBindResDescType::kTexture2DArray ||
				tex_it->second._res_type == EBindResDescType::kTexture2D))
			{
				GraphicsPipelineStateMgr::SubmitBindResource(it.second, tex_it->second._bind_slot);
			}
		}
		for (auto& it : s_global_matrix_bind_info)
		{
			auto mat_it = _bind_res_infos.find(it.first);
			if (mat_it != _bind_res_infos.end() && (mat_it->second._res_type & EBindResDescType::kCBufferAttribute))
			{
				auto offset = ShaderBindResourceInfo::GetVariableOffset(mat_it->second);
				Matrix4x4f* mat_ptr = std::get<0>(it.second);
				u32 mat_num = std::get<1>(it.second);
				memcpy(reinterpret_cast<u8*>(g_pGfxContext->GetPerFrameCbufDataStruct()) + offset, mat_ptr, sizeof(Matrix4x4f) * mat_num);
				memcpy(g_pGfxContext->GetPerFrameCbufData(), g_pGfxContext->GetPerFrameCbufDataStruct(), sizeof(ScenePerFrameData));
			}
		}
	}

	bool Shader::Compile()
	{
		if (PreProcessShader() && RHICompileImpl())
		{
			GraphicsPipelineStateMgr::OnShaderRecompiled(this);
			_actual_id = _id;
			return true;
		}
		else
		{
			_actual_id = ShaderLibrary::Get("error")->ID();
			return false;
		}
	}

	Vector4f Shader::GetVectorValue(const String& name)
	{
		auto it = _bind_res_infos.find(name);
		if (it != _bind_res_infos.end())
		{
			return *reinterpret_cast<Vector4f*>(_p_cbuffer + ShaderBindResourceInfo::GetVariableOffset(it->second));
		}
		else
		{
			g_pLogMgr->LogWarningFormat("Get vector: {} on shader: {} failed!", name, _name);
			return Vector4f::kZero;
		}
	}
	float Shader::GetFloatValue(const String& name)
	{
		auto it = _bind_res_infos.find(name);
		if (it != _bind_res_infos.end())
		{
			return *reinterpret_cast<float*>(_p_cbuffer + ShaderBindResourceInfo::GetVariableOffset(it->second));
		}
		else
		{
			g_pLogMgr->LogWarningFormat("Get float: {} on shader: {} failed!", name, _name);
			return 0.0f;
		}
	}

	void Shader::SetGlobalTexture(const String& name, Texture* texture)
	{
		if (!s_global_textures_bind_info.contains(name))
			s_global_textures_bind_info.insert(std::make_pair(name, texture));
		else
			s_global_textures_bind_info[name] = texture;
	}
	void Shader::SetGlobalMatrix(const String& name, Matrix4x4f* matrix)
	{
		if (!s_global_matrix_bind_info.contains(name))
			s_global_matrix_bind_info.insert(std::make_pair(name, std::make_tuple(matrix,1)));
		else
			s_global_matrix_bind_info[name] = std::make_tuple(matrix, 1);
	}

	void Shader::SetGlobalMatrixArray(const String& name, Matrix4x4f* matrix, u32 num)
	{
		if (!s_global_matrix_bind_info.contains(name))
			s_global_matrix_bind_info.insert(std::make_pair(name, std::make_tuple(matrix, num)));
		else
			s_global_matrix_bind_info[name] = std::make_tuple(matrix, num);
	}

	void Shader::ConfigurePerFrameConstBuffer(ConstantBuffer* cbuf)
	{
		s_p_per_frame_cbuffer = cbuf;
	}

	void* Shader::GetByteCode(EShaderType type)
	{
		return nullptr;
	}

	Vector<Material*> Shader::GetAllReferencedMaterials()
	{
		Vector<Material*> ret;
		ret.reserve(_reference_mats.size());
		for (auto it = _reference_mats.begin(); it != _reference_mats.end(); it++)
			ret.emplace_back(*it);
		return ret;
	}
	void Shader::AddMaterialRef(Material* mat)
	{
		_reference_mats.insert(mat);
	}
	void Shader::RemoveMaterialRef(Material* mat)
	{
		_reference_mats.erase(mat);
	}
	bool Shader::RHICompileImpl()
	{
		return true;
	}
	void Shader::ParserShaderProperty(String& line, List<ShaderPropertyInfo>& props)
	{
		line = su::Trim(line);
		String addi_info{};
		if (line.find("[") != line.npos)
		{
			auto begin = line.find("["), end = line.find("]");
			addi_info = su::SubStrRange(line, begin + 1, end - 1);
			line = line.substr(end + 1);
			line = su::Trim(line);
		}
		auto cur_edge = line.find_first_of("(");
		String value_name = line.substr(0, cur_edge);
		line = line.substr(cur_edge);
		cur_edge = line.find_first_of("=");
		String defalut_value = line.substr(cur_edge + 1);
		line = line.substr(0, cur_edge - 1);
		su::RemoveSpaces(defalut_value);
		line = line.substr(1, line.find_last_of(")") - 1);
		cur_edge = line.find_first_of(",");
		String prop_name = line.substr(1, cur_edge - 2);
		String prop_type = line.substr(cur_edge + 1);
		ESerializablePropertyType seri_type;
		if (prop_type == "Texture2D") seri_type = ESerializablePropertyType::kTexture2D;
		else if (prop_type == "Color") seri_type = ESerializablePropertyType::kColor;
		else if (*(prop_type.begin()) == *"R") seri_type = ESerializablePropertyType::kRange;
		else if (prop_type == GetSerializablePropertyTypeStr(ESerializablePropertyType::kFloat)) seri_type = ESerializablePropertyType::kFloat;
		else if (prop_type == "Vector") seri_type = ESerializablePropertyType::kVector4f;
		else seri_type = ESerializablePropertyType::kUndefined;
		if (!addi_info.empty())
		{
			if (addi_info.starts_with("Toggle"))
			{
				seri_type = ESerializablePropertyType::kBool;
				_keywords[value_name].emplace_back(value_name + "_ON");
				_keywords[value_name].emplace_back(value_name + "_OFF");
			}
			else if (addi_info.starts_with("Enum"))
			{
				seri_type = ESerializablePropertyType::kEnum;
				auto enum_strs = su::Split(su::SubStrRange(addi_info, addi_info.find("(") + 1, addi_info.find(")") - 1), ",");
				_keywords[value_name].resize(enum_strs.size() / 2);
				for (size_t i = 0; i < enum_strs.size(); i += 2)
				{
					_keywords[value_name][std::stoi(enum_strs[i + 1])] = value_name + "_" + enum_strs[i];
				}
			}
		}
		Vector4f prop_param;
		if (seri_type == ESerializablePropertyType::kRange)
		{
			cur_edge = prop_type.find_first_of(",");
			size_t left_bracket = prop_type.find_first_of("(");
			size_t right_bracket = prop_type.find_first_of(")");
			prop_param.x = static_cast<float>(std::stod(su::SubStrRange(prop_type, left_bracket + 1, cur_edge - 1)));
			prop_param.y = static_cast<float>(std::stod(su::SubStrRange(prop_type, cur_edge + 1, right_bracket - 1)));
			prop_param.z = static_cast<float>(std::stod(defalut_value));
		}
		else if (seri_type == ESerializablePropertyType::kFloat || seri_type == ESerializablePropertyType::kBool)
		{
			prop_param.z = static_cast<float>(std::stod(defalut_value));
		}
		else if (seri_type == ESerializablePropertyType::kColor || seri_type == ESerializablePropertyType::kVector4f)
		{
			defalut_value = su::SubStrRange(defalut_value, 1, defalut_value.find_first_of(")") - 1);
			auto vec_str = su::Split(defalut_value, ",");
			prop_param.x = static_cast<float>(std::stod(vec_str[0]));
			prop_param.y = static_cast<float>(std::stod(vec_str[1]));
			prop_param.z = static_cast<float>(std::stod(vec_str[2]));
			prop_param.w = static_cast<float>(std::stod(vec_str[3]));
		}
		if (seri_type != ESerializablePropertyType::kUndefined)
		{
			props.emplace_back(ShaderPropertyInfo{ value_name ,prop_name,seri_type ,prop_param });
			LOG_INFO("prop name: {},default value {}", prop_name, defalut_value);
		}
		else
		{
			g_pLogMgr->LogWarningFormat("Undefined shader property type with name {}", prop_name);
		}
	}
	void Shader::ExtractValidShaderProperty()
	{
		auto it = _shader_prop_infos.begin();
		while (it != _shader_prop_infos.end())
		{
			if (!_bind_res_infos.contains(it->_value_name))
			{
				it = _shader_prop_infos.erase(it);
			}
			else
				it++;
		}
	}
	bool Shader::PreProcessShader()
	{
		//parser property and pipeline state
		_shader_prop_infos.clear();
		List<String> lines{};
		String line{};
		u32 line_count = 0;
		lines = ReadFileToLines(_src_file_path, line_count, "//info bein", "//info end");
		u8 need_zbuf_count = 2;
		if (line_count > 0)
		{
			auto prop_start_it = std::find(lines.begin(), lines.end(), "//Properties");
			auto prop_end_it = std::find(prop_start_it, lines.end(), "//}");
			if (prop_start_it != lines.end())
			{
				prop_start_it++;
				prop_start_it++;
				for (auto& it = prop_start_it; it != prop_end_it; it++)
				{
					line = it->substr(2);
					ParserShaderProperty(line, _shader_prop_infos);
				}
				prop_start_it--;
				prop_start_it--;
			}
			String k, v, blend_info;
			bool is_transparent = false;

			for (auto it = lines.begin(); it != prop_start_it; it++)
			{
				line = it->substr(2);
				k = line.substr(0, line.find_first_of(":"));
				su::RemoveSpaces(k);
				v = line.substr(line.find_first_of(":") + 1);
				su::RemoveSpaces(v);
				if (su::Equal(k, ShaderCommand::kName, false)) _name = v;
				else if (su::Equal(k, ShaderCommand::kVSEntry, false) && _vert_entry.empty()) _vert_entry = v;
				else if (su::Equal(k, ShaderCommand::kPSEntry, false) && _pixel_entry.empty()) _pixel_entry = v;
				else if (su::Equal(k, ShaderCommand::kCull, false))
				{
					if (su::Equal(v, ShaderCommand::kCullValue.kBack, false))
					{
						_pipeline_raster_state._cull_mode = ECullMode::kBack;
					}
					else
					{
						_pipeline_raster_state._cull_mode = ECullMode::kFront;
					}
				}
				else if (su::Equal(k, ShaderCommand::kTopology, false))
				{
					if (su::Equal(v, ShaderCommand::kTopologyValue.kLine))
					{
						_pipeline_topology = ETopology::kLine;
					}
				}
				else if (su::Equal(k, ShaderCommand::kBlend, false))
				{
					blend_info = v;
				}
				else if (su::Equal(k, ShaderCommand::kQueue, false))
				{
					if (su::Equal(v, ShaderCommand::kQueueValue.kTransplant, false))
						is_transparent = true;
				}
				else if (su::Equal(k, ShaderCommand::KFill, false))
				{
					if (su::Equal(v, ShaderCommand::kFillValue.kWireframe))
					{
						_pipeline_raster_state._fill_mode = EFillMode::kWireframe;
					}
				}
				else if (su::Equal(k, ShaderCommand::kZTest, false))
				{
					if (su::Equal(v, ShaderCommand::kZTestValue.kOff))
					{
						need_zbuf_count--;
						_pipeline_ds_state._depth_test_func = ECompareFunc::kAlways;
					}
					else if (su::Equal(v, ShaderCommand::kZTestValue.kEqual))
						_pipeline_ds_state._depth_test_func = ECompareFunc::kEqual;
					else if (su::Equal(v, ShaderCommand::kZTestValue.kLEqual))
						_pipeline_ds_state._depth_test_func = ECompareFunc::kLessEqual;
				}
				else if (su::Equal(k, ShaderCommand::kZWrite, false))
				{
					if (su::Equal(v, ShaderCommand::kZWriteValue.kOff))
					{
						need_zbuf_count--;
						_pipeline_ds_state._b_depth_write = false;
					}
				}
			}
			if (is_transparent && !blend_info.empty())
			{
				auto blend_factors = su::Split(blend_info, ",");
				static auto get_ailu_blend_factor = [](const String& s) -> EBlendFactor {
					if (su::Equal(s, ShaderCommand::kBlendFactorValue.kOne)) return EBlendFactor::kOne;
					else if (su::Equal(s, ShaderCommand::kBlendFactorValue.kZero)) return EBlendFactor::kZero;
					else if (su::Equal(s, ShaderCommand::kBlendFactorValue.kSrc)) return EBlendFactor::kSrcAlpha;
					else if (su::Equal(s, ShaderCommand::kBlendFactorValue.kOneMinusSrc)) return EBlendFactor::kOneMinusSrcAlpha;
					else return EBlendFactor::kOne;
					};
				_pipeline_blend_state._b_enable = true;
				_pipeline_blend_state._src_color = get_ailu_blend_factor(blend_factors[0]);
				_pipeline_blend_state._dst_color = get_ailu_blend_factor(blend_factors[1]);
			}
			for (auto& it = prop_end_it; it != lines.end(); it++)
			{
				line = it->substr(2);
				if (line.find("multi") != line.npos)
				{
					//format: groupname_keywordname
					auto keywords_str = line.substr(line.find_first_of("e") + 1);
					keywords_str = su::Trim(keywords_str);
					auto kw_seq = su::Split(keywords_str, " ");
					if (kw_seq.size() > 0)
					{
						String kw_group_name = kw_seq[0].substr(0, kw_seq[0].rfind("_"));
						for (auto& kw : kw_seq)
						{
							if (std::find(_keywords[kw_group_name].begin(), _keywords[kw_group_name].end(), kw) == _keywords[kw_group_name].end())
								_keywords[kw_group_name].emplace_back(kw);
						}
					}
				}
			}
		}
		else
		{
			g_pLogMgr->LogErrorFormat("PreProcess shader: {} with line count 0", _src_file_path);
		}

		if (need_zbuf_count == 0)
		{
			_pipeline_ds_state._b_depth_write = false;
			_pipeline_ds_state._depth_test_func = ECompareFunc::kAlways;
		}
		//construct keywords
		Vector<Vector<String>> kw_permutation_in{};
		int kw_group_count = 0;
		for (auto& it : _keywords)
		{
			Vector<String> v;
			for (int i = 0; i < it.second.size(); i++)
			{
				auto& kw = it.second[i];
				_keywords_ids[kw] = std::make_tuple(kw_group_count, i);
				v.emplace_back(kw);
			}
			++kw_group_count;
			kw_permutation_in.emplace_back(v);
		}
		if (!kw_permutation_in.empty())
		{
			for (auto& kw_seq : Algorithm::Permutations(kw_permutation_in))
			{
				u64 kw_hash = 0;
				u64 temp_hash = 0;
				//D3D_SHADER_MACRO* shader_marcos = new D3D_SHADER_MACRO[kw_seq.size() + 1];
				for (int i = 0; i < kw_seq.size(); i++)
				{
					auto& [group_id, inner_id] = _keywords_ids[kw_seq[i]];
					temp_hash = inner_id;
					temp_hash <<= group_id * 3;//每个关键字组占三位，也就是每组内最多8个关键字
					kw_hash |= temp_hash;
					//shader_marcos[i] = { kw_seq[i].c_str(),"1" };
				}
				//shader_marcos[kw_seq.size()] = { NULL,NULL };
				//_keyword_defines.emplace_back(shader_marcos);
			}
		}
		return true;
	}

	//---------------------------------------------------------------------ComputeShader-------------------------------------------------------------------
	Ref<ComputeShader> ComputeShader::Create(const String& file_name)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT(false, "None render api used!");
			return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
		{
			auto shader = MakeRef<D3DComputeShader>(file_name);
			auto asset_path = PathUtils::ExtractAssetPath(file_name);
			s_cs_library.insert(std::make_pair(asset_path, shader));
			return shader;
		}
		}
		AL_ASSERT(false, "Unsupported render api!")
			return nullptr;
	}

	Ref<ComputeShader> ComputeShader::Get(const String& name)
	{
		if (s_cs_library.contains(name))
			return s_cs_library[name];
		else
			return nullptr;
	}

	ComputeShader::ComputeShader(const String& sys_path) : _src_file_path(sys_path)
	{
		_name = PathUtils::GetFileName(sys_path);
		_id = s_global_cs_id++;
	}

	void ComputeShader::Bind(u16 thread_group_x, u16 thread_group_y, u16 thread_group_z)
	{

	}

	void ComputeShader::SetTexture(const String& name, Texture* texture)
	{
		auto it = _bind_res_infos.find(name);
		if (texture != nullptr && it != _bind_res_infos.end())
		{
			it->second._p_res = texture;
		}
		//else
		//{
		//	LOG_WARNING("Set compute shader {} texture {} failed", _name, name);
		//}
	}

	void ComputeShader::SetTexture(u8 bind_slot, Texture* texture)
	{
		if (texture != nullptr)
		{
			auto rit = std::find_if(_bind_res_infos.begin(), _bind_res_infos.end(), [=](const auto& it) {
				return it.second._bind_slot == bind_slot;
				});
			if(rit != _bind_res_infos.end())
				rit->second._p_res = texture;
		}
	}

	bool ComputeShader::Compile()
	{
		return RHICompileImpl();
	}

	bool ComputeShader::RHICompileImpl()
	{
		return false;
	}

	//---------------------------------------------------------------------ComputeShader-------------------------------------------------------------------
}
