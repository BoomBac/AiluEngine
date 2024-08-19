#pragma warning(push)
#pragma warning(disable : 4251)//disable dll export warning

#pragma once
#ifndef __RESOURCE_MGR_H__
#define __RESOURCE_MGR_H__
#include "FileManager.h"
#include "Framework/Common/Asset.h"
#include "Framework/Common/SceneMgr.h"
#include "Framework/Common/Utils.h"
#include "Framework/Interface/IRuntimeModule.h"
#include "Path.h"
#include "Render/Material.h"
#include "Render/Mesh.h"
#include "Render/Texture.h"
#include <optional>
#include <unordered_map>

namespace Ailu
{
    struct AILU_API ImportSetting
    {
    public:
        static ImportSetting &Default()
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

    struct AILU_API TextureImportSetting : public ImportSetting
    {
    public:
        static TextureImportSetting &Default()
        {
            static TextureImportSetting s_default;
            return s_default;
        }
        bool _is_srgb = false;
        bool _generate_mipmap = true;
    };
    struct AILU_API MeshImportSetting : public ImportSetting
    {
    public:
        static MeshImportSetting &Default()
        {
            static MeshImportSetting s_default;
            return s_default;
        }
        MeshImportSetting() = default;
        MeshImportSetting(String name_id, bool is_copy, bool is_import_material = true) : ImportSetting(name_id, is_copy)
        {
            _name_id = name_id;
            _is_import_material = is_import_material;
        }
        bool _is_import_material = true;
    };
    struct AILU_API ShaderImportSetting : public ImportSetting
    {
    public:
        static ShaderImportSetting &Default()
        {
            static ShaderImportSetting s_default;
            return s_default;
        }
        String _vs_entry, _ps_entry;
        String _cs_kernel;
    };

    class AILU_API ISearchFilter
    {
    public:
        virtual bool Filter(Asset *asset) const = 0;
    };

    class AILU_API SearchFilterByDirectory : public ISearchFilter
    {
    public:
        SearchFilterByDirectory(Vector<WString> directories) : _directories(directories)
        {
        }
        bool Filter(Asset *asset) const final
        {
            for (auto &dir: _directories)
            {
                auto path_prex = asset->_asset_path.substr(0, asset->_asset_path.find_last_of(L"/"));
                if (path_prex == dir)
                    return true;
            }
            return false;
        }

    private:
        Vector<WString> _directories;
    };

    class AILU_API SearchFilterByPath : public ISearchFilter
    {
    public:
        SearchFilterByPath(Vector<WString> asset_pathes) : _asset_pathes(asset_pathes)
        {
        }
        bool Filter(Asset *asset) const final
        {
            for (auto &dir: _asset_pathes)
            {
                if (dir == asset->_asset_path)
                    return true;
            }
            return false;
        }

    private:
        Vector<WString> _asset_pathes;
    };

    class AILU_API SearchFilterByFileName : public ISearchFilter
    {
    public:
        SearchFilterByFileName(Vector<WString> asset_names) : _asset_names(asset_names)
        {
        }
        bool Filter(Asset *asset) const final
        {
            for (auto &file_name: _asset_names)
            {
                if (PathUtils::GetFileName(asset->_asset_path).substr(0, file_name.size()) == file_name)
                    return true;
            }
            return false;
        }

    private:
        Vector<WString> _asset_names;
    };

    class AILU_API ResourceMgr : public IRuntimeModule
    {
        using ResourcePoolContainer = Map<WString, Ref<Object>>;
        using ResourcePoolContainerIter = ResourcePoolContainer::iterator;
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
        inline const static std::set<String> kLDRImageExt = {".png", ".PNG", ".tga", ".TGA", ".jpg", ".JPG", ".jpg", ".JPEG"};
        inline const static std::set<String> kHDRImageExt = {".exr", ".EXR", ".hdr", ".HDR"};
        inline const static std::set<String> kMeshExt = {".obj", ".OBJ", ".fbx", ".FBX"};

    public:
        static const WString &EngineResRootPath() { return s_engine_res_root_pathw; };
        static String GetResSysPath(const String &sub_path);
        static WString GetResSysPath(const WString &sub_path);

        DISALLOW_COPY_AND_ASSIGN(ResourceMgr)
        ResourceMgr() = default;
        int Initialize() final;
        void Finalize() final;
        void Tick(const float &delta_time) final;
        auto Begin() { return _asset_db.begin(); };
        auto End() { return _asset_db.end(); };
        u64 AssetNum() { return _asset_db.size(); }
        //在目标位置创建资产对象并将其注册
        Asset *CreateAsset(const WString &asset_path, Ref<Object> obj, bool overwrite = true);
        void DeleteAsset(Asset *asset);
        bool RenameAsset(Asset *p_asset, const WString &new_name);
        bool MoveAsset(Asset *p_asset, const WString &new_asset_path);
        void SaveAsset(const Asset *asset);
        void SaveAllUnsavedAssets();
        Asset *GetLinkedAsset(Object *obj);
        static void ConfigRootPath(const WString &prex);
        //.../Res，最后不带斜杠
        const WString &GetProjectRootSysPath() const
        {
            return _project_root_path;
        }

        //static Material* LoadAsset(const String& asset_path);
        Ref<void> ImportResource(const WString &sys_path, const ImportSetting &setting = ImportSetting::Default());
        //async editon always reutn nullptr
        Ref<void> ImportResourceAsync(const WString &sys_path, const ImportSetting &setting = ImportSetting::Default(), OnResourceTaskCompleted callback = [](Ref<void> asset) {});


        const WString &GetAssetPath(Object *obj) const;
        const Guid &GetAssetGuid(Object *obj) const;
        const WString &GuidToAssetPath(const Guid &guid) const;

        Asset *GetAsset(const WString &asset_path) const;
        Vector<Asset *> GetAssets(const ISearchFilter &filter) const;

        void AddAssetChangedListener(std::function<void()> callback) { _asset_changed_callbacks.emplace_back(callback); };
        void RemoveAssetChangedListener(std::function<void()> callback)
        {
            _asset_changed_callbacks.erase(
                    std::remove_if(_asset_changed_callbacks.begin(), _asset_changed_callbacks.end(),
                                   [&](const std::function<void()> &fn)
                                   { return fn.target<void()>() == callback.target<void()>(); }),
                    _asset_changed_callbacks.end());
        }

        template<typename T>
        T *Load(const WString &asset_path);
        template<typename T>
        T *Load(const Guid &guid);
        template<typename T>
        T *Get(const WString &res_id);
        template<typename T>
        Ref<T> GetRef(const WString &res_id);
        template<typename T>
        T *Get(const Guid &guid);
        template<typename T>
        Ref<T> GetRef(const Guid &guid);
        template<typename T>
        T *Get(u32 object_id);
        template<typename T>
        u32 TotalNum() const;
        void Release(const WString &asset_path)
        {
            u32 obj_id = 0;
            if (_global_resources.contains(asset_path))
            {
                auto ref_count = _global_resources[asset_path].use_count();
                obj_id = _global_resources[asset_path]->ID();
                _global_resources.erase(asset_path);
                g_pLogMgr->LogFormat(L"Release resource: {},and current ref count is {}", asset_path.c_str(), ref_count - 1);
            }
        }
        void Release(Object *obj)
        {
        }
        template<typename T>
        auto ResourceBegin();
        template<typename T>
        auto ResourceEnd();
        template<typename T>
        static Ref<T> IterToRefPtr(const Vector<ResourcePoolContainer::iterator>::iterator &iter);

    public:
        List<ImportInfo> _import_infos;
        std::mutex _import_infos_mutex;
        std::mutex _asset_db_mutex;

    private:
        static void FormatLine(const String &line, String &key, String &value);
        static void ExtractCommonAssetInfo(const WString &asset_path, WString &name, Guid &guid, EAssetType::EAssetType &type);
        static EAssetType::EAssetType GetObjectAssetType(Object *obj);
        static bool IsFileOnDiskUpdated(const WString &sys_path);

        void AddResourceTask(ResourceTask task);
        bool ExistInAssetDB(const Asset *asset);
        bool ExistInAssetDB(const WString &asset_path);
        void RemoveFromAssetDB(const Asset *asset);

        void LoadAssetDB();
        void SaveAssetDB();

        Asset *RegisterAsset(Scope<Asset> &&asset, bool override = true);
        void UnRegisterAsset(Asset *asset);
        void RegisterResource(const WString &asset_path, Ref<Object> obj, bool override = true);
        void UnRegisterResource(const WString &asset_path);

        //加载引擎处理后的资产
        Scope<Asset> LoadMaterial(const WString &asset_path);
        Scope<Asset> LoadShader(const WString &asset_path);
        Scope<Asset> LoadTexture(const WString &asset_path);
        Scope<Asset> LoadMesh(const WString &asset_path);
        Scope<Asset> LoadComputeShader(const WString &asset_path);
        Scope<Asset> LoadScene(const WString &asset_path);
        //加载原始资产
        List<Ref<Mesh>> LoadExternalMesh(const WString &asset_path);
        Ref<Texture2D> LoadExternalTexture(const WString &asset_path, const ImportSetting *setting = nullptr);
        Ref<Shader> LoadExternalShader(const WString &asset_path, const ImportSetting *setting = nullptr);
        Ref<ComputeShader> LoadExternalComputeShader(const WString &asset_path, const ImportSetting *setting = nullptr);

        void SaveMaterial(const WString &asset_path, Material *mat);
        void SaveShader(const WString &asset_path, const Asset *asset);
        void SaveComputeShader(const WString &asset_path, const Asset *asset);
        void SaveMesh(const WString &asset_path, const Asset *asset);
        void SaveTexture2D(const WString &asset_path, const Asset *asset);
        void SaveScene(const WString &asset_path, const Asset *asset);

        void WatchDirectory();
        void SubmitResourceTask();
        //导入外部资源并创建对应的asset
        Ref<void> ImportResourceImpl(const WString &sys_path, const ImportSetting *setting);
        void OnAssetDataBaseChanged();

    private:
        inline static String kAssetDatabasePath;
        inline static WString s_engine_res_root_pathw;

        inline static ImportInfo *s_p_current_import_info;
        inline static Map<u32, WString> s_object_sys_path_map;
        inline static List<ResourceTask> s_task_queue;
        inline static Queue<Asset *> s_pending_save_assets;
        inline static std::unordered_map<WString, fs::file_time_type> s_file_last_load_time;
        bool _is_watching_directory = true;
        WString _project_root_path;
        //<GUID,Asset>
        std::map<Guid, Scope<Asset>> _asset_db{};
        //AssetPath,GUID
        std::map<WString, Guid> _asset_looktable{};
        //object_id,asset*
        std::map<u32, Asset *> _object_to_asset{};

        ResourcePoolContainer _global_resources;
        ResourcePoolLut _lut_global_resources;
        Map<EAssetType::EAssetType, Vector<ResourcePoolContainerIter>> _lut_global_resources_by_type;
        Vector<std::function<void()>> _asset_changed_callbacks;
        Queue<Asset *> _pending_delete_assets;
        Queue<Shader *> _shader_waiting_for_compile;
        Queue<ComputeShader *> _compute_shader_waiting_for_compile;
    };
    extern AILU_API ResourceMgr *g_pResourceMgr;

    template<typename T>
    inline T *ResourceMgr::Load(const WString &asset_path)
    {
        if (asset_path.empty())
            return nullptr;
        using Loader = std::function<Scope<Asset>(ResourceMgr *, const WString &)>;
        WString sys_path = ResourceMgr::GetResSysPath(asset_path);
        auto ext = PathUtils::ExtractExt(sys_path);
        bool is_engine_asset = ext == L".alasset" || L".almap";
        AL_ASSERT(!is_engine_asset);
        WString asset_name;
        Guid guid;
        EAssetType::EAssetType type = EAssetType::kUndefined;
        ExtractCommonAssetInfo(asset_path, asset_name, guid, type);
        AL_ASSERT(type == EAssetType::kUndefined);
        if (type == EAssetType::kUndefined)
        {
            g_pLogMgr->LogErrorFormat(L"Load at {} failed!", asset_path);
            return nullptr;
        }
        bool is_skip_load = false;
        Loader asset_loader = nullptr;
        if (!IsFileOnDiskUpdated(sys_path))
        {
            g_pLogMgr->LogWarningFormat(L"File {} is new,skip load!", sys_path);
            return _global_resources.contains(asset_path) ? static_cast<T *>(_global_resources[asset_path].get()) : nullptr;
        }
        if constexpr (std::is_same<T, Texture2D>::value)
        {
            asset_loader = &ResourceMgr::LoadTexture;
        }
        else if constexpr (std::is_same<T, Mesh>::value)
        {
            asset_loader = &ResourceMgr::LoadMesh;
        }
        else if constexpr (std::is_same<T, Shader>::value)
        {
            asset_loader = &ResourceMgr::LoadShader;
        }
        else if constexpr (std::is_same<T, ComputeShader>::value)
        {
            asset_loader = &ResourceMgr::LoadComputeShader;
        }
        else if constexpr (std::is_same<T, Material>::value)
        {
            asset_loader = &ResourceMgr::LoadMaterial;
        }
        else if constexpr (std::is_same<T, Scene>::value)
        {
            asset_loader = &ResourceMgr::LoadScene;
        }
        Scope<Asset> out_asset = std::move(asset_loader(this, asset_path));
        if (out_asset != nullptr)
        {
            out_asset->_name = asset_name;
            out_asset->AssignGuid(guid);
            RegisterResource(asset_path, out_asset->_p_obj);
            RegisterAsset(std::move(out_asset));
            s_file_last_load_time[sys_path] = std::filesystem::last_write_time(sys_path);
        }
        return _global_resources.contains(asset_path) ? static_cast<T *>(_global_resources[asset_path].get()) : nullptr;
    }
    template<typename T>
    inline T *ResourceMgr::Load(const Guid &guid)
    {
        return Load<T>(GuidToAssetPath(guid));
    }
    template<typename T>
    inline T *ResourceMgr::Get(const WString &res_id)
    {
        if constexpr (std::is_same<T, Texture2D>::value)
        {
            if (_global_resources.contains(res_id))
                return std::static_pointer_cast<T>(_global_resources[res_id]).get();
            else
            {
                return dynamic_cast<T *>(Texture::s_p_default_white);
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
    inline Ref<T> ResourceMgr::GetRef(const WString &res_id)
    {
        if constexpr (std::is_same<T, Texture2D>::value)
        {
            if (_global_resources.contains(res_id))
                return std::static_pointer_cast<T>(_global_resources[res_id]);
            else
            {
                std::static_pointer_cast<Texture2D>(_lut_global_resources[Texture::s_p_default_white->ID()]->second);
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
    inline T *ResourceMgr::Get(const Guid &guid)
    {
        return Get<T>(GuidToAssetPath(guid));
    }
    template<typename T>
    inline Ref<T> ResourceMgr::GetRef(const Guid &guid)
    {
        return GetRef<T>(GuidToAssetPath(guid));
    }
    template<typename T>
    inline T *ResourceMgr::Get(u32 object_id)
    {
        if (_lut_global_resources.contains(object_id))
        {
            return std::static_pointer_cast<T>(_lut_global_resources[object_id]->second).get();
        }
        return nullptr;
    }
    template<typename T>
    inline u32 ResourceMgr::TotalNum() const
    {
        if constexpr (std::is_same<T, Texture2D>::value)
        {
            return _lut_global_resources_by_type.at(EAssetType::kTexture2D).size();
        }
        else if constexpr (std::is_same<T, Shader>::value)
        {
            return _lut_global_resources_by_type.at(EAssetType::kShader).size();
        }
        else if constexpr (std::is_same<T, ComputeShader>::value)
        {
            return _lut_global_resources_by_type.at(EAssetType::kComputeShader).size();
        }
        else if constexpr (std::is_same<T, Mesh>::value)
        {
            return _lut_global_resources_by_type.at(EAssetType::kMesh).size();
        }
        else if constexpr (std::is_same<T, Material>::value)
        {
            return _lut_global_resources_by_type.at(EAssetType::kMaterial).size();
        }
        else if constexpr (std::is_same<T, Scene>::value)
        {
            return _lut_global_resources_by_type.at(EAssetType::kScene).size();
        }
        else
        {
            AL_ASSERT(true);
        }
        return 0;
    }
    template<typename T>
    inline auto ResourceMgr::ResourceBegin()
    {
        if constexpr (std::is_same<T, Texture2D>::value)
        {
            return _lut_global_resources_by_type[EAssetType::kTexture2D].begin();
        }
        else if constexpr (std::is_same<T, Shader>::value)
        {
            return _lut_global_resources_by_type[EAssetType::kShader].begin();
        }
        else if constexpr (std::is_same<T, ComputeShader>::value)
        {
            return _lut_global_resources_by_type[EAssetType::kComputeShader].begin();
        }
        else if constexpr (std::is_same<T, Mesh>::value)
        {
            return _lut_global_resources_by_type[EAssetType::kMesh].begin();
        }
        else if constexpr (std::is_same<T, Material>::value)
        {
            return _lut_global_resources_by_type[EAssetType::kMaterial].begin();
        }
        else if constexpr (std::is_same<T, Scene>::value)
        {
            return _lut_global_resources_by_type[EAssetType::kScene].begin();
        }
        else
        {
            AL_ASSERT(true);
        }
    }
    template<typename T>
    inline auto ResourceMgr::ResourceEnd()
    {
        if constexpr (std::is_same<T, Texture2D>::value)
        {
            return _lut_global_resources_by_type[EAssetType::kTexture2D].end();
        }
        else if constexpr (std::is_same<T, Shader>::value)
        {
            return _lut_global_resources_by_type[EAssetType::kShader].end();
        }
        else if constexpr (std::is_same<T, ComputeShader>::value)
        {
            return _lut_global_resources_by_type[EAssetType::kComputeShader].end();
        }
        else if constexpr (std::is_same<T, Mesh>::value)
        {
            return _lut_global_resources_by_type[EAssetType::kMesh].end();
        }
        else if constexpr (std::is_same<T, Material>::value)
        {
            return _lut_global_resources_by_type[EAssetType::kMaterial].end();
        }
        else if constexpr (std::is_same<T, Scene>::value)
        {
            return _lut_global_resources_by_type[EAssetType::kScene].end();
        }
        else
        {
            AL_ASSERT(true);
        }
    }
    template<typename T>
    inline Ref<T> ResourceMgr::IterToRefPtr(const Vector<ResourcePoolContainer::iterator>::iterator &iter)
    {
        auto t_ptr = std::dynamic_pointer_cast<T>((*iter)->second);
        AL_ASSERT(t_ptr == nullptr);
        return t_ptr;
    }
}// namespace Ailu

#endif// !RESOURCE_MGR_H__
#pragma warning(pop)