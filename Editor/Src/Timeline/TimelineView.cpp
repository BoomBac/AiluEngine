#include "Timeline/TimelineView.h"

#include "Framework/Common/Input.h"
#include "UI/UIRenderer.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>

namespace Ailu::Editor
{
    namespace
    {
        constexpr f32 kMinPixelsPerSecond = 10.0f;
        constexpr f32 kMaxPixelsPerSecond = 1600.0f;
        constexpr f32 kScrollbarSize = 10.0f;
        constexpr f32 kScrollbarMinThumbSize = 24.0f;
        const Color kBackgroundColor(0.075f, 0.085f, 0.10f, 1.0f);
        const Color kHeaderColor(0.10f, 0.115f, 0.135f, 1.0f);
        const Color kRowColor(0.095f, 0.105f, 0.12f, 1.0f);
        const Color kAlternateRowColor(0.085f, 0.095f, 0.11f, 1.0f);
        const Color kGridColor(0.28f, 0.32f, 0.38f, 0.28f);
        const Color kMajorGridColor(0.40f, 0.46f, 0.54f, 0.42f);
        const Color kPlayheadColor(1.0f, 0.55f, 0.18f, 1.0f);
        const Color kTextColor(0.72f, 0.76f, 0.82f, 1.0f);
        const Color kScrollbarTrackColor(0.055f, 0.065f, 0.08f, 1.0f);
        const Color kScrollbarColor(0.30f, 0.35f, 0.42f, 1.0f);
        const Color kScrollbarHoverColor(0.40f, 0.48f, 0.58f, 1.0f);
        const Color kScrollbarPressedColor(0.50f, 0.62f, 0.74f, 1.0f);

        UI::UIBrush MakeColorBrush(Color color)
        {
            UI::UIBrush brush;
            brush._type = UI::EUIBrushType::kColor;
            brush._tint = color;
            return brush;
        }

        bool IsKeyDown(u32 key)
        {
            return Input::IsKeyDown(static_cast<EKey>(key));
        }
    }

    TimelineView::TimelineView() : UI::UIElement("TimelineView")
    {
        SetWantsMouseEvents(true);
        SetInteractiveEnabled(true);

        OnMouseDown() += [this](UI::UIEvent &event)
        {
            if (event._key_code != EKey::kLBUTTON)
                return;
            RequestFocus();
            if (HasHorizontalScrollbar() && IsPointInside(event._mouse_position, GetHorizontalScrollbarThumbRect()))
            {
                _is_dragging_horizontal_scrollbar = true;
                _drag_start_mouse = event._mouse_position;
                _drag_start_scroll = _target_scroll_x;
                event._is_handled = true;
                InvalidatePaint();
                return;
            }
            if (HasVerticalScrollbar() && IsPointInside(event._mouse_position, GetVerticalScrollbarThumbRect()))
            {
                _is_dragging_vertical_scrollbar = true;
                _drag_start_mouse = event._mouse_position;
                _drag_start_scroll = _target_scroll_y;
                event._is_handled = true;
                InvalidatePaint();
                return;
            }
            const Vector4f rect = GetContentRect();
            const Vector2f local = event._mouse_position - rect.xy;
            const u32 track_index = TrackAtLocalY(local.y);
            if (track_index < _tracks.size())
            {
                SetSelectedTrack(track_index);
                _on_track_selected_delegate.Invoke(track_index);
            }
            _is_scrubbing = true;
            Scrub(event._mouse_position);
            event._is_handled = true;
        };

        OnMouseMove() += [this](UI::UIEvent &event)
        {
            if (_is_dragging_horizontal_scrollbar || _is_dragging_vertical_scrollbar)
            {
                UpdateScrollbarDrag(event._mouse_position);
                event._is_handled = true;
                return;
            }
            const bool old_hover_horizontal = _is_hover_horizontal_scrollbar;
            const bool old_hover_vertical = _is_hover_vertical_scrollbar;
            _is_hover_horizontal_scrollbar = HasHorizontalScrollbar() &&
                                              IsPointInside(event._mouse_position, GetHorizontalScrollbarThumbRect());
            _is_hover_vertical_scrollbar = HasVerticalScrollbar() &&
                                           IsPointInside(event._mouse_position, GetVerticalScrollbarThumbRect());
            if (old_hover_horizontal != _is_hover_horizontal_scrollbar ||
                old_hover_vertical != _is_hover_vertical_scrollbar)
                InvalidatePaint();
            if (!_is_scrubbing)
                return;
            Scrub(event._mouse_position);
            event._is_handled = true;
        };

        OnMouseUp() += [this](UI::UIEvent &event)
        {
            if (_is_dragging_horizontal_scrollbar || _is_dragging_vertical_scrollbar)
            {
                _is_dragging_horizontal_scrollbar = false;
                _is_dragging_vertical_scrollbar = false;
                event._is_handled = true;
                InvalidatePaint();
                return;
            }
            if (event._key_code == EKey::kLBUTTON && _is_scrubbing)
            {
                _is_scrubbing = false;
                event._is_handled = true;
            }
        };

        OnMouseExit() += [this](UI::UIEvent &event)
        {
            if (_is_dragging_horizontal_scrollbar || _is_dragging_vertical_scrollbar)
                return;
            if (_is_hover_horizontal_scrollbar || _is_hover_vertical_scrollbar)
            {
                _is_hover_horizontal_scrollbar = false;
                _is_hover_vertical_scrollbar = false;
                InvalidatePaint();
            }
            event._is_handled = true;
        };

        OnMouseScroll() += [this](UI::UIEvent &event)
        {
            const Vector4f rect = GetContentRect();
            const Vector2f local = event._mouse_position - rect.xy;
            const bool ctrl = IsKeyDown(EKey::kLCONTROL) || IsKeyDown(EKey::kRCONTROL) ||
                               IsKeyDown(EKey::kCONTROL);
            const bool shift = IsKeyDown(EKey::kLSHIFT) || IsKeyDown(EKey::kRSHIFT);
            const f32 scroll_steps = event._scroll_delta / 120.0f;
            if (ctrl && event._scroll_delta != 0.0f)
            {
                const f32 old_pixels_per_second = _pixels_per_second;
                const Vector4f viewport = GetViewportRect();
                const f32 anchor_x = std::clamp(local.x, _header_width, viewport.z);
                const f32 anchor_time = LocalXToTime(anchor_x);
                const f32 scale = event._scroll_delta > 0.0f ? 1.15f : 1.0f / 1.15f;
                _pixels_per_second = std::clamp(_pixels_per_second * scale, kMinPixelsPerSecond,
                                                kMaxPixelsPerSecond);
                _target_scroll_x = (anchor_time - _start_time) * _pixels_per_second -
                                   (anchor_x - _header_width);
                if (!NearbyEqual(old_pixels_per_second, _pixels_per_second))
                {
                    ClampView();
                    _scroll_x = _target_scroll_x;
                    InvalidatePaint();
                }
            }
            else if (shift)
            {
                _target_scroll_x -= scroll_steps * 28.0f;
                ClampView();
                InvalidatePaint();
            }
            else
            {
                _target_scroll_y -= scroll_steps * 28.0f;
                ClampView();
                InvalidatePaint();
            }
            event._is_handled = true;
        };
    }

    void TimelineView::SetTimeRange(f32 start_time, f32 end_time)
    {
        _start_time = std::min(start_time, end_time);
        _end_time = std::max(end_time, _start_time + 0.001f);
        _current_time = std::clamp(_current_time, _start_time, _end_time);
        _fit_time_range_pending = true;
        ClampView();
        InvalidatePaint();
    }

    void TimelineView::SetCurrentTime(f32 time, bool snap)
    {
        const f32 next_time = std::clamp(snap ? SnapTime(time) : time, _start_time, _end_time);
        if (NearbyEqual(next_time, _current_time))
            return;
        _current_time = next_time;
        _on_time_changed_delegate.Invoke(_current_time);
        InvalidatePaint();
    }

    void TimelineView::SetPixelsPerSecond(f32 pixels_per_second)
    {
        _pixels_per_second = std::clamp(pixels_per_second, kMinPixelsPerSecond, kMaxPixelsPerSecond);
        ClampView();
        InvalidatePaint();
    }

    void TimelineView::SetSnapInterval(f32 interval)
    {
        _snap_interval = std::max(interval, 0.0f);
    }

    f32 TimelineView::TimeToLocalX(f32 time) const
    {
        return _header_width + (time - _start_time) * _pixels_per_second - _scroll_x;
    }

    f32 TimelineView::LocalXToTime(f32 x) const
    {
        return _start_time + (x - _header_width + _scroll_x) / _pixels_per_second;
    }

    f32 TimelineView::VisibleStartTime() const
    {
        return std::max(_start_time, _start_time + (_scroll_x / _pixels_per_second));
    }

    f32 TimelineView::VisibleEndTime() const
    {
        const f32 width = std::max(GetViewportRect().z - _header_width, 1.0f);
        return std::min(_end_time, LocalXToTime(_header_width + width));
    }

    void TimelineView::SetTracks(const Vector<TimelineTrack> &tracks)
    {
        _tracks = tracks;
        if (_selected_track >= _tracks.size())
            _selected_track = _tracks.empty() ? 0u : static_cast<u32>(_tracks.size() - 1u);
        ClampView();
        InvalidateLayout();
        InvalidatePaint();
    }

    void TimelineView::SetSelectedTrack(u32 index)
    {
        const u32 next_index = _tracks.empty() ? 0u : std::min(index, static_cast<u32>(_tracks.size() - 1u));
        if (_selected_track == next_index)
            return;
        _selected_track = next_index;
        InvalidatePaint();
    }

    void TimelineView::SetHeaderWidth(f32 width)
    {
        _header_width = std::max(width, 0.0f);
        ClampView();
        InvalidatePaint();
    }

    void TimelineView::SetRulerHeight(f32 height)
    {
        _ruler_height = std::max(height, 1.0f);
        ClampView();
        InvalidateLayout();
        InvalidatePaint();
    }

    Vector2f TimelineView::MeasureDesiredSize()
    {
        return {0.0f, _ruler_height + std::max(static_cast<f32>(_tracks.size()), 1.0f) * _track_height};
    }

    UI::UIElement *TimelineView::HitTest(Vector2f pos)
    {
        return IsPointInside(pos, GetContentRect()) ? this : nullptr;
    }

    void TimelineView::Update(f32 dt)
    {
        if ((_is_dragging_horizontal_scrollbar || _is_dragging_vertical_scrollbar) &&
            !Input::IsKeyDown(EKey::kLBUTTON))
        {
            _is_dragging_horizontal_scrollbar = false;
            _is_dragging_vertical_scrollbar = false;
            InvalidatePaint();
        }
        if (_fit_time_range_pending)
        {
            const Vector4f viewport = GetViewportRect();
            const f32 visible_width = viewport.z - _header_width;
            const f32 duration = _end_time - _start_time;
            if (visible_width > 1.0f && duration > 0.0f)
            {
                _pixels_per_second = std::clamp(visible_width / duration, kMinPixelsPerSecond,
                                                kMaxPixelsPerSecond);
                _scroll_x = 0.0f;
                _target_scroll_x = 0.0f;
                _fit_time_range_pending = false;
                ClampView();
                InvalidatePaint();
            }
        }
        ClampView();
        const f32 interpolation = std::clamp(std::max(dt, 0.0f) * _scroll_speed, 0.0f, 1.0f);
        const f32 old_scroll_x = _scroll_x;
        const f32 old_scroll_y = _scroll_y;
        _scroll_x += (_target_scroll_x - _scroll_x) * interpolation;
        _scroll_y += (_target_scroll_y - _scroll_y) * interpolation;
        if (std::abs(_target_scroll_x - _scroll_x) < 0.05f)
            _scroll_x = _target_scroll_x;
        if (std::abs(_target_scroll_y - _scroll_y) < 0.05f)
            _scroll_y = _target_scroll_y;
        if (!NearbyEqual(old_scroll_x, _scroll_x) || !NearbyEqual(old_scroll_y, _scroll_y))
            InvalidatePaint();
    }

    void TimelineView::RenderImpl(UI::UIRenderer &renderer)
    {
        const Vector4f rect = GetContentRect();
        if (rect.z <= 0.0f || rect.w <= 0.0f)
            return;

        renderer.DrawQuad(rect, MakeColorBrush(kBackgroundColor), -0.3f);
        const Vector4f viewport = GetViewportRect();
        const f32 track_top = viewport.y + std::min(_ruler_height, viewport.w);
        const Vector4f track_viewport{viewport.x, track_top, viewport.z,
                                      std::max(viewport.w - _ruler_height, 0.0f)};
        renderer.PushScissor(viewport);
        const Vector4f ruler_rect{viewport.x, viewport.y, viewport.z, _ruler_height};
        renderer.DrawQuad(ruler_rect, MakeColorBrush(kHeaderColor), -0.29f);

        const f32 visible_start = VisibleStartTime();
        const f32 visible_end = VisibleEndTime();
        const f32 ruler_step = ChooseRulerStep();
        const f32 first_ruler = std::floor(visible_start / ruler_step) * ruler_step;
        for (f32 time = first_ruler; time <= visible_end + ruler_step; time += ruler_step)
        {
            const f32 x = rect.x + TimeToLocalX(time);
            if (x < viewport.x + _header_width || x > viewport.x + viewport.z)
                continue;
            const bool major = std::fmod(std::abs(time / ruler_step), 5.0f) < 0.01f;
            renderer.DrawLine({x, viewport.y + _ruler_height}, {x, viewport.y + viewport.w}, 1.0f,
                              major ? kMajorGridColor : kGridColor, -0.2f);
            renderer.DrawLine({x, viewport.y + _ruler_height - (major ? 9.0f : 5.0f)},
                              {x, viewport.y + _ruler_height}, 1.0f, major ? kMajorGridColor : kGridColor, -0.18f);
            const String label = ruler_step < 1.0f ? std::format("{:.1f}", time) : std::format("{:.0f}", time);
            renderer.DrawText(label, {x + 3.0f, viewport.y + 3.0f}, 10.0f, kTextColor);
        }

        renderer.PushScissor(track_viewport);
        for (u32 index = 0u; index < _tracks.size(); ++index)
        {
            const f32 y = rect.y + _ruler_height + static_cast<f32>(index) * _track_height - _scroll_y;
            if (y + _track_height < viewport.y + _ruler_height || y > viewport.y + viewport.w)
                continue;
            const Color row_color = index == _selected_track ? Color(0.13f, 0.19f, 0.25f, 1.0f) :
                                      (index % 2u == 0u ? kRowColor : kAlternateRowColor);
            renderer.DrawQuad({viewport.x, y, viewport.z, _track_height}, MakeColorBrush(row_color), -0.25f);
            renderer.DrawLine({viewport.x, y + _track_height}, {viewport.x + viewport.z, y + _track_height}, 1.0f,
                              kGridColor, -0.2f);
            renderer.DrawText(_tracks[index]._name, {viewport.x + 7.0f, y + 5.0f}, 11.0f, kTextColor);

            for (f32 marker_time : _tracks[index]._markers)
            {
                if (marker_time < visible_start || marker_time > visible_end)
                    continue;
                const f32 x = rect.x + TimeToLocalX(marker_time);
                if (_tracks[index]._is_event_track)
                {
                    renderer.DrawQuad({x - 4.0f, y + _track_height * 0.5f - 4.0f, 8.0f, 8.0f},
                                      MakeColorBrush(_tracks[index]._marker_color), -0.1f);
                }
                else
                {
                    renderer.DrawQuad({x - 3.0f, y + _track_height * 0.5f - 3.0f, 6.0f, 6.0f},
                                      MakeColorBrush(_tracks[index]._marker_color), -0.1f);
                }
            }
        }
        renderer.PopScissor();

        const f32 playhead_x = rect.x + TimeToLocalX(_current_time);
        if (playhead_x >= viewport.x + _header_width && playhead_x <= viewport.x + viewport.z)
        {
            renderer.DrawLine({playhead_x, viewport.y}, {playhead_x, viewport.y + viewport.w}, 2.0f,
                              kPlayheadColor, 0.0f);
            renderer.DrawQuad({playhead_x - 5.0f, viewport.y, 10.0f, 8.0f}, MakeColorBrush(kPlayheadColor), -0.01f);
        }
        renderer.PopScissor();

        if (HasHorizontalScrollbar())
        {
            const Vector4f track = GetHorizontalScrollbarTrackRect();
            const Vector4f thumb = GetHorizontalScrollbarThumbRect();
            renderer.DrawQuad(track, MakeColorBrush(kScrollbarTrackColor), -0.28f);
            const Color thumb_color = _is_dragging_horizontal_scrollbar ? kScrollbarPressedColor :
                                      (_is_hover_horizontal_scrollbar ? kScrollbarHoverColor : kScrollbarColor);
            renderer.DrawQuad(thumb, MakeColorBrush(thumb_color), -0.27f);
        }
        if (HasVerticalScrollbar())
        {
            const Vector4f track = GetVerticalScrollbarTrackRect();
            const Vector4f thumb = GetVerticalScrollbarThumbRect();
            renderer.DrawQuad(track, MakeColorBrush(kScrollbarTrackColor), -0.28f);
            const Color thumb_color = _is_dragging_vertical_scrollbar ? kScrollbarPressedColor :
                                      (_is_hover_vertical_scrollbar ? kScrollbarHoverColor : kScrollbarColor);
            renderer.DrawQuad(thumb, MakeColorBrush(thumb_color), -0.27f);
        }
    }

    void TimelineView::Scrub(Vector2f mouse_position, bool notify)
    {
        const Vector4f rect = GetContentRect();
        const f32 local_x = mouse_position.x - rect.x;
        const Vector4f viewport = GetViewportRect();
        if (mouse_position.y < viewport.y || mouse_position.y >= viewport.y + viewport.w ||
            local_x < _header_width || mouse_position.x > viewport.x + viewport.z)
            return;
        const f32 time = std::clamp(SnapTime(LocalXToTime(local_x)), _start_time, _end_time);
        if (!NearbyEqual(time, _current_time))
        {
            _current_time = time;
            if (notify)
                _on_time_changed_delegate.Invoke(_current_time);
            InvalidatePaint();
        }
    }

    void TimelineView::ClampView()
    {
        const Vector4f viewport = GetViewportRect();
        const f32 visible_width = std::max(viewport.z - _header_width, 1.0f);
        const f32 content_width = std::max((_end_time - _start_time) * _pixels_per_second, visible_width);
        const f32 max_scroll_x = std::max(content_width - visible_width, 0.0f);
        _target_scroll_x = std::clamp(_target_scroll_x, 0.0f, max_scroll_x);
        _scroll_x = std::clamp(_scroll_x, 0.0f, max_scroll_x);
        const f32 content_height = _ruler_height + static_cast<f32>(_tracks.size()) * _track_height;
        const f32 max_scroll_y = std::max(content_height - viewport.w, 0.0f);
        _target_scroll_y = std::clamp(_target_scroll_y, 0.0f, max_scroll_y);
        _scroll_y = std::clamp(_scroll_y, 0.0f, max_scroll_y);
    }

    void TimelineView::UpdateScrollbarDrag(Vector2f mouse_position)
    {
        const Vector4f viewport = GetViewportRect();
        if (_is_dragging_horizontal_scrollbar)
        {
            const Vector4f thumb = GetHorizontalScrollbarThumbRect();
            const f32 movable_width = std::max(GetHorizontalScrollbarTrackRect().z - thumb.z, 1.0f);
            const f32 content_width = std::max((_end_time - _start_time) * _pixels_per_second,
                                               viewport.z - _header_width);
            const f32 scrollable_width = std::max(content_width - (viewport.z - _header_width), 0.0f);
            _target_scroll_x = std::clamp(_drag_start_scroll + (mouse_position.x - _drag_start_mouse.x) *
                                          scrollable_width / movable_width, 0.0f, scrollable_width);
            _scroll_x = _target_scroll_x;
        }
        else if (_is_dragging_vertical_scrollbar)
        {
            const Vector4f thumb = GetVerticalScrollbarThumbRect();
            const f32 movable_height = std::max(GetVerticalScrollbarTrackRect().w - thumb.w, 1.0f);
            const f32 content_height = _ruler_height + static_cast<f32>(_tracks.size()) * _track_height;
            const f32 scrollable_height = std::max(content_height - viewport.w, 0.0f);
            _target_scroll_y = std::clamp(_drag_start_scroll + (mouse_position.y - _drag_start_mouse.y) *
                                          scrollable_height / movable_height, 0.0f, scrollable_height);
            _scroll_y = _target_scroll_y;
        }
        InvalidatePaint();
    }

    Vector4f TimelineView::GetViewportRect() const
    {
        const Vector4f rect = GetContentRect();
        return {rect.x, rect.y, std::max(rect.z - (HasVerticalScrollbar() ? kScrollbarSize : 0.0f), 0.0f),
                std::max(rect.w - (HasHorizontalScrollbar() ? kScrollbarSize : 0.0f), 0.0f)};
    }

    Vector4f TimelineView::GetHorizontalScrollbarTrackRect() const
    {
        const Vector4f viewport = GetViewportRect();
        const f32 width = std::max(viewport.z - _header_width, 0.0f);
        return {viewport.x + _header_width, viewport.y + viewport.w, width, kScrollbarSize};
    }

    Vector4f TimelineView::GetVerticalScrollbarTrackRect() const
    {
        const Vector4f viewport = GetViewportRect();
        return {viewport.x + viewport.z, viewport.y, kScrollbarSize, viewport.w};
    }

    Vector4f TimelineView::GetHorizontalScrollbarThumbRect() const
    {
        const Vector4f track = GetHorizontalScrollbarTrackRect();
        const Vector4f viewport = GetViewportRect();
        const f32 content_width = std::max((_end_time - _start_time) * _pixels_per_second,
                                           viewport.z - _header_width);
        const f32 thumb_width = std::max(kScrollbarMinThumbSize, track.z * (viewport.z - _header_width) /
                                                                  std::max(content_width, 1.0f));
        const f32 movable_width = std::max(track.z - thumb_width, 0.0f);
        const f32 scrollable_width = std::max(content_width - (viewport.z - _header_width), 0.0f);
        const f32 thumb_x = track.x + (scrollable_width > 0.0f ? _scroll_x / scrollable_width * movable_width : 0.0f);
        return {thumb_x, track.y, std::min(thumb_width, track.z), track.w};
    }

    Vector4f TimelineView::GetVerticalScrollbarThumbRect() const
    {
        const Vector4f track = GetVerticalScrollbarTrackRect();
        const Vector4f viewport = GetViewportRect();
        const f32 content_height = _ruler_height + static_cast<f32>(_tracks.size()) * _track_height;
        const f32 thumb_height = std::max(kScrollbarMinThumbSize, track.w * viewport.w /
                                                                  std::max(content_height, 1.0f));
        const f32 movable_height = std::max(track.w - thumb_height, 0.0f);
        const f32 scrollable_height = std::max(content_height - viewport.w, 0.0f);
        const f32 thumb_y = track.y +
                            (scrollable_height > 0.0f ? _scroll_y / scrollable_height * movable_height : 0.0f);
        return {track.x, thumb_y, track.z, std::min(thumb_height, track.w)};
    }

    bool TimelineView::HasHorizontalScrollbar() const
    {
        const Vector4f rect = GetContentRect();
        const f32 content_width = (_end_time - _start_time) * _pixels_per_second;
        const f32 content_height = _ruler_height + static_cast<f32>(_tracks.size()) * _track_height;
        const bool vertical = content_height > rect.w;
        return content_width > rect.z - _header_width - (vertical ? kScrollbarSize : 0.0f);
    }

    bool TimelineView::HasVerticalScrollbar() const
    {
        const Vector4f rect = GetContentRect();
        const f32 content_width = (_end_time - _start_time) * _pixels_per_second;
        const bool horizontal = content_width > rect.z - _header_width;
        return _ruler_height + static_cast<f32>(_tracks.size()) * _track_height >
               rect.w - (horizontal ? kScrollbarSize : 0.0f);
    }

    f32 TimelineView::SnapTime(f32 time) const
    {
        return _snap_interval > 0.0f ? std::round(time / _snap_interval) * _snap_interval : time;
    }

    u32 TimelineView::TrackAtLocalY(f32 local_y) const
    {
        if (local_y < _ruler_height)
            return std::numeric_limits<u32>::max();
        const Vector4f viewport = GetViewportRect();
        if (local_y >= viewport.w)
            return std::numeric_limits<u32>::max();
        const f32 track_y = local_y - _ruler_height + _scroll_y;
        const u32 index = static_cast<u32>(track_y / _track_height);
        return index < _tracks.size() && track_y >= 0.0f ? index : std::numeric_limits<u32>::max();
    }

    f32 TimelineView::ChooseRulerStep() const
    {
        const f32 visible_range = std::max(VisibleEndTime() - VisibleStartTime(), 0.0f);
        if (visible_range < 2.0f)
            return 0.1f;
        if (visible_range <= 10.0f)
            return 1.0f;
        return 5.0f;
    }
}
