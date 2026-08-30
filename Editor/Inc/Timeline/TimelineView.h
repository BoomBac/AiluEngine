#pragma once

#include "UI/UIElement.h"

namespace Ailu::Editor
{
    struct TimelineTrack
    {
        String _name;
        Vector<f32> _markers;
        Color _marker_color = Color(0.26f, 0.68f, 0.95f, 1.0f);
        bool _is_event_track = false;
    };

    class TimelineView : public UI::UIElement
    {
        DECLARE_DELEGATE(on_time_changed, f32);
        DECLARE_DELEGATE(on_track_selected, u32);

    public:
        TimelineView();

        void SetTimeRange(f32 start_time, f32 end_time);
        void SetCurrentTime(f32 time, bool snap = false);
        f32 CurrentTime() const { return _current_time; }

        void SetPixelsPerSecond(f32 pixels_per_second);
        f32 PixelsPerSecond() const { return _pixels_per_second; }
        void SetSnapInterval(f32 interval);
        f32 SnapInterval() const { return _snap_interval; }

        f32 TimeToLocalX(f32 time) const;
        f32 LocalXToTime(f32 x) const;
        f32 VisibleStartTime() const;
        f32 VisibleEndTime() const;

        void SetTracks(const Vector<TimelineTrack> &tracks);
        const Vector<TimelineTrack> &Tracks() const { return _tracks; }
        void SetSelectedTrack(u32 index);
        u32 SelectedTrack() const { return _selected_track; }

        void SetHeaderWidth(f32 width);
        void SetRulerHeight(f32 height);

        Vector2f MeasureDesiredSize() override;
        UI::UIElement *HitTest(Vector2f pos) override;
        void Update(f32 dt) override;

    protected:
        void RenderImpl(UI::UIRenderer &renderer) override;

        private:
        void Scrub(Vector2f mouse_position, bool notify = true);
        void ClampView();
        void UpdateScrollbarDrag(Vector2f mouse_position);
        f32 SnapTime(f32 time) const;
        u32 TrackAtLocalY(f32 local_y) const;
        f32 ChooseRulerStep() const;
        Vector4f GetViewportRect() const;
        Vector4f GetHorizontalScrollbarTrackRect() const;
        Vector4f GetVerticalScrollbarTrackRect() const;
        Vector4f GetHorizontalScrollbarThumbRect() const;
        Vector4f GetVerticalScrollbarThumbRect() const;
        bool HasHorizontalScrollbar() const;
        bool HasVerticalScrollbar() const;

        f32 _start_time = 0.0f;
        f32 _end_time = 1.0f;
        f32 _current_time = 0.0f;
        f32 _pixels_per_second = 100.0f;
        f32 _snap_interval = 0.0f;
        f32 _scroll_x = 0.0f;
        f32 _scroll_y = 0.0f;
        f32 _target_scroll_x = 0.0f;
        f32 _target_scroll_y = 0.0f;
        f32 _scroll_speed = 10.0f;
        f32 _header_width = 160.0f;
        f32 _ruler_height = 24.0f;
        f32 _track_height = 24.0f;
        bool _is_scrubbing = false;
        bool _is_dragging_horizontal_scrollbar = false;
        bool _is_dragging_vertical_scrollbar = false;
        bool _is_hover_horizontal_scrollbar = false;
        bool _is_hover_vertical_scrollbar = false;
        Vector2f _drag_start_mouse = Vector2f::kZero;
        f32 _drag_start_scroll = 0.0f;
        u32 _selected_track = 0u;
        Vector<TimelineTrack> _tracks;
    };
}
