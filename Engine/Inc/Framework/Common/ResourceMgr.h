#pragma warning(push)
#pragma warning(disable : 4251)//disable dll export warning

#pragma once
#ifndef __RESOURCE_MGR_H__
#define __RESOURCE_MGR_H__
#include "Framework/Common/NonCopyable.h"
#include "FileManager.h"
#include "Assets/Asset.h"
#include "Framework/Common/Utils.h"
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
    class FileWatchService;
    using AssetPath = WString;
    using SystemPath = WString;

    class AILU_API ResourceMgr : public NonCopyable
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
        inline const static std::set<String> kAudioExt = {".wav", ".WAV", ".mp3", ".MP3", ".flac", ".FLAC", ".ogg", ".OGG"};
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
        static WString NormalizeAssetPath(const WString &asset_path, EAssetDomain default_domain = EAssetDomain::kEngine);
        static EAssetDomain GetAssetPathDomain(const WString &asset_path, EAssetDomain default_domain = EAssetDomain::kEngine);

        ResourceMgr() = default;
        int Initialize();
        void Finalize();
        void Tick(f32 delta_time);
        auto Begin() { return _asset_db.begin(); };
        auto End() { return _asset_db.end(); };
        u64 AssetNum() { return _asset_db.size(); }
        //在目标位置创建资产对象并将其注册
        Asset *CreateAsset(const WString &asset_path, Ref<Object> obj, bool overwrite = true);
        void DeleteAsset(Asset *asset);
        bool RenameAsset(Asset *p_asset, const WString &new_name);
        bool MoveAsset(Asset *p_asset, const WString &new_asset_path);
        bool SaveAsset(Asset *asset);
        void SaveAllDirtyAssets();
        void SaveAllUnsavedAssets();
        //由 Editor 编辑操作标记 Asset 为 Dirty（revision 递增）。
        void MarkAssetDirty(Asset *asset);
        void MarkAssetDirty(Object *obj);
        //Editor 启动时注入文件监视服务，用于保存后同步 watcher baseline。
        static void SetFileWatchService(FileWatchService *service);
        static FileWatchService *GetFileWatchService();
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
        Ref<Object> Load(const WString& p,const ImportSetting* settings,const Type* type);
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
            std::lock_guard<std::mutex> lock(_asset_db_mutex);
            const WString resource_path = _global_resources.contains(asset_path) ? asset_path : NormalizeAssetPath(asset_path);
            if (_global_resources.contains(resource_path))
            {
                auto ref_count = _global_resources[resource_path].use_count();
                _global_resources.erase(resource_path);
                RebuildResourceLookups();
                LOG_INFO(L"Release resource: {},and current ref count is {}", resource_path.c_str(), ref_count - 1);
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

        // External resource loaders (used by asset handlers)
        List<Ref<Mesh>> LoadExternalMesh(const WString &asset_path, const MeshImportSetting &setting, List<Ref<AnimationClip>> &clips);
        Ref<Texture2D> LoadExternalTexture(const WString &asset_path,const ImportSetting& settings);
        bool LoadExternalTexture(const WString &asset_path,Ref<Texture2D>& tex,const ImportSetting& settings);
        Ref<Shader> LoadExternalShader(const WString &asset_path);
        Ref<ComputeShader> LoadExternalComputeShader(const WString &asset_path);
        void CreateAndRegisterEmbeddedMaterial(Mesh* mesh);

        // Import settings management
        ImportSetting* GetImportSetting(const WString &asset_path) const;
        void SetImportSetting(const WString &asset_path, ImportSetting *setting);

        // Handler lookup (used by GetAssetLoader lambda which needs public access)
        IAssetHandler* FindAssetHandler(const Type *asset_type) const;

    public:
        Ref<Font> _default_font;

    private:
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

        static bool IsAssetType(const Asset *asset, const Type *type)
        {
            return asset != nullptr && asset->_asset_type == type;
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

        bool ExistInAssetDB(const Asset *asset) const;
        bool ExistInAssetDB(const WString &asset_path) const;
        void RemoveFromAssetDB(const Asset *asset);
        bool IsAssetLoaded(const WString &asset_path) const;

        void LoadAssetDB(const AssetMountDomain& domain);
        void SaveAssetDB(EAssetDomain domain);
        void RebuildResourceLookups();

        //导入外部资源并创建对应的asset
        Ref<void> ImportResourceImpl(const WString &sys_path,const WString& target_dir,const ImportSetting *setting);
        void OnAssetDataBaseChanged();

    private:
        class AssetHandlerRegistry
        {
        public:
            void Register(Scope<IAssetHandler> handler)
            {
                AL_ASSERT(handler != nullptr);
                const Type *asset_type = handler->AssetType();
                AL_ASSERT(asset_type != nullptr);
                _handlers[asset_type] = std::move(handler);
            }

            IAssetHandler *Find(const Type *asset_type) const
            {
                auto iter = _handlers.find(asset_type);
                return iter == _handlers.end() ? nullptr : iter->second.get();
            }

        private:
            Map<const Type *, Scope<IAssetHandler>> _handlers;
        };

        inline static WString s_engine_res_root_path;
        inline static WString s_editor_res_root_path;
        inline static WString s_project_root_path;
        inline static WString s_project_asset_root_path;
        inline static WString s_project_library_root_path;
        inline static WString s_project_asset_database_path;

        inline static Map<u32, WString> s_object_sys_path_map;
        inline static Queue<Asset *> s_pending_save_assets;
        inline static FileWatchService *s_p_file_watch_service = nullptr;
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
        AssetHandlerRegistry _asset_handler_registry;
    };

    template<typename T>
    inline Ref<T> ResourceMgr::Load(const WString &asset_path, const ImportSetting *setting)
    {
        return std::static_pointer_cast<T>(Load(asset_path,setting,T::StaticType()));
    }
    template<typename T>
    inline Ref<T> ResourceMgr::Load(const Guid &guid, const ImportSetting *setting)
    {
        return std::static_pointer_cast<T>(Load(GuidToAssetPath(guid),setting,T::StaticType()));
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
        const WString normalized_asset_path = NormalizeAssetPath(res_id);
        if (normalized_asset_path != res_id && _global_resources.contains(normalized_asset_path))
        {
            return std::static_pointer_cast<T>(_global_resources[normalized_asset_path]).get();
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
        const WString normalized_asset_path = NormalizeAssetPath(res_id);
        if (normalized_asset_path != res_id && _global_resources.contains(normalized_asset_path))
        {
            return std::static_pointer_cast<T>(_global_resources[normalized_asset_path]);
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
