#pragma once

#include "FrameCaptureTypes.h"
#include "FrameCaptureReason.h"
#include "FrameCapture.h"
#include "Framework/Core/String.h"
#include "Framework/Core/Containers/Vector.h"

namespace Ailu
{
    namespace Render
    {
        class Shader;
        class Material;
        class GpuResource;
        class GraphicsPipelineStateObject;
        struct CommandProfiler;
        struct CompiledRenderPass;
    }
}

namespace Ailu::Render::FrameDebugger
{
class FrameCaptureSession;

class FrameCaptureWriter final
{
public:
    FrameCaptureWriter(FrameCaptureSession &session, u32 submission_index, StringView command_buffer_name);
    ~FrameCaptureWriter() = default;

    u32 BeginEvent(EFrameEventType type, CaptureStringId name, u32 parent_event_id = kInvalidFrameEventId);
    void EndEvent(u32 event_id, EFrameEventExecutionResult result = EFrameEventExecutionResult::kExecuted);
    void SetEventResult(u32 event_id, EFrameEventExecutionResult result) { if (event_id < (u32)_events.size()) _events[event_id]._execution_result = result; }
    void SetEventPayloadIndex(u32 event_id, u32 payload_index) { if (event_id < (u32)_events.size()) _events[event_id]._payload_index = payload_index; }

    u32 RecordDrawEvent(const DrawEventCapture &draw, CaptureObjectId material_id, CaptureObjectId shader_id);
    u32 RecordDispatchEvent(const DispatchEventCapture &dispatch);
    u32 RecordBarrierEvent(const ResourceBarrierCapture &barrier);
    u32 RecordRenderGraphPassEvent(const RenderGraphPassCapture &pass);

    void RecordBinding(const PipelineBindingCapture &binding);
    void RecordGeometryBinding(const GeometryBindingCapture &geo);
    void RecordMaterialBinding(const MaterialBindingCapture &mat_binding);
    void RecordPsoLookup(const PsoLookupCapture &lookup);
    void RecordBarrier(const ResourceBarrierCapture &barrier);
    void RecordRenderGraphPass(const RenderGraphPassCapture &pass);
    void RecordRenderGraphResourceAccess(const RenderGraphResourceAccessCapture &access);

    u32 DrawDataIndex() const { return (u32)_draws.size(); }
    u32 DispatchDataIndex() const { return (u32)_dispatches.size(); }
    u32 BarrierDataIndex() const { return (u32)_barriers.size(); }
    u32 BindingDataIndex() const { return (u32)_bindings.size(); }
    u32 RGResourceAccessDataIndex() const { return (u32)_rg_resource_accesses.size(); }

    void FinalizeLastDrawBindingRange()
    {
        if (!_draws.empty())
        {
            auto &d = _draws.back();
            d._binding_count = (u16)((u32)_bindings.size() - d._binding_range_begin);
            d._barrier_count = (u16)((u32)_barriers.size() - d._barrier_range_begin);
        }
    }

    void FinalizeLastDispatchBindingRange()
    {
        if (!_dispatches.empty())
        {
            auto &d = _dispatches.back();
            d._binding_count = (u16)((u32)_bindings.size() - d._binding_range_begin);
        }
    }

    CaptureStringId InternString(StringView text);
    CaptureObjectId RegisterObject(void *runtime_ptr, ECaptureObjectType type, CaptureStringId name_id);

    CaptureGraphicsStateCache &StateCache() { return _state_cache; }
    const CaptureGraphicsStateCache &StateCache() const { return _state_cache; }

    u32 CurrentProfilerEventId() const { return _profiler_stack.empty() ? 0u : _profiler_stack.back(); }
    void PushProfilerEvent(u32 event_id) { _profiler_stack.push_back(event_id); }
    void PopProfilerEvent() { if (!_profiler_stack.empty()) _profiler_stack.pop_back(); }

    u32 CommandIndex() const { return _command_index; }
    void SetCommandIndex(u32 index) { _command_index = index; }
    void IncrementCommandIndex() { ++_command_index; }
    u16 SubEventIndex() const { return _sub_event_index; }
    void IncrementSubEventIndex() { ++_sub_event_index; }
    void ResetSubEventIndex() { _sub_event_index = 0u; }
    u32 RenderPassCaptureId() const { return _render_pass_capture_id; }
    void SetRenderPassCaptureId(u32 id) { _render_pass_capture_id = id; }
    u32 CurrentParentEventId() const { return _current_parent_event_id; }
    void SetCurrentParentEventId(u32 id) { _current_parent_event_id = id; }
    u32 CurrentDrawEventId() const { return _current_draw_event_id; }
    void SetCurrentDrawEventId(u32 id) { _current_draw_event_id = id; }

    FrameCaptureChunk &Chunk() { return _chunk; }
    const FrameCaptureChunk &Chunk() const { return _chunk; }

private:
    FrameCaptureSession &_session;
    FrameCaptureChunk _chunk;
    CaptureGraphicsStateCache _state_cache;
    Vector<u32> _profiler_stack;
    u32 _command_index = 0u;
    u16 _sub_event_index = 0u;
    u32 _render_pass_capture_id = 0u;
    u32 _current_parent_event_id = kInvalidFrameEventId;
    u32 _current_draw_event_id = kInvalidFrameEventId;

    Vector<FrameEvent> &_events;
    Vector<DrawEventCapture> &_draws;
    Vector<DispatchEventCapture> &_dispatches;
    Vector<PipelineBindingCapture> &_bindings;
    Vector<ResourceBarrierCapture> &_barriers;
    Vector<GeometryBindingCapture> &_geometry_bindings;
    Vector<MaterialBindingCapture> &_material_bindings;
    Vector<PsoLookupCapture> &_pso_lookups;
    Vector<RenderGraphPassCapture> &_rg_passes;
    Vector<RenderGraphResourceAccessCapture> &_rg_resource_accesses;
};

} // namespace Ailu::Render::FrameDebugger
