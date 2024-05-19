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
#include "Render/Mesh.h"
#include "Path.h"
#include "FileManager.h"

namespace Ailu
{
	struct ImportSetting
	{
	public:
		static ImportSetting& Default()
		{
			static ImportSetting s_default;
			return s_default;
		}
		String _name_id;
		bool _is_copy = true;
		ImportSetting() = default;
		ImportSetting(String name_id, bool is_copy = true)
		{
			_name_id = name_id;
			_is_copy = is_copy;
		}
		virtual ~ImportSetting() = default;
		//virtual void* operator new(size_t size) = 0;
		//virtual void* operator new[](size_t size) = 0;
		//virtual void operator delete(void* ptr) = 0;
		//virtual void operator delete[](void* ptr) = 0;)
	};

	struct TextureImportSetting : public ImportSetting
	{
	public:
		static TextureImportSetting& Default()
		{
			static TextureImportSetting s_default;
			return s_default;
		}
		bool _is_srgb = false;
		bool _generate_mipmap = true;
	};
	struct MeshImportSetting : public ImportSetting
	{
	public:
		static MeshImportSetting& Default()
		{
			static MeshImportSetting s_default;
			return s_default;
		}
		MeshImportSetting() = default;
		MeshImportSetting(String name_id, bool is_copy,bool is_import_material = true) : ImportSetting(name_id, is_copy)
		{
			_name_id = name_id;
			_is_import_material = is_import_material;
		}
		bool _is_import_material = true;
	};
	struct ShaderImportSetting : public ImportSetting
	{
	public:
		static ShaderImportSetting& Default()
		{
			static ShaderImportSetting s_default;
			return s_default;
		}
		String _vs_entry, _ps_entry;
		String _cs_kernel;
	};
	class ISearchFilter
	{
	public:
		virtual bool Filter(Asset* asset) const= 0;
	};
	class SearchFilterByDirectory : public ISearchFilter
	{
	public:
		SearchFilterByDirectory(Vector<WString> directories) : _directories(directories) {}
		bool Filter(Asset* asset) const final
		{
			for (auto& dir : _directories)
			{
				auto path_prex = asset->_asset_path.substr(0, asset->_asset_path.find_last_of(L"/") + 1);
				if (path_prex == dir)
					return true;
			}
			return false;
		}
	private:
		Vector<WString> _directories;
	};

	class ResourceMgr : public IRuntimeModule
	{
		using ResourcePoolContainer = Map<WString, Ref<Object>>;
		using ResourcePoolLut = Map<u32, ResourcePoolContainer::iterator>;
		using ResourceTask = std::function<void()>;
		using OnResourceTaskCompleted = std::function<void(Ref<void> asset)>;
		struct ImportInfo
		{
			String _msg;
			WString _sys_path;
			float _progress;
			std::filesystem::file_time_type _last_write_time;
			bool _is_succeed;
		};
	public:
		inline const static std::set<String> kLDRImageExt = { ".png",".PNG",".tga",".TGA",".jpg",".JPG",".jpg",".JPEG" };
		inline const static std::set<String> kHDRImageExt = { ".exr",".EXR",".hdr",".HDR" };
	public:
		int Initialize() final;
		void Finalize() final;
		void Tick(const float& delta_time) final;
		auto Begin() { return _asset_db.begin(); };
		auto End() { return _asset_db.end(); };
		u64 AssetNum() { return _asset_db.size(); }
		void SaveScene(Scene* scene, std::string& scene_path);
		Scene* LoadScene(std::string& scene_path);

		Asset* CreateAsset(const WString& asset_path, Ref<Object> obj,bool overwrite = true);
		void AttachToAsset(Asset* asset, Ref<Object> obj);
		void DeleteAsset(Asset* asset);
		void SaveAsset(const Asset* asset);
		void SaveAllUnsavedAssets();
		Asset* GetLinkedAsset(Object* obj);

		//static Material* LoadAsset(const String& asset_path);
		Ref<void> ImportResource(const WString& sys_path, const ImportSetting& setting = ImportSetting::Default());
		//async editon always reutn nullptr
		Ref<void> ImportResourceAsync(const WString& sys_path, const ImportSetting& setting = ImportSetting::Default(), OnResourceTaskCompleted callback = [](Ref<void> asset) {});


		const WString& GetAssetPath(Object* obj) const;
		const Guid& GetAssetGuid(Object* obj) const;
		const WString& GuidToAssetPath(const Guid& guid) const;

		Asset* GetAsset(const WString& asset_path) const;
		Vector<Asset*> GetAssets(const ISearchFilter& filter) const;
		bool RenameAsset(Asset* p_asset, const String& new_name);

		static WString GetObjectSysPath(Object* obj);
		static void SetObjectSysPath(Object* obj, const WString& new_sys_path);

		void AddAssetChangedListener(std::function<void()> callback) { _asset_changed_callbacks.emplace_back(callback); };
		void RemoveAssetChangedListener(std::function<void()> callback)
		{
			_asset_changed_callbacks.erase(
				std::remove_if(_asset_changed_callbacks.begin(), _asset_changed_callbacks.end(),
					[&](const std::function<void()>& fn) { return fn.target<void()>() == callback.target<void()>(); }),
				_asset_changed_callbacks.end());
		}

		template<typename T>
		T* Load(const WString& asset_path, const ImportSetting* import_setting = nullptr);
		template<typename T>
		T* Load(const Guid& guid, const ImportSetting* import_setting = nullptr);
		/// <summary>
		/// 
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <param name="res_id">: texture and material: AssetPath，mesh: AssetPath_Name，shader: AssetPath_VS_PS</param>
		/// <returns></returns>
		template<typename T>
		T* Get(const WString& res_id);
		template<typename T>
		Ref<T> GetRef(const WString& res_id);
		template<typename T>
		T* Get(const Guid& guid);
		template<typename T>
		Ref<T> GetRef(const Guid& guid);
		template<typename T>
		T* Get(u32 object_id);
		template<typename T>
		u32 TotalNum() const;
		void Release(const WString& asset_path)
		{
			u32 obj_id = 0;
			if (_global_resources.contains(asset_path))
			{
				auto ref_count = _global_resources[asset_path].use_count();
				obj_id = _global_resources[asset_path]->ID();
				_global_resources.erase(asset_path);
				g_pLogMgr->LogFormat(L"Release resource: {},and current ref count is {}", asset_path.c_str(), ref_count - 1);
				_lut_global_texture2d.erase(obj_id);
				_lut_global_meshs.erase(obj_id);
				_lut_global_materials.erase(obj_id);
				_lut_global_shaders.erase(obj_id);
			}
		}
		void Release(Object* obj)
		{
			
		}
		template<typename T>
		auto ResourceBegin();
		template<typename T>
		auto ResourceEnd();
		template<typename T>
		static Ref<T> IterToRefPtr(const Map<u32, ResourcePoolContainer::iterator>::iterator& iter);

	public:
		List<ImportInfo> _import_infos;
		std::mutex _import_infos_mutex;
	private:
		static void FormatLine(const String& line, String& key, String& value);
		void AddResourceTask(ResourceTask task);
		bool ExistInAssetDB(const Asset* asset);
		bool ExistInAssetDB(const WString& asset_path);
		void AddToAssetDB(Asset* asset, bool override = true);
		void RemoveFromAssetDB(const Asset* asset);

		void LoadAssetDB();
		void SaveAssetDB();

		static void ExtractCommonAssetInfo(const WString& asset_path, WString& name, Guid& guid,EAssetType::EAssetType& type);

		//加载引擎处理后的资产
		Asset* LoadMaterial(const WString& asset_path);
		Asset* LoadShader(const WString& asset_path);
		Asset* LoadTexture(const WString& asset_path, const ImportSetting* setting);
		Asset* LoadMesh(const WString& asset_path, const ImportSetting* setting);
		Asset* LoadComputeShader(const WString& asset_path);
		//加载原始资产
		List<Ref<Mesh>> LoadExternalMesh(const WString& asset_path);
		Ref<Texture2D>  LoadExternalTexture(const WString& asset_path, const ImportSetting* setting = nullptr);
		Ref<Shader>     LoadExternalShader(const WString& asset_path, const ImportSetting* setting = nullptr);
		Ref<ComputeShader> LoadExternalComputeShader(const WString& asset_path, const ImportSetting* setting = nullptr);

		void SaveMaterial(const WString& asset_path, Material* mat);
		void SaveShader(const WString& asset_path, const Asset* asset);
		void SaveComputeShader(const WString& asset_path, const Asset* asset);
		void SaveMesh(const WString& asset_path, const Asset* asset);
		void SaveTexture2D(const WString& asset_path, const Asset* asset);

		static bool IsFileOnDiskNewer(const WString& sys_path)
		{
			fs::path p(sys_path);
			fs::file_time_type last_load_time;
			fs::file_time_type last_write_time = std::filesystem::last_write_time(p);
			bool newer = true;
			if (s_file_last_load_time.contains(sys_path))
			{
				last_load_time = s_file_last_load_time[sys_path];
				newer = last_write_time > last_load_time;
			}
			return newer;
		}
		void WatchDirectory();
		void SubmitResourceTask();
		//导入外部资源并创建对应的asset
		Ref<void> ImportResourceImpl(const WString& sys_path, const ImportSetting* setting);
		void OnAssetDataBaseChanged();
	private:
		inline const static String kAssetDatabasePath = kEngineResRootPath + "assetdb.alasset";
		inline static ImportInfo* s_p_current_import_info;
		inline static Map<u32, WString> s_object_sys_path_map;
		inline static List<ResourceTask> s_task_queue;
		inline static Queue<Asset*> s_pending_save_assets;
		inline static std::unordered_map<WString,fs::file_time_type> s_file_last_load_time;
		bool _is_watching_directory = true;
		//<GUID,Asset>
		std::map<Guid, Scope<Asset>> _asset_db{};
		//AssetPath,GUID
		std::map<WString, Guid> _asset_looktable{};
		//object_id,asset*
		std::map<u32, Asset*> _object_to_asset{};

		Vector<std::function<void()>> _asset_changed_callbacks;

		//所有从文件中加载的资源同一管理,使用资产路径来索引,所有资源对象的所有权也归于_global_resources
		//纹理和材质使用资产路径索引，网格使用 AssetPath_Name，shader使用AssetPath_vs_ps。区别于asset，这里的res只负责运行时的相关信息。
		ResourcePoolContainer _global_resources;
		ResourcePoolLut _lut_global_resources;
		ResourcePoolLut _lut_global_texture2d;
		ResourcePoolLut _lut_global_meshs;
		ResourcePoolLut _lut_global_materials;
		ResourcePoolLut _lut_global_shaders;
	};
	extern ResourceMgr* g_pResourceMgr;

	template<typename T>
	inline T* ResourceMgr::Load(const WString& asset_path, const ImportSetting* import_setting)
	{
		WString sys_path = PathUtils::GetResSysPath(asset_path);
		bool is_engine_asset = PathUtils::ExtractExt(sys_path) == L".alasset";
		WString asset_name;
		Guid guid;
		EAssetType::EAssetType type = EAssetType::kUndefined;
		ExtractCommonAssetInfo(asset_path, asset_name, guid, type);
		bool is_skip_load = false;
		Ref<Object> out_obj = nullptr;
		Asset* out_asset = nullptr;
		if (!IsFileOnDiskNewer(sys_path))
		{
			g_pLogMgr->LogWarningFormat(L"File {} is new,skip load!",sys_path);
			is_skip_load = true;
		}
		if constexpr (std::is_same<T, Texture2D>::value)
		{
			if (!is_skip_load)
			{
				const TextureImportSetting* setting;
				if (import_setting == nullptr || dynamic_cast<const TextureImportSetting*>(import_setting) == nullptr)
					setting = &TextureImportSetting::Default();
				else
					setting = dynamic_cast<const TextureImportSetting*>(import_setting);
				if (is_engine_asset)
				{
					out_asset = LoadTexture(asset_path, setting);
					out_obj = out_asset->AsRef<T>();
				} 
				//直接替换应该会有问题，依然被gpu引用，暂时先这么写
				_global_resources[asset_path] = out_obj;
				_lut_global_texture2d[out_obj->ID()] = _global_resources.find(asset_path);
			}
		}
		else if constexpr (std::is_same<T, Mesh>::value)
		{
			if (!is_skip_load)
			{
				if (is_engine_asset)
				{
					out_asset = LoadMesh(asset_path,nullptr);
					out_obj = out_asset->AsRef<T>();
					_global_resources[asset_path] = out_obj;
					_lut_global_meshs[out_obj->ID()] = _global_resources.find(asset_path);
				}
				else
				{
					auto mesh_list = std::move(LoadExternalMesh(asset_path));
					for (auto& m : mesh_list)
					{
						WString name_id = asset_path + L"_" + ToWStr(m->Name().c_str());
						_global_resources[name_id] = m;
						_lut_global_meshs[m->ID()] = _global_resources.find(name_id);
					}
				}
			}
		}
		else if constexpr (std::is_same<T, Shader>::value)
		{
			if (!is_skip_load)
			{
				const ShaderImportSetting* setting;
				if (import_setting == nullptr || dynamic_cast<const ShaderImportSetting*>(import_setting) == nullptr)
					setting = &ShaderImportSetting::Default();
				else
					setting = dynamic_cast<const ShaderImportSetting*>(import_setting);
				if (is_engine_asset)
				{
					out_asset = LoadShader(asset_path);
					out_obj = out_asset->AsRef<T>();
				}
				else
				{
					out_obj = LoadExternalShader(asset_path, setting);
				}
				_global_resources[asset_path] = out_obj;
				_lut_global_shaders[out_obj->ID()] = _global_resources.find(asset_path);
			}
		}
		else if constexpr (std::is_same<T, ComputeShader>::value)
		{
			if (!is_skip_load)
			{
				const ShaderImportSetting* setting;
				if (import_setting == nullptr || dynamic_cast<const ShaderImportSetting*>(import_setting) == nullptr)
					setting = &ShaderImportSetting::Default();
				else
					setting = dynamic_cast<const ShaderImportSetting*>(import_setting);
				if (is_engine_asset)
				{
					out_asset = LoadComputeShader(asset_path);
					out_obj = out_asset->AsRef<T>();
				}
				else
				{
					out_obj = LoadExternalComputeShader(asset_path, setting);
				}
				_global_resources[asset_path] = out_obj;
				_lut_global_shaders[out_obj->ID()] = _global_resources.find(asset_path);
			}
		}
		else if constexpr (std::is_same<T, Material>::value)
		{
			if (!is_skip_load)
			{
				out_asset = LoadMaterial(asset_path);
				out_obj = out_asset->AsRef<T>();
				_global_resources[asset_path] = out_obj;
				_lut_global_materials[out_obj->ID()] = _global_resources.find(asset_path);
			}
		}
		if (out_obj && is_engine_asset)
		{
			out_asset->_name = asset_name;
			out_asset->AssignGuid(guid);
			AddToAssetDB(out_asset);
			AttachToAsset(out_asset,out_obj);
			_lut_global_resources[out_obj->ID()] = _global_resources.find(asset_path);
			s_file_last_load_time[sys_path] = std::filesystem::last_write_time(sys_path);
		}	
		return static_cast<T*>(out_obj.get());
	}
	template<typename T>
	inline T* ResourceMgr::Load(const Guid& guid, const ImportSetting* import_setting)
	{
		return Load<T>(GuidToAssetPath(guid), import_setting);
	}
	template<typename T>
	inline T* ResourceMgr::Get(const WString& res_id)
	{
		if constexpr (std::is_same<T, Texture2D>::value)
		{
			if (_global_resources.contains(res_id))
				return std::static_pointer_cast<T>(_global_resources[res_id]).get();
			else
			{
				return dynamic_cast<T*>(Texture::s_p_default_white);
			}
		}
		else
		{
			if (_global_resources.contains(res_id))
				return std::static_pointer_cast<T>(_global_resources[res_id]).get();
		}
		return nullptr;
	}
	template<typename T>
	inline Ref<T> ResourceMgr::GetRef(const WString& res_id)
	{
		if constexpr (std::is_same<T, Texture2D>::value)
		{
			if (_global_resources.contains(res_id))
				return std::static_pointer_cast<T>(_global_resources[res_id]);
			else
			{
				std::static_pointer_cast<Texture2D>(_lut_global_texture2d[Texture::s_p_default_white->ID()]->second);
			}
		}
		else
		{
			if (_global_resources.contains(res_id))
				return std::static_pointer_cast<T>(_global_resources[res_id]);
		}
		return nullptr;
	}
	template<typename T>
	inline T* ResourceMgr::Get(const Guid& guid)
	{
		return Get<T>(GuidToAssetPath(guid));
	}
	template<typename T>
	inline Ref<T> ResourceMgr::GetRef(const Guid& guid)
	{
		return GetRef<T>(GuidToAssetPath(guid));
	}
	template<typename T>
	inline T* ResourceMgr::Get(u32 object_id)
	{
		if constexpr (std::is_same<T, Texture2D>::value)
		{
			AL_ASSERT(!_lut_global_texture2d.contains(object_id));
			return std::static_pointer_cast<T>(_lut_global_texture2d[object_id]->second).get();
		}
		else if constexpr (std::is_same<T, Mesh>::value)
		{
			AL_ASSERT(!_lut_global_meshs.contains(object_id));
			return std::static_pointer_cast<T>(_lut_global_meshs[object_id]->second).get();
		}
		else if constexpr (std::is_same<T, Shader>::value)
		{
			AL_ASSERT(!_lut_global_shaders.contains(object_id));
			return std::static_pointer_cast<T>(_lut_global_shaders[object_id]->second).get();
		}
	}
	template<typename T>
	inline u32 ResourceMgr::TotalNum() const
	{
		if (std::is_same<T, Texture2D>::value)
		{
			return static_cast<u32>(_lut_global_texture2d.size());
		}
		else if (std::is_same<T, Mesh>::value)
		{
			return static_cast<u32>(_lut_global_meshs.size());
		}
		else if (std::is_same<T, Mesh>::value)
		{
			return static_cast<u32>(_lut_global_shaders.size());
		}
		return 0;
	}
	template<typename T>
	inline auto ResourceMgr::ResourceBegin()
	{
		if constexpr (std::is_same<T, Texture2D>::value)
		{
			return _lut_global_texture2d.begin();
		}
		else if constexpr (std::is_same<T, Shader>::value)
		{
			return _lut_global_shaders.begin();
		}
		else if constexpr (std::is_same<T, Mesh>::value)
		{
			return _lut_global_meshs.begin();
		}
		else if constexpr (std::is_same<T, Material>::value)
		{
			return _lut_global_materials.begin();
		}
	}
	template<typename T>
	inline auto ResourceMgr::ResourceEnd()
	{
		if constexpr (std::is_same<T, Texture2D>::value)
		{
			return _lut_global_texture2d.end();
		}
		else if constexpr (std::is_same<T, Shader>::value)
		{
			return _lut_global_shaders.end();
		}
		else if constexpr (std::is_same<T, Mesh>::value)
		{
			return _lut_global_meshs.end();
		}
		else if constexpr (std::is_same<T, Material>::value)
		{
			return _lut_global_materials.end();
		}
	}
	template<typename T>
	inline Ref<T> ResourceMgr::IterToRefPtr(const Map<u32, ResourcePoolContainer::iterator>::iterator& iter)
	{
		auto t_ptr = std::dynamic_pointer_cast<T>(iter->second->second);
		AL_ASSERT(t_ptr == nullptr);
		return t_ptr;
	}
}

#endif // !RESOURCE_MGR_H__
