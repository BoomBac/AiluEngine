#include "pch.h"

#include <fstream>
#include <array>
#include <type_traits>
#include <Windows.h>

#include "Framework/Common/Application.h"
#include "Framework/Common/FileManager.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/Path.h"
#include "Render/ShaderArtifactCache.h"
#include "Render/ShaderDependencyRegistry.h"
#include "Render/ShaderDependencyPath.h"

namespace Ailu::Render
{
    namespace
    {
        u64 HashFile(const WString &path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open())
                return 0u;

            constexpr u64 kFnvOffset = 14695981039346656037ull;
            constexpr u64 kFnvPrime = 1099511628211ull;
            u64 hash = kFnvOffset;
            std::array<char, 64u * 1024u> buffer{};
            while (file.good())
            {
                file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const auto read_size = file.gcount();
                for (std::streamsize index = 0; index < read_size; ++index)
                {
                    hash ^= static_cast<u8>(buffer[static_cast<size_t>(index)]);
                    hash *= kFnvPrime;
                }
            }
            return hash;
        }

        ShaderFileFingerprint ReadFingerprint(const WString &path)
        {
            ShaderFileFingerprint fingerprint;
            const fs::path file_path(PathUtils::FormatFilePath(path));
            std::error_code error;
            const auto status = fs::status(file_path, error);
            if (error || !fs::is_regular_file(status))
                return fingerprint;
            fingerprint._exists = true;
            fingerprint._file_size = fs::file_size(file_path, error);
            if (error)
                return ShaderFileFingerprint{};
            const auto write_time = fs::last_write_time(file_path, error);
            if (error)
                return ShaderFileFingerprint{};
            fingerprint._last_write_time = static_cast<u64>(write_time.time_since_epoch().count());
            return fingerprint;
        }

        u64 GetSourceFingerprint(const Vector<ShaderArtifactDependency> &dependencies)
        {
            return dependencies.empty() ? 0u : dependencies.front()._content_hash;
        }

        void SetReason(EShaderArtifactInvalidReason *reason, EShaderArtifactInvalidReason value)
        {
            if (reason)
                *reason = value;
        }
    }

    ShaderFileFingerprintCache &ShaderFileFingerprintCache::Get()
    {
        static ShaderFileFingerprintCache s_cache;
        return s_cache;
    }

    const ShaderFileFingerprint &ShaderFileFingerprintCache::GetFingerprint(const WString &path)
    {
        const WString normalized_path = NormalizeShaderDependencyPath(path);
        std::lock_guard lock(_mutex);
        auto it = _fingerprints.find(normalized_path);
        if (it == _fingerprints.end())
        {
            auto fingerprint = ReadFingerprint(normalized_path);
            it = _fingerprints.emplace(normalized_path, fingerprint).first;
        }
        return it->second;
    }

    u64 ShaderFileFingerprintCache::GetContentHash(const WString &path)
    {
        const WString normalized_path = NormalizeShaderDependencyPath(path);
        std::lock_guard lock(_mutex);
        auto it = _fingerprints.find(normalized_path);
        if (it == _fingerprints.end())
        {
            auto fingerprint = ReadFingerprint(normalized_path);
            it = _fingerprints.emplace(normalized_path, fingerprint).first;
        }
        if (!it->second._exists)
            return 0u;
        if (it->second._content_hash == 0u)
            it->second._content_hash = HashFile(normalized_path);
        return it->second._content_hash;
    }

    void ShaderFileFingerprintCache::Invalidate(const WString &path)
    {
        std::lock_guard lock(_mutex);
        _fingerprints.erase(NormalizeShaderDependencyPath(path));
    }

    void ShaderFileFingerprintCache::Clear()
    {
        std::lock_guard lock(_mutex);
        _fingerprints.clear();
    }

    ShaderArtifactCache &ShaderArtifactCache::Get()
    {
        static ShaderArtifactCache s_cache;
        return s_cache;
    }

    WString ShaderArtifactCache::GetArtifactPath(const ShaderArtifactKey &key) const
    {
        const u64 artifact_key = key.Hash();
        const WString root = Application::GetWorkingPath() + L"cache/shader_artifacts/" + ToWChar(key._backend) + L"/";
        const WString prefix = std::format(L"{:02x}", static_cast<u32>(artifact_key & 0xffu));
        return PathUtils::FormatFilePath(root + prefix + std::format(L"/{:016x}.ashader", artifact_key));
    }

    template<typename Artifact>
    bool ShaderArtifactCache::ValidateDependencies(Artifact &artifact, EShaderArtifactInvalidReason *reason)
    {
        bool valid = true;
        bool metadata_changed = false;
        for (size_t dependency_index = 0u; dependency_index < artifact._dependencies.size(); ++dependency_index)
        {
            auto &dependency = artifact._dependencies[dependency_index];
            const auto &current = ShaderFileFingerprintCache::Get().GetFingerprint(dependency._path);
            if (!current._exists)
            {
                SetReason(reason, EShaderArtifactInvalidReason::kDependencyChanged);
                valid = false;
                break;
            }
            if (current._file_size == dependency._file_size && current._last_write_time == dependency._last_write_time)
                continue;
            const u64 current_content_hash = ShaderFileFingerprintCache::Get().GetContentHash(dependency._path);
            if (current_content_hash == dependency._content_hash)
            {
                dependency._file_size = current._file_size;
                dependency._last_write_time = current._last_write_time;
                metadata_changed = true;
                continue;
            }
            SetReason(reason, dependency_index == 0u ? EShaderArtifactInvalidReason::kSourceChanged
                                                      : EShaderArtifactInvalidReason::kDependencyChanged);
            valid = false;
            break;
        }
        if (valid && metadata_changed)
            artifact._header._source_fingerprint = GetSourceFingerprint(artifact._dependencies);
        return valid;
    }

    template<typename Artifact>
    bool ShaderArtifactCache::LoadImpl(const ShaderArtifactKey &key, Artifact &artifact,
                                       EShaderArtifactInvalidReason *reason)
    {
        SetReason(reason, EShaderArtifactInvalidReason::kNotFound);
        const u64 artifact_key = key.Hash();
        bool memory_hit = false;
        {
            std::lock_guard lock(_mutex);
            if constexpr (std::is_same_v<Artifact, GraphicsShaderVariantArtifact>)
            {
                auto it = _graphics_artifacts.find(artifact_key);
                if (it != _graphics_artifacts.end())
                {
                    artifact = it->second;
                    memory_hit = artifact._header._artifact_key == artifact_key && ValidateDependencies(artifact, reason);
                    if (memory_hit)
                        it->second = artifact;
                    else
                        _graphics_artifacts.erase(it);
                }
            }
            else if constexpr (std::is_same_v<Artifact, ComputeShaderVariantArtifact>)
            {
                auto it = _compute_artifacts.find(artifact_key);
                if (it != _compute_artifacts.end())
                {
                    artifact = it->second;
                    memory_hit = artifact._header._artifact_key == artifact_key && ValidateDependencies(artifact, reason);
                    if (memory_hit)
                        it->second = artifact;
                    else
                        _compute_artifacts.erase(it);
                }
            }
            else
            {
                auto it = _ray_tracing_artifacts.find(artifact_key);
                if (it != _ray_tracing_artifacts.end())
                {
                    artifact = it->second;
                    memory_hit = artifact._header._artifact_key == artifact_key && ValidateDependencies(artifact, reason);
                    if (memory_hit)
                        it->second = artifact;
                    else
                        _ray_tracing_artifacts.erase(it);
                }
            }
        }
        if (memory_hit)
        {
            if constexpr (std::is_same_v<Artifact, GraphicsShaderVariantArtifact>)
                ++_stats._graphics_hit_count;
            else if constexpr (std::is_same_v<Artifact, ComputeShaderVariantArtifact>)
                ++_stats._compute_hit_count;
            else
                ++_stats._ray_tracing_hit_count;
            ShaderDependencyRegistry::Get().Register(artifact_key, artifact._dependencies);
            return true;
        }

        const WString artifact_path = GetArtifactPath(key);
        std::ifstream file(artifact_path, std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            if constexpr (std::is_same_v<Artifact, GraphicsShaderVariantArtifact>)
                ++_stats._graphics_miss_count;
            else if constexpr (std::is_same_v<Artifact, ComputeShaderVariantArtifact>)
                ++_stats._compute_miss_count;
            else
                ++_stats._ray_tracing_miss_count;
            return false;
        }

        const auto size = file.tellg();
        if (size <= 0)
        {
            SetReason(reason, EShaderArtifactInvalidReason::kCorrupted);
            return false;
        }
        Vector<u8> data(static_cast<size_t>(size));
        file.seekg(0, std::ios::beg);
        if (!file.read(reinterpret_cast<char *>(data.data()), size))
        {
            SetReason(reason, EShaderArtifactInvalidReason::kCorrupted);
            return false;
        }
        _stats._artifact_bytes_read.fetch_add(data.size());
        if (!ShaderArtifactSerializer::Deserialize(data, artifact, reason) ||
            artifact._header._artifact_key != artifact_key || artifact._header._type != key._type ||
            artifact._header._dependency_count != artifact._dependencies.size())
        {
            SetReason(reason, EShaderArtifactInvalidReason::kCorrupted);
            if constexpr (std::is_same_v<Artifact, GraphicsShaderVariantArtifact>)
                ++_stats._graphics_miss_count;
            else if constexpr (std::is_same_v<Artifact, ComputeShaderVariantArtifact>)
                ++_stats._compute_miss_count;
            else
                ++_stats._ray_tracing_miss_count;
            return false;
        }
        if (!ValidateDependencies(artifact, reason))
        {
            ++_stats._invalid_dependency_count;
            if constexpr (std::is_same_v<Artifact, GraphicsShaderVariantArtifact>)
                ++_stats._graphics_miss_count;
            else if constexpr (std::is_same_v<Artifact, ComputeShaderVariantArtifact>)
                ++_stats._compute_miss_count;
            else
                ++_stats._ray_tracing_miss_count;
            return false;
        }
        {
            std::lock_guard lock(_mutex);
            if constexpr (std::is_same_v<Artifact, GraphicsShaderVariantArtifact>)
                _graphics_artifacts[artifact_key] = artifact;
            else if constexpr (std::is_same_v<Artifact, ComputeShaderVariantArtifact>)
                _compute_artifacts[artifact_key] = artifact;
            else
                _ray_tracing_artifacts[artifact_key] = artifact;
        }
        ShaderDependencyRegistry::Get().Register(artifact_key, artifact._dependencies);
        if constexpr (std::is_same_v<Artifact, GraphicsShaderVariantArtifact>)
            ++_stats._graphics_hit_count;
        else if constexpr (std::is_same_v<Artifact, ComputeShaderVariantArtifact>)
            ++_stats._compute_hit_count;
        else
            ++_stats._ray_tracing_hit_count;
        return true;
    }

    template<typename Artifact>
    bool ShaderArtifactCache::StoreImpl(const ShaderArtifactKey &key, const Artifact &artifact)
    {
        Artifact stored = artifact;
        stored._header._type = key._type;
        stored._header._artifact_key = key.Hash();
        stored._header._dependency_count = static_cast<u32>(stored._dependencies.size());
        stored._header._source_fingerprint = GetSourceFingerprint(stored._dependencies);
        Vector<u8> data;
        if (!ShaderArtifactSerializer::Serialize(stored, data))
            return false;

        const WString target_path = GetArtifactPath(key);
        const fs::path target(target_path);
        const fs::path temporary = target.wstring() + L".tmp";
        auto remove_temporary = [&]
        {
            std::error_code ignored;
            fs::remove(temporary, ignored);
        };
        try
        {
            FileManager::CreateDirectory(target.parent_path().wstring());
            {
                std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
                if (!file.is_open())
                {
                    remove_temporary();
                    return false;
                }
                file.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
                file.flush();
                if (!file.good())
                {
                    remove_temporary();
                    return false;
                }
            }
            if (MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
            {
                remove_temporary();
                return false;
            }
        }
        catch (const fs::filesystem_error &error)
        {
            remove_temporary();
            LOG_WARNING("ShaderArtifactCache: store failed: {}", error.what());
            return false;
        }
        {
            std::lock_guard lock(_mutex);
            if constexpr (std::is_same_v<Artifact, GraphicsShaderVariantArtifact>)
                _graphics_artifacts[key.Hash()] = stored;
            else if constexpr (std::is_same_v<Artifact, ComputeShaderVariantArtifact>)
                _compute_artifacts[key.Hash()] = stored;
            else
                _ray_tracing_artifacts[key.Hash()] = stored;
        }
        ShaderDependencyRegistry::Get().Register(key.Hash(), stored._dependencies);
        _stats._artifact_bytes_written.fetch_add(data.size());
        return true;
    }

    bool ShaderArtifactCache::Load(const ShaderArtifactKey &key, GraphicsShaderVariantArtifact &artifact,
                                   EShaderArtifactInvalidReason *reason)
    {
        return LoadImpl(key, artifact, reason);
    }

    bool ShaderArtifactCache::Load(const ShaderArtifactKey &key, ComputeShaderVariantArtifact &artifact,
                                   EShaderArtifactInvalidReason *reason)
    {
        return LoadImpl(key, artifact, reason);
    }

    bool ShaderArtifactCache::Store(const ShaderArtifactKey &key, const GraphicsShaderVariantArtifact &artifact)
    {
        return StoreImpl(key, artifact);
    }

    bool ShaderArtifactCache::Store(const ShaderArtifactKey &key, const ComputeShaderVariantArtifact &artifact)
    {
        return StoreImpl(key, artifact);
    }

    bool ShaderArtifactCache::Load(const ShaderArtifactKey &key, RayTracingShaderVariantArtifact &artifact,
                                  EShaderArtifactInvalidReason *reason)
    {
        return LoadImpl(key, artifact, reason);
    }

    bool ShaderArtifactCache::Store(const ShaderArtifactKey &key, const RayTracingShaderVariantArtifact &artifact)
    {
        return StoreImpl(key, artifact);
    }

    bool ShaderArtifactCache::ValidateDependencies(RayTracingShaderVariantArtifact &artifact,
                                                  EShaderArtifactInvalidReason *reason)
    {
        return ValidateDependencies<RayTracingShaderVariantArtifact>(artifact, reason);
    }

    void ShaderArtifactCache::Invalidate(const ShaderArtifactKey &key)
    {
        InvalidateArtifact(key.Hash());
    }

    void ShaderArtifactCache::InvalidateArtifact(u64 artifact_key)
    {
        {
            std::lock_guard lock(_mutex);
            _graphics_artifacts.erase(artifact_key);
            _compute_artifacts.erase(artifact_key);
            _ray_tracing_artifacts.erase(artifact_key);
        }
        ShaderDependencyRegistry::Get().Unregister(artifact_key);
        std::error_code error;
        const WString root = Application::GetWorkingPath() + L"cache/shader_artifacts/dx12/";
        const WString prefix = std::format(L"{:02x}", static_cast<u32>(artifact_key & 0xffu));
        fs::remove(PathUtils::FormatFilePath(root + prefix + std::format(L"/{:016x}.ashader", artifact_key)), error);
    }

    void ShaderArtifactCache::Clear()
    {
        {
            std::lock_guard lock(_mutex);
            _graphics_artifacts.clear();
            _compute_artifacts.clear();
            _ray_tracing_artifacts.clear();
        }
        ShaderFileFingerprintCache::Get().Clear();
    }

    bool ShaderArtifactCache::WaitForCompile(const ShaderArtifactKey &key)
    {
        std::unique_lock lock(_compile_mutex);
        const auto it = _compiling_artifacts.find(key.Hash());
        if (it == _compiling_artifacts.end() || !it->second)
            return false;
        _compile_condition_variable.wait(lock, [&]
        {
            const auto state = _compiling_artifacts.find(key.Hash());
            return state == _compiling_artifacts.end() || !state->second;
        });
        return true;
    }

    void ShaderArtifactCache::BeginCompile(const ShaderArtifactKey &key)
    {
        std::unique_lock lock(_compile_mutex);
        auto &compiling = _compiling_artifacts[key.Hash()];
        _compile_condition_variable.wait(lock, [&] { return !compiling; });
        compiling = true;
    }

    void ShaderArtifactCache::EndCompile(const ShaderArtifactKey &key)
    {
        {
            std::lock_guard lock(_compile_mutex);
            _compiling_artifacts[key.Hash()] = false;
        }
        _compile_condition_variable.notify_all();
    }

    void ShaderArtifactCache::LogStats() const
    {
        const u64 graphics_hit = _stats._graphics_hit_count.load();
        const u64 graphics_miss = _stats._graphics_miss_count.load();
        const u64 compute_hit = _stats._compute_hit_count.load();
        const u64 compute_miss = _stats._compute_miss_count.load();
        const u64 ray_tracing_hit = _stats._ray_tracing_hit_count.load();
        const u64 ray_tracing_miss = _stats._ray_tracing_miss_count.load();
        const u64 total = graphics_hit + graphics_miss + compute_hit + compute_miss + ray_tracing_hit + ray_tracing_miss;
        const f64 hit_rate = total == 0u ? 100.0
            : static_cast<f64>(graphics_hit + compute_hit + ray_tracing_hit) * 100.0 / total;
        LOG_INFO("Shader Artifact Cache: Graphics: {} hit / {} miss, Compute: {} hit / {} miss, "
                 "RayTracing: {} hit / {} miss, Hit rate: {:.1}%, "
                 "Compiled variants: {}, Artifact read: {} bytes, Artifact written: {} bytes",
                 graphics_hit, graphics_miss, compute_hit, compute_miss, ray_tracing_hit, ray_tracing_miss, hit_rate,
                 _stats._compiled_variant_count.load(), _stats._artifact_bytes_read.load(),
                 _stats._artifact_bytes_written.load());
    }

    bool ShaderArtifactCache::ValidateDependencies(GraphicsShaderVariantArtifact &artifact,
                                                    EShaderArtifactInvalidReason *reason)
    {
        return ValidateDependencies<GraphicsShaderVariantArtifact>(artifact, reason);
    }

    bool ShaderArtifactCache::ValidateDependencies(ComputeShaderVariantArtifact &artifact,
                                                   EShaderArtifactInvalidReason *reason)
    {
        return ValidateDependencies<ComputeShaderVariantArtifact>(artifact, reason);
    }
}
