#include "Framework/Common/FileWatcher.h"
#include "Framework/Common/Path.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Script/ScriptSystem.h"
#include "Render/GraphicsContext.h"
#include "Render/RayTracing/RayTracingShader.h"
#include "Render/Shader.h"
#include <algorithm>
#include <cwctype>

namespace Ailu
{
    namespace fs = std::filesystem;

    namespace
    {
        bool IsReloadableChange(FileChangeType type)
        {
            return type == FileChangeType::kAdded || type == FileChangeType::kModified || type == FileChangeType::kRenamed;
        }
    }

    void FileWatchService::AddDirectory(const fs::path &path)
    {
        _directories.emplace_back(path);
    }

    void FileWatchService::AddFile(const fs::path &path)
    {
        _files.emplace_back(path);
    }

    void FileWatchService::Clear()
    {
        _directories.clear();
        _files.clear();
        _known_files.clear();
        _has_snapshot = false;
    }

    void FileWatchService::Snapshot()
    {
        std::set<fs::path> files;
        CollectWatchedFiles(files);

        _known_files.clear();
        for (const auto &file: files)
            _known_files[file] = fs::last_write_time(file);
        _has_snapshot = true;
    }

    Vector<FileChangeEvent> FileWatchService::PollChanges()
    {
        Vector<FileChangeEvent> events;
        std::set<fs::path> files;
        CollectWatchedFiles(files);

        if (!_has_snapshot)
        {
            for (const auto &file: files)
                _known_files[file] = fs::last_write_time(file);
            _has_snapshot = true;
            return events;
        }

        HashMap<fs::path, fs::file_time_type> current_files;
        for (const auto &file: files)
            current_files[file] = fs::last_write_time(file);

        for (const auto &[file, last_write_time]: current_files)
        {
            auto it = _known_files.find(file);
            if (it == _known_files.end())
            {
                events.emplace_back(FileChangeEvent{FileChangeType::kAdded, NormalizePath(file), L""});
                continue;
            }
            if (it->second != last_write_time)
                events.emplace_back(FileChangeEvent{FileChangeType::kModified, NormalizePath(file), L""});
        }

        for (const auto &[file, last_write_time]: _known_files)
        {
            if (!current_files.contains(file))
                events.emplace_back(FileChangeEvent{FileChangeType::kRemoved, NormalizePath(file), L""});
        }

        _known_files = std::move(current_files);
        return events;
    }

    void FileWatchService::AcknowledgeWrite(const fs::path &path)
    {
        if (!fs::exists(path))
            return;

        const WString normalized_input = NormalizePath(path);
        const fs::file_time_type write_time = fs::last_write_time(path);

        for (auto &[known_path, known_time]: _known_files)
        {
            if (NormalizePath(known_path) == normalized_input)
            {
                known_time = write_time;
                return;
            }
        }

        fs::path native_key = path;
        native_key.make_preferred();
        _known_files.emplace(std::move(native_key), write_time);
    }

    void FileWatchService::CollectWatchedFiles(std::set<fs::path> &out_files) const
    {
        for (const auto &dir: _directories)
        {
            if (!fs::exists(dir))
                continue;

            for (const auto &entry: fs::recursive_directory_iterator(dir))
            {
                if (entry.is_regular_file())
                    out_files.insert(entry.path());
            }
        }

        for (const auto &file: _files)
        {
            if (fs::exists(file) && fs::is_regular_file(file))
                out_files.insert(file);
        }
    }

    WString FileWatchService::NormalizePath(const fs::path &path)
    {
        return PathUtils::FormatFilePath(path.wstring());
    }

    void ResourceReloadService::SetEngineConfigPath(const WString &path)
    {
        _engine_config_path = PathUtils::FormatFilePath(path);
    }

    void ResourceReloadService::SetConfigReloadCallback(ConfigReloadCallback callback)
    {
        _config_reload_callback = std::move(callback);
    }

    void ResourceReloadService::ReloadForFile(const FileChangeEvent &event)
    {
        if (!IsReloadableChange(event._type))
            return;

        const WString sys_path = PathUtils::FormatFilePath(event._path);
        u16 reload_shader_count = 0;
        u16 reload_compute_count = 0;
        u16 reload_rt_shader_count = 0;
        u16 reload_tex2d_count = 0;

        if (ReloadConfig(sys_path))
            return;

        if (HasExtension(sys_path, {L".hlsl", L".hlsli"}))
        {
            reload_shader_count = ReloadShaderDependencies(sys_path);
            reload_compute_count = ReloadComputeShaderDependencies(sys_path);
            reload_rt_shader_count = ReloadRayTracingShaderDependencies(sys_path);
        }

        if (HasExtension(sys_path, {L".png", L".tga", L".jpg", L".jpeg", L".exr", L".hdr", L".dds"}))
            reload_tex2d_count = ReloadTextureDependencies(sys_path);

        if (HasExtension(sys_path, {L".lua"}))
            ReloadScript(fs::path(sys_path));

        if ((reload_shader_count + reload_compute_count + reload_rt_shader_count + reload_tex2d_count) > 0)
        {
            LOG_INFO("Reload {} shader, {} compute shader, {} ray tracing shader, {} texture2d",
                     reload_shader_count, reload_compute_count, reload_rt_shader_count, reload_tex2d_count);
        }
    }

    u16 ResourceReloadService::ReloadShaderDependencies(const WString &sys_path) const
    {
        u16 count = 0;
        for (auto it = ResourceMgr::Get().ResourceBegin<Render::Shader>(); it != ResourceMgr::Get().ResourceEnd<Render::Shader>(); it++)
        {
            auto shader = ResourceMgr::IterToRefPtr<Render::Shader>(it);
            if (!shader)
                continue;

            bool match_file = false;
            for (i16 i = 0; i < shader->PassCount(); i++)
            {
                if (match_file)
                    break;
                const auto &pass = shader->GetPassInfo(i);
                for (const auto &source_file: pass._source_files)
                {
                    if (source_file == sys_path)
                    {
                        match_file = true;
                        Render::GraphicsContext::Get().CompileShaderAsync(shader.get());
                        ++count;
                        break;
                    }
                }
            }
        }
        return count;
    }

    u16 ResourceReloadService::ReloadComputeShaderDependencies(const WString &sys_path) const
    {
        u16 count = 0;
        for (auto it = ResourceMgr::Get().ResourceBegin<Render::ComputeShader>(); it != ResourceMgr::Get().ResourceEnd<Render::ComputeShader>(); it++)
        {
            auto cs = ResourceMgr::IterToRefPtr<Render::ComputeShader>(it);
            if (cs && cs->IsDependencyFile(sys_path))
            {
                Render::GraphicsContext::Get().CompileShaderAsync(cs.get());
                ++count;
            }
        }
        return count;
    }

    u16 ResourceReloadService::ReloadRayTracingShaderDependencies(const WString &sys_path) const
    {
        u16 count = 0;
        for (auto *shader: Render::RayTracingShader::GetLiveInstances())
        {
            if (shader != nullptr && shader->IsDependencyFile(sys_path))
            {
                Render::GraphicsContext::Get().CompileShaderAsync(shader);
                ++count;
            }
        }
        return count;
    }

    u16 ResourceReloadService::ReloadTextureDependencies(const WString &sys_path) const
    {
        u16 count = 0;
        const WString cur_asset_path = PathUtils::ExtractAssetPath(sys_path);
        for (auto it = ResourceMgr::Get().ResourceBegin<Render::Texture2D>(); it != ResourceMgr::Get().ResourceEnd<Render::Texture2D>(); it++)
        {
            auto tex = ResourceMgr::IterToRefPtr<Render::Texture2D>(it);
            auto linked_asset = ResourceMgr::Get().GetLinkedAsset(tex.get());
            if (linked_asset && !linked_asset->_external_asset_path.empty() && cur_asset_path == linked_asset->_external_asset_path)
            {
                LOG_WARNING(L"Texture2d {} has changed,but reload not support yet", linked_asset->_asset_path);
                ++count;
            }
        }
        return count;
    }

    bool ResourceReloadService::ReloadConfig(const WString &sys_path) const
    {
        if (_engine_config_path.empty() || sys_path != _engine_config_path)
            return false;

        if (_config_reload_callback)
            _config_reload_callback();
        return true;
    }

    bool ResourceReloadService::ReloadScript(const fs::path &sys_path) const
    {
        ScriptSystem::Get().OnScriptFileChanged(sys_path);
        return true;
    }

    bool ResourceReloadService::HasExtension(const WString &path, std::initializer_list<WStringView> exts)
    {
        const WString ext = fs::path(path).extension().wstring();
        for (auto candidate: exts)
        {
            if (_wcsicmp(ext.c_str(), WString(candidate).c_str()) == 0)
                return true;
        }
        return false;
    }

    void FileChangeDispatcher::SetReloadService(ResourceReloadService *reload_service)
    {
        _reload_service = reload_service;
    }

    void FileChangeDispatcher::RegisterPathHandler(const WString &path, Handler handler)
    {
        _path_handlers[NormalizePath(path)].emplace_back(std::move(handler));
    }

    void FileChangeDispatcher::RegisterExtensionHandler(const WString &ext, Handler handler)
    {
        WString normalized_ext = ext;
        if (!normalized_ext.empty() && normalized_ext.front() != L'.')
            normalized_ext.insert(normalized_ext.begin(), L'.');
        std::transform(normalized_ext.begin(), normalized_ext.end(), normalized_ext.begin(), ::towlower);
        _extension_handlers[normalized_ext].emplace_back(std::move(handler));
    }

    void FileChangeDispatcher::AddPostDispatchHandler(Handler handler)
    {
        _post_dispatch_handlers.emplace_back(std::move(handler));
    }

    void FileChangeDispatcher::Dispatch(const FileChangeEvent &event) const
    {
        FileChangeEvent normalized_event = event;
        normalized_event._path = NormalizePath(event._path);
        if (!normalized_event._old_path.empty())
            normalized_event._old_path = NormalizePath(event._old_path);

        if (auto it = _path_handlers.find(normalized_event._path); it != _path_handlers.end())
        {
            for (const auto &handler: it->second)
                handler(normalized_event);
        }

        const WString ext = NormalizeExtension(fs::path(normalized_event._path));
        if (auto it = _extension_handlers.find(ext); it != _extension_handlers.end())
        {
            for (const auto &handler: it->second)
                handler(normalized_event);
        }

        if (_reload_service)
            _reload_service->ReloadForFile(normalized_event);

        for (const auto &handler: _post_dispatch_handlers)
            handler(normalized_event);
    }

    WString FileChangeDispatcher::NormalizePath(const WString &path)
    {
        return PathUtils::FormatFilePath(path);
    }

    WString FileChangeDispatcher::NormalizeExtension(const fs::path &path)
    {
        WString ext = path.extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        return ext;
    }
} // namespace Ailu
