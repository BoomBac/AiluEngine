#include "Render/FrameDebugger/FrameCaptureFinalizer.h"
#include "Render/FrameDebugger/FrameCaptureSession.h"
#include "Render/FrameDebugger/FrameCaptureWriter.h"
#include "Framework/Core/Containers/Map.h"
#include <algorithm>

namespace Ailu::Render::FrameDebugger
{

void FrameCaptureFinalizer::Finalize(const FrameCaptureSession &session, FrameCapture &out_capture)
{
    auto &mut_session = const_cast<FrameCaptureSession &>(session);
    auto &global_strings = out_capture.Strings();
    auto &global_objects = out_capture.Objects();

    Vector<FrameCaptureWriter *> ordered_writers;
    ordered_writers.reserve(mut_session._writers.size());
    for (auto &writer : mut_session._writers)
        ordered_writers.push_back(writer.get());
    std::stable_sort(ordered_writers.begin(), ordered_writers.end(), [](const FrameCaptureWriter *a, const FrameCaptureWriter *b)
    {
        const auto &a_chunk = a->Chunk();
        const auto &b_chunk = b->Chunk();
        if (a_chunk._submission_index != b_chunk._submission_index)
            return a_chunk._submission_index < b_chunk._submission_index;
        return a_chunk._chunk_index < b_chunk._chunk_index;
    });

    for (FrameCaptureWriter *writer : ordered_writers)
    {
        FrameCaptureChunk &chunk = writer->Chunk();

        u32 event_offset = (u32)out_capture._events.size();
        u32 draw_offset = (u32)out_capture._draws.size();
        u32 dispatch_offset = (u32)out_capture._dispatches.size();
        u32 binding_offset = (u32)out_capture._bindings.size();
        u32 barrier_offset = (u32)out_capture._barriers.size();
        u32 rg_pass_offset = (u32)out_capture._rg_passes.size();
        u32 rg_access_offset = (u32)out_capture._rg_resource_accesses.size();

        HashMap<CaptureStringId, CaptureStringId> string_remap;
        for (u32 i = 1u; i <= chunk._strings.Count(); ++i)
        {
            StringView local_str = chunk._strings.Get(i);
            CaptureStringId new_id = global_strings.Intern(local_str);
            string_remap[i] = new_id;
        }

        HashMap<CaptureObjectId, CaptureObjectId> object_remap;
        for (u32 i = 1u; i <= chunk._objects.Count(); ++i)
        {
            if (auto info = chunk._objects.Get(i))
            {
                CaptureObjectId new_id = global_objects.RegisterOrGet((void *)info->_runtime_instance_id, info->_type, string_remap[info->_name]);
                object_remap[i] = new_id;
            }
        }

        auto remap_str = [&](CaptureStringId &id) { if (id) id = string_remap[id]; };
        auto remap_obj = [&](CaptureObjectId &id) { if (id) id = object_remap[id]; };

        for (FrameEvent &event : chunk._events)
        {
            event._event_id += event_offset;
            if (event._parent_event_id != kInvalidFrameEventId)
                event._parent_event_id += event_offset;
            remap_str(event._name);
            if (event._type == EFrameEventType::kDraw)
                event._payload_index += draw_offset;
            else if (event._type == EFrameEventType::kDispatch)
                event._payload_index += dispatch_offset;
            else if (event._type == EFrameEventType::kResourceBarrier)
                event._payload_index += barrier_offset;
            else if (event._type == EFrameEventType::kRenderGraphPass)
                event._payload_index += rg_pass_offset;
        }

        for (DrawEventCapture &d : chunk._draws)
        {
            remap_obj(d._material_id);
            remap_obj(d._shader_id);
            remap_obj(d._pso_id);
            remap_obj(d._vertex_buffer_id);
            remap_obj(d._index_buffer_id);
            remap_obj(d._argument_buffer_id);
            d._binding_range_begin += binding_offset;
            d._barrier_range_begin += barrier_offset;
        }

        for (DispatchEventCapture &d : chunk._dispatches)
        {
            remap_obj(d._compute_shader_id);
            remap_str(d._kernel_name);
            d._binding_range_begin += binding_offset;
        }

        for (PipelineBindingCapture &b : chunk._bindings)
        {
            remap_obj(b._key._resource_id);
            remap_str(b._slot_name);
            if (b._inherited_from_event != kInvalidFrameEventId)
                b._inherited_from_event += event_offset;
        }

        for (ResourceBarrierCapture &b : chunk._barriers)
        {
            remap_obj(b._resource_id);
            remap_str(b._resource_name);
        }

        for (GeometryBindingCapture &g : chunk._geometry_bindings)
        {
            remap_obj(g._vertex_buffer_id);
            remap_obj(g._index_buffer_id);
        }

        for (RenderGraphPassCapture &p : chunk._rg_passes)
        {
            remap_str(p._name);
            p._input_range_begin += rg_access_offset;
            p._output_range_begin += rg_access_offset;
            p._pre_barrier_range_begin += barrier_offset;
            p._post_barrier_range_begin += barrier_offset;
        }

        for (RenderGraphResourceAccessCapture &a : chunk._rg_resource_accesses)
        {
            remap_obj(a._resource_id);
            remap_str(a._resource_name);
        }

        out_capture._events.insert(out_capture._events.end(), chunk._events.begin(), chunk._events.end());
        out_capture._draws.insert(out_capture._draws.end(), chunk._draws.begin(), chunk._draws.end());
        out_capture._dispatches.insert(out_capture._dispatches.end(), chunk._dispatches.begin(), chunk._dispatches.end());
        out_capture._bindings.insert(out_capture._bindings.end(), chunk._bindings.begin(), chunk._bindings.end());
        out_capture._barriers.insert(out_capture._barriers.end(), chunk._barriers.begin(), chunk._barriers.end());
        out_capture._geometry_bindings.insert(out_capture._geometry_bindings.end(), chunk._geometry_bindings.begin(), chunk._geometry_bindings.end());
        out_capture._material_bindings.insert(out_capture._material_bindings.end(), chunk._material_bindings.begin(), chunk._material_bindings.end());
        out_capture._pso_lookups.insert(out_capture._pso_lookups.end(), chunk._pso_lookups.begin(), chunk._pso_lookups.end());
        out_capture._rg_passes.insert(out_capture._rg_passes.end(), chunk._rg_passes.begin(), chunk._rg_passes.end());
        out_capture._rg_resource_accesses.insert(out_capture._rg_resource_accesses.end(), chunk._rg_resource_accesses.begin(), chunk._rg_resource_accesses.end());
    }

    std::stable_sort(out_capture._events.begin(), out_capture._events.end(),
                     [](const FrameEvent &a, const FrameEvent &b)
                     {
                         if (a._submission_index != b._submission_index)
                             return a._submission_index < b._submission_index;
                         if (a._command_index != b._command_index)
                             return a._command_index < b._command_index;
                         return a._sub_event_index < b._sub_event_index;
                      });
}

void FrameCapture::CalculateStatistics()
{
    _statistics = {};
    _statistics._event_count = (u32)_events.size();
    _statistics._barrier_count = (u32)_barriers.size();

    for (const auto &e : _events)
    {
        if (e._type == EFrameEventType::kDraw) ++_statistics._draw_count;
        else if (e._type == EFrameEventType::kDispatch) ++_statistics._dispatch_count;
    }

    for (const auto &p : _pso_lookups)
    {
        if (p._pso_lookup_result == (u8)EPsoLookupResult::kCacheMiss) ++_statistics._pso_miss_event_count;
        if (p._dirty_reasons != 0u) ++_statistics._pso_dirty_event_count;
    }

    for (const auto &m : _material_bindings)
        if (m._result != (u8)EMaterialBindingResolveResult::kCacheHit) ++_statistics._material_cache_miss_event_count;

    for (const auto &b : _bindings)
    {
        if (b._cache_result == (u8)EBindingCacheResult::kInherited) ++_statistics._inherited_binding_count;
        if (b._cache_result == (u8)EBindingCacheResult::kUnbound && b._is_required) ++_statistics._warning_count;
    }

}

} // namespace Ailu::Render::FrameDebugger
