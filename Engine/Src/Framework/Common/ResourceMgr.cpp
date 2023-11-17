#include "pch.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/Material.h"
#include "Framework/Parser/AssetParser.h"
#include "Framework/Common/TimeMgr.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/ThreadPool.h"
#include "GlobalMarco.h"
#include "Framework/Common/LogMgr.h"
#include "Framework/Common/ThreadPool.h"

namespace Ailu
{
	int ResourceMgr::Initialize()
	{
		LOG_WARNING("Begin init engine internal resource...");
		g_pTimeMgr->Mark();
		LoadAssetDB();
		for (auto& [guid, asset_info] : s_asset_db)
		{
			auto& [asset_path, asset_type] = asset_info;
			//s_all_asset.insert(Asset{asset_path,GetResPath(asset_path),guid,AssetType::GetType(asset_type)});
		}
		LoadTexture(EnginePath::kEngineTexturePath + "MyImage01.jpg");
		LoadTexture(EnginePath::kEngineTexturePath + "Intergalactic Spaceship_color_4.png");
		LoadTexture(EnginePath::kEngineTexturePath + "Intergalactic Spaceship_emi.jpg");
		LoadTexture(EnginePath::kEngineTexturePath + "Intergalactic Spaceship_nmap_2_Tris.jpg");
		LoadAsset("Materials/StandardPBR_new.alasset")->IsInternal(true);
		LoadAsset("Materials/WireFrame_new.alasset");
		

		auto parser = TStaticAssetLoader<EResourceType::kStaticMesh, EMeshLoader>::GetParser(EMeshLoader::kFbx);
		MeshPool::AddMesh("sphere", parser->Parser(GetResPath("Meshs/sphere.fbx")).front());
		MeshPool::AddMesh("cube", parser->Parser(GetResPath("Meshs/cube.fbx")).front());
		MeshPool::AddMesh("cone", parser->Parser(GetResPath("Meshs/cone.fbx")).front());
		MeshPool::AddMesh("cylinder", parser->Parser(GetResPath("Meshs/cylinder.fbx")).front());
		MeshPool::AddMesh("monkey", parser->Parser(GetResPath("Meshs/monkey.fbx")).front());
		MeshPool::AddMesh("plane", parser->Parser(GetResPath("Meshs/plane.fbx")).front());
		MeshPool::AddMesh("torus", parser->Parser(GetResPath("Meshs/torus.fbx")).front());

		auto png_parser = TStaticAssetLoader<EResourceType::kImage, EImageLoader>::GetParser(EImageLoader::kPNG);
		auto tga_parser = TStaticAssetLoader<EResourceType::kImage, EImageLoader>::GetParser(EImageLoader::kTGA);


		for (auto& [name, tex] : TexturePool::GetAllResourceMap())
		{
			LOG_INFO("texture: {}", name);
		}
		auto mat = MaterialPool::GetMaterial("Materials/StandardPBR_new.alasset");

		LOG_WARNING("Finish after {}ms", g_pTimeMgr->GetElapsedSinceLastMark());
		g_pLogMgr->LogError("ResourceMgr end");
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
	}

	void ResourceMgr::SaveScene(Scene* scene, std::string& scene_path)
	{
		g_thread_pool->Enqueue(&ResourceMgr::SaveSceneImpl, this, scene, scene_path);
	}

	Scene* ResourceMgr::LoadScene(std::string& scene_path)
	{
		return nullptr;
	}

	void ResourceMgr::SaveAsset(const std::string asset_path, Material* mat)
	{
		using std::endl;
		String sys_path = GetResPath(asset_path);
		Asset mat_asset(asset_path, sys_path, Guid::Generate().ToString(), EAssetType::kMaterial);
		AddToAssetDB(mat_asset);
		std::ofstream out_mat(sys_path, std::ios::out | std::ios::trunc);
		if (out_mat.is_open())
		{
			out_mat << "guid: " << mat_asset._guid.ToString() << endl;
			out_mat << "type: " << AssetType::kMaterial << endl;
			out_mat << "name: " << mat->Name() << endl;
			out_mat.close();
			SaveMaterialImpl(asset_path, mat);
		}
		else
		{
			LOG_WARNING("Save material to {} failed", sys_path);
		}
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
			String sys_path = GetResPath(asset._asset_path);
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
			if (override) s_asset_db.insert(std::make_pair(asset._guid.ToString(), std::make_pair(asset._asset_path, AssetType::GetTypeString(asset._asset_type))));
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
		auto sys_path = GetResPath(asset_path);
		std::multimap<std::string, SerializableProperty*> props{};
		//为了写入通用资产头信息，暂时使用追加方式打开
		std::ofstream out_mat(sys_path, std::ios::out | std::ios::app);
		out_mat << "shader_path: " << ShaderLibrary::GetShaderPath(mat->_p_shader->GetName());
		for (auto& prop : mat->properties)
		{
			props.insert(std::make_pair(GetSerializablePropertyTypeStr(prop.second._type), &prop.second));
		}
		if (!props.empty()) out_mat << endl;
#ifdef OLD
		std::string cur_prop_type = "type";
		int prop_count = 0;
		for (auto& [prop_name, prop] : props)
		{
			if (cur_prop_type != prop_name)
			{
				out_mat << "  prop_type: " << prop_name << endl;
				cur_prop_type = prop_name;
			}
			if (prop->_type == ESerializablePropertyType::kColor || prop->_type == ESerializablePropertyType::kVector4f)
				out_mat << "    " << prop->_name << ": " << SerializableProperty::GetProppertyValue<Vector4f>(*prop);
			else if (prop->_type == ESerializablePropertyType::kFloat)
				out_mat << "    " << prop->_name << ": " << SerializableProperty::GetProppertyValue<float>(*prop);
			else if (prop->_type == ESerializablePropertyType::kTexture2D)
			{
				String tex_path = prop->_value_ptr == nullptr ? "none" : SerializableProperty::GetProppertyValue<String>(*prop);
				out_mat << "    " << prop->_name << ": " << tex_path;
			}
			if (prop_count++ != props.size() - 1)
				out_mat << endl;
		}
#else
		auto float_props = mat->GetAllFloatValue();
		auto vector_props = mat->GetAllVectorValue();
		out_mat << "  prop_type: " << "Float" << endl;
		for (auto& [name,value] : float_props)
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
				String tex_path = prop->_value_ptr == nullptr ? "none" : SerializableProperty::GetProppertyValue<String>(*prop);
				out_mat << "    " << prop->_name << ": " << tex_path << endl;
			}
		}
#endif // OLD
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
		auto mat = MaterialPool::CreateMaterial(ShaderLibrary::Add(std::get<1>(lines[1])), GetFileName(path));
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
			auto prop = mat->properties.find(k);
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
		auto mat = MakeRef<Material>(ShaderLibrary::Add(shader_path), name);
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
#ifdef OLD
			auto prop = mat->properties.find(k);
			if (prop != mat->properties.end())
			{
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
				else if (cur_type == ShaderPropertyType::Texture2D)
				{
					if (v == "none") continue;
					mat->SetTexture(k, v);
				}
			}
			else
			{
				g_pLogMgr->LogWarningFormat("Load material: {},property {} failed!", mat->_name, k);
			}
#else
			if (cur_type == ShaderPropertyType::Float)
			{
				float f;
				if (sscanf_s(v.c_str(), "%f", &f) == 1)
					mat->SetFloat(k,f);
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
				if (v == "none") continue;
				mat->SetTexture(k, v);
			}
#endif
		}
		return mat;
	}

	Ref<Mesh> ResourceMgr::LoadMeshImpl(const String& asset_path)
	{
		static auto parser = TStaticAssetLoader<EResourceType::kStaticMesh, EMeshLoader>::GetParser(EMeshLoader::kFbx);
		auto sys_path = GetResPath(asset_path);
		auto mesh_list = parser->Parser(sys_path);
		for (auto& mesh : mesh_list)
		{
			MeshPool::AddMesh(asset_path + ":" + mesh->Name(), mesh);
		}
		return mesh_list.front();
	}

	Ref<Texture2D> ResourceMgr::LoadTexture(const String& asset_path)
	{
		auto png_parser = TStaticAssetLoader<EResourceType::kImage, EImageLoader>::GetParser(EImageLoader::kPNG);
		String sys_path = kEngineResRootPath + asset_path;
		return TexturePool::Add(asset_path, png_parser->Parser(sys_path));
	}

	Material* ResourceMgr::LoadAsset(const String& asset_path)
	{
		String sys_path = GetResPath(asset_path);
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
		MaterialPool::AddMaterial(mat, asset_path);
		s_all_asset.find(asset_path)->second->_p_inst_asset = reinterpret_cast<void*>(mat.get());
		return mat.get();
	}

	void ResourceMgr::SaveSceneImpl(Scene* scene, std::string& scene_path)
	{
		std::ofstream outputFile(scene_path);
		if (!outputFile.is_open())
		{
			std::cerr << "无法打开文件" << std::endl;
		}
		outputFile << "这是要写入到文件的文本内容." << std::endl;
		outputFile << "可以写入更多内容." << std::endl;
		LOG_WARNING("Save scene to {}", scene_path);
		outputFile.close();
	}
	void ResourceMgr::WatchDirectory()
	{
		namespace fs = std::filesystem;
		fs::path dir(kEngineResRootPath + EnginePath::kEngineShaderPath);
		std::chrono::duration<int, std::milli> sleep_duration(1000); // 1秒
		std::unordered_map<fs::path, fs::file_time_type> files;
		while (true)
		{
			std::unordered_map<fs::path, fs::file_time_type> current_files;
			for (const auto& entry : fs::directory_iterator(dir))
			{
				if (fs::is_regular_file(entry.status())) {
					current_files[entry.path()] = fs::last_write_time(entry.path());
				}
			}

			for (const auto& [file, last_write_time] : current_files)
			{
				if (files.find(file) == files.end() || files[file] != last_write_time)
				{
					LOG_INFO("File: {0} been modified!", file.string());
					files[file] = last_write_time;
				}
			}
			std::this_thread::sleep_for(sleep_duration);
		}
	}
}
