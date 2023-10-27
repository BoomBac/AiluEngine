#pragma once
#ifndef __RESOURCE_MGR_H__
#define __RESOURCE_MGR_H__
#include <unordered_map>
#include <optional>
#include "Framework/Interface/IRuntimeModule.h"
#include "Framework/Common/Utils.h"
#include "Framework/Common/SceneMgr.h"
#include "Framework/Common/Asset.h"
#include "Render/Material.h"
#include "Render/Texture.h"

namespace Ailu
{
#ifdef COMPANY_ENV
	static const string kEngineResRootPath = "E:/AiluEngine/Engine/Res/";
#else
	static const string kEngineResRootPath = "F:/ProjectCpp/AiluEngine/Engine/Res";
#endif

	namespace EnginePath
	{
		static const string kEngineShaderPath =   "Shaders/";
		static const string kEngineMaterialPath = "Materials/";
		static const string kEngineMeshPath =     "Meshs/";
		static const string kEngineTexturePath =  "Textures/";
	}

	class TexturePool
	{
	public:
		//name with ext
		static Ref<Texture2D> Add(const std::string& name, Ref<Texture2D> res)
		{
			if (s_res_pool.insert(std::make_pair(name, res)).second)
			{
				res->Name(GetFileName(name));
				++s_res_num;
				s_is_dirty = true;
				return res;
			}
			return TexturePool::Get(name);
		}
		static Ref<Texture2D> Get(const std::string& name)
		{
			auto it = s_res_pool.find(name);
			if (it != s_res_pool.end()) return it->second;
			else return nullptr;
		}
		static uint32_t GetResNum()
		{
			return s_res_num;
		}
		static std::map<std::string, Ref<Texture2D>>& GetAllResourceMap() { return s_res_pool; };
	protected:
		inline static std::map<std::string, Ref<Texture2D>> s_res_pool{};
		inline static bool s_is_dirty = true;
		inline static uint32_t s_res_num = 0u;
	};

	static inline std::string GetResPath(const std::string& sub_path)
	{
		return kEngineResRootPath + sub_path;
	}

	class ResourceMgr : public IRuntimeModule
	{
	public:
		int Initialize() final;
		void Finalize() final;
		void Tick(const float& delta_time) final;
		void SaveScene(Scene* scene, std::string& scene_path);
		Scene* LoadScene(std::string& scene_path);
		void SaveAsset(const std::string asset_path, Material* mat);
		void SaveAsset(const Asset& asset);
		
		Ref<Texture2D> LoadTexture(const string& asset_path);
		//template<typename T>
		//static T* LoadAsset(const string& asset_path);
		static Material* LoadAsset(const string& asset_path);
	private:
		inline const static string kAssetDatabasePath = kEngineResRootPath + "assetdb.alasset";
		static Ref<Material> LoadMaterial(std::string path);
		static Ref<Material> LoadMaterial(const vector<string>& blob);
		static bool ExistInAssetDB(const Guid& guid);
		static void AddToAssetDB(const Asset& asset,bool override = true);
		static void LoadAssetDB();
		static void SaveAssetDB();
		static void FormatLine(const string& line, string& key, string& value);
		void SaveMaterialImpl(const string& asset_path,Material* mat);
		void SaveSceneImpl(Scene* scene, std::string& scene_path);
	private:
		inline static std::map<string, std::tuple<string, string>> s_asset_db{};
		inline static std::map<string, Scope<Asset>> s_all_asset{};

	};
	extern ResourceMgr* g_pResourceMgr;

	class ShaderLibrary
	{
#define VAILD_SHADER_ID(id) id != ShaderLibrary::s_error_id
		friend class Shader;
	public:
		static Ref<Shader> Get(std::string& shader_name)
		{
			uint32_t shader_id = NameToId(shader_name);
			if (VAILD_SHADER_ID(shader_id)) return s_shader_library.find(shader_id)->second;
			else
			{
				LOG_ERROR("Shader: {} dont's exist in ShaderLibrary!", shader_name);
				return nullptr;
			}
		}
		static bool Exist(const std::string& name)
		{
			return NameToId(name) != s_error_id;
		}
		static std::string GetShaderPath(const std::string& name)
		{
			auto it = s_shader_path.find(name);
			return it == s_shader_path.end() ? "" : it->second;
		}
		static Ref<Shader> Add(const std::string& res_path)
		{
			auto abs_path = GetResPath(res_path);
			auto name = GetFileName(abs_path);
			if (Exist(name)) return s_shader_library.find(NameToId(name))->second;
			else
			{
				s_shader_path.insert(std::make_pair(name, res_path));
				return Shader::Create(abs_path);
			}
		}
		inline static uint32_t NameToId(const std::string& name)
		{
			auto it = s_shader_name.find(name);
			if (it != s_shader_name.end()) return it->second;
			else return s_error_id;
		}
	private:
		inline static uint32_t s_shader_id = 0u;
		inline static uint32_t s_error_id = 9999u;
		inline static std::unordered_map<uint32_t, Ref<Shader>> s_shader_library;
		inline static std::unordered_map<std::string, uint32_t> s_shader_name;
		inline static std::unordered_map<std::string, std::string> s_shader_path;
	};

	//template<typename T>
	//inline T* ResourceMgr::LoadAsset(const string& asset_path)
	//{
	//	//if (std::is_same<T, Material>::value)
	//	//{
	//	//	auto mat = LoadMaterial(path);
	//	//	s_all_asset.insert(Asset{ asset_path,GetResPath(asset_path),guid,AssetType::kMaterial});
	//	//	return mat.get();
	//	//}
	//	return nullptr;
	//}
}

#endif // !RESOURCE_MGR_H__
