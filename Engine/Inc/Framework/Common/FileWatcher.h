#ifndef __FILE_WATCHER_H__
#define __FILE_WATCHER_H__
#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/String.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/Containers/Map.h"
#include <filesystem>
#include <functional>
#include <set>

namespace Ailu
{
    enum class FileChangeType
    {
        kAdded,
        kModified,
        kRemoved,
        kRenamed
    };

    struct FileChangeEvent
    {
        FileChangeType _type = FileChangeType::kModified;
        WString _path;
        WString _old_path;//for rename
    };

    class AILU_API FileWatchService
    {
    public:
        void AddDirectory(const std::filesystem::path &path);
        void AddFile(const std::filesystem::path &path);
        void Clear();
        void Snapshot();
        Vector<FileChangeEvent> PollChanges();
        //接受一次由引擎自身写入磁盘产生的修改，作为新的 watcher baseline。
        void AcknowledgeWrite(const std::filesystem::path &path);

    private:
        void CollectWatchedFiles(std::set<std::filesystem::path> &out_files) const;
        static WString NormalizePath(const std::filesystem::path &path);

    private:
        Vector<std::filesystem::path> _directories;
        Vector<std::filesystem::path> _files;
        HashMap<std::filesystem::path, std::filesystem::file_time_type> _known_files;
        bool _has_snapshot = false;
    };

    class AILU_API ResourceReloadService
    {
    public:
        using ConfigReloadCallback = std::function<void()>;

        void SetEngineConfigPath(const WString &path);
        void SetConfigReloadCallback(ConfigReloadCallback callback);
        void ReloadForFile(const FileChangeEvent &event);

        u16 ReloadShaderDependencies(const WString &sys_path) const;
        u16 ReloadComputeShaderDependencies(const WString &sys_path) const;
        u16 ReloadRayTracingShaderDependencies(const WString &sys_path) const;
        u16 ReloadTextureDependencies(const WString &sys_path) const;
        bool ReloadConfig(const WString &sys_path) const;
        bool ReloadScript(const std::filesystem::path &sys_path) const;

    private:
        static bool HasExtension(const WString &path, std::initializer_list<WStringView> exts);

    private:
        WString _engine_config_path;
        ConfigReloadCallback _config_reload_callback;
    };

    class AILU_API FileChangeDispatcher
    {
    public:
        using Handler = std::function<void(const FileChangeEvent &)>;

        void SetReloadService(ResourceReloadService *reload_service);
        void RegisterPathHandler(const WString &path, Handler handler);
        void RegisterExtensionHandler(const WString &ext, Handler handler);
        void AddPostDispatchHandler(Handler handler);
        void Dispatch(const FileChangeEvent &event) const;

    private:
        static WString NormalizePath(const WString &path);
        static WString NormalizeExtension(const std::filesystem::path &path);

    private:
        ResourceReloadService *_reload_service = nullptr;
        HashMap<WString, Vector<Handler>> _path_handlers;
        HashMap<WString, Vector<Handler>> _extension_handlers;
        Vector<Handler> _post_dispatch_handlers;
    };
}// namespace Ailu
#endif//__FILE_WATCHER_H__
