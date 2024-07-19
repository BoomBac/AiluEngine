#include "pch.h"
#include <regex>

#include "Render/Shader.h"
#include "Render/Renderer.h"
#include "Render/RenderQueue.h"
#include "RHI/DX12/D3DShader.h"
#include "Render/GraphicsPipelineStateObject.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/Utils.h"
#include "Render/GraphicsContext.h"



namespace Ailu
{
	std::vector<std::vector<std::string>> ExtractPassBlocks(const std::list<std::string>& lines)
	{
		std::vector<std::vector<std::string>> passBlocks;
		auto it = lines.begin();
		while (it != lines.end()) {
			// 查找 pass begin::
			auto passBeginIt = std::find(it, lines.end(), "//pass begin::");
			if (passBeginIt == lines.end())
				break; // 找不到则退出循环

			// 查找 pass end::
			auto passEndIt = std::find(passBeginIt, lines.end(), "//pass end::");
			if (passEndIt == lines.end())
				break; // 找不到则退出循环

			// 提取 pass 区块
			std::vector<std::string> passBlock(++passBeginIt, passEndIt);
			passBlocks.push_back(passBlock);

			// 更新迭代器，以便继续搜索下一个 pass 区块
			it = passEndIt;
		}

		return passBlocks;
	}

	Ref<Shader> Shader::Create(const WString& sys_path)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT_MSG(false, "None render api used!");
			return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
		{
			auto shader = MakeRef<D3DShader>(sys_path);
            shader->ID(s_global_shader_unique_id++);
			return shader;
		}
		}
		AL_ASSERT_MSG(false, "Unsupported render api!");
		return nullptr;
	}
	//46bit hash,0~5 shader_id,6~8 pass_index 9~45 variant_hash
	u64 Shader::ConstructHash(const u32& shader_id, const u16& pass_hash, ShaderVariantHash variant_hash)
	{
		ALHash::Hash<64> shader_hash;
		shader_hash.Set(0, 6, shader_id);
		shader_hash.Set(6, 3, pass_hash);
		shader_hash.Set(9, 37, variant_hash);
		return shader_hash.Get(0,46);
	}
	void Shader::ExtractInfoFromHash(const u64& shader_hash, u32& shader_id, u16& pass_hash, ShaderVariantHash& variant_hash)
	{
		ALHash::Hash<64> hash(shader_hash);
		shader_id = (u32)hash.Get(0,6);
		pass_hash = (u16)hash.Get(6,3);
		variant_hash = hash.Get(9,37);
	}

	u32 Shader::GetShaderState(Shader* shader, u16 pass_index, ShaderVariantHash variant_hash)
	{
		auto& state = shader->_variant_state[pass_index][variant_hash];
		if (state == EShaderVariantState::kError)
		{
			return 4000;
		}
		else if (state == EShaderVariantState::kCompiling)
		{
			return 4001;
		}
		else
		{
			return 0;
		}
	}

	Shader::Shader(const WString& sys_path)
	{
		_src_file_path = sys_path;
		_name = ToChar(PathUtils::GetFileName(sys_path));
		PreProcessShader();
	}

	void Shader::Bind(u16 pass_index, ShaderVariantHash variant_hash)
	{
		GraphicsPipelineStateMgr::ConfigureVertexInputLayout(_passes[pass_index]._variants[variant_hash]._pipeline_input_layout.Hash());
		GraphicsPipelineStateMgr::ConfigureRasterizerState(_passes[pass_index]._pipeline_raster_state.Hash());
		GraphicsPipelineStateMgr::ConfigureDepthStencilState(_passes[pass_index]._pipeline_ds_state.Hash());
		GraphicsPipelineStateMgr::ConfigureTopology(static_cast<u8>(ALHash::Hasher(_passes[pass_index]._pipeline_topology)));
		GraphicsPipelineStateMgr::ConfigureBlendState(_passes[pass_index]._pipeline_blend_state.Hash());
		GraphicsPipelineStateMgr::ConfigureShader(ConstructHash(_id,pass_index, variant_hash));
		for (auto& it : s_global_textures_bind_info)
		{
			auto& pass_bind_res_info = _passes[pass_index]._variants[variant_hash]._bind_res_infos;
			auto tex_it = pass_bind_res_info.find(it.first);
			if (tex_it != pass_bind_res_info.end())
			{
				if (tex_it->second._res_type == EBindResDescType::kCubeMap || tex_it->second._res_type == EBindResDescType::kTexture2DArray || tex_it->second._res_type == EBindResDescType::kTexture2D)
					GraphicsPipelineStateMgr::SubmitBindResource(it.second, tex_it->second._bind_slot);
			}
		}
		for (auto& it : s_global_matrix_bind_info)
		{
			throw std::runtime_error("s_global_matrix_bind_info");
			//auto mat_it = _bind_res_infos.find(it.first);
			//if (mat_it != _bind_res_infos.end() && (mat_it->second._res_type & EBindResDescType::kCBufferAttribute))
			//{
			//	auto offset = ShaderBindResourceInfo::GetVariableOffset(mat_it->second);
			//	Matrix4x4f* mat_ptr = std::get<0>(it.second);
			//	u32 mat_num = std::get<1>(it.second);
			//	memcpy(s_p_per_frame_cbuffer->GetData() + offset, mat_ptr, sizeof(Matrix4x4f) * mat_num);
			//}
		}
	}

	bool Shader::Compile(u16 pass_id, ShaderVariantHash variant_hash)
	{
		g_pLogMgr->LogFormat(L"Begin compile shader: {},pass: {},variant: {}...", _src_file_path,pass_id, variant_hash);
		AL_ASSERT(pass_id >= _passes.size());
		auto& pass = _passes[pass_id];
		AL_ASSERT(!pass._variants.contains(variant_hash));
		_variant_state[pass_id][variant_hash] = EShaderVariantState::kCompiling;
		if (RHICompileImpl(pass_id, variant_hash))
		{
			pass._variants[variant_hash]._pipeline_input_layout.Hash(PipelineStateHash<VertexInputLayout>::GenHash(pass._variants[variant_hash]._pipeline_input_layout));
			pass._pipeline_raster_state.Hash(PipelineStateHash<RasterizerState>::GenHash(pass._pipeline_raster_state));
			pass._pipeline_blend_state.Hash(PipelineStateHash<BlendState>::GenHash(pass._pipeline_blend_state));
			pass._pipeline_ds_state.Hash(PipelineStateHash<DepthStencilState>::GenHash(pass._pipeline_ds_state));
			GraphicsPipelineStateMgr::OnShaderRecompiled(this, pass_id, variant_hash);
			_variant_state[pass_id][variant_hash] = EShaderVariantState::kReady;
		}
		else
		{
			_variant_state[pass_id][variant_hash] = EShaderVariantState::kError;
		}
		g_pLogMgr->Log("Shader compile end");
		return _variant_state[pass_id][variant_hash] == EShaderVariantState::kReady;
	}

	bool Shader::Compile()
	{
		u16 pass_index = 0;
		u32 variant_index = 0;
		u32 error_variant_count = 0;
		for (auto& pass : _passes)
		{
			for (auto& variant_it : pass._variants)
			{
				auto& [vhash, kw_seq] = variant_it;
				if (!Compile(pass_index, vhash))
					++error_variant_count;
				++variant_index;
			}
			++pass_index;
		}
		return error_variant_count == 0;
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
			s_global_matrix_bind_info.insert(std::make_pair(name, std::make_tuple(matrix, 1)));
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

	void* Shader::GetByteCode(EShaderType type, u16 pass_index, ShaderVariantHash variant_hash)
	{
		return nullptr;
	}

	i16 Shader::FindPass(String pass_name)
	{
		auto it = std::find_if(_passes.begin(), _passes.end(), [&](const ShaderPass& shader_pass)->bool {return pass_name == shader_pass._name; });
		return it != _passes.end() ? (i16)it->_index : -1;
	}

	ShaderVariantHash Shader::ConstructVariantHash(u16 pass_index, const std::set<String>& kw_seq) const
	{
		std::set<String> active_kw_seq;
		return ConstructVariantHash(pass_index,kw_seq, active_kw_seq);
	}

	ShaderVariantHash Shader::ConstructVariantHash(u16 pass_index, const std::set<String>& kw_seq,std::set<String>& active_kw_seq) const
	{
		AL_ASSERT(pass_index >= _passes.size());
		//ShaderVariantHash hash{ 0 };
		Math::ALHash::Hash<32> hash;
		auto& kw_index_info = _passes[pass_index]._keywords_ids;
		for (auto& kw : kw_seq)
		{
			auto it = kw_index_info.find(kw);
			if (it != kw_index_info.end())
			{
				auto& [group_id, inner_id] = it->second;
				hash.Set(group_id * 3, 3, inner_id);
				active_kw_seq.insert(it->first);
			}
		}
		ShaderVariantHash hash_value = hash.Get(0, 32);
		return hash_value;
	}

	bool Shader::IsKeywordValid(u16 pass_index, const String& kw) const
	{		
		return std::find_if(_passes[pass_index]._keywords.begin(), _passes[pass_index]._keywords.end(), [&](const auto& kws)->bool {return kws.contains(kw); }) != _passes[pass_index]._keywords.end();
	}

	std::set<String> Shader::KeywordsSameGroup(u16 pass_index,const String& kw) const
	{
		auto it = _passes[pass_index]._keywords_ids.find(kw);
		if (it != _passes[pass_index]._keywords_ids.end())
		{
			auto& [group_id, inner_group_id] = it->second;
			return _passes[pass_index]._keywords[group_id];
		}
		return std::set<String>();
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
	bool Shader::RHICompileImpl(u16 pass_index, ShaderVariantHash variant_hash)
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
				//seri_type = ESerializablePropertyType::kBool;
				//_passshared_keywords[_value_name].emplace_back(_value_name + "_ON");
				//_passshared_keywords[_value_name].emplace_back(_value_name + "_OFF");
			}
			else if (addi_info.starts_with("Enum"))
			{
				//seri_type = ESerializablePropertyType::kEnum;
				//auto enum_strs = su::Split(su::SubStrRange(addi_info, addi_info.find("(") + 1, addi_info.find(")") - 1), ",");
				//_passshared_keywords[_value_name].resize(enum_strs.size() / 2);
				//for (size_t i = 0; i < enum_strs.size(); i += 2)
				//{
				//	_passshared_keywords[_value_name][std::stoi(enum_strs[i + 1])] = _value_name + "_" + enum_strs[i];
				//}
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
		}
		else
		{
			//g_pLogMgr->LogWarningFormat("Undefined shader property type with name {}", prop_name);
		}
	}

	//void Shader::ExtractValidShaderProperty(u16 pass_index, ShaderVariantHash variant_hash)
	//{
	//	auto it = _passes[pass_index]._shader_prop_infos.begin();
	//	while (it != _passes[pass_index]._shader_prop_infos.end())
	//	{
	//		if (!_passes[pass_index]._bind_res_infos[variant_hash].contains(it->_value_name))
	//		{
	//			it = _passes[pass_index]._shader_prop_infos.erase(it);
	//		}
	//		else
	//			it++;
	//	}
	//}

	//这里后台线程编译时会影响主线程对其的访问
	bool Shader::PreProcessShader()
	{
		bool is_process_succeed = true;
		List<String> lines{};
		String line{};
		u32 line_count = 0;
		lines = ReadFileToLines(ToChar(_src_file_path), line_count, "//info bein", "//info end");
		if (line_count > 0)
		{
			auto pass_blocks = ExtractPassBlocks(lines);
			_passes.resize(pass_blocks.size());
			for(auto& pass : _passes)
				pass._shader_prop_infos.clear();
			for (int i = 0; i < pass_blocks.size(); i++)
			{
				ShaderPass cur_pass;
				String k, v, blend_info;
				bool is_transparent = false;
				RasterizerState    pass_pipeline_raster_state;
				DepthStencilState  pass_pipeline_ds_state;
				ETopology          pass_pipeline_topology = ETopology::kTriangle;
				BlendState         pass_pipeline_blend_state;
				u8                 need_zbuf_count = 2;
				u32                pass_render_queue = kRenderQueueOpaque;
				//Pass基本信息和管线状态信息
				for (auto it = pass_blocks[i].begin(); it != pass_blocks[i].end(); it++)
				{
					line = it->substr(2);
					k = line.substr(0, line.find_first_of(":"));
					su::RemoveSpaces(k);
					v = line.substr(line.find_first_of(":") + 1);
					su::RemoveSpaces(v);
					if (su::Equal(k, ShaderCommand::kName, false))
					{
						cur_pass._name = v;
					}
					else if (su::Equal(k, ShaderCommand::kVSEntry, false))
					{
						cur_pass._vert_entry = v;
					}
					else if (su::Equal(k, ShaderCommand::kPSEntry, false))
					{
						cur_pass._pixel_entry = v;
					}
					else if (su::Equal(k, ShaderCommand::kCull, false))
					{
						if (su::Equal(v, ShaderCommand::kCullValue.kBack, false))
						{
							pass_pipeline_raster_state._cull_mode = ECullMode::kBack;
						}
						else if (su::Equal(v, ShaderCommand::kCullValue.kFront, false))
						{
							pass_pipeline_raster_state._cull_mode = ECullMode::kFront;
						}
						else
						{
							pass_pipeline_raster_state._cull_mode = ECullMode::kNone;
						}
					}
					else if (su::Equal(k, ShaderCommand::kTopology, false))
					{
						if (su::Equal(v, ShaderCommand::kTopologyValue.kLine))
						{
							pass_pipeline_topology = ETopology::kLine;
						}
					}
					else if (su::Equal(k, ShaderCommand::kBlend, false))
					{
						blend_info = v;
					}
					else if (su::Equal(k, ShaderCommand::kQueue, false))
					{
						if (su::Equal(v, ShaderCommand::kQueueValue.kTransplant, false))
						{
							pass_render_queue = kRenderQueueTransparent;
							is_transparent = true;
						}
					}
					else if (su::Equal(k, ShaderCommand::KFill, false))
					{
						if (su::Equal(v, ShaderCommand::kFillValue.kWireframe))
						{
							pass_pipeline_raster_state._fill_mode = EFillMode::kWireframe;
						}
					}
					else if (su::Equal(k, ShaderCommand::kZTest, false))
					{
						if (su::Equal(v, ShaderCommand::kZTestValue.kAlways, false))
						{
							need_zbuf_count--;
							pass_pipeline_ds_state._depth_test_func = ECompareFunc::kAlways;
						}
						else if (su::Equal(v, ShaderCommand::kZTestValue.kEqual, false))
							pass_pipeline_ds_state._depth_test_func = ECompareFunc::kEqual;
						else if (su::Equal(v, ShaderCommand::kZTestValue.kLEqual, false))
							pass_pipeline_ds_state._depth_test_func = ECompareFunc::kLessEqual;
					}
					else if (su::Equal(k, ShaderCommand::kZWrite, false))
					{
						if (su::Equal(v, ShaderCommand::kZWriteValue.kOff))
						{
							need_zbuf_count--;
							pass_pipeline_ds_state._b_depth_write = false;
						}
					}
					else if (su::Equal(k, ShaderCommand::kStencil, false))
					{
						pass_pipeline_ds_state._b_front_stencil = true;
						//v = {Ref 5,Comp Always,Pass Replace}
						std::regex regex("\\{([^}]*)\\}");
						std::smatch match;
						if (std::regex_search(v, match, regex)) 
						{
							String extracted = match[1].str();
							Map<String, String> stencil_values;
							std::stringstream ss(extracted);
							String item;
							while (std::getline(ss, item, ',')) 
							{
								std::stringstream item_ss(item);
								std::string key, value;
								std::getline(item_ss, key, ':');
								std::getline(item_ss, value, ':');
								stencil_values[key] = value;
							}
							if (!stencil_values.empty())
							{
								if (stencil_values.contains(ShaderCommand::kStencilStateValue.kStencilRef))
								{
									pass_pipeline_ds_state._stencil_ref_value = std::stoi(stencil_values[ShaderCommand::kStencilStateValue.kStencilRef]);
								}
								if (stencil_values.contains(ShaderCommand::kStencilStateValue.kStencilComp))
								{
									auto& comp_str = stencil_values[ShaderCommand::kStencilStateValue.kStencilComp];
									if (su::Equal(comp_str, ShaderCommand::Comparison::kAlways, false))
									{
										pass_pipeline_ds_state._stencil_test_func = ECompareFunc::kAlways;
									}
									else if (su::Equal(comp_str, ShaderCommand::Comparison::kEqual, false))
									{
										pass_pipeline_ds_state._stencil_test_func = ECompareFunc::kEqual;
									}
									else if (su::Equal(comp_str, ShaderCommand::Comparison::kNotEqual, false))
									{
										pass_pipeline_ds_state._stencil_test_func = ECompareFunc::kNotEqual;
									}
									else if (su::Equal(comp_str, ShaderCommand::Comparison::kLEqual, false))
									{
										pass_pipeline_ds_state._stencil_test_func = ECompareFunc::kLessEqual;
									}
									else if (su::Equal(comp_str, ShaderCommand::Comparison::kGreater, false))
									{
										pass_pipeline_ds_state._stencil_test_func = ECompareFunc::kGreater;
									}
									else if (su::Equal(comp_str, ShaderCommand::Comparison::kGEqual, false))
									{
										pass_pipeline_ds_state._stencil_test_func = ECompareFunc::kGreaterEqual;
									}
									else
									{
										AL_ASSERT(true);
									}
								}
								if (stencil_values.contains(ShaderCommand::kStencilStateValue.kStencilPass))
								{
									auto& pass_str = stencil_values[ShaderCommand::kStencilStateValue.kStencilPass];
									if (su::Equal(pass_str, ShaderCommand::kStencilStateValue.StencilPassValue.kReplace, false))
									{
										pass_pipeline_ds_state._stencil_test_pass_op = EStencilOp::kReplace;
									}
									else if (su::Equal(pass_str, ShaderCommand::kStencilStateValue.StencilPassValue.kKeep, false))
									{
										pass_pipeline_ds_state._stencil_test_pass_op = EStencilOp::kKeep;
									}
									else if (su::Equal(pass_str, ShaderCommand::kStencilStateValue.StencilPassValue.kIncr, false))
									{
										pass_pipeline_ds_state._stencil_test_pass_op = EStencilOp::kIncrementAndClamp;
									}
									else if (su::Equal(pass_str, ShaderCommand::kStencilStateValue.StencilPassValue.kDecr, false))
									{
										pass_pipeline_ds_state._stencil_test_pass_op = EStencilOp::kDecrementAndClamp;
									}
									else
									{
										AL_ASSERT(true);
									}
								}
							}
						}
					}
					else {};
					//提取变体信息
					if (line.find("multi") != line.npos)
					{
						//format: groupname_keywordname
						auto keywords_str = line.substr(line.find_first_of("e") + 1);
						keywords_str = su::Trim(keywords_str);
						auto kw_seq = su::Split(keywords_str, " ");
						if (kw_seq.size() > 0)
						{
							std::set<String> kws;
							for (auto& kw : kw_seq)
							{
								kws.insert(kw);
							}
							cur_pass._keywords.emplace_back(kws);
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
						pass_pipeline_blend_state._b_enable = true;
						pass_pipeline_blend_state._src_color = get_ailu_blend_factor(blend_factors[0]);
						pass_pipeline_blend_state._dst_color = get_ailu_blend_factor(blend_factors[1]);
					}
				}
				if (need_zbuf_count == 0)
				{
					pass_pipeline_ds_state._b_depth_write = false;
					pass_pipeline_ds_state._depth_test_func = ECompareFunc::kAlways;
				}
				//解析shader属性
				auto prop_start_it = std::find(lines.begin(), lines.end(), "//Properties");
				auto prop_end_it = std::find(prop_start_it, lines.end(), "//}");
				if (prop_start_it != lines.end())
				{
					prop_start_it++;
					prop_start_it++;
					for (auto& it = prop_start_it; it != prop_end_it; it++)
					{
						line = it->substr(2);
						ParserShaderProperty(line, cur_pass._shader_prop_infos);
					}
					prop_start_it--;
					prop_start_it--;
				}
				//construct keywords
				Vector<Vector<String>> kw_permutation_in{};
				u16 kw_group_id = 0u;
				for (auto& kw_group : cur_pass._keywords)
				{
					Vector<String> kw_cur_group;
					u16 kw_innear_group_id;
					if (kw_group.contains("_")) //set 会导致关键字声明顺序丢失，所以这里手动将“_”调至首位
					{
						u16 kw_innear_group_id = 1u;
						cur_pass._keywords_ids["_"] = std::make_tuple(kw_group_id, 0);
						for (auto& kw : kw_group)
						{
							if(kw != "_")
							cur_pass._keywords_ids[kw] = std::make_tuple(kw_group_id, kw_innear_group_id++);
							kw_cur_group.emplace_back(kw);
						}
					}
					else
					{
						kw_innear_group_id = 0u;
						for (auto& kw : kw_group)
						{
							cur_pass._keywords_ids[kw] = std::make_tuple(kw_group_id, kw_innear_group_id++);
							kw_cur_group.emplace_back(kw);
						}
					}

					++kw_group_id;
					kw_permutation_in.emplace_back(kw_cur_group);
				}
				if (!kw_permutation_in.empty())
				{
					auto all_kw_seq = Algorithm::Permutations(kw_permutation_in);
					for (auto& kw_seq : all_kw_seq)
					{
						Math::ALHash::Hash<32> kw_hash;
						std::set<String> kw_set;
						for (int i = 0; i < kw_seq.size(); i++)
						{
							auto& [group_id, inner_id] = cur_pass._keywords_ids[kw_seq[i]];
							kw_hash.Set(group_id * 3,3,inner_id);//每个关键字组占三位，也就是每组内最多8个关键字
							kw_set.insert(kw_seq[i]);
						}
						ShaderPass::PassVariant v;
						v._active_keywords = kw_set;
						cur_pass._variants.insert(std::make_pair(kw_hash.Get(0, 32), v));
					}
				}
				else
				{
					ShaderPass::PassVariant v;
					cur_pass._variants.insert(std::make_pair(0, v));
				}
				Map<ShaderVariantHash, EShaderVariantState> cur_pass_variant_state;
				for (auto& it : cur_pass._variants)
				{
					auto& [vhash, variant] = it;
					cur_pass_variant_state[vhash] = EShaderVariantState::kNone;
				}

				auto pass_it = std::find_if(_passes.begin(), _passes.end(), [&](const ShaderPass& p) {return p._name == cur_pass._name; });
				//预处理时，从原始pass info中，对于完全一致的变体，复制编译后才能获取的信息并填充,否则这些变体未编译的话，导致引用他们的材质出现问题
				if (pass_it != _passes.end())
				{
					for (auto& it : cur_pass._variants)
					{
						auto& [vhash, variant] = it;
						for (auto& old_it : pass_it->_variants)
						{
							auto& [old_vhash, old_variant] = old_it;
							if (variant._active_keywords == old_variant._active_keywords)
							{
								variant = old_variant;
							}
						}
					}
				}
				cur_pass._index = i;
				cur_pass._source_files.insert(_src_file_path);
				cur_pass._pipeline_raster_state = pass_pipeline_raster_state;
				cur_pass._pipeline_ds_state = pass_pipeline_ds_state;
				cur_pass._pipeline_topology = pass_pipeline_topology;
				cur_pass._pipeline_blend_state = pass_pipeline_blend_state;
				cur_pass._render_queue = pass_render_queue;
				_passes[i] = cur_pass;
				_variant_state.emplace_back(std::move(cur_pass_variant_state));
			}
		}
		_is_pass_elements_init.store(!is_process_succeed);
		return is_process_succeed;
	}

	//---------------------------------------------------------------------ComputeShader-------------------------------------------------------------------
	Ref<ComputeShader> ComputeShader::Create(const WString& sys_path)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT_MSG(false, "None render api used!");
			return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
		{
			auto shader = MakeRef<D3DComputeShader>(sys_path);
			auto asset_path = PathUtils::ExtractAssetPath(sys_path);
			s_cs_library.insert(std::make_pair(asset_path, shader));
			return shader;
		}
		}
		AL_ASSERT_MSG(false, "Unsupported render api!");
		return nullptr;
	}

	Ref<ComputeShader> ComputeShader::Get(const WString& name)
	{
		if (s_cs_library.contains(name))
			return s_cs_library[name];
		else
			return nullptr;
	}

	ComputeShader::ComputeShader(const WString& sys_path) : _src_file_path(sys_path)
	{
		_name = ToChar(PathUtils::GetFileName(sys_path));
		_kernels.emplace_back("cs_main");
	}

	void ComputeShader::Bind(CommandBuffer* cmd, u16 thread_group_x, u16 thread_group_y, u16 thread_group_z)
	{
	}

	void ComputeShader::SetTexture(const String& name, Texture* texture)
	{
		auto it = _bind_res_infos.find(name);
		if (texture != nullptr && it != _bind_res_infos.end())
		{
			it->second._p_res = texture;
			_texture_addi_bind_info[it->second._bind_slot] = std::make_pair(ECubemapFace::kUnknown, 0);
		}
	}

	void ComputeShader::SetTexture(u8 bind_slot, Texture* texture)
	{
		if (texture != nullptr)
		{
			auto rit = std::find_if(_bind_res_infos.begin(), _bind_res_infos.end(), [=](const auto& it) {
				return it.second._bind_slot == bind_slot;
				});
			if (rit != _bind_res_infos.end())
			{
				rit->second._p_res = texture;
				_texture_addi_bind_info[rit->second._bind_slot] = std::make_pair(ECubemapFace::kUnknown, 0);
			}
		}
	}

	void ComputeShader::SetTexture(const String& name, Texture* texture, ECubemapFace::ECubemapFace face, u16 mipmap)
	{
		auto it = _bind_res_infos.find(name);
		if (texture != nullptr && it != _bind_res_infos.end())
		{
			it->second._p_res = texture;
			_texture_addi_bind_info[it->second._bind_slot] = std::make_pair(face, mipmap);
		}
	}

	void ComputeShader::SetFloat(const String& name, f32 value)
	{
		auto it = _bind_res_infos.find(name);
		if (it != _bind_res_infos.end())
		{
			memcpy(_p_cbuffer->GetData() + ShaderBindResourceInfo::GetVariableOffset(it->second), &value, sizeof(f32));
		}
	}

	void ComputeShader::SetBool(const String& name, bool value)
	{
		auto it = _bind_res_infos.find(name);
		if (it != _bind_res_infos.end())
		{
			memcpy(_p_cbuffer->GetData() + ShaderBindResourceInfo::GetVariableOffset(it->second), &value, sizeof(f32));
		}
	}

	void ComputeShader::SetInt(const String& name, i32 value)
	{
		auto it = _bind_res_infos.find(name);
		if (it != _bind_res_infos.end())
		{
			memcpy(_p_cbuffer->GetData() + ShaderBindResourceInfo::GetVariableOffset(it->second), &value, sizeof(f32));
		}
	}

	void ComputeShader::SetVector(const String& name, Vector4f vector)
	{
		auto it = _bind_res_infos.find(name);
		if (it != _bind_res_infos.end())
		{
			memcpy(_p_cbuffer->GetData() + ShaderBindResourceInfo::GetVariableOffset(it->second), &vector, sizeof(Vector4f));
		}
	}

	bool ComputeShader::Compile()
	{
		_is_compiling.store(true);
		if (!FileManager::Exist(_src_file_path))
		{
			g_pLogMgr->LogErrorFormat(L"ComputeShader::Compile: source:{} not exist!",_src_file_path);
			return false;
		}
		bool is_succeed = RHICompileImpl();
		_is_compiling.store(false);
		return is_succeed;
	}

	bool ComputeShader::RHICompileImpl()
	{
		return false;
	}

	//---------------------------------------------------------------------ComputeShader-------------------------------------------------------------------
}
