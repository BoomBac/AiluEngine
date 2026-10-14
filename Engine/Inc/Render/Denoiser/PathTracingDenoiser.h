#pragma once
#include "Render/RenderGraph/RenderGraphFwd.h"
#include <memory>
#include <atomic>

#include "generated/PathTracingDenoiser.gen.h"

namespace Ailu::Render
{
    class Texture;
    AENUM()
    enum class EPathTracingDenoiser : u8
    {
        kNone,
        kOidnGpu
    };
    enum class EDenoiserStatus : u8
    {
        kDisabled,
        kUnsupported,
        kInitializing,
        kIdle,
        kSubmitted,
        kPackPending,
        kFiltering,
        kUnpacking,
        kReady,
        kFailed,
        kRetiring
    };
    AENUM()
    enum class EPathTracingDenoiserQuality : u8
    {
        kFast,
        kBalanced,
        kHigh
    };
    struct PathTracingDenoiserDiagnostics
    {
        u32 _last_sample_count = 0u;
        float _latency_ms = 0.0f, _shared_mb = 0.0f;
    };
    struct PathTracingDenoiserInput
    {
        RDG::RGHandle _radiance{}, _albedo{}, _normal{}, _view_z{}, _motion_vector{};
        u64 _generation = 0u, _backend_instance = 0u, _frame_index = 0u;
        u32 _width = 0u, _height = 0u, _sample_count = 0u;
        bool _valid_radiance = false;
        std::shared_ptr<const bool> _raw_executed;
        std::shared_ptr<Texture> _radiance_owner;
        std::shared_ptr<std::atomic<bool>> _activation_owner;
        EPathTracingDenoiserQuality _quality = EPathTracingDenoiserQuality::kBalanced;
        u32 _sample_interval = 32u;
    };
    struct PathTracingDenoiserOutput
    {
        RDG::RGHandle _display_texture{};
        u64 _generation = 0u, _backend_instance = 0u, _request_id = 0u;
        u32 _width = 0u, _height = 0u, _source_sample_count = 0u;
        bool _valid = false;
    };
    class IPathTracingDenoiser
    {
    public:
        virtual ~IPathTracingDenoiser() = default;
        virtual void Reset(u64 generation) = 0;
        virtual PathTracingDenoiserOutput Record(RDG::RenderGraph &graph, const PathTracingDenoiserInput &input) = 0;
        // Stop submissions and retire pending work without waiting on the rendering thread.
        virtual void Shutdown() = 0;
        virtual EDenoiserStatus Status() const = 0;
        virtual const char *Reason() const = 0;
        virtual void SetActive(bool active) {}
        virtual PathTracingDenoiserDiagnostics Diagnostics() const { return {}; }
    };
    class AILU_API PathTracingDenoiserHost
    {
    public:
        using BackendFactory = std::unique_ptr<IPathTracingDenoiser> (*)(EPathTracingDenoiser);
        explicit PathTracingDenoiserHost(BackendFactory factory = nullptr) : _factory(factory) {}
        ~PathTracingDenoiserHost();
        void Configure(EPathTracingDenoiser backend, bool reference_mode, bool reset_history);
        void Reset();
        void Shutdown();
        PathTracingDenoiserOutput Record(RDG::RenderGraph &graph, PathTracingDenoiserInput input);
        EDenoiserStatus Status() const { return _status; }
        const char *Reason() const;
        PathTracingDenoiserDiagnostics Diagnostics() const { return _backend ? _backend->Diagnostics() :
                                                                             PathTracingDenoiserDiagnostics{}; }
        u64 Generation() const { return _generation; }
        u64 BackendInstance() const { return _backend_instance; }
        static bool CanDisplay(const PathTracingDenoiserInput &input, const PathTracingDenoiserOutput &output);

    private:
        std::unique_ptr<IPathTracingDenoiser> _backend;
        BackendFactory _factory = nullptr;
        EPathTracingDenoiser _selected = EPathTracingDenoiser::kNone;
        EDenoiserStatus _status = EDenoiserStatus::kDisabled;
        bool _reference_mode = false;
        u64 _generation = 1u, _backend_instance = 0u;
    };
}// namespace Ailu::Render
