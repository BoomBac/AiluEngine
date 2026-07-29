#include "Framework/Common/MemoryDebugService.h"
#include "Framework/Common/Allocator.hpp"
#include "pch.h"

namespace Ailu
{
    MemoryDebugService &MemoryDebugService::Get()
    {
        static MemoryDebugService s_service;
        return s_service;
    }

    void MemoryDebugService::Tick(f32 delta_time)
    {
        _time_sec += delta_time;
        Capture(delta_time, false);
    }

    u64 MemoryDebugService::CreateSnapshot()
    {
        Capture(0.0f, true);
        _current_snapshot._snapshot_id = _next_snapshot_id++;
        _saved_snapshots.push_back(_current_snapshot);
        return _current_snapshot._snapshot_id;
    }

    void MemoryDebugService::ClearEvents()
    {
        Allocator::Get().ClearMemoryDebugEvents();
        _recent_events.clear();
    }

    void MemoryDebugService::Capture(f32 delta_time, bool force)
    {
        if (_paused && !force)
            return;

        _time_since_refresh += delta_time;
        if (!force && _time_since_refresh < _refresh_interval_sec)
            return;

        const f64 sample_period = _time_since_refresh > 0.0f ? _time_since_refresh : _refresh_interval_sec;
        _time_since_refresh = 0.0f;
        _current_snapshot._time_sec = _time_sec;

        Allocator &allocator = Allocator::Get();
        allocator.CaptureGlobalSnapshot(_current_snapshot._global);
        allocator.CaptureBinSnapshots(_current_snapshot._bins);
        allocator.CaptureArenaSnapshots(_current_snapshot._arenas);
        if (_track_leaks)
            allocator.CaptureActiveAllocations(_current_snapshot._allocations);
        else
            _current_snapshot._allocations.clear();
        allocator.CaptureRecentEvents(_recent_events, 1000u);

        const u64 total_alloc_count = _current_snapshot._global._total_allocation_count;
        const u64 total_free_count = _current_snapshot._global._total_free_count;
        MemoryTimelineSample sample;
        sample._time_sec = _time_sec;
        sample._requested_bytes = _current_snapshot._global._requested_bytes;
        sample._reserved_bytes = _current_snapshot._global._reserved_bytes;
        sample._active_allocations = _current_snapshot._global._active_allocation_count;
        sample._allocation_rate = static_cast<u64>((total_alloc_count - _last_total_alloc_count) / sample_period);
        sample._free_rate = static_cast<u64>((total_free_count - _last_total_free_count) / sample_period);
        PushTimelineSample(sample);
        _last_total_alloc_count = total_alloc_count;
        _last_total_free_count = total_free_count;
    }

    void MemoryDebugService::PushTimelineSample(const MemoryTimelineSample &sample)
    {
        if (_timeline.size() < kMemoryDebugTimelineCapacity)
        {
            _timeline.push_back(sample);
            return;
        }
        std::move(_timeline.begin() + 1, _timeline.end(), _timeline.begin());
        _timeline.back() = sample;
    }
}
