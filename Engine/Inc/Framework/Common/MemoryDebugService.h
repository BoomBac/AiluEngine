#pragma once

#include "Framework/Common/MemoryDebugTypes.h"

namespace Ailu
{
    class AILU_API MemoryDebugService
    {
    public:
        static MemoryDebugService &Get();

        void Tick(f32 delta_time);
        void SetPaused(bool paused) { _paused = paused; }
        void SetRefreshInterval(f32 interval_sec) { _refresh_interval_sec = interval_sec; }
        void SetTrackLeaks(bool track_leaks) { _track_leaks = track_leaks; }
        bool IsPaused() const { return _paused; }
        f32 RefreshInterval() const { return _refresh_interval_sec; }

        const AllocatorSnapshot &CurrentSnapshot() const { return _current_snapshot; }
        const Vector<MemoryTimelineSample> &Timeline() const { return _timeline; }
        const Vector<MemoryDebugEvent> &RecentEvents() const { return _recent_events; }

        u64 CreateSnapshot();
        const Vector<AllocatorSnapshot> &SavedSnapshots() const { return _saved_snapshots; }
        void ClearEvents();

    private:
        void Capture(f32 delta_time, bool force);
        void PushTimelineSample(const MemoryTimelineSample &sample);

    private:
        AllocatorSnapshot _current_snapshot;
        Vector<MemoryTimelineSample> _timeline;
        Vector<MemoryDebugEvent> _recent_events;
        Vector<AllocatorSnapshot> _saved_snapshots;
        f32 _refresh_interval_sec = 0.2f;
        f32 _time_since_refresh = 0.0f;
        f64 _time_sec = 0.0;
        u64 _next_snapshot_id = 1u;
        u64 _last_total_alloc_count = 0u;
        u64 _last_total_free_count = 0u;
        bool _paused = false;
        bool _track_leaks = true;
    };
}
