#include "Render/Denoiser/PathTracingDenoiser.h"

namespace Ailu::Render
{
    std::unique_ptr<IPathTracingDenoiser> CreateOidnGpuDenoiser();
    PathTracingDenoiserHost::~PathTracingDenoiserHost() { Shutdown(); }
    void PathTracingDenoiserHost::Configure(EPathTracingDenoiser backend, bool reference_mode, bool reset_history)
    {
        if (_selected != backend || _reference_mode != reference_mode)
        {
            _selected = backend;
            _reference_mode = reference_mode;
            ++_backend_instance;
            if (!_backend && reference_mode && backend != EPathTracingDenoiser::kNone)
                _backend = _factory ? _factory(backend) : CreateOidnGpuDenoiser();
            Reset();
        }
        else if (reset_history)
            Reset();
        if (_backend) _backend->SetActive(reference_mode && backend != EPathTracingDenoiser::kNone);
        _status = backend == EPathTracingDenoiser::kNone || !reference_mode ? EDenoiserStatus::kDisabled
                                                                            : EDenoiserStatus::kUnsupported;
        if (_backend && reference_mode && backend != EPathTracingDenoiser::kNone) _status = _backend->Status();
    }
    void PathTracingDenoiserHost::Reset()
    {
        ++_generation;
        if (_backend) _backend->Reset(_generation);
    }
    void PathTracingDenoiserHost::Shutdown()
    {
        if (_backend) _backend->Shutdown();
        _backend.reset();
        ++_generation;
        _selected = EPathTracingDenoiser::kNone;
        _reference_mode = false;
        _status = EDenoiserStatus::kDisabled;
    }
    const char *PathTracingDenoiserHost::Reason() const
    {
        if (_selected == EPathTracingDenoiser::kNone) return "Disabled";
        if (!_reference_mode) return "OIDN requires Reference accumulation";
        return _backend ? _backend->Reason() : "OIDN GPU backend unavailable";
    }
    bool PathTracingDenoiserHost::CanDisplay(const PathTracingDenoiserInput &input,
                                             const PathTracingDenoiserOutput &output)
    {
        return input._valid_radiance && input._radiance.IsValid() && input._width > 0u && input._height > 0u &&
               output._valid && output._display_texture.IsValid() && output._request_id > 0u &&
               output._generation == input._generation && output._backend_instance == input._backend_instance &&
               output._width == input._width && output._height == input._height && output._source_sample_count > 0u &&
               output._source_sample_count <= input._sample_count;
    }
    PathTracingDenoiserOutput PathTracingDenoiserHost::Record(RDG::RenderGraph &graph, PathTracingDenoiserInput input)
    {
        input._generation = _generation;
        input._backend_instance = _backend_instance;
        if (!_backend || !_reference_mode || _selected == EPathTracingDenoiser::kNone ||
            !input._valid_radiance || !input._radiance.IsValid()) return {};
        const auto output = _backend->Record(graph, input);
        _status = _backend->Status();
        return CanDisplay(input, output) ? output : PathTracingDenoiserOutput{};
    }
}// namespace Ailu::Render
