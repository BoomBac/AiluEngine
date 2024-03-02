#include "pch.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/Material.h"
#include "Framework/Parser/AssetParser.h"
#include "Framework/Common/TimeMgr.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/ThreadPool.h"
#include "GlobalMarco.h"
#include "Framework/Common/LogMgr.h"
#include "Render/GraphicsContext.h"

namespace Ailu
{
	namespace fs = std::filesystem;
	static void TraverseDirectory(const fs::path& directoryPath,std::set<fs::path>& path_set)
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
		LOG_WARNING("Begin init engine internal resource...");
		g_pTimeMgr->Mark();

		u8* default_data = new u8[4 * 4 * 4];
		memset(default_data, 255, 64);
		auto default_white = Texture2D::Create(4, 4);
		default_white->FillData({ default_data });
		default_white->Name("default_white");
		default_white->AssetPath("Runtime/default_white");
		TexturePool::Add("Runtime/default_white", default_white);

		default_data = new u8[4 * 4 * 4];
		memset(default_data, 0, 64);
		for (int i = 3; i < 64; i += 4)
			default_data[i] = 255;
		auto default_black = Texture2D::Create(4, 4);
		default_black->FillData({ default_data });
		default_black->Name("default_black");
		default_black->AssetPath("Runtime/default_black");
		TexturePool::Add("Runtime/default_black", default_black);

		default_data = new u8[4 * 4 * 4];
		memset(default_data, 128, 64);
		for (int i = 3; i < 64; i += 4)
			default_data[i] = 255;
		auto default_gray = Texture2D::Create(4, 4);
		default_gray->FillData({ default_data });
		default_gray->Name("default_gray");
		default_gray->AssetPath("Runtime/default_gray");
		TexturePool::Add("Runtime/default_gray", default_gray);

		LoadAssetDB();
		auto tex0 = LoadTexture(EnginePath::kEngineTexturePath + "MyImage01.jpg","default");
		TexturePool::Add(tex0->AssetPath(), tex0);

		Vector<String> default_cubemaps{
			EnginePath::kEngineTexturePath + "Cubemaps/sea/right.jpg",
			EnginePath::kEngineTexturePath + "Cubemaps/sea/left.jpg",
			EnginePath::kEngineTexturePath + "Cubemaps/sea/top.jpg",
			EnginePath::kEngineTexturePath + "Cubemaps/sea/bottom.jpg",
			EnginePath::kEngineTexturePath + "Cubemaps/sea/front.jpg",
			EnginePath::kEngineTexturePath + "Cubemaps/sea/back.jpg",
		};
		//LoadTexture(default_cubemaps, "cubemap_sea");
		LoadAsset("Materials/StandardPBR_new.alasset")->IsInternal(true);
		LoadAsset("Materials/WireFrame_new.alasset");
		auto skybox = MaterialLibrary::CreateMaterial(ShaderLibrary::Load("Shaders/skybox.hlsl"), "Skybox");
		skybox->IsInternal(false);
		skybox->OriginPath("Skybox");
		//skybox->SetTexture("SkyBox", "cubemap_sea");
		MaterialLibrary::CreateMaterial(ShaderLibrary::Load("Shaders/error_fallback.hlsl"), "Error");
		MaterialLibrary::CreateMaterial(ShaderLibrary::Load("Shaders/blit.shader"), "Blit");
		MaterialLibrary::CreateMaterial(ShaderLibrary::Load("Shaders/cubemap_gen.hlsl"), "CubemapGen");
		MaterialLibrary::CreateMaterial(ShaderLibrary::Load("Shaders/filter_irradiance.hlsl"), "EnvmapFilter");
		MaterialLibrary::CreateMaterial(ShaderLibrary::Load("Shaders/bone_test.hlsl"), "BoneWeight");


		auto parser = TStaticAssetLoader<EResourceType::kStaticMesh, EMeshLoader>::GetParser(EMeshLoader::kFbx);
		//auto anim = parser->Parser(PathUtils::GetResPath("Meshs/soldier.fbx"));
		//auto anim = parser->Parser(PathUtils::GetResPath("Meshs/Walking.fbx"));
		//auto anim = parser->Parser(PathUtils::GetResPath("Meshs/mutant.fbx"));
		//auto anim = parser->Parser(PathUtils::GetResPath("Meshs/anim.fbx"));
		//auto anim = parser->Parser(PathUtils::GetResPath("Meshs/one_bone.fbx"));
		//auto anim = parser->Parser(PathUtils::GetResPath("Meshs/two_bone.fbx"));
		//MeshPool::AddMesh("anim", anim.front());
		auto new_mesh = parser->Parser(PathUtils::GetResPath("Meshs/sphere.fbx")).front();
		new_mesh->BuildRHIResource();
		MeshPool::AddMesh("sphere", new_mesh);
		new_mesh = parser->Parser(PathUtils::GetResPath("Meshs/plane.fbx")).front();
		new_mesh->BuildRHIResource();
		MeshPool::AddMesh("plane", new_mesh);
		new_mesh = parser->Parser(PathUtils::GetResPath("Meshs/cube.fbx")).front();
		new_mesh->BuildRHIResource();
		MeshPool::AddMesh("cube", new_mesh);
		new_mesh = parser->Parser(PathUtils::GetResPath("Meshs/monkey.fbx")).front();
		new_mesh->BuildRHIResource();
		//MeshPool::AddMesh("cone", parser->Parser(GetResPath("Meshs/cone.fbx")).front());
		//MeshPool::AddMesh("cylinder", parser->Parser(GetResPath("Meshs/cylinder.fbx")).front());
		MeshPool::AddMesh("monkey", new_mesh);
		//MeshPool::AddMesh("torus", parser->Parser(GetResPath("Meshs/torus.fbx")).front());
		auto FullScreenQuad = MakeRef<Mesh>("FullScreenQuad");
		Vector<Vector3f> vertices = {
		{ -1.0f, 1.0f, 0.0f },
		{  1.0f, 1.0f, 0.0f },
		{ -1.0f,  -1.0f, 0.0f },
		{  1.0f,  -1.0f, 0.0f }
		};
		Vector<u32> indices = {0, 1, 2, 1, 3, 2};
		Vector<Vector2f> uv0 = { {0.f,0.f}, {1.f,0.f},{0.f,1.f} ,{1.f,1.f} };
		FullScreenQuad->_vertex_count = 4;
		FullScreenQuad->_index_count = 6;
		FullScreenQuad->SetVertices(vertices.data());
		FullScreenQuad->SetIndices(indices.data());
		FullScreenQuad->SetUVs(uv0.data(),0);
		FullScreenQuad->BuildRHIResource();
		MeshPool::AddMesh("FullScreenQuad", FullScreenQuad);

		auto png_parser = TStaticAssetLoader<EResourceType::kImage, EImageLoader>::GetParser(EImageLoader::kPNG);
		auto tga_parser = TStaticAssetLoader<EResourceType::kImage, EImageLoader>::GetParser(EImageLoader::kTGA);


		for (auto& [name, tex] : TexturePool::GetAllResourceMap())
		{
			LOG_INFO("texture: {}", name);
		}

		LOG_WARNING("Finish after {}ms", g_pTimeMgr->GetElapsedSinceLastMark());
		g_pLogMgr->Log("ResourceMgr end");
		g_thread_pool->Enqueue(&ResourceMgr::WatchDirectory, this);
		return 0;
	}
	void ResourceMgr::Finalize()
	{
		for (auto& [asset_path, asset] : s_all_asset)
		{
			SaveAsset(*asset.get());
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

	void ResourceMgr::SaveAsset(const std::string& path, Material* mat)
	{
		using std::endl;
		String sys_path = PathUtils::IsSystemPath(path)? path : PathUtils::GetResPath(path);
		String asset_path = PathUtils::ExtractAssetPath(path);
		auto asset = MakeScope<Asset>(asset_path, sys_path, Guid::Generate().ToString(), EAssetType::kMaterial);
		asset->_p_inst_asset = mat;
		AddToAssetDB(*asset.get());
		std::ofstream out_mat(sys_path, std::ios::out | std::ios::trunc);
		if (out_mat.is_open())
		{
			out_mat << "guid: " << asset->_guid.ToString() << endl;
			out_mat << "type: " << AssetType::kMaterial << endl;
			out_mat << "name: " << mat->Name() << endl;
			out_mat.close();
			SaveMaterialImpl(asset_path, mat);
		}
		else
		{
			LOG_WARNING("Save material to {} failed", sys_path);
		}
		s_all_asset[mat->Name()] = std::move(asset);
	}

	void ResourceMgr::SaveAsset(const Asset& asset)
	{
		if (asset._p_inst_asset == nullptr)
		{
			LOG_WARNING("Asset: {} save failed!it hasn't a instanced asset!");
			return;
		}
		switch (asset._asset_type)
		{
		case Ailu::EAssetType::kMesh:
			break;
		case Ailu::EAssetType::kMaterial:
		{
			using std::endl;
			String sys_path = PathUtils::GetResPath(asset._asset_path);
			AddToAssetDB(asset);
			std::ofstream out_mat(sys_path, std::ios::out | std::ios::trunc);
			if (out_mat.is_open())
			{
				out_mat << "guid: " << asset._guid.ToString() << endl;
				out_mat << "type: " << AssetType::kMaterial << endl;
				auto mat = reinterpret_cast<Material*>(asset._p_inst_asset);
				out_mat << "name: " << mat->Name() << endl;
				out_mat.close();
				SaveMaterialImpl(asset._asset_path, mat);
			}
			else
			{
				LOG_WARNING("Save material to {} failed", sys_path);
			}
		}
		break;
		case Ailu::EAssetType::kTexture2D:
			break;
		}
	}

	bool ResourceMgr::ExistInAssetDB(const Guid& guid)
	{
		return s_asset_db.contains(guid.ToString());
	}

	void ResourceMgr::AddToAssetDB(const Asset& asset, bool override)
	{
		if (ExistInAssetDB(asset._guid))
		{
			if (override)
			{
				s_asset_db.insert(std::make_pair(asset._guid.ToString(), std::make_pair(asset._asset_path, AssetType::GetTypeString(asset._asset_type))));
			}
		}
		else s_asset_db.insert(std::make_pair(asset._guid.ToString(), std::make_pair(asset._asset_path, AssetType::GetTypeString(asset._asset_type))));
	}

	void ResourceMgr::LoadAssetDB()
	{
		std::ifstream file(kAssetDatabasePath);
		if (!file.is_open())
		{
			LOG_ERROR("Load asset_db with path: {} failed!", kAssetDatabasePath);
			return;
		}
		String line;
		while (std::getline(file, line))
		{
			std::vector<std::string> tokens;
			std::istringstream lineStream(line);
			std::string token;
			while (std::getline(lineStream, token, ',')) tokens.push_back(token);
			for (const std::string& value : tokens) s_asset_db.insert(std::make_pair(tokens[0], std::make_pair(tokens[1], tokens[2])));
		}
		file.close();
		for (auto& it : s_asset_db)
		{
			String type = std::get<1>(it.second);
			String path = std::get<0>(it.second);
			auto mat = LoadAsset(path);
			if (mat)
			{
				g_pLogMgr->LogFormat("Load {} asset at path {}",type,path);
				if (mat->GetShader()->Name() == "shaders")
					mat->IsInternal(true);
			}
			else
			{
				static std::source_location sl;
				g_pLogMgr->LogErrorFormat(sl, "Load {} asset at path {} failed!", type, path);
			}
		}
	}

	void ResourceMgr::SaveAssetDB()
	{
		std::ofstream file(kAssetDatabasePath, std::ios::out | std::ios::trunc);
		for (auto& [guid, asset] : s_asset_db)
		{
			auto& [sys_path, asset_type] = asset;
			file << guid << "," << sys_path << "," << asset_type << std::endl;
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

	void ResourceMgr::SaveMaterialImpl(const String& asset_path, Material* mat)
	{
		using std::endl;
		auto sys_path = PathUtils::GetResPath(asset_path);
		std::multimap<std::string, SerializableProperty*> props{};
		//为了写入通用资产头信息，暂时使用追加方式打开
		std::ofstream out_mat(sys_path, std::ios::out | std::ios::app);
		out_mat << "shader_path: " << ShaderLibrary::GetShaderPath(mat->_p_shader->Name());
		for (auto& prop : mat->_properties)
		{
			props.insert(std::make_pair(GetSerializablePropertyTypeStr(prop.second._type), &prop.second));
		}
		out_mat << endl;
		auto float_props = mat->GetAllFloatValue();
		auto vector_props = mat->GetAllVectorValue();
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
				auto tex = SerializableProperty::GetProppertyValue<std::tuple<u8, Ref<Texture>>>(*prop);
				String tex_path;
				if (tex.has_value() && std::get<1>(tex.value()))
					tex_path = std::reinterpret_pointer_cast<Texture2D>(std::get<1>(tex.value()))->AssetPath();
				else
					tex_path = "none";
				out_mat << "    " << prop->_value_name << ": " << tex_path << endl;
			}
		}
		out_mat.close();
		LOG_WARNING("Save material to {}", sys_path);
	}

	Ref<Material> ResourceMgr::LoadMaterial(std::string path)
	{
		std::ifstream file(path);
		if (!file.is_open())
		{
			LOG_ERROR("Load material with path: {} failed!", path);
			return nullptr;
		}
		std::vector<std::tuple<std::string, std::string>> lines{};
		std::string line;
		int line_count = 0u;
		while (std::getline(file, line))
		{
			std::istringstream iss(line);
			std::string key;
			std::string value;
			if (std::getline(iss, key, ':') && std::getline(iss, value))
			{
				key.erase(key.begin(), std::find_if(key.begin(), key.end(), [](int ch) {return !std::isspace(ch); }));
				value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](int ch) {return !std::isspace(ch); }));
				if (line_count == 0 && value != AssetType::kMaterial)
				{
					LOG_ERROR("Load material with path: {} failed,asset isn't mateiral", path);
					return nullptr;
				}
				lines.emplace_back(std::make_pair(key, value));
				++line_count;
			}
		}
		file.close();
		auto mat = MaterialLibrary::CreateMaterial(ShaderLibrary::Load(std::get<1>(lines[1])), PathUtils::GetFileName(path));
		std::string cur_type{ " " };
		std::string prop_type{ "prop_type" };
		for (int i = 2; i < lines.size(); ++i)
		{
			auto& [k, v] = lines[i];
			if (k == prop_type && cur_type != v)
			{
				cur_type = v;
				continue;
			}
			auto prop = mat->_properties.find(k);
			if (cur_type == ShaderPropertyType::Color)
			{
				Vector4f vec{};
				if (sscanf_s(v.c_str(), "%f,%f,%f,%f", &vec.r, &vec.g, &vec.b, &vec.a) == 4) memcpy(prop->second._value_ptr, &vec, sizeof(Vector4f));
				else LOG_WARNING("Load material: {},property {} failed!", mat->_name, prop->first);
			}
			else if (cur_type == ShaderPropertyType::Float)
			{
				float roughness;
				if (sscanf_s(v.c_str(), "%f", &roughness) == 1) memcpy(prop->second._value_ptr, &roughness, sizeof(float));
				else LOG_WARNING("Load material: {},property {} failed!", mat->_name, prop->first);
			}
			//else if (cur_type == ShaderPropertyType::Texture2D)
			//{
			//	if (v == "none") continue;
			//	//if (k == InternalStandardMaterialTexture::kAlbedo)
			//	//	mat->MarkTextureUsed({ ETextureUsage::kAlbedo }, true);
			//	//else if (k == InternalStandardMaterialTexture::kMetallic)
			//	//	mat->MarkTextureUsed({ ETextureUsage::kMetallic}, true);
			//	//else if (k == InternalStandardMaterialTexture::kSpecular)
			//	//	mat->MarkTextureUsed({ ETextureUsage::kSpecular }, true);
			//	//else if (k == InternalStandardMaterialTexture::k)
			//	//	mat->MarkTextureUsed({ ETextureUsage::kMetallic }, true);
			//	//else if (k == InternalStandardMaterialTexture::kMetallic)
			//	//	mat->MarkTextureUsed({ ETextureUsage::kMetallic }, true);
			//	mat->SetTexture(k, v);
			//}
		}
		return mat;
	}

	Ref<Material> ResourceMgr::LoadMaterial(const Vector<String>& blob)
	{
		String key{}, name{}, shader_path{};
		FormatLine(blob[2], key, name);
		FormatLine(blob[3], key, shader_path);
		auto mat = MakeRef<Material>(ShaderLibrary::Load(shader_path), name);
		std::string cur_type{ " " };
		std::string prop_type{ "prop_type" };
		for (int i = 4; i < blob.size(); ++i)
		{
			String k{}, v{};
			FormatLine(blob[i], k, v);
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
			else if (cur_type == ShaderPropertyType::Texture2D)
			{
				if (v.empty() || v == "none") continue;
				if (!TexturePool::Contain(v))
				{
					auto tex = g_pResourceMgr->LoadTexture(v);
					TexturePool::Add(tex->AssetPath(),tex);
				}
				mat->SetTexture(k, v);
			}
		}
		return mat;
	}

	Ref<Mesh> ResourceMgr::LoadMeshImpl(const String& asset_path)
	{
		static auto parser = TStaticAssetLoader<EResourceType::kStaticMesh, EMeshLoader>::GetParser(EMeshLoader::kFbx);
		auto sys_path = PathUtils::GetResPath(asset_path);
		auto mesh_list = parser->Parser(sys_path);
		for (auto& mesh : mesh_list)
		{
			MeshPool::AddMesh(asset_path + ":" + mesh->Name(), mesh);
		}
		return mesh_list.front();
	}

	Ref<Texture2D> ResourceMgr::LoadTexture(const String& asset_path, String name)
	{
		if (!TexturePool::Contain(asset_path))
		{
			Scope<ITextureParser> parser;
			if (HDRParser::IsHDRFormat(asset_path))
				parser = std::move(TStaticAssetLoader<EResourceType::kImage, EImageLoader>::GetParser(EImageLoader::kHDR));
			else
				parser = TStaticAssetLoader<EResourceType::kImage, EImageLoader>::GetParser(EImageLoader::kPNG);
			auto tex = parser->Parser(kEngineResRootPath + asset_path, 10);
			if(!name.empty()) 
				tex->Name(name);
			return tex;
		}
		return TexturePool::GetTexture2D(asset_path);
	}

	Ref<TextureCubeMap> ResourceMgr::LoadTexture(const Vector<String>& asset_paths, String name)
	{
		auto png_parser = TStaticAssetLoader<EResourceType::kImage, EImageLoader>::GetParser(EImageLoader::kPNG);
		Vector<String> sys_paths{};
		for (auto& asset_path : asset_paths)
		{
			sys_paths.emplace_back(kEngineResRootPath + asset_path);
		}
		auto tex = TexturePool::Add(name.empty() ? asset_paths[0] : name, png_parser->Parser(sys_paths));
		tex->AssetPath(asset_paths);
		return tex;
	}

	Material* ResourceMgr::LoadAsset(const String& asset_path)
	{
		if (MaterialLibrary::Contain(asset_path))
			return MaterialLibrary::GetMaterial(asset_path).get();
		String sys_path = PathUtils::GetResPath(asset_path);
		std::ifstream file(sys_path);
		if (!file.is_open())
		{
			LOG_ERROR("Load asset with path: {} failed!", sys_path);
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
		String key{}, guid_str{}, type_str{};
		FormatLine(lines[0], key, guid_str);
		FormatLine(lines[1], key, type_str);
		s_all_asset.insert(std::make_pair(asset_path, std::move(MakeScope<Asset>(asset_path, sys_path, guid_str, AssetType::GetType(type_str)))));
		auto mat = LoadMaterial(lines);
		mat->OriginPath(asset_path);
		MaterialLibrary::AddMaterial(mat, asset_path);
		s_all_asset.find(asset_path)->second->_p_inst_asset = reinterpret_cast<void*>(mat.get());
		return mat.get();
	}

	void ResourceMgr::ImportAsset(const WString& sys_path)
	{
		ImportAssetImpl(sys_path);
	}

	void ResourceMgr::ImportAssetAsync(const WString& sys_path, OnResourceTaskCompleted callback)
	{
		s_task_queue.emplace_back([=]() {
			ImportAssetImpl(sys_path);
			callback();
			});
	}

	void ResourceMgr::AddResourceTask(ResourceTask task, OnResourceTaskCompleted callback)
	{
		s_task_queue.emplace_back([=]() {
			task();
			callback();
			});
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
			const String cur_path = PathUtils::FormatFilePath(file.string());
			for (auto it = ShaderLibrary::Begin(); it != ShaderLibrary::End(); it++)
			{
				for (auto& head_file : (*it)->GetSourceFiles())
				{
					if (head_file == cur_path)
					{
						AddResourceTask([=]() {
							if ((*it)->Compile())
							{
								auto mats = (*it)->GetAllReferencedMaterials();
								for (auto mat = mats.begin(); mat != mats.end(); mat++)
									(*mat)->ChangeShader((*it));
								LOG_INFO("Compile shader {} succeed!", (*it)->Name());
							}
							else
								LOG_ERROR("Compile shader {} failed!", (*it)->Name());
							});
						break;
					}
				}
			}
			auto cs = ComputeShader::Get(PathUtils::ExtractAssetPath(cur_path));
			if (cs)
			{
				AddResourceTask([=]() {
					cs->Compile();
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
				else if(!is_first_execute)
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
			g_thread_pool->Enqueue(std::move(task));
		}
		s_task_queue.clear();
	}

	void ResourceMgr::ImportAssetImpl(const WString& sys_path)
	{
		namespace fs = std::filesystem;
		fs::path p(sys_path);
		auto ext = p.extension().string();
		TimeMgr time_mgr;
		if (!ext.empty())
		{
			time_mgr.Mark();
			if (ext == ".fbx" || ext == ".FBX")
			{
				static auto parser = TStaticAssetLoader<EResourceType::kStaticMesh, EMeshLoader>::GetParser(EMeshLoader::kFbx);
				g_pLogMgr->LogFormat(L"Start import mesh file {}",sys_path);
				for (auto& mesh : parser->Parser(sys_path))
				{
					MeshPool::AddMesh(mesh);
					g_pGfxContext->SubmitRHIResourceBuildTask([=]() {mesh->BuildRHIResource(); });
				}
			}
			else if (ext == ".png" || ext == ".PNG")
			{
				g_pLogMgr->LogFormat(L"Start import image file {}", sys_path);
				String asset_path = PathUtils::ExtractAssetPath(ToChar(sys_path.data()));
				if (!TexturePool::Contain(asset_path))
				{
					auto png_parser = TStaticAssetLoader<EResourceType::kImage, EImageLoader>::GetParser(EImageLoader::kPNG);
					auto tex = png_parser->Parser(kEngineResRootPath + asset_path, 1);
					String name = PathUtils::GetFileName(asset_path);
					if (!name.empty())
						tex->Name(name);
					TexturePool::Add(asset_path, tex);
				}
			}
			else if (ext == ".exr" || ext == ".EXR" || ext == ".hdr" || ext == ".HDR")
			{
				g_pLogMgr->LogFormat(L"Start import image file {}", sys_path);
				String asset_path = PathUtils::ExtractAssetPath(ToChar(sys_path.data()));
				if (!TexturePool::Contain(asset_path))
				{
					auto hdr_parser = TStaticAssetLoader<EResourceType::kImage, EImageLoader>::GetParser(EImageLoader::kHDR);
					auto tex = hdr_parser->Parser(kEngineResRootPath + asset_path, 1);
					String name = PathUtils::GetFileName(asset_path);
					if (!name.empty())
						tex->Name(name);
					TexturePool::Add(asset_path, tex);
				}
			}
			else
			{
				g_pLogMgr->LogFormat("Havn't handled asset type: {}",ext);
				return;
			}
			g_pLogMgr->LogFormat(L"Import asset: {} succeed,cost {}ms", sys_path, time_mgr.GetElapsedSinceLastMark());
		}
		else
		{
			g_pLogMgr->LogFormat("Path: {} isn't valid!");
		}
	}
}
