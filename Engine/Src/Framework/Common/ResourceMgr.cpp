#include "pch.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/Material.h"
#include "Framework/Parser/AssetParser.h"
#include "Framework/Common/TimeMgr.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/ThreadPool.h"
#include "GlobalMarco.h"

namespace Ailu
{
	int ResourceMgr::Initialize()
	{
		LOG_WARNING("Begin init engine internal resource...");
		g_pTimeMgr->Mark();
		LoadAssetDB();
		for (auto& [guid,asset_info] : s_asset_db)
		{
			auto& [asset_path, asset_type] = asset_info;
			s_all_asset.insert(Asset{asset_path,GetResPath(asset_path),guid,AssetType::GetType(asset_type)});
		}
		//MaterialPool::CreateMaterial(ShaderLibrary::Add("Shaders/shaders.hlsl"), "StandardPBR");
		//MaterialPool::CreateMaterial(ShaderLibrary::Add("Shaders/PureColor.hlsl"), "WireFrame");
		LoadMaterial(std::string(STR2(RES_PATH)) + "Materials/StandardPBR.alasset");
		LoadMaterial(std::string(STR2(RES_PATH)) + "Materials/WireFrame.alasset");
		auto parser = TStaticAssetLoader<EResourceType::kStaticMesh, EMeshLoader>::GetParser(EMeshLoader::kFbx);
		//_plane = parser->Parser(GET_RES_PATH(Meshs/plane.fbx));
		//_plane = parser->Parser(GET_RES_PATH(Meshs/gizmo.fbx));

		//_tree = parser->Parser(GetResPath("Meshs/stone.fbx"));
		//_tree = parser->Parser(GetResPath("Meshs/plane.fbx"));
		//_tree = parser->Parser(GetResPath("Meshs/space_ship.fbx"));
		parser->Parser(GetResPath("Meshs/sphere.fbx"));
		auto png_parser = TStaticAssetLoader<EResourceType::kImage, EImageLoader>::GetParser(EImageLoader::kPNG);
		auto tga_parser = TStaticAssetLoader<EResourceType::kImage, EImageLoader>::GetParser(EImageLoader::kTGA);

		//_tex_water = tga_parser->Parser(GET_RES_PATH(Textures/PK_stone03_static_0_D.tga));
		//tga_parser->Parser(GetResPath("Textures/PK_stone03_static_0_D.tga"));
		//tga_parser->Parser(GetResPath("Textures/PK_stone03_static_0_N.tga"));
		//png_parser->Parser(GetResPath("Textures/Intergalactic Spaceship_color_4.png"));
		LoadTexture(EnginePath::kEngineTexturePath + "MyImage01.jpg");
		//png_parser->Parser(GetResPath("Textures/Intergalactic Spaceship_emi.jpg"));
		//png_parser->Parser(GetResPath("Textures/Intergalactic Spaceship_nmap_2_Tris.jpg"));
		LOG_WARNING("Finish after {}ms", g_pTimeMgr->GetElapsedSinceLastMark());
		return 0;
	}
	void ResourceMgr::Finalize()
	{
		for (auto mat : MaterialPool::GetAllMaterial())
		{
			//SaveMaterial(mat, std::string(STR2(RES_PATH)) + "Materials/");
			SaveAsset(EnginePath::kEngineMaterialPath + mat->Name() + "_new.alasset", mat);
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

	void ResourceMgr::SaveMaterial(Material* mat, std::string path)
	{
		using std::endl;
		auto path_name = path + mat->Name() + ".alasset";
		std::multimap<std::string, SerializableProperty*> props{};
		std::ofstream out_mat(path_name, std::ios::out | std::ios::trunc);
		out_mat << "type: " << "material" << endl;
		out_mat << "shader_path: " << ShaderLibrary::GetShaderPath(mat->_p_shader->GetName());
		for (auto& prop : mat->properties)
		{
			props.insert(std::make_pair(prop.second._type_name, &prop.second));
		}
		if (!props.empty()) out_mat << endl;
		std::string cur_prop_type = "type";
		int prop_count = 0;
		for (auto& prop : props)
		{
			if (cur_prop_type != prop.first)
			{
				out_mat << "  prop_type: " << prop.first << endl;
				cur_prop_type = prop.first;
			}
			if (prop.second->_type_name == ShaderPropertyType::Color || prop.second->_type_name == ShaderPropertyType::Vector)
				out_mat << "    " << prop.second->_name << ": " << SerializableProperty::GetProppertyValue<Vector4f>(*prop.second);
			else if (prop.second->_type_name == ShaderPropertyType::Float)
				out_mat << "    " << prop.second->_name << ": " << SerializableProperty::GetProppertyValue<float>(*prop.second);
			else if (prop.second->_type_name == ShaderPropertyType::Texture2D)
				out_mat << "    " << prop.second->_name << ": " << "texture path";
			if (prop_count++ != props.size() - 1)
				out_mat << endl;
		}
		out_mat.close();
		LOG_WARNING("Save material to {}", path_name);
	}

	void ResourceMgr::SaveAsset(const std::string asset_path, Material* mat)
	{
		using std::endl;
		string sys_path = GetResPath(asset_path);
		Asset mat_asset(asset_path,sys_path, Guid::Generate().ToString(),EAssetType::kMaterial);
		AddToAssetDB(mat_asset);
		std::ofstream out_mat(sys_path, std::ios::out | std::ios::trunc);
		if (out_mat.is_open())
		{
			out_mat << "guid: " << mat_asset._guid.ToString() << endl;
			out_mat << "type: " << AssetType::kMaterial << endl;
			out_mat << "shader_path: " << ShaderLibrary::GetShaderPath(mat->_p_shader->GetName());
			std::multimap<std::string, SerializableProperty*> props{};
			for (auto& prop : mat->properties)
			{
				props.insert(std::make_pair(prop.second._type_name, &prop.second));
			}
			if (!props.empty()) out_mat << endl;
			std::string cur_prop_type = "type";
			int prop_count = 0;
			for (auto& prop : props)
			{
				if (cur_prop_type != prop.first)
				{
					out_mat << "  prop_type: " << prop.first << endl;
					cur_prop_type = prop.first;
				}
				if (prop.second->_type_name == ShaderPropertyType::Color || prop.second->_type_name == ShaderPropertyType::Vector)
					out_mat << "    " << prop.second->_name << ": " << SerializableProperty::GetProppertyValue<Vector4f>(*prop.second);
				else if (prop.second->_type_name == ShaderPropertyType::Float)
					out_mat << "    " << prop.second->_name << ": " << SerializableProperty::GetProppertyValue<float>(*prop.second);
				else if (prop.second->_type_name == ShaderPropertyType::Texture2D)
					out_mat << "    " << prop.second->_name << ": " << "texture path";
				if (prop_count++ != props.size() - 1)
					out_mat << endl;
			}
			out_mat.close();
			LOG_WARNING("Save material to {}", sys_path);
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
			SaveAsset(asset._asset_path, reinterpret_cast<Material*>(asset._p_inst_asset));
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
		string line;
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
		for (auto& [guid,asset] : s_asset_db)
		{
			auto& [sys_path, asset_type] = asset;
			file << guid << "," << sys_path << "," << asset_type << std::endl;
		}
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
		}
		return mat;
	}

	Ref<Texture2D> ResourceMgr::LoadTexture(const string& file_path)
	{
		auto png_parser = TStaticAssetLoader<EResourceType::kImage, EImageLoader>::GetParser(EImageLoader::kPNG);
		string path = kEngineResRootPath + file_path;
		return TexturePool::Add(file_path, png_parser->Parser(path));
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
}
