#include "Render/FrameDebugger/FrameCaptureWriter.h"
#include "Render/FrameDebugger/FrameCaptureSession.h"

namespace Ailu::Render::FrameDebugger
{
namespace
{
    constexpr u64 kCaptureStorageMultiplier = 4u;

    bool ReserveCaptureStorage(FrameCaptureSession &session, u64 size)
    {
        return session.TryReserveCaptureMemory(size * kCaptureStorageMultiplier);
    }
}

FrameCaptureWriter::FrameCaptureWriter(FrameCaptureSession &session, u32 submission_index, StringView command_buffer_name)
    : _session(session)
    , _events(_chunk._events)
    , _draws(_chunk._draws)
    , _dispatches(_chunk._dispatches)
    , _bindings(_chunk._bindings)
    , _barriers(_chunk._barriers)
    , _geometry_bindings(_chunk._geometry_bindings)
    , _material_bindings(_chunk._material_bindings)
    , _pso_lookups(_chunk._pso_lookups)
    , _rg_passes(_chunk._rg_passes)
    , _rg_resource_accesses(_chunk._rg_resource_accesses)
{
    _chunk._submission_index = submission_index;
    _chunk._command_group_name = InternString(command_buffer_name);
}

u32 FrameCaptureWriter::BeginEvent(EFrameEventType type, CaptureStringId name, u32 parent_event_id)
{
    if (!ReserveCaptureStorage(_session, sizeof(FrameEvent)))
        return kInvalidFrameEventId;
    u32 event_id = (u32)_events.size();
    FrameEvent event;
    event._event_id = event_id;
    event._parent_event_id = parent_event_id;
    event._submission_index = _chunk._submission_index;
    event._command_index = _command_index;
    event._sub_event_index = _sub_event_index++;
    event._type = type;
    event._name = name;
    _events.push_back(event);
    return event_id;
}

void FrameCaptureWriter::EndEvent(u32 event_id, EFrameEventExecutionResult result)
{
    if (event_id < (u32)_events.size())
        _events[event_id]._execution_result = result;
}

u32 FrameCaptureWriter::RecordDrawEvent(const DrawEventCapture &draw, CaptureObjectId material_id, CaptureObjectId shader_id)
{
    if (!ReserveCaptureStorage(_session, sizeof(DrawEventCapture)))
        return kInvalidFrameEventId;
    u32 payload_idx = (u32)_draws.size();
    DrawEventCapture d = draw;
    d._material_id = material_id;
    d._shader_id = shader_id;
    d._binding_range_begin = (u32)_bindings.size();
    d._barrier_range_begin = (u32)_barriers.size();
    _draws.push_back(d);

    u32 event_id = BeginEvent(EFrameEventType::kDraw, InternString("Draw"), _current_parent_event_id);
    if (event_id != kInvalidFrameEventId)
        _events[event_id]._payload_index = payload_idx;
    return event_id;
}

u32 FrameCaptureWriter::RecordDispatchEvent(const DispatchEventCapture &dispatch)
{
    if (!ReserveCaptureStorage(_session, sizeof(DispatchEventCapture)))
        return kInvalidFrameEventId;
    u32 payload_idx = (u32)_dispatches.size();
    _dispatches.push_back(dispatch);

    u32 event_id = BeginEvent(EFrameEventType::kDispatch, InternString("Dispatch"), _current_parent_event_id);
    if (event_id != kInvalidFrameEventId)
        _events[event_id]._payload_index = payload_idx;
    return event_id;
}

u32 FrameCaptureWriter::RecordBarrierEvent(const ResourceBarrierCapture &barrier)
{
    if (!ReserveCaptureStorage(_session, sizeof(ResourceBarrierCapture)))
        return kInvalidFrameEventId;
    u32 payload_idx = (u32)_barriers.size();
    _barriers.push_back(barrier);

    u32 event_id = BeginEvent(EFrameEventType::kResourceBarrier, InternString("Barrier"), _current_parent_event_id);
    if (event_id != kInvalidFrameEventId)
        _events[event_id]._payload_index = payload_idx;
    return event_id;
}

u32 FrameCaptureWriter::RecordRenderGraphPassEvent(const RenderGraphPassCapture &pass)
{
    if (!ReserveCaptureStorage(_session, sizeof(RenderGraphPassCapture)))
        return kInvalidFrameEventId;
    u32 payload_idx = (u32)_rg_passes.size();
    _rg_passes.push_back(pass);

    u32 event_id = BeginEvent(EFrameEventType::kRenderGraphPass, pass._name);
    if (event_id != kInvalidFrameEventId)
        _events[event_id]._payload_index = payload_idx;
    return event_id;
}

void FrameCaptureWriter::RecordBinding(const PipelineBindingCapture &binding)
{
    if (!ReserveCaptureStorage(_session, sizeof(PipelineBindingCapture)))
        return;
    _bindings.push_back(binding);
}

void FrameCaptureWriter::RecordGeometryBinding(const GeometryBindingCapture &geo)
{
    if (!ReserveCaptureStorage(_session, sizeof(GeometryBindingCapture)))
        return;
    _geometry_bindings.push_back(geo);
}

void FrameCaptureWriter::RecordMaterialBinding(const MaterialBindingCapture &mat_binding)
{
    if (!ReserveCaptureStorage(_session, sizeof(MaterialBindingCapture)))
        return;
    _material_bindings.push_back(mat_binding);
}

void FrameCaptureWriter::RecordPsoLookup(const PsoLookupCapture &lookup)
{
    if (!ReserveCaptureStorage(_session, sizeof(PsoLookupCapture)))
        return;
    _pso_lookups.push_back(lookup);
}

void FrameCaptureWriter::RecordBarrier(const ResourceBarrierCapture &barrier)
{
    if (!ReserveCaptureStorage(_session, sizeof(ResourceBarrierCapture)))
        return;
    _barriers.push_back(barrier);
}

void FrameCaptureWriter::RecordRenderGraphPass(const RenderGraphPassCapture &pass)
{
    if (!ReserveCaptureStorage(_session, sizeof(RenderGraphPassCapture)))
        return;
    _rg_passes.push_back(pass);
}

void FrameCaptureWriter::RecordRenderGraphResourceAccess(const RenderGraphResourceAccessCapture &access)
{
    if (!ReserveCaptureStorage(_session, sizeof(RenderGraphResourceAccessCapture)))
        return;
    _rg_resource_accesses.push_back(access);
}

CaptureStringId FrameCaptureWriter::InternString(StringView text)
{
    return _chunk._strings.Intern(text);
}

CaptureObjectId FrameCaptureWriter::RegisterObject(void *runtime_ptr, ECaptureObjectType type, CaptureStringId name_id)
{
    return _chunk._objects.RegisterOrGet(runtime_ptr, type, name_id);
}

} // namespace Ailu::Render::FrameDebugger
