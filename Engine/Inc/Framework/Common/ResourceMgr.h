#pragma warning(push)
#pragma warning(disable : 4251)//disable dll export warning

#pragma once
#ifndef __RESOURCE_MGR_H__
#define __RESOURCE_MGR_H__
#include "FileManager.h"
#include "Framework/Common/Asset.h"
#include "Framework/Common/Utils.h"
#include "Framework/Interface/IRuntimeModule.h"
#include "Framework/Parser/AssetParser.h"
#include "Objects/Type.h"
#include "Path.h"
#include "Render/Font.h"
#include "Render/Material.h"
#include "Render/Mesh.h"
#include "Render/Texture.h"
#include "Scene/Scene.h"
#include <optional>
#include <unordered_map>

namespace Ailu
{
    using Render::Mesh;
    using Render::SkeletonMesh;
    using Render::Material;
    using Render::Texture;
    using Render::Texture2D;
    using Render::Texture3D;
    using Render::Shader;
    using Render::ComputeShader;
    using Render::Font;
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
    class Project;
    using AssetPath = WString;
    using SystemPath = WString;

    class AILU_API ResourceMgr : public IRuntimeModule
    {
    public:
        struct AssetMountDesc
        {
            EAssetDomain _domain;
            WString _scheme;
            WString _asset_root;
            WString _database_path;
            bool _read_only;
        };

        struct AssetMountDomain
        {
            AssetMountDomain(const AssetMountDesc& desc)
            {
                _domain = desc._domain;
                _scheme = desc._scheme;
                _asset_root = desc._asset_root;
                _database_path = desc._database_path;
                _read_only = desc._read_only;
            }
            EAssetDomain _domain;
            WString _scheme;
            WString _asset_root;
            WString _database_path;
            bool _read_only;
        };

        static void Init();
        static void Shutdown();
        static ResourceMgr& Get();
        using ResourcePoolContainer = Map<WString, Ref<Object>>;
        using ResourcePoolContainerIter = ResourcePoolContainer::iterator;
        using ResourcePoolLut = Map<u32, ResourcePoolContainer::iterator>;
        using ResourceTypeLut = Map<const Type *, Vector<ResourcePoolContainerIter>>;
        using ResourceTask = std::function<bool()>;
        using OnResourceTaskCompleted = std::function<void(Ref<void> asset)>;
        template<typename T>
        using OnLoadTaskCompleted = std::function<void(Ref<T> asset)>;
        inline const static std::set<String> kLDRImageExt = {".png", ".PNG", ".tga", ".TGA", ".jpg", ".JPG", ".jpg", ".JPEG"};
        inline const static std::set<String> kHDRImageExt = {".exr", ".EXR", ".hdr", ".HDR"};
        inline const static std::set<String> kMeshExt = {".obj", ".OBJ", ".fbx", ".FBX", ".gltf", ".GLTF"};
        inline const static Array<WString,3> kPathScheme = {L"engine://",L"editor://",L"project://"};

    public:
        static const WString &EngineResRootPath() { return s_engine_res_root_path; };
        static const WString &EditorResRootPath() { return s_editor_res_root_path; };
        static const WString &ProjectRootPath() { return s_project_root_path; };
        static void ConfigProject(Project* proj);
        static void ConfigEditorResRoot(const WString &root);
        static void ConfigEngineResRoot(const WString &root);

        static WString GetResSysPath(const WString &p);
        static WString GetResSysPath(EAssetDomain domain, const WString &relative_path);

        DISALLOW_COPY_AND_ASSIGN(ResourceMgr)
        ResourceMgr() = default;
        int Initialize() final;
        void Finalize() final;
        void Tick(f32 delta_time) final;
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
        void MigrateLegacyAssetDocuments(const WString &root_asset_dir = L"");
        Asset *GetLinkedAsset(Object *obj);
        //提交一个任务，该任务会在ResourceMgr tick时在主线程执行
        void SubmitTaskSync(ResourceTask task);
        void SubmitTaskSync(ResourceTask task,std::function<void(bool)> callback);
        //static Material* LoadAsset(const String& asset_path);
        Ref<void> ImportResource(const WString &sys_path, const WString &target_dir, const ImportSetting &setting = ImportSetting::Default());
        //async editon always reutn nullptr
        Ref<void> ImportResourceAsync(const WString &sys_path, const WString &target_dir, const ImportSetting &setting = ImportSetting::Default(), OnResourceTaskCompleted callback = [](Ref<void> asset) {});

        const WString &GetAssetPath(Object *obj) const;
        const Guid &GetAssetGuid(Object *obj) const;
        const WString &GuidToAssetPath(const Guid &guid) const;

        Asset *GetAsset(const WString &asset_path) const;
        Vector<Asset *> GetAssets(const ISearchFilter &filter) const;
        Asset *RegisterAsset(Scope<Asset> &&asset, bool override = true);
        void UnRegisterAsset(Asset *asset);
        void RegisterResource(const WString &asset_path, Ref<Object> obj, bool override = true);
        void UnRegisterResource(const WString &asset_path);

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
        Ref<T> Load(const WString &asset_path, const ImportSetting *setting = nullptr);
        template<typename T>
        Ref<T> Load(const Guid &guid, const ImportSetting *setting = nullptr);
        template<typename T>
        void LoadAsync(const WString &asset_path, const ImportSetting *setting = nullptr, OnLoadTaskCompleted<T> callback = {});
        template<typename T>
        void LoadAsync(const Guid &guid, const ImportSetting *setting = nullptr, OnLoadTaskCompleted<T> callback = {});
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
                LOG_INFO(L"Release resource: {},and current ref count is {}", asset_path.c_str(), ref_count - 1);
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

        Ref<Material> GetEmbeddedMaterial(Mesh *mesh,u16 slot);

    public:
        Ref<Font> _default_font;

    private:
        using Loader = std::function<Scope<Asset>(ResourceMgr *, const WString &, const ImportSetting &)>;

        static Loader GetAssetLoader(const Type *type);
        static WString GetAssetTypeName(const Type *type);
        static const Type *FindAssetType(const WString &type_name);

        static bool IsTypeCompatible(const Type *requested_type, const Type *actual_type)
        {
            while (actual_type != nullptr)
            {
                if (actual_type == requested_type)
                    return true;
                actual_type = actual_type->BaseType();
            }
            return false;
        }

        static const Type *GetObjectResourceType(Object *obj);

        static bool IsAssetType(const Asset *asset, const Type *type)
        {
            return asset != nullptr && asset->_asset_type == type;
        }

        template<typename T>
        static const ImportSetting *ResolveImportSetting(const ImportSetting *setting, const T *)
        {
            return setting ? setting : &ImportSetting::Default();
        }
        static const ImportSetting *ResolveImportSetting(const ImportSetting *setting, const Texture2D *)
        {
            return setting ? setting : &TextureImportSetting::Default();
        }

        template<typename T>
        static ImportSetting CopyAsyncImportSetting(const ImportSetting *setting, const T *)
        {
            return setting ? *setting : ImportSetting::Default();
        }
        static TextureImportSetting CopyAsyncImportSetting(const ImportSetting *setting, const Texture2D *)
        {
            auto typed_setting = dynamic_cast<const TextureImportSetting *>(setting);
            return typed_setting ? *typed_setting : TextureImportSetting::Default();
        }
        static MeshImportSetting CopyAsyncImportSetting(const ImportSetting *setting, const Mesh *)
        {
            auto typed_setting = dynamic_cast<const MeshImportSetting *>(setting);
            return typed_setting ? *typed_setting : MeshImportSetting::Default();
        }
        static MeshImportSetting CopyAsyncImportSetting(const ImportSetting *setting, const SkeletonMesh *)
        {
            auto typed_setting = dynamic_cast<const MeshImportSetting *>(setting);
            return typed_setting ? *typed_setting : MeshImportSetting::Default();
        }
        static ShaderImportSetting CopyAsyncImportSetting(const ImportSetting *setting, const Shader *)
        {
            auto typed_setting = dynamic_cast<const ShaderImportSetting *>(setting);
            return typed_setting ? *typed_setting : ShaderImportSetting::Default();
        }
        static ShaderImportSetting CopyAsyncImportSetting(const ImportSetting *setting, const ComputeShader *)
        {
            auto typed_setting = dynamic_cast<const ShaderImportSetting *>(setting);
            return typed_setting ? *typed_setting : ShaderImportSetting::Default();
        }

        static void FormatLine(const String &line, String &key, String &value);
        static void ExtractCommonAssetInfo(const WString &asset_path, WString &name, Guid &guid, const Type *&type);

        bool IsFileOnDiskUpdated(const WString &sys_path);
        void MarkFileTimeStamp(const WString &sys_path);

        bool ExistInAssetDB(const Asset *asset) const;
        bool ExistInAssetDB(const WString &asset_path) const;
        void RemoveFromAssetDB(const Asset *asset);
        bool IsAssetLoaded(const WString &asset_path) const;

        void LoadAssetDB(const AssetMountDomain& domain);
        void SaveAssetDB(EAssetDomain domain);

        void CreateAndRegisterEmbeddedMaterial(Mesh* mesh);

        //加载引擎处理后的资产
        Scope<Asset> LoadMaterial(const WString &asset_path,const ImportSetting& settings);
        Scope<Asset> LoadShader(const WString &asset_path,const ImportSetting& settings);
        Scope<Asset> LoadTexture(const WString &asset_path,const ImportSetting& settings);
        Scope<Asset> LoadMesh(const WString &asset_path,const ImportSetting& settings);
        Scope<Asset> LoadComputeShader(const WString &asset_path,const ImportSetting& settings);
        Scope<Asset> LoadScene(const WString &asset_path,const ImportSetting& settings);
        Scope<Asset> LoadAnimClip(const WString &asset_path,const ImportSetting& settings);
        //加载原始资产
        List<Ref<Mesh>> LoadExternalMesh(const WString &asset_path, const MeshImportSetting &setting, List<Ref<AnimationClip>> &clips);
        Ref<Texture2D> LoadExternalTexture(const WString &asset_path,const ImportSetting& settings);
        bool LoadExternalTexture(const WString &asset_path,Ref<Texture2D>& tex,const ImportSetting& settings);
        Ref<Shader> LoadExternalShader(const WString &asset_path);
        Ref<ComputeShader> LoadExternalComputeShader(const WString &asset_path);

        void SaveMaterial(const WString &asset_path, Material *mat);
        void SaveShader(const WString &asset_path, const Asset *asset);
        void SaveComputeShader(const WString &asset_path, const Asset *asset);
        void SaveMesh(const WString &asset_path, const Asset *asset);
        void SaveTexture2D(const WString &asset_path, const Asset *asset);
        void SaveScene(const WString &asset_path, const Asset *asset);
        void SaveAnimClip(const WString &asset_path, const Asset *asset);

        //导入外部资源并创建对应的asset
        Ref<void> ImportResourceImpl(const WString &sys_path,const WString& target_dir,const ImportSetting *setting);
        void OnAssetDataBaseChanged();

    private:
        inline static WString s_engine_res_root_path;
        inline static WString s_editor_res_root_path;
        inline static WString s_project_root_path;
        inline static WString s_project_asset_root_path;
        inline static WString s_project_library_root_path;
        inline static WString s_project_asset_database_path;

        inline static Map<u32, WString> s_object_sys_path_map;
        inline static Queue<Asset *> s_pending_save_assets;
        HashMap<WString, fs::file_time_type> _file_last_load_time;
        bool _is_watching_directory = true;
        WString _project_root_path;
        std::mutex _asset_db_mutex;
        //<GUID,Asset>
        std::map<Guid, Scope<Asset>> _asset_db{};
        //AssetPath,GUID
        std::map<WString, Guid> _asset_looktable{};
        //object_id,asset*
        std::map<u32, Asset *> _object_to_asset{};
        ResourcePoolContainer _global_resources;
        ResourcePoolLut _lut_global_resources;
        ResourceTypeLut _lut_global_resources_by_type;
        Vector<std::function<void()>> _asset_changed_callbacks;
        Queue<std::function<void()>> _sync_tasks;
        Queue<std::function<void()>> _async_tasks;
        Queue<Asset *> _pending_delete_assets;
        HashMap<WString, ImportSetting*> _importers;
        Vector<AssetMountDomain> _asset_domains;
    };
    template<typename T>
    inline Ref<T> ResourceMgr::Load(const WString &asset_path, const ImportSetting *setting)
    {
        if (asset_path.empty())
        {
            LOG_ERROR(L"ResourceMgr::Load: Asset path is empty");
            return nullptr;
        }
        LOG_WARNING(L"Begin load asset {}...", asset_path);
        TimeMgr timer;
        timer.Mark();
        using Loader = std::function<Scope<Asset>(ResourceMgr *, const WString &,const ImportSetting&)>;
        WString sys_path = ResourceMgr::GetResSysPath(asset_path);
        auto ext = PathUtils::ExtractExt(sys_path);
        bool is_engine_asset = ext == L".alasset" || L".almap";
        AL_ASSERT(is_engine_asset);
        WString asset_name;
        Guid guid;
        const Type *type = nullptr;
        ExtractCommonAssetInfo(asset_path, asset_name, guid, type);
        if (type == nullptr)
        {
            LOG_ERROR(L"Load asset {} failed with invalid asset type after {}ms", asset_path, timer.GetElapsedSinceLastMark());
            return nullptr;
        }
        auto requested_type = StaticClass<T>();
        auto asset_loader = GetAssetLoader(type);
        if (requested_type == nullptr || !IsTypeCompatible(requested_type, type))
        {
            LOG_ERROR(L"Load asset {} failed with mismatched asset type {} after {}ms", asset_path, GetAssetTypeName(type), timer.GetElapsedSinceLastMark());
            return nullptr;
        }
        if (asset_loader == nullptr)
        {
            LOG_ERROR(L"Load asset {} failed because no loader is registered for asset type {} after {}ms", asset_path, GetAssetTypeName(type), timer.GetElapsedSinceLastMark());
            return nullptr;
        }
        // bool is_skip_load = false;
        // if (!IsFileOnDiskUpdated(sys_path))
        // {
        //     LogMgr::Get().LogWarningFormat(L"Load asset {} succeed with everything is new after {}ms", asset_path, timer.GetElapsedSinceLastMark());
        //     return _global_resources.contains(asset_path) ? std::static_pointer_cast<T>(_global_resources[asset_path]) : nullptr;
        // }
        auto cur_setting = ResolveImportSetting(setting, static_cast<const T *>(nullptr));
        if (!IsAssetLoaded(asset_path))
        {
            Scope<Asset> out_asset;
            out_asset = std::move(asset_loader(this, asset_path,*cur_setting));
            if (out_asset != nullptr)
            {
                out_asset->_name = asset_name;
                out_asset->AssignGuid(guid);
                RegisterResource(asset_path, out_asset->_p_obj);
                RegisterAsset(std::move(out_asset));
                MarkFileTimeStamp(sys_path);
                LOG_WARNING(L"Load asset {} succeed after {} ms", asset_path, timer.GetElapsedSinceLastMark());
            }
            else
            LOG_ERROR(L"Load asset {} failed after {} ms", asset_path, timer.GetElapsedSinceLastMark());
        }
        else
        {
            if (cur_setting->_is_reimport)
            {
                asset_loader(this, asset_path,*cur_setting);
                MarkFileTimeStamp(sys_path);
                LOG_WARNING(L"Reload asset {} after {} ms", asset_path, timer.GetElapsedSinceLastMark());
            }
        }

        return _global_resources.contains(asset_path) ? std::static_pointer_cast<T>(_global_resources[asset_path]) : nullptr;
    }
    template<typename T>
    inline Ref<T> ResourceMgr::Load(const Guid &guid, const ImportSetting *setting)
    {
        return Load<T>(GuidToAssetPath(guid),setting);
    }

    template<typename T>
    inline void ResourceMgr::LoadAsync(const WString &asset_path, const ImportSetting *setting, OnLoadTaskCompleted<T> callback)
    {
        auto execute_callback = [callback = std::move(callback)](Ref<T> asset) mutable
        {
            if (callback)
                callback(std::move(asset));
        };

        auto async_setting = CopyAsyncImportSetting(setting, static_cast<const T *>(nullptr));
        Core::ThreadPool::Get().Enqueue([this, asset_path, async_setting, callback = std::move(execute_callback)]() mutable
                               {
                                   auto asset = this->Load<T>(asset_path, &async_setting);
                                   callback(std::move(asset));
                               });
    }
    template<typename T>
    inline void ResourceMgr::LoadAsync(const Guid &guid, const ImportSetting *setting, OnLoadTaskCompleted<T> callback)
    {
        auto execute_callback = [callback = std::move(callback)](Ref<T> asset) mutable
        {
            if (callback)
                callback(std::move(asset));
        };

        auto async_setting = CopyAsyncImportSetting(setting, static_cast<const T *>(nullptr));
        Core::ThreadPool::Get().Enqueue([this, guid, async_setting, callback = std::move(execute_callback)]() mutable
                               {
                                   auto asset = this->Load<T>(guid, &async_setting);
                                   callback(std::move(asset));
                               });
    }
    template<typename T>
    inline T *ResourceMgr::Get(const WString &res_id)
    {
        if (_global_resources.contains(res_id))
        {
            return std::static_pointer_cast<T>(_global_resources[res_id]).get();
        }
        return nullptr;
    }
    template<typename T>
    inline Ref<T> ResourceMgr::GetRef(const WString &res_id)
    {
        if (_global_resources.contains(res_id))
        {
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
        auto resource_type = StaticClass<T>();
        AL_ASSERT(resource_type != nullptr);
        return _lut_global_resources_by_type.contains(resource_type) ? (u32) _lut_global_resources_by_type.at(resource_type).size() : 0u;
    }
    template<typename T>
    inline auto ResourceMgr::ResourceBegin()
    {
        auto resource_type = StaticClass<T>();
        AL_ASSERT(resource_type != nullptr);
        return _lut_global_resources_by_type[resource_type].begin();
    }
    template<typename T>
    inline auto ResourceMgr::ResourceEnd()
    {
        auto resource_type = StaticClass<T>();
        AL_ASSERT(resource_type != nullptr);
        return _lut_global_resources_by_type[resource_type].end();
    }
    template<typename T>
    inline Ref<T> ResourceMgr::IterToRefPtr(const Vector<ResourcePoolContainer::iterator>::iterator &iter)
    {
        auto t_ptr = std::dynamic_pointer_cast<T>((*iter)->second);
        AL_ASSERT(t_ptr != nullptr);
        return t_ptr;
    }
}// namespace Ailu

#endif// !RESOURCE_MGR_H__
#pragma warning(pop)