#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <unordered_map>

#include "Render/ShaderArtifact.h"

namespace Ailu::Render
{
    struct ShaderFileFingerprint
    {
        bool _exists = false;
        u64 _file_size = 0u;
        u64 _last_write_time = 0u;
        u64 _content_hash = 0u;
    };

    class AILU_API ShaderFileFingerprintCache
    {
    public:
        static ShaderFileFingerprintCache &Get();

        const ShaderFileFingerprint &GetFingerprint(const WString &path);
        u64 GetContentHash(const WString &path);
        void Invalidate(const WString &path);
        void Clear();

    private:
        std::mutex _mutex;
        HashMap<WString, ShaderFileFingerprint> _fingerprints;
    };

    struct ShaderArtifactStats
    {
        std::atomic<u64> _graphics_hit_count = 0u;
        std::atomic<u64> _graphics_miss_count = 0u;
        std::atomic<u64> _ray_tracing_hit_count = 0u;
        std::atomic<u64> _ray_tracing_miss_count = 0u;
        std::atomic<u64> _compute_hit_count = 0u;
        std::atomic<u64> _compute_miss_count = 0u;
        std::atomic<u64> _invalid_dependency_count = 0u;
        std::atomic<u64> _compiled_variant_count = 0u;
        std::atomic<u64> _artifact_bytes_read = 0u;
        std::atomic<u64> _artifact_bytes_written = 0u;
    };

    class AILU_API ShaderArtifactCache
    {
    public:
        static ShaderArtifactCache &Get();

        bool Load(const ShaderArtifactKey &key, GraphicsShaderVariantArtifact &artifact,
                  EShaderArtifactInvalidReason *reason = nullptr);
        bool Load(const ShaderArtifactKey &key, ComputeShaderVariantArtifact &artifact,
                  EShaderArtifactInvalidReason *reason = nullptr);
        bool Store(const ShaderArtifactKey &key, const GraphicsShaderVariantArtifact &artifact);
        bool Store(const ShaderArtifactKey &key, const ComputeShaderVariantArtifact &artifact);

        bool Load(const ShaderArtifactKey &key, RayTracingShaderVariantArtifact &artifact,
                  EShaderArtifactInvalidReason *reason = nullptr);
        bool Store(const ShaderArtifactKey &key, const RayTracingShaderVariantArtifact &artifact);
        bool ValidateDependencies(RayTracingShaderVariantArtifact &artifact,
                                  EShaderArtifactInvalidReason *reason = nullptr);

        void Invalidate(const ShaderArtifactKey &key);
        void InvalidateArtifact(u64 artifact_key);
        void Clear();

        bool ValidateDependencies(GraphicsShaderVariantArtifact &artifact,
                                   EShaderArtifactInvalidReason *reason = nullptr);
        bool ValidateDependencies(ComputeShaderVariantArtifact &artifact,
                                  EShaderArtifactInvalidReason *reason = nullptr);

        bool WaitForCompile(const ShaderArtifactKey &key);
        void BeginCompile(const ShaderArtifactKey &key);
        void EndCompile(const ShaderArtifactKey &key);

        ShaderArtifactStats &Stats() { return _stats; }
        const ShaderArtifactStats &Stats() const { return _stats; }
        void LogStats() const;

    private:
        template<typename Artifact>
        bool LoadImpl(const ShaderArtifactKey &key, Artifact &artifact, EShaderArtifactInvalidReason *reason);

        template<typename Artifact>
        bool StoreImpl(const ShaderArtifactKey &key, const Artifact &artifact);

        template<typename Artifact>
        bool ValidateDependencies(Artifact &artifact, EShaderArtifactInvalidReason *reason);

        WString GetArtifactPath(const ShaderArtifactKey &key) const;

        mutable std::mutex _mutex;
        std::mutex _compile_mutex;
        std::condition_variable _compile_condition_variable;
        HashMap<u64, bool> _compiling_artifacts;
        HashMap<u64, GraphicsShaderVariantArtifact> _graphics_artifacts;
        HashMap<u64, ComputeShaderVariantArtifact> _compute_artifacts;
        HashMap<u64, RayTracingShaderVariantArtifact> _ray_tracing_artifacts;
        ShaderArtifactStats _stats;
    };
}
