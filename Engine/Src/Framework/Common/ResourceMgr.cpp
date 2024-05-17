#include "pch.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/Material.h"
#include "Framework/Parser/AssetParser.h"
#include "Framework/Common/TimeMgr.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/ThreadPool.h"
#include "GlobalMarco.h"
#include "Framework/Common/LogMgr.h"
#include "Framework/Common/FileManager.h"
#include "Render/GraphicsContext.h"

#include "RHI/DX12/D3DTexture.h"
#include "Framework/Math/Random.h"


namespace Ailu
{
	namespace fs = std::filesystem;
	static void TraverseDirectory(const fs::path& directoryPath, std::set<fs::path>& path_set)
	{
		for (const auto& entry : fs::directory_iterator(directoryPath))
		{
			if (fs::is_directory(entry.status()))
			{
				TraverseDirectory(entry.path(), path_set);
			}
			else if (fs::is_regular_file(entry.status()))
			{
				path_set.insert(entry.path());
			}
		}
	}

	int ResourceMgr::Initialize()
	{
		{
			u8* default_data = new u8[4 * 4 * 4];
			memset(default_data, 255, 64);
			auto default_white = Texture2D::Create(4, 4, false);
			default_white->SetPixelData(default_data, 0);
			default_white->Name("default_white");
			default_white->Apply();
			_lut_global_texture2d[default_white->ID()] = _global_resources.emplace(std::make_pair(L"Runtime/default_white", default_white)).first;
			
			memset(default_data, 0, 64);
			for (int i = 3; i < 64; i += 4)
				default_data[i] = 255;
			auto default_black = Texture2D::Create(4, 4, false);
			default_black->SetPixelData(default_data, 0);
			default_white->Name("default_black");
			default_black->Apply();
			_lut_global_texture2d[default_black->ID()] = _global_resources.emplace(std::make_pair(L"Runtime/default_white", default_black)).first;
			memset(default_data, 128, 64);
			for (int i = 3; i < 64; i += 4)
				default_data[i] = 255;
			auto default_gray = Texture2D::Create(4, 4, false);
			default_gray->SetPixelData(default_data, 0);
			default_white->Name("default_gray");
			default_gray->Apply();
			_lut_global_texture2d[default_gray->ID()] = _global_resources.emplace(std::make_pair(L"Runtime/default_gray", default_black)).first;
			memset(default_data, 255, 64);
			for (int i = 0; i < 64; i += 4)
			{
				default_data[i] = 128;
				default_data[i + 1] = 128;
			}
			auto default_normal = Texture2D::Create(4, 4, false);
			default_normal->SetPixelData(default_data, 0);
			default_white->Name("default_normal");
			default_normal->Apply();
			_lut_global_texture2d[default_normal->ID()] = _global_resources.emplace(std::make_pair(L"Runtime/default_normal", default_black)).first;
			DESTORY_PTRARR(default_data);
			Texture::s_p_default_white = default_white.get();
			Texture::s_p_default_black = default_black.get();
			Texture::s_p_default_gray = default_gray.get();
			Texture::s_p_default_normal = default_normal.get();
			ImportSetting setting("no_name_id", false);
			Load<Texture2D>(EnginePath::kEngineIconPathW + L"folder.png", &setting);
			Load<Texture2D>(EnginePath::kEngineIconPathW + L"file.png"  , &setting);
			Load<Texture2D>(EnginePath::kEngineIconPathW + L"3d.png"    , &setting);
			Load<Texture2D>(EnginePath::kEngineIconPathW + L"shader.png", &setting);
			Load<Texture2D>(EnginePath::kEngineIconPathW + L"image.png" , &setting);
			Load<Texture2D>(EnginePath::kEngineIconPathW + L"scene.png" , &setting);
		}
		auto skybox = MakeRef<Material>(Load<Shader>(WString(L"Shaders/skybox.hlsl")), "Skybox");
		skybox->IsInternal(false);
		_lut_global_materials[skybox->ID()] = _global_resources.emplace(std::make_pair(L"Runtime/Material/Skybox", skybox)).first;
		Shader::s_p_defered_standart_lit = Load<Shader>(L"Shaders/defered_standard_lit.hlsl");
		auto standard_lit = MakeRef<Material>(Shader::s_p_defered_standart_lit, "StandardLit");
		_lut_global_materials[standard_lit->ID()] = _global_resources.emplace(std::make_pair(L"Runtime/Material/StandardLit", standard_lit)).first;
		
		auto blit = MakeRef<Material>(Load<Shader>(L"Shaders/blit.hlsl"), "Blit");
		_lut_global_materials[blit->ID()] = _global_resources.emplace(std::make_pair(L"Runtime/Material/Blit", blit)).first;
		auto cubemap_gen = MakeRef<Material>(Load<Shader>(L"Shaders/cubemap_gen.hlsl"), "CubemapGen");
		_lut_global_materials[cubemap_gen->ID()] = _global_resources.emplace(std::make_pair(L"Runtime/Material/CubemapGen", cubemap_gen)).first;
		auto filter_irradiance = MakeRef<Material>(Load<Shader>(L"Shaders/filter_irradiance.hlsl"), "EnvmapFilter");
		_lut_global_materials[filter_irradiance->ID()] = _global_resources.emplace(std::make_pair(L"Runtime/Material/EnvmapFilter", filter_irradiance)).first;

		LoadAssetDB();

		//auto tex0 = LoadTexture(EnginePath::kEngineTexturePath + "MyImage01.jpg", "default");
		//TexturePool::Add(tex0->AssetPath(), tex0);

		//ImportResource(PathUtils::GetResSysPath(EnginePath::kEngineIconPathW + L"folder.png"), ImportSetting("Editor/folder", false));
		//ImportResource(PathUtils::GetResSysPath(EnginePath::kEngineIconPathW + L"file.png"), ImportSetting("Editor/file_us", false));
		//ImportResource(PathUtils::GetResSysPath(EnginePath::kEngineIconPathW + L"3d.png"), ImportSetting("Editor/3d", false));
		//ImportResource(PathUtils::GetResSysPath(EnginePath::kEngineIconPathW + L"shader.png"), ImportSetting("Editor/shader", false));
		//ImportResource(PathUtils::GetResSysPath(EnginePath::kEngineIconPathW + L"image.png"), ImportSetting("Editor/image", false));
		//ImportResource(PathUtils::GetResSysPath(EnginePath::kEngineIconPathW + L"scene.png"), ImportSetting("Editor/scene", false));
		TextureImportSetting setting;
		setting._generate_mipmap = false;
		setting._is_copy = false;
		setting._name_id = "Innernal/IBLLUT";
		Load<Texture2D>(EnginePath::kEngineTexturePathW + L"ibl_brdf_lut.png", &setting);
		//ImportResource(PathUtils::GetResSysPath(EnginePath::kEngineTexturePathW + L"ibl_brdf_lut.png"), setting);

		ImportResource(PathUtils::GetResSysPath(L"Meshs/sphere.fbx"), MeshImportSetting("sphere", false, false));
		ImportResource(PathUtils::GetResSysPath(L"Meshs/plane.fbx"), MeshImportSetting("plane", false, false));
		ImportResource(PathUtils::GetResSysPath(L"Meshs/cube.fbx"), MeshImportSetting("cube", false, false));
		ImportResource(PathUtils::GetResSysPath(L"Meshs/monkey.fbx"), MeshImportSetting("monkey", false, false));
		Mesh::s_p_cube = static_cast<Mesh*>(_global_resources[L"Meshs/cube.fbx"].get());
		Mesh::s_p_shpere = static_cast<Mesh*>(_global_resources[L"Meshs/sphere.fbx"].get());

		auto FullScreenQuad = MakeRef<Mesh>("FullScreenQuad");
		Vector3f* vertices = new Vector3f[4]{ { -1.0f, 1.0f, 0.0f },
		{  1.0f, 1.0f, 0.0f },
		{ -1.0f,  -1.0f, 0.0f },
		{  1.0f,  -1.0f, 0.0f } };
		u32* indices = new u32[6]{ 0, 1, 2, 1, 3, 2 };
		Vector2f* uv0 = new Vector2f[4]{ {0.f,0.f}, {1.f,0.f},{0.f,1.f} ,{1.f,1.f} };
		FullScreenQuad->_vertex_count = 4;
		FullScreenQuad->SetVertices(vertices);
		FullScreenQuad->AddSubmesh(indices, 6);
		FullScreenQuad->SetUVs(uv0, 0);
		FullScreenQuad->BuildRHIResource();
		_lut_global_meshs[FullScreenQuad->ID()] = _global_resources.emplace(std::make_pair(L"Runtime/Mesh/FullScreenQuad", FullScreenQuad)).first;
		Mesh::s_p_quad = FullScreenQuad.get();
		g_pThreadTool->Enqueue(&ResourceMgr::WatchDirectory, this);
		return 0;
	}
	void ResourceMgr::Finalize()
	{
		for (auto& [guid, asset] : s_asset_db)
		{
			SaveAsset(asset);
		}
		SaveAssetDB();
	}
	void ResourceMgr::Tick(const float& delta_time)
	{
		SubmitResourceTask();
	}

	void ResourceMgr::SaveScene(Scene* scene, std::string& scene_path)
	{

	}

	Scene* ResourceMgr::LoadScene(std::string& scene_path)
	{
		return nullptr;
	}

	void ResourceMgr::SaveAsset(const WString& sys_path, Material* mat)
	{
		using std::endl;
		WString asset_path = PathUtils::ExtractAssetPath(sys_path);
		std::ofstream out_mat(sys_path, std::ios::out | std::ios::trunc);
		if (out_mat.is_open())
		{
			auto asset = new Asset(Guid::Generate(), EAssetType::kMaterial, asset_path);
			while (ExistInAssetDB(asset))
			{
				asset->AssignGuid(Guid::Generate());
			}
			out_mat << "guid: " << asset->GetGuid().ToString() << endl;
			out_mat << "type: " << EAssetType::ToString(asset->_asset_type) << endl;
			out_mat << "name: " << mat->Name() << endl;
			out_mat.close();
			SaveMaterialImpl(asset_path, mat);
			mat->AttachToAsset(asset);
			AddToAssetDB(asset);
		}
		else
		{
			LOG_WARNING(L"Save material to {} failed", sys_path);
		}
	}

	void ResourceMgr::SaveAsset(const Asset* asset)
	{
		if (asset->_p_inst_asset == nullptr)
		{
			LOG_WARNING("Asset: {} save failed!it hasn't a instanced asset!");
			return;
		}
		switch (asset->_asset_type)
		{
		case Ailu::EAssetType::kMesh:
			break;
		case Ailu::EAssetType::kMaterial:
		{
			using std::endl;
			std::wofstream out_mat(asset->_sys_path, std::ios::out | std::ios::trunc);
			if (out_mat.is_open())
			{
				out_mat << "guid: " << ToWChar(asset->GetGuid().ToString()) << endl;
				out_mat << "type: " << EAssetType::ToString(EAssetType::kMaterial) << endl;
				auto mat = dynamic_cast<Material*>(asset->_p_inst_asset);
				out_mat << "name: " << ToWChar(mat->Name()) << endl;
				out_mat.close();
				SaveMaterialImpl(asset->_sys_path, mat);
			}
			else
			{
				LOG_WARNING(L"Save material to {} failed", asset->_sys_path);
			}
		}
		break;
		case Ailu::EAssetType::kTexture2D:
			break;
		}
	}

	WString ResourceMgr::GetObjectSysPath(Object* obj)
	{
		if (s_object_sys_path_map.contains(obj->ID()))
			return s_object_sys_path_map[obj->ID()];
		return L"";
	}

	void ResourceMgr::SetObjectSysPath(Object* obj, const WString& new_sys_path)
	{
		s_object_sys_path_map[obj->ID()] = new_sys_path;
	}

	Ref<Material> ResourceMgr::LoadMaterial(const WString& asset_path)
	{
		WString sys_path = PathUtils::GetResSysPath(asset_path);
		std::ifstream file(sys_path);
		if (!file.is_open())
		{
			LOG_ERROR(L"Load material with path: {} failed!", sys_path);
			return nullptr;
		}
		std::vector<String> lines{};
		std::string line;
		int line_count = 0u;
		while (std::getline(file, line))
		{
			lines.emplace_back(line);
			++line_count;
		}
		file.close();
		AL_ASSERT_MSG(line_count <= 3, "material file error");
		String key{}, guid_str{}, type_str{}, name{}, shader_path{};
		FormatLine(lines[0], key, guid_str);
		FormatLine(lines[1], key, type_str);
		FormatLine(lines[2], key, name);
		FormatLine(lines[3], key, shader_path);
		auto mat = MakeRef<Material>(Load<Shader>(ToWChar(shader_path)) ,name);
		std::string cur_type{ " " };
		std::string prop_type{ "prop_type" };
		for (int i = 4; i < lines.size(); ++i)
		{
			String k{}, v{};
			FormatLine(lines[i], k, v);
			if (k == prop_type && cur_type != v)
			{
				cur_type = v;
				continue;
			}
			if (cur_type == ShaderPropertyType::Float)
			{
				float f;
				if (sscanf_s(v.c_str(), "%f", &f) == 1)
					mat->SetFloat(k, f);
				else LOG_WARNING("Load material: {},property {} failed!", mat->_name, k);
			}
			else if (cur_type == ShaderPropertyType::Vector)
			{
				Vector4f vec{};
				if (sscanf_s(v.c_str(), "%f,%f,%f,%f", &vec.r, &vec.g, &vec.b, &vec.a) == 4)
					mat->SetVector(k, vec);
				else LOG_WARNING("Load material: {},property {} failed!", mat->_name, k);
			}
			else if (cur_type == ShaderPropertyType::Uint)
			{
				uint32_t u;
				if (sscanf_s(v.c_str(), "%u", &u) == 1)
					mat->SetUint(k, u);
				else LOG_WARNING("Load material: {}, property {} failed!", mat->_name, k);
			}
			else if (cur_type == ShaderPropertyType::Texture2D)
			{
				if (v.empty() || v == "null guid") continue;
				auto asset_path = GetAssetPath(Guid(v));
				if (!asset_path.empty())
				{
					mat->SetTexture(k,Load<Texture2D>(asset_path));
				}
				else
				{
					LOG_WARNING("Load material: {}, property {} failed!", mat->_name, k);
				}
			}
		}
		return mat;
	}

	Ref<Shader> ResourceMgr::LoadShader(const WString& asset_path, const ImportSetting* setting)
	{
		auto& shader_import_setting = setting ? static_cast<const ShaderImportSetting&>(*setting) : ShaderImportSetting::Default();
		auto sys_path = PathUtils::GetResSysPath(asset_path);
		auto shader = Shader::Create(sys_path, shader_import_setting._vs_entry,shader_import_setting._ps_entry);
		return shader;
	}

	bool ResourceMgr::ExistInAssetDB(const Asset* asset)
	{
		return s_asset_db.contains(asset->GetGuid());
	}

	bool ResourceMgr::ExistInAssetDB(const WString& asset_path)
	{
		return s_asset_looktable.contains(asset_path);
	}

	WString ResourceMgr::GetAssetPath(const Guid& guid)
	{
		if (s_asset_db.contains(guid))
			return s_asset_db.at(guid)->_asset_path;
		return L"";
	}

	Asset* ResourceMgr::GetAsset(const WString& asset_path) const
	{
		if (s_asset_looktable.contains(asset_path))
		{
			return s_asset_db.contains(s_asset_looktable[asset_path]) ? s_asset_db.at(s_asset_looktable[asset_path]) : nullptr;
		}
		return nullptr;
	}

	Vector<Asset*> ResourceMgr::GetAssets(const ISearchFilter& filter) const
	{
		Vector<Asset*> assets;
		for(auto& [guid, asset] : s_asset_db)
		{
			if(filter.Filter(asset))
				assets.emplace_back(asset);
		}
		return assets;
	}

	void ResourceMgr::AddToAssetDB(Asset* asset, bool override)
	{
		if (ExistInAssetDB(asset))
		{
			if (override)
			{
				s_asset_db[asset->GetGuid()] = asset;
				s_asset_looktable[asset->_asset_path] = asset->GetGuid();
			}
			else
			{
				g_pLogMgr->LogWarningFormat(L"Asset {} already exist in database,it will be destory...", asset->_asset_path);
				//不插入的话要销毁，db中的指针最后会统一销毁
				delete asset;
			}
		}
		else
		{
			s_asset_db[asset->GetGuid()] = asset;
			s_asset_looktable[asset->_asset_path] = asset->GetGuid();
		}
	}

	void ResourceMgr::LoadAssetDB()
	{
		std::wifstream file(kAssetDatabasePath);
		if (!file.is_open())
		{
			LOG_ERROR("Load asset_db with path: {} failed!", kAssetDatabasePath);
			return;
		}
		WString line;
		while (std::getline(file, line))
		{
			std::vector<WString> tokens;
			std::wistringstream lineStream(line);
			WString token;
			while (std::getline(lineStream, token, L','))
				tokens.push_back(token);
			String guid = ToChar(tokens[0]);
			WString asset_path = tokens[1];
			EAssetType::EAssetType asset_type = EAssetType::FromString(ToChar(tokens[2]));
			//先占位，不进行资源加载，实际有使用时才加载。
			AddToAssetDB(new Asset(Guid(guid), asset_type, asset_path));
			//if(asset_type == EAssetType::kMaterial)
			//{
			//	auto mat = LoadAsset<Material>(PathUtils::ExtractAssetPath(sys_path));
			//	if (mat)
			//	{
			//		g_pLogMgr->LogFormat(L"Load {} asset at path {}", EAssetType::ToString(asset_type), sys_path);
			//		if (mat->GetShader()->Name() == "shaders")
			//			mat->IsInternal(true);
			//	}
			//	else
			//	{
			//		static std::source_location sl;
			//		g_pLogMgr->LogErrorFormat(sl, L"Load {} asset at path {} failed!", EAssetType::ToString(asset_type), sys_path);
			//	}
			//}
		}
		file.close();
	}

	void ResourceMgr::SaveAssetDB()
	{
		std::wofstream file(kAssetDatabasePath, std::ios::out | std::ios::trunc);
		for (auto& [guid, asset] : s_asset_db)
		{
			if(asset->_p_inst_asset)
				file << ToWChar(guid.ToString()) << "," << asset->_asset_path << "," << EAssetType::ToString(asset->_asset_type) << std::endl;
			delete asset;
		}
	}

	void ResourceMgr::FormatLine(const String& line, String& key, String& value)
	{
		std::istringstream iss(line);
		if (std::getline(iss, key, ':') && std::getline(iss, value))
		{
			key.erase(key.begin(), std::find_if(key.begin(), key.end(), [](int ch) {return !std::isspace(ch); }));
			value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](int ch) {return !std::isspace(ch); }));
		}
	}

	void ResourceMgr::SaveMaterialImpl(const WString& sys_path, Material* mat)
	{
		using std::endl;
		std::multimap<std::string, SerializableProperty*> props{};
		//为了写入通用资产头信息，暂时使用追加方式打开
		std::ofstream out_mat(sys_path, std::ios::out | std::ios::app);
		out_mat << "shader_path: " << ToChar(GetObjectSysPath(mat->_p_shader));
		for (auto& prop : mat->_properties)
		{
			props.insert(std::make_pair(GetSerializablePropertyTypeStr(prop.second._type), &prop.second));
		}
		out_mat << endl;
		auto float_props = mat->GetAllFloatValue();
		auto vector_props = mat->GetAllVectorValue();
		auto uint_props = mat->GetAllUintValue();
		//auto tex_props = mat->GetAllTexture();
		out_mat << "  prop_type: " << "Uint" << endl;
		for (auto& [name, value] : uint_props)
		{
			out_mat << "    " << name << ": " << value << endl;
		}
		out_mat << "  prop_type: " << "Float" << endl;
		for (auto& [name, value] : float_props)
		{
			out_mat << "    " << name << ": " << value << endl;
		}
		out_mat << "  prop_type: " << "Vector" << endl;
		for (auto& [name, value] : vector_props)
		{
			out_mat << "    " << name << ": " << value << endl;
		}
		out_mat << "  prop_type: " << "Texture2D" << endl;
		for (auto& [prop_name, prop] : props)
		{
			if (prop->_type == ESerializablePropertyType::kTexture2D)
			{
				auto tex_opti = SerializableProperty::GetProppertyValue<std::tuple<u8, Texture*>>(*prop);
				Guid tex_guid;
				if (tex_opti.has_value() && std::get<1>(tex_opti.value()))
				{
					auto [bind_slot, tex] = tex_opti.value();
					if(tex->GetAsset())
						tex_guid = tex->GetAsset()->GetGuid();
				}
				else
					tex_guid = Guid::EmptyGuid();
				out_mat << "    " << prop->_value_name << ": " << tex_guid.ToString() << endl;
			}
		}
		out_mat.close();
		LOG_WARNING(L"Save material to {}", sys_path);
	}

	Ref<Texture2D> ResourceMgr::LoadTexture(const WString& asset_path, const ImportSetting* setting)
	{
		auto image_import_setting = dynamic_cast<const TextureImportSetting*>(setting);
		image_import_setting = image_import_setting ? image_import_setting : &TextureImportSetting::Default();
		auto sys_path = PathUtils::GetResSysPath(asset_path);
		String ext = fs::path(ToChar(asset_path.c_str())).extension().string();
		Scope<ITextureParser> tex_parser = nullptr;
		if (kLDRImageExt.contains(ext))
		{
			tex_parser = std::move(TStaticAssetLoader<EResourceType::kImage, EImageLoader>::GetParser(EImageLoader::kPNG));
		}
		else if (kHDRImageExt.contains(ext))
		{
			tex_parser = std::move(TStaticAssetLoader<EResourceType::kImage, EImageLoader>::GetParser(EImageLoader::kHDR));
		}
		else {};
		AL_ASSERT(tex_parser == nullptr);
		g_pLogMgr->LogFormat(L"Start load image file {}...", sys_path);
		auto tex = tex_parser->Parser(sys_path);
		g_pGfxContext->SubmitRHIResourceBuildTask([=]() {tex->Apply(); });
		return tex;
	}

	Ref<CubeMap> ResourceMgr::LoadTexture(const Vector<WString>& asset_paths)
	{
		auto png_parser = TStaticAssetLoader<EResourceType::kImage, EImageLoader>::GetParser(EImageLoader::kPNG);
		Vector<String> sys_paths{};
		for (auto& asset_path : asset_paths)
		{
			String asset_path_n = ToChar(asset_path.c_str());
			sys_paths.emplace_back(kEngineResRootPath + asset_path_n);
		}
		auto tex = png_parser->Parser(sys_paths);
		//tex->AssetPath(sys_paths);
		return tex;
	}

	List<Ref<Mesh>> ResourceMgr::LoadMesh(const WString& asset_path)
	{
		static auto parser = TStaticAssetLoader<EResourceType::kStaticMesh, EMeshLoader>::GetParser(EMeshLoader::kFbx);
		auto sys_path = PathUtils::GetResSysPath(asset_path);
		g_pLogMgr->LogFormat(L"Start load mesh file {}...", sys_path);
		auto mesh_list = parser->Parser(sys_path);
		auto& imported_mat_infos = parser->GetImportedMaterialInfos();
		for (auto& mesh : mesh_list)
		{
			for (auto it = imported_mat_infos.begin(); it != imported_mat_infos.end(); ++it)
			{
				mesh->AddCacheMaterial(*it);
			}
			g_pGfxContext->SubmitRHIResourceBuildTask([=]() {mesh->BuildRHIResource(); });
			SetObjectSysPath(mesh.get(), sys_path);
		}
		return mesh_list;
	}

	Ref<void> ResourceMgr::ImportResource(const WString& sys_path, const ImportSetting& setting)
	{
		return ImportResourceImpl(sys_path, &setting);
	}

	Ref<void> ResourceMgr::ImportResourceAsync(const WString& sys_path, const ImportSetting& setting, OnResourceTaskCompleted callback)
	{
		s_task_queue.emplace_back([=]() {
			callback(ImportResourceImpl(sys_path, &setting));
			return nullptr;
			});
		return nullptr;
	}

	void ResourceMgr::AddResourceTask(ResourceTask task)
	{
		s_task_queue.emplace_back(task);
	}

	void ResourceMgr::WatchDirectory()
	{
		namespace fs = std::filesystem;
		fs::path dir(kEngineResRootPath + EnginePath::kEngineShaderPath);
		std::chrono::duration<int, std::milli> sleep_duration(1000); // 1秒
		std::unordered_map<fs::path, fs::file_time_type> files;
		static auto reload_shader = [&](const fs::path& file) {
			const WString cur_path = PathUtils::FormatFilePath(file.wstring());
			for (auto it = ResourceBegin<Shader>(); it != ResourceEnd<Shader>(); it++)
			{
				
				auto shader = static_cast<Shader*>((*it).second->second.get());
				for (auto& head_file : shader->GetSourceFiles())
				{
					if (head_file == cur_path)
					{
						AddResourceTask([=]()->Ref<void> {
							if (shader->Compile())
							{
								auto mats = shader->GetAllReferencedMaterials();
								for (auto mat = mats.begin(); mat != mats.end(); mat++)
									(*mat)->ChangeShader(shader);
							}
							return nullptr;
							});
						break;
					}
				}
			}
			auto cs = ComputeShader::Get(PathUtils::ExtractAssetPath(cur_path));
			if (cs)
			{
				AddResourceTask([=]()->void* {
					cs->Compile();
					return nullptr;
					});
			}
			};
		bool is_first_execute = true;
		static std::set<fs::path> path_set{};
		static std::unordered_map<fs::path, fs::file_time_type> current_files;
		while (true)
		{
			TraverseDirectory(dir, path_set);
			for (auto& cur_path : path_set)
			{
				current_files[cur_path] = fs::last_write_time(cur_path);
			}
			for (const auto& [file, last_write_time] : current_files)
			{
				if (files.contains(file))
				{
					if (files[file] != last_write_time)
						reload_shader(file);
				}
				else if (!is_first_execute)
					reload_shader(file);
				files[file] = last_write_time;
			}
			std::this_thread::sleep_for(sleep_duration);
			is_first_execute = false;
		}
	}
	void ResourceMgr::SubmitResourceTask()
	{
		for (auto& task : s_task_queue)
		{
			g_pThreadTool->Enqueue(std::move(task));
		}
		s_task_queue.clear();
	}

	Ref<void> ResourceMgr::ImportResourceImpl(const WString& sys_path, const ImportSetting* setting)
	{
		namespace fs = std::filesystem;
		fs::path p(sys_path);
		if (!FileManager::Exist(sys_path))
		{
			g_pLogMgr->LogErrorFormat(std::source_location::current(), L"Path {} not exist on the disk!", sys_path);
			return nullptr;
		}
		auto ext = p.extension().string();
		auto new_sys_path = FileManager::GetCurDirStr();
		if (setting->_is_copy)
		{
			new_sys_path += p.filename().wstring();
			if(sys_path.find_first_of(L".") != sys_path.npos)
				FileManager::CopyFile(sys_path, new_sys_path);
			else
			{
				g_pLogMgr->LogErrorFormat(std::source_location::current(), L"Path {} is not a file!", sys_path);
			}
		}
		else
		{
			new_sys_path = sys_path;
		}
		if (!FileManager::Exist(new_sys_path))
		{
			return nullptr;
		}
		ImportInfo info;
		info._is_succeed = false;
		info._last_write_time = fs::last_write_time(p);
		info._msg = std::format("Import {}...", p.filename().string());
		info._progress = 0.0f;
		info._sys_path = new_sys_path;
		_import_infos.push_back(info);
		PathUtils::FormatFilePathInPlace(new_sys_path);
		WString asset_path = PathUtils::ExtractAssetPath(new_sys_path);
		TimeMgr time_mgr;
		Ref<void> ret_res = nullptr;
		EAssetType::EAssetType asset_type = EAssetType::kUndefined;
		if (!ext.empty())
		{
			time_mgr.Mark();
			if (ext == ".fbx" || ext == ".FBX")
			{
				asset_type = EAssetType::kMesh;
				auto mesh_list = std::move(LoadMesh(asset_path));
				if (!mesh_list.empty())
				{
					auto mesh_import_setting = dynamic_cast<const MeshImportSetting*>(setting);
					mesh_import_setting = mesh_import_setting ? mesh_import_setting : &MeshImportSetting::Default();
					for (auto& mesh : mesh_list)
					{
						//String name_id = mesh_import_setting->_name_id.empty() ? ToChar(asset_path) + "_" + mesh->Name() : mesh_import_setting->_name_id;
						WString name_id = asset_path + L"_" + ToWStr(mesh->Name().c_str());
						_lut_global_meshs[mesh->ID()] = _global_resources.emplace(std::make_pair(name_id, mesh)).first;
						if (ExistInAssetDB(asset_path))
						{
							auto asset = GetAsset(asset_path);
							mesh->AttachToAsset(asset);
						}
						else
						{
							auto asset = new Asset(asset_type, asset_path);
							mesh->AttachToAsset(asset);
							AddToAssetDB(asset);
						}
						if (mesh_import_setting->_is_import_material)
						{
							for (auto it = mesh->GetCacheMaterials().begin(); it != mesh->GetCacheMaterials().end(); it++)
							{
								auto mat = MakeRef<Material>(Shader::s_p_defered_standart_lit, it->_name);
								if (!it->_textures[0].empty())
								{
									auto albedo = ImportResource(ToWChar(it->_textures[0]));
									if (albedo != nullptr)
										mat->SetTexture(InternalStandardMaterialTexture::kAlbedo, std::static_pointer_cast<Texture>(albedo).get());
								}
								if (!it->_textures[1].empty())
								{
									auto normal = ImportResource(ToWChar(it->_textures[1]));
									if (normal != nullptr)
										mat->SetTexture(InternalStandardMaterialTexture::kNormal, std::static_pointer_cast<Texture>(normal).get());
								}
								auto mat_sys_path = std::format(L"{}{}.almat", FileManager::GetCurDirStr(), ToWChar(it->_name));
								auto mat_asset_path = PathUtils::ExtractAssetPath(mat_sys_path);
								auto asset = GetAsset(mat_asset_path);
								if (asset)
								{
									mat->AttachToAsset(asset);
								}
								else
								{
									SaveAsset(mat_sys_path, mat.get());
								}
								_lut_global_materials[mat->ID()] = _global_resources.emplace(std::make_pair(asset_path, mat)).first;
							}
						}
					}
					ret_res = std::static_pointer_cast<void>(mesh_list.front());
				}
			}
			else if (kHDRImageExt.contains(ext) || kLDRImageExt.contains(ext))
			{
				asset_type = EAssetType::kTexture2D;
				auto tex = LoadTexture(asset_path, setting);
				_lut_global_texture2d[tex->ID()] = _global_resources.emplace(std::make_pair(asset_path, tex)).first;
				ret_res = std::static_pointer_cast<void>(tex);
				if (ret_res)
				{
					if (ExistInAssetDB(asset_path))
					{
						auto asset = GetAsset(asset_path);
						tex->AttachToAsset(asset);
					}
					else
					{
						auto asset = new Asset(asset_type, asset_path);
						tex->AttachToAsset(asset);
						AddToAssetDB(asset);
					}
				}
			}
			else
			{
				g_pLogMgr->LogFormat("Haven't handled asset type: {}", ext);
			}
			g_pLogMgr->LogFormat(L"Import asset: {} succeed,cost {}ms", new_sys_path, time_mgr.GetElapsedSinceLastMark());
		}
		else
		{
			g_pLogMgr->LogFormat("Path: {} isn't valid!");
		}
		_import_infos.erase(std::remove_if(_import_infos.begin(), _import_infos.end(), [&](auto it) {return it._msg == info._msg; }), _import_infos.end());
		OnAssetDataBaseChanged();
		auto asset = GetAsset(asset_path);
		return ret_res;
	}
	void ResourceMgr::OnAssetDataBaseChanged()
	{
		for (auto& f : _asset_changed_callbacks)
		{
			f();
		}
	}
}
