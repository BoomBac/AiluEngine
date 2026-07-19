#include "pch.h"
#include "UI/Style/UIStyles.h"

namespace Ailu
{
    namespace UI
    {
        // ================================================================
        // UIControlVisualOverride
        // ================================================================

        void UIControlVisualOverride::SetBackground(const UIBrush &brush)
        {
            _background = brush;
            _override_mask |= (u32)EUIControlVisualOverride::kBackground;
        }
        void UIControlVisualOverride::SetForeground(const UIBrush &brush)
        {
            _foreground = brush;
            _override_mask |= (u32)EUIControlVisualOverride::kForeground;
        }
        void UIControlVisualOverride::SetContentColor(const Color &c)
        {
            _content_color = c;
            _override_mask |= (u32)EUIControlVisualOverride::kTextColor;
        }
        void UIControlVisualOverride::SetBorderColor(const Color &c)
        {
            _border_color = c;
            _override_mask |= (u32)EUIControlVisualOverride::kBorderColor;
        }
        void UIControlVisualOverride::SetBorderWidth(f32 w)
        {
            _border_width = w;
            _override_mask |= (u32)EUIControlVisualOverride::kBorderWidth;
        }
        void UIControlVisualOverride::SetCornerRadius(const Vector4f &r)
        {
            _corner_radius = r;
            _override_mask |= (u32)EUIControlVisualOverride::kCornerRadius;
        }
        void UIControlVisualOverride::SetCornerRadius(f32 uniform)
        {
            _corner_radius = Vector4f(uniform, uniform, uniform, uniform);
            _override_mask |= (u32)EUIControlVisualOverride::kCornerRadius;
        }
        void UIControlVisualOverride::SetOpacity(f32 o)
        {
            _opacity = o;
            Clamp(_opacity, 0.0f, 1.0f);
            _override_mask |= (u32)EUIControlVisualOverride::kOpacity;
        }
        void UIControlVisualOverride::SetPadding(const Padding &p)
        {
            _padding = p;
            _override_mask |= (u32)EUIControlVisualOverride::kPadding;
        }
        void UIControlVisualOverride::SetFontSize(f32 s)
        {
            _font_size = s;
            _override_mask |= (u32)EUIControlVisualOverride::kFontSize;
        }

        void UIControlVisualOverride::ClearOverride(EUIControlVisualOverride flag)
        {
            _override_mask &= ~(u32)flag;
        }
        void UIControlVisualOverride::ClearAllOverrides()
        {
            _override_mask = 0u;
        }
        bool UIControlVisualOverride::HasOverride(EUIControlVisualOverride flag) const
        {
            return (_override_mask & (u32)flag) != 0u;
        }

        void UIControlVisualOverride::ApplyTo(UIControlVisual &visual) const
        {
            if (HasOverride(EUIControlVisualOverride::kBackground))
                visual._background = _background;
            if (HasOverride(EUIControlVisualOverride::kTextColor))
                visual._content_color = _content_color;
            if (HasOverride(EUIControlVisualOverride::kBorderColor))
                visual._border_color = _border_color;
            if (HasOverride(EUIControlVisualOverride::kBorderWidth))
                visual._border_width = _border_width;
            if (HasOverride(EUIControlVisualOverride::kCornerRadius))
                visual._corner_radius = _corner_radius;
        }

        // ================================================================
        // UIButtonStyleOverride
        // ================================================================

        void UIButtonStyleOverride::SetNormal(const UIControlVisual &v)       { _normal = v;   _override_mask |= (u32)EUIButtonStyleOverride::kNormal; }
        void UIButtonStyleOverride::SetHovered(const UIControlVisual &v)      { _hovered = v;  _override_mask |= (u32)EUIButtonStyleOverride::kHovered; }
        void UIButtonStyleOverride::SetPressed(const UIControlVisual &v)      { _pressed = v;  _override_mask |= (u32)EUIButtonStyleOverride::kPressed; }
        void UIButtonStyleOverride::SetFocused(const UIControlVisual &v)      { _focused = v;  _override_mask |= (u32)EUIButtonStyleOverride::kFocused; }
        void UIButtonStyleOverride::SetDisabled(const UIControlVisual &v)     { _disabled = v; _override_mask |= (u32)EUIButtonStyleOverride::kDisabled; }
        void UIButtonStyleOverride::SetPadding(const Padding &p)              { _padding = p;  _override_mask |= (u32)EUIButtonStyleOverride::kPadding; }
        void UIButtonStyleOverride::SetMinSize(const Vector2f &s)             { _min_size = s; _override_mask |= (u32)EUIButtonStyleOverride::kMinSize; }
        void UIButtonStyleOverride::SetFontSize(f32 s)                        { _font_size = s; _override_mask |= (u32)EUIButtonStyleOverride::kFontSize; }

        void UIButtonStyleOverride::ClearOverride(EUIButtonStyleOverride flag) { _override_mask &= ~(u32)flag; }
        void UIButtonStyleOverride::ClearAllOverrides()                        { _override_mask = 0u; }
        bool UIButtonStyleOverride::HasOverride(EUIButtonStyleOverride flag) const { return (_override_mask & (u32)flag) != 0u; }

        void UIButtonStyleOverride::ApplyTo(UIButtonStyle &style) const
        {
            if (HasOverride(EUIButtonStyleOverride::kNormal))   style._normal = _normal;
            if (HasOverride(EUIButtonStyleOverride::kHovered))  style._hovered = _hovered;
            if (HasOverride(EUIButtonStyleOverride::kPressed))  style._pressed = _pressed;
            if (HasOverride(EUIButtonStyleOverride::kFocused))  style._focused = _focused;
            if (HasOverride(EUIButtonStyleOverride::kDisabled)) style._disabled = _disabled;
            if (HasOverride(EUIButtonStyleOverride::kPadding))  style._padding = _padding;
            if (HasOverride(EUIButtonStyleOverride::kMinSize))  style._min_size = _min_size;
            if (HasOverride(EUIButtonStyleOverride::kFontSize)) style._font_size = _font_size;
        }

        // ================================================================
        // UISliderStyleOverride
        // ================================================================

        void UISliderStyleOverride::SetNormal(const UIControlVisual &v)       { _normal = v;   _override_mask |= (u32)EUISliderStyleOverride::kNormal; }
        void UISliderStyleOverride::SetHovered(const UIControlVisual &v)      { _hovered = v;  _override_mask |= (u32)EUISliderStyleOverride::kHovered; }
        void UISliderStyleOverride::SetPressed(const UIControlVisual &v)      { _pressed = v;  _override_mask |= (u32)EUISliderStyleOverride::kPressed; }
        void UISliderStyleOverride::SetFocused(const UIControlVisual &v)      { _focused = v;  _override_mask |= (u32)EUISliderStyleOverride::kFocused; }
        void UISliderStyleOverride::SetDisabled(const UIControlVisual &v)     { _disabled = v; _override_mask |= (u32)EUISliderStyleOverride::kDisabled; }
        void UISliderStyleOverride::SetTrackBackground(const UIBrush &b)      { _track_background = b; _override_mask |= (u32)EUISliderStyleOverride::kTrackBackground; }
        void UISliderStyleOverride::SetTrackFill(const UIBrush &b)            { _track_fill = b;      _override_mask |= (u32)EUISliderStyleOverride::kTrackFill; }
        void UISliderStyleOverride::SetThumb(const UIBrush &b)                { _thumb = b;           _override_mask |= (u32)EUISliderStyleOverride::kThumb; }
        void UISliderStyleOverride::SetThumbHovered(const UIBrush &b)         { _thumb_hovered = b;   _override_mask |= (u32)EUISliderStyleOverride::kThumbHovered; }
        void UISliderStyleOverride::SetThumbPressed(const UIBrush &b)         { _thumb_pressed = b;   _override_mask |= (u32)EUISliderStyleOverride::kThumbPressed; }
        void UISliderStyleOverride::SetThumbDisabled(const UIBrush &b)        { _thumb_disabled = b;  _override_mask |= (u32)EUISliderStyleOverride::kThumbDisabled; }
        void UISliderStyleOverride::SetThumbSize(const Vector2f &s)           { _thumb_size = s;      _override_mask |= (u32)EUISliderStyleOverride::kThumbSize; }
        void UISliderStyleOverride::SetTrackThickness(f32 t)                  { _track_thickness = t; _override_mask |= (u32)EUISliderStyleOverride::kTrackThickness; }
        void UISliderStyleOverride::SetTrackCornerRadius(f32 r)               { _track_corner_radius = r; _override_mask |= (u32)EUISliderStyleOverride::kTrackCornerRadius; }
        void UISliderStyleOverride::SetThumbCornerRadius(f32 r)              { _thumb_corner_radius = r; _override_mask |= (u32)EUISliderStyleOverride::kThumbCornerRadius; }
        void UISliderStyleOverride::SetPadding(const Padding &p)              { _padding = p;         _override_mask |= (u32)EUISliderStyleOverride::kPadding; }
        void UISliderStyleOverride::SetMinSize(const Vector2f &s)             { _min_size = s;        _override_mask |= (u32)EUISliderStyleOverride::kMinSize; }

        void UISliderStyleOverride::ClearOverride(EUISliderStyleOverride flag) { _override_mask &= ~(u32)flag; }
        void UISliderStyleOverride::ClearAllOverrides()                        { _override_mask = 0u; }
        bool UISliderStyleOverride::HasOverride(EUISliderStyleOverride flag) const { return (_override_mask & (u32)flag) != 0u; }

        void UISliderStyleOverride::ApplyTo(UISliderStyle &style) const
        {
            if (HasOverride(EUISliderStyleOverride::kNormal))           style._normal = _normal;
            if (HasOverride(EUISliderStyleOverride::kHovered))          style._hovered = _hovered;
            if (HasOverride(EUISliderStyleOverride::kPressed))          style._pressed = _pressed;
            if (HasOverride(EUISliderStyleOverride::kFocused))          style._focused = _focused;
            if (HasOverride(EUISliderStyleOverride::kDisabled))         style._disabled = _disabled;
            if (HasOverride(EUISliderStyleOverride::kTrackBackground))  style._track_background = _track_background;
            if (HasOverride(EUISliderStyleOverride::kTrackFill))        style._track_fill = _track_fill;
            if (HasOverride(EUISliderStyleOverride::kThumb))            style._thumb = _thumb;
            if (HasOverride(EUISliderStyleOverride::kThumbHovered))     style._thumb_hovered = _thumb_hovered;
            if (HasOverride(EUISliderStyleOverride::kThumbPressed))     style._thumb_pressed = _thumb_pressed;
            if (HasOverride(EUISliderStyleOverride::kThumbDisabled))    style._thumb_disabled = _thumb_disabled;
            if (HasOverride(EUISliderStyleOverride::kThumbSize))        style._thumb_size = _thumb_size;
            if (HasOverride(EUISliderStyleOverride::kTrackThickness))   style._track_thickness = _track_thickness;
            if (HasOverride(EUISliderStyleOverride::kTrackCornerRadius)) style._track_corner_radius = _track_corner_radius;
            if (HasOverride(EUISliderStyleOverride::kThumbCornerRadius)) style._thumb_corner_radius = _thumb_corner_radius;
            if (HasOverride(EUISliderStyleOverride::kPadding))          style._padding = _padding;
            if (HasOverride(EUISliderStyleOverride::kMinSize))          style._min_size = _min_size;
        }

        // ================================================================
        // UICheckBoxStyleOverride
        // ================================================================

        void UICheckBoxStyleOverride::SetNormal(const UIControlVisual &v)      { _normal = v;   _override_mask |= (u32)EUICheckBoxStyleOverride::kNormal; }
        void UICheckBoxStyleOverride::SetHovered(const UIControlVisual &v)     { _hovered = v;  _override_mask |= (u32)EUICheckBoxStyleOverride::kHovered; }
        void UICheckBoxStyleOverride::SetPressed(const UIControlVisual &v)     { _pressed = v;  _override_mask |= (u32)EUICheckBoxStyleOverride::kPressed; }
        void UICheckBoxStyleOverride::SetFocused(const UIControlVisual &v)     { _focused = v;  _override_mask |= (u32)EUICheckBoxStyleOverride::kFocused; }
        void UICheckBoxStyleOverride::SetDisabled(const UIControlVisual &v)    { _disabled = v; _override_mask |= (u32)EUICheckBoxStyleOverride::kDisabled; }
        void UICheckBoxStyleOverride::SetUnchecked(const UIBrush &b)           { _unchecked = b;           _override_mask |= (u32)EUICheckBoxStyleOverride::kUnchecked; }
        void UICheckBoxStyleOverride::SetUncheckedHovered(const UIBrush &b)    { _unchecked_hovered = b;   _override_mask |= (u32)EUICheckBoxStyleOverride::kUncheckedHovered; }
        void UICheckBoxStyleOverride::SetUncheckedPressed(const UIBrush &b)    { _unchecked_pressed = b;   _override_mask |= (u32)EUICheckBoxStyleOverride::kUncheckedPressed; }
        void UICheckBoxStyleOverride::SetChecked(const UIBrush &b)             { _checked = b;             _override_mask |= (u32)EUICheckBoxStyleOverride::kChecked; }
        void UICheckBoxStyleOverride::SetCheckedHovered(const UIBrush &b)      { _checked_hovered = b;     _override_mask |= (u32)EUICheckBoxStyleOverride::kCheckedHovered; }
        void UICheckBoxStyleOverride::SetCheckedPressed(const UIBrush &b)      { _checked_pressed = b;     _override_mask |= (u32)EUICheckBoxStyleOverride::kCheckedPressed; }
        void UICheckBoxStyleOverride::SetIndeterminate(const UIBrush &b)       { _indeterminate = b;       _override_mask |= (u32)EUICheckBoxStyleOverride::kIndeterminate; }
        void UICheckBoxStyleOverride::SetDisabledMark(const UIBrush &b)        { _disabled_mark = b;       _override_mask |= (u32)EUICheckBoxStyleOverride::kDisabledMark; }
        void UICheckBoxStyleOverride::SetMarkColor(const Color &c)             { _mark_color = c;          _override_mask |= (u32)EUICheckBoxStyleOverride::kMarkColor; }
        void UICheckBoxStyleOverride::SetBoxSize(const Vector2f &s)            { _box_size = s;            _override_mask |= (u32)EUICheckBoxStyleOverride::kBoxSize; }
        void UICheckBoxStyleOverride::SetLabelSpacing(f32 s)                   { _label_spacing = s;       _override_mask |= (u32)EUICheckBoxStyleOverride::kLabelSpacing; }
        void UICheckBoxStyleOverride::SetPadding(const Padding &p)             { _padding = p;             _override_mask |= (u32)EUICheckBoxStyleOverride::kPadding; }
        void UICheckBoxStyleOverride::SetFontSize(f32 s)                       { _font_size = s;           _override_mask |= (u32)EUICheckBoxStyleOverride::kFontSize; }

        void UICheckBoxStyleOverride::ClearOverride(EUICheckBoxStyleOverride flag) { _override_mask &= ~(u32)flag; }
        void UICheckBoxStyleOverride::ClearAllOverrides()                           { _override_mask = 0u; }
        bool UICheckBoxStyleOverride::HasOverride(EUICheckBoxStyleOverride flag) const { return (_override_mask & (u32)flag) != 0u; }

        void UICheckBoxStyleOverride::ApplyTo(UICheckBoxStyle &style) const
        {
            if (HasOverride(EUICheckBoxStyleOverride::kNormal))            style._normal = _normal;
            if (HasOverride(EUICheckBoxStyleOverride::kHovered))           style._hovered = _hovered;
            if (HasOverride(EUICheckBoxStyleOverride::kPressed))           style._pressed = _pressed;
            if (HasOverride(EUICheckBoxStyleOverride::kFocused))           style._focused = _focused;
            if (HasOverride(EUICheckBoxStyleOverride::kDisabled))          style._disabled = _disabled;
            if (HasOverride(EUICheckBoxStyleOverride::kUnchecked))         style._unchecked = _unchecked;
            if (HasOverride(EUICheckBoxStyleOverride::kUncheckedHovered))  style._unchecked_hovered = _unchecked_hovered;
            if (HasOverride(EUICheckBoxStyleOverride::kUncheckedPressed))  style._unchecked_pressed = _unchecked_pressed;
            if (HasOverride(EUICheckBoxStyleOverride::kChecked))           style._checked = _checked;
            if (HasOverride(EUICheckBoxStyleOverride::kCheckedHovered))    style._checked_hovered = _checked_hovered;
            if (HasOverride(EUICheckBoxStyleOverride::kCheckedPressed))    style._checked_pressed = _checked_pressed;
            if (HasOverride(EUICheckBoxStyleOverride::kIndeterminate))     style._indeterminate = _indeterminate;
            if (HasOverride(EUICheckBoxStyleOverride::kDisabledMark))      style._disabled_mark = _disabled_mark;
            if (HasOverride(EUICheckBoxStyleOverride::kMarkColor))         style._mark_color = _mark_color;
            if (HasOverride(EUICheckBoxStyleOverride::kBoxSize))           style._box_size = _box_size;
            if (HasOverride(EUICheckBoxStyleOverride::kLabelSpacing))      style._label_spacing = _label_spacing;
            if (HasOverride(EUICheckBoxStyleOverride::kPadding))           style._padding = _padding;
            if (HasOverride(EUICheckBoxStyleOverride::kFontSize))          style._font_size = _font_size;
        }

        // ================================================================
        // UIInputStyleOverride
        // ================================================================

        void UIInputStyleOverride::SetNormal(const UIControlVisual &v)       { _normal = v;    _override_mask |= (u32)EUIInputStyleOverride::kNormal; }
        void UIInputStyleOverride::SetHovered(const UIControlVisual &v)      { _hovered = v;   _override_mask |= (u32)EUIInputStyleOverride::kHovered; }
        void UIInputStyleOverride::SetFocused(const UIControlVisual &v)      { _focused = v;   _override_mask |= (u32)EUIInputStyleOverride::kFocused; }
        void UIInputStyleOverride::SetReadOnly(const UIControlVisual &v)     { _read_only = v; _override_mask |= (u32)EUIInputStyleOverride::kReadOnly; }
        void UIInputStyleOverride::SetDisabled(const UIControlVisual &v)     { _disabled = v;  _override_mask |= (u32)EUIInputStyleOverride::kDisabled; }
        void UIInputStyleOverride::SetInvalid(const UIControlVisual &v)      { _invalid = v;   _override_mask |= (u32)EUIInputStyleOverride::kInvalid; }
        void UIInputStyleOverride::SetPlaceholderColor(const Color &c)       { _placeholder_color = c; _override_mask |= (u32)EUIInputStyleOverride::kPlaceholderColor; }
        void UIInputStyleOverride::SetSelectionColor(const Color &c)         { _selection_color = c;   _override_mask |= (u32)EUIInputStyleOverride::kSelectionColor; }
        void UIInputStyleOverride::SetCaretColor(const Color &c)             { _caret_color = c;       _override_mask |= (u32)EUIInputStyleOverride::kCaretColor; }
        void UIInputStyleOverride::SetPadding(const Padding &p)              { _padding = p;           _override_mask |= (u32)EUIInputStyleOverride::kPadding; }
        void UIInputStyleOverride::SetMinSize(const Vector2f &s)             { _min_size = s;          _override_mask |= (u32)EUIInputStyleOverride::kMinSize; }
        void UIInputStyleOverride::SetFontSize(f32 s)                        { _font_size = s;         _override_mask |= (u32)EUIInputStyleOverride::kFontSize; }
        void UIInputStyleOverride::SetCaretWidth(f32 w)                      { _caret_width = w;       _override_mask |= (u32)EUIInputStyleOverride::kCaretWidth; }
        void UIInputStyleOverride::SetSelectionCornerRadius(f32 r)           { _selection_corner_radius = r; _override_mask |= (u32)EUIInputStyleOverride::kSelectionCornerRadius; }

        void UIInputStyleOverride::ClearOverride(EUIInputStyleOverride flag) { _override_mask &= ~(u32)flag; }
        void UIInputStyleOverride::ClearAllOverrides()                       { _override_mask = 0u; }
        bool UIInputStyleOverride::HasOverride(EUIInputStyleOverride flag) const { return (_override_mask & (u32)flag) != 0u; }

        void UIInputStyleOverride::ApplyTo(UIInputStyle &style) const
        {
            if (HasOverride(EUIInputStyleOverride::kNormal))                style._normal = _normal;
            if (HasOverride(EUIInputStyleOverride::kHovered))               style._hovered = _hovered;
            if (HasOverride(EUIInputStyleOverride::kFocused))               style._focused = _focused;
            if (HasOverride(EUIInputStyleOverride::kReadOnly))              style._read_only = _read_only;
            if (HasOverride(EUIInputStyleOverride::kDisabled))              style._disabled = _disabled;
            if (HasOverride(EUIInputStyleOverride::kInvalid))               style._invalid = _invalid;
            if (HasOverride(EUIInputStyleOverride::kPlaceholderColor))      style._placeholder_color = _placeholder_color;
            if (HasOverride(EUIInputStyleOverride::kSelectionColor))        style._selection_color = _selection_color;
            if (HasOverride(EUIInputStyleOverride::kCaretColor))            style._caret_color = _caret_color;
            if (HasOverride(EUIInputStyleOverride::kPadding))               style._padding = _padding;
            if (HasOverride(EUIInputStyleOverride::kMinSize))               style._min_size = _min_size;
            if (HasOverride(EUIInputStyleOverride::kFontSize))              style._font_size = _font_size;
            if (HasOverride(EUIInputStyleOverride::kCaretWidth))            style._caret_width = _caret_width;
            if (HasOverride(EUIInputStyleOverride::kSelectionCornerRadius)) style._selection_corner_radius = _selection_corner_radius;
        }

        // ================================================================
        // UIScrollBarStyleOverride
        // ================================================================

        void UIScrollBarStyleOverride::SetNormal(const UIControlVisual &v)               { _normal = v;    _override_mask |= (u32)EUIScrollBarStyleOverride::kNormal; }
        void UIScrollBarStyleOverride::SetHovered(const UIControlVisual &v)              { _hovered = v;   _override_mask |= (u32)EUIScrollBarStyleOverride::kHovered; }
        void UIScrollBarStyleOverride::SetFocused(const UIControlVisual &v)              { _focused = v;   _override_mask |= (u32)EUIScrollBarStyleOverride::kFocused; }
        void UIScrollBarStyleOverride::SetDisabled(const UIControlVisual &v)             { _disabled = v;  _override_mask |= (u32)EUIScrollBarStyleOverride::kDisabled; }
        void UIScrollBarStyleOverride::SetTrack(const UIBrush &b)                        { _track = b;                   _override_mask |= (u32)EUIScrollBarStyleOverride::kTrack; }
        void UIScrollBarStyleOverride::SetTrackHovered(const UIBrush &b)                 { _track_hovered = b;           _override_mask |= (u32)EUIScrollBarStyleOverride::kTrackHovered; }
        void UIScrollBarStyleOverride::SetTrackDisabled(const UIBrush &b)                { _track_disabled = b;          _override_mask |= (u32)EUIScrollBarStyleOverride::kTrackDisabled; }
        void UIScrollBarStyleOverride::SetThumb(const UIBrush &b)                        { _thumb = b;                   _override_mask |= (u32)EUIScrollBarStyleOverride::kThumb; }
        void UIScrollBarStyleOverride::SetThumbHovered(const UIBrush &b)                 { _thumb_hovered = b;           _override_mask |= (u32)EUIScrollBarStyleOverride::kThumbHovered; }
        void UIScrollBarStyleOverride::SetThumbPressed(const UIBrush &b)                 { _thumb_pressed = b;           _override_mask |= (u32)EUIScrollBarStyleOverride::kThumbPressed; }
        void UIScrollBarStyleOverride::SetThumbDisabled(const UIBrush &b)                { _thumb_disabled = b;          _override_mask |= (u32)EUIScrollBarStyleOverride::kThumbDisabled; }
        void UIScrollBarStyleOverride::SetDecreaseButton(const UIBrush &b)               { _decrease_button = b;         _override_mask |= (u32)EUIScrollBarStyleOverride::kDecreaseButton; }
        void UIScrollBarStyleOverride::SetDecreaseButtonHovered(const UIBrush &b)        { _decrease_button_hovered = b; _override_mask |= (u32)EUIScrollBarStyleOverride::kDecreaseButtonHovered; }
        void UIScrollBarStyleOverride::SetDecreaseButtonPressed(const UIBrush &b)        { _decrease_button_pressed = b; _override_mask |= (u32)EUIScrollBarStyleOverride::kDecreaseButtonPressed; }
        void UIScrollBarStyleOverride::SetDecreaseButtonDisabled(const UIBrush &b)       { _decrease_button_disabled = b; _override_mask |= (u32)EUIScrollBarStyleOverride::kDecreaseButtonDisabled; }
        void UIScrollBarStyleOverride::SetIncreaseButton(const UIBrush &b)               { _increase_button = b;         _override_mask |= (u32)EUIScrollBarStyleOverride::kIncreaseButton; }
        void UIScrollBarStyleOverride::SetIncreaseButtonHovered(const UIBrush &b)        { _increase_button_hovered = b; _override_mask |= (u32)EUIScrollBarStyleOverride::kIncreaseButtonHovered; }
        void UIScrollBarStyleOverride::SetIncreaseButtonPressed(const UIBrush &b)        { _increase_button_pressed = b; _override_mask |= (u32)EUIScrollBarStyleOverride::kIncreaseButtonPressed; }
        void UIScrollBarStyleOverride::SetIncreaseButtonDisabled(const UIBrush &b)       { _increase_button_disabled = b; _override_mask |= (u32)EUIScrollBarStyleOverride::kIncreaseButtonDisabled; }
        void UIScrollBarStyleOverride::SetThickness(f32 t)                               { _thickness = t;               _override_mask |= (u32)EUIScrollBarStyleOverride::kThickness; }
        void UIScrollBarStyleOverride::SetMinThumbLength(f32 l)                          { _min_thumb_length = l;        _override_mask |= (u32)EUIScrollBarStyleOverride::kMinThumbLength; }
        void UIScrollBarStyleOverride::SetThumbCornerRadius(f32 r)                       { _thumb_corner_radius = r;     _override_mask |= (u32)EUIScrollBarStyleOverride::kThumbCornerRadius; }
        void UIScrollBarStyleOverride::SetTrackCornerRadius(f32 r)                       { _track_corner_radius = r;     _override_mask |= (u32)EUIScrollBarStyleOverride::kTrackCornerRadius; }
        void UIScrollBarStyleOverride::SetButtonLength(f32 l)                            { _button_length = l;           _override_mask |= (u32)EUIScrollBarStyleOverride::kButtonLength; }
        void UIScrollBarStyleOverride::SetPadding(const Padding &p)                      { _padding = p;                 _override_mask |= (u32)EUIScrollBarStyleOverride::kPadding; }

        void UIScrollBarStyleOverride::ClearOverride(EUIScrollBarStyleOverride flag) { _override_mask &= ~(u32)flag; }
        void UIScrollBarStyleOverride::ClearAllOverrides()                            { _override_mask = 0u; }
        bool UIScrollBarStyleOverride::HasOverride(EUIScrollBarStyleOverride flag) const { return (_override_mask & (u32)flag) != 0u; }

        void UIScrollBarStyleOverride::ApplyTo(UIScrollBarStyle &style) const
        {
            if (HasOverride(EUIScrollBarStyleOverride::kNormal))                  style._normal = _normal;
            if (HasOverride(EUIScrollBarStyleOverride::kHovered))                 style._hovered = _hovered;
            if (HasOverride(EUIScrollBarStyleOverride::kFocused))                 style._focused = _focused;
            if (HasOverride(EUIScrollBarStyleOverride::kDisabled))                style._disabled = _disabled;
            if (HasOverride(EUIScrollBarStyleOverride::kTrack))                   style._track = _track;
            if (HasOverride(EUIScrollBarStyleOverride::kTrackHovered))            style._track_hovered = _track_hovered;
            if (HasOverride(EUIScrollBarStyleOverride::kTrackDisabled))           style._track_disabled = _track_disabled;
            if (HasOverride(EUIScrollBarStyleOverride::kThumb))                   style._thumb = _thumb;
            if (HasOverride(EUIScrollBarStyleOverride::kThumbHovered))            style._thumb_hovered = _thumb_hovered;
            if (HasOverride(EUIScrollBarStyleOverride::kThumbPressed))            style._thumb_pressed = _thumb_pressed;
            if (HasOverride(EUIScrollBarStyleOverride::kThumbDisabled))           style._thumb_disabled = _thumb_disabled;
            if (HasOverride(EUIScrollBarStyleOverride::kDecreaseButton))          style._decrease_button = _decrease_button;
            if (HasOverride(EUIScrollBarStyleOverride::kDecreaseButtonHovered))   style._decrease_button_hovered = _decrease_button_hovered;
            if (HasOverride(EUIScrollBarStyleOverride::kDecreaseButtonPressed))   style._decrease_button_pressed = _decrease_button_pressed;
            if (HasOverride(EUIScrollBarStyleOverride::kDecreaseButtonDisabled))  style._decrease_button_disabled = _decrease_button_disabled;
            if (HasOverride(EUIScrollBarStyleOverride::kIncreaseButton))          style._increase_button = _increase_button;
            if (HasOverride(EUIScrollBarStyleOverride::kIncreaseButtonHovered))   style._increase_button_hovered = _increase_button_hovered;
            if (HasOverride(EUIScrollBarStyleOverride::kIncreaseButtonPressed))   style._increase_button_pressed = _increase_button_pressed;
            if (HasOverride(EUIScrollBarStyleOverride::kIncreaseButtonDisabled))  style._increase_button_disabled = _increase_button_disabled;
            if (HasOverride(EUIScrollBarStyleOverride::kThickness))               style._thickness = _thickness;
            if (HasOverride(EUIScrollBarStyleOverride::kMinThumbLength))          style._min_thumb_length = _min_thumb_length;
            if (HasOverride(EUIScrollBarStyleOverride::kThumbCornerRadius))       style._thumb_corner_radius = _thumb_corner_radius;
            if (HasOverride(EUIScrollBarStyleOverride::kTrackCornerRadius))       style._track_corner_radius = _track_corner_radius;
            if (HasOverride(EUIScrollBarStyleOverride::kButtonLength))            style._button_length = _button_length;
            if (HasOverride(EUIScrollBarStyleOverride::kPadding))                 style._padding = _padding;
        }

        // ================================================================
        // UIScrollViewStyleOverride
        // ================================================================

        void UIScrollViewStyleOverride::SetNormal(const UIControlVisual &v)                     { _normal = v;    _override_mask |= (u32)EUIScrollViewStyleOverride::kNormal; }
        void UIScrollViewStyleOverride::SetHovered(const UIControlVisual &v)                    { _hovered = v;   _override_mask |= (u32)EUIScrollViewStyleOverride::kHovered; }
        void UIScrollViewStyleOverride::SetFocused(const UIControlVisual &v)                    { _focused = v;   _override_mask |= (u32)EUIScrollViewStyleOverride::kFocused; }
        void UIScrollViewStyleOverride::SetDisabled(const UIControlVisual &v)                   { _disabled = v;  _override_mask |= (u32)EUIScrollViewStyleOverride::kDisabled; }
        void UIScrollViewStyleOverride::SetHorizontalScrollbar(const UIScrollBarStyle &s)       { _horizontal_scrollbar = s; _override_mask |= (u32)EUIScrollViewStyleOverride::kHorizontalScrollbar; }
        void UIScrollViewStyleOverride::SetVerticalScrollbar(const UIScrollBarStyle &s)         { _vertical_scrollbar = s;   _override_mask |= (u32)EUIScrollViewStyleOverride::kVerticalScrollbar; }
        void UIScrollViewStyleOverride::SetContentPadding(const Padding &p)                     { _content_padding = p;                    _override_mask |= (u32)EUIScrollViewStyleOverride::kContentPadding; }
        void UIScrollViewStyleOverride::SetScrollbarSpacing(f32 s)                              { _scrollbar_spacing = s;                  _override_mask |= (u32)EUIScrollViewStyleOverride::kScrollbarSpacing; }
        void UIScrollViewStyleOverride::SetMouseWheelScrollDelta(f32 d)                         { _mouse_wheel_scroll_delta = d;           _override_mask |= (u32)EUIScrollViewStyleOverride::kMouseWheelScrollDelta; }
        void UIScrollViewStyleOverride::SetHorizontalMouseWheelScrollDelta(f32 d)               { _horizontal_mouse_wheel_scroll_delta = d; _override_mask |= (u32)EUIScrollViewStyleOverride::kHorizontalMouseWheelScrollDelta; }
        void UIScrollViewStyleOverride::SetReserveHorizontalScrollbarSpace(bool b)              { _reserve_horizontal_scrollbar_space = b; _override_mask |= (u32)EUIScrollViewStyleOverride::kReserveHorizontalScrollbarSpace; }
        void UIScrollViewStyleOverride::SetReserveVerticalScrollbarSpace(bool b)                { _reserve_vertical_scrollbar_space = b;   _override_mask |= (u32)EUIScrollViewStyleOverride::kReserveVerticalScrollbarSpace; }

        void UIScrollViewStyleOverride::ClearOverride(EUIScrollViewStyleOverride flag) { _override_mask &= ~(u32)flag; }
        void UIScrollViewStyleOverride::ClearAllOverrides()                             { _override_mask = 0u; }
        bool UIScrollViewStyleOverride::HasOverride(EUIScrollViewStyleOverride flag) const { return (_override_mask & (u32)flag) != 0u; }

        void UIScrollViewStyleOverride::ApplyTo(UIScrollViewStyle &style) const
        {
            if (HasOverride(EUIScrollViewStyleOverride::kNormal))                            style._normal = _normal;
            if (HasOverride(EUIScrollViewStyleOverride::kHovered))                           style._hovered = _hovered;
            if (HasOverride(EUIScrollViewStyleOverride::kFocused))                           style._focused = _focused;
            if (HasOverride(EUIScrollViewStyleOverride::kDisabled))                          style._disabled = _disabled;
            if (HasOverride(EUIScrollViewStyleOverride::kHorizontalScrollbar))               style._horizontal_scrollbar = _horizontal_scrollbar;
            if (HasOverride(EUIScrollViewStyleOverride::kVerticalScrollbar))                 style._vertical_scrollbar = _vertical_scrollbar;
            if (HasOverride(EUIScrollViewStyleOverride::kContentPadding))                    style._content_padding = _content_padding;
            if (HasOverride(EUIScrollViewStyleOverride::kScrollbarSpacing))                  style._scrollbar_spacing = _scrollbar_spacing;
            if (HasOverride(EUIScrollViewStyleOverride::kMouseWheelScrollDelta))             style._mouse_wheel_scroll_delta = _mouse_wheel_scroll_delta;
            if (HasOverride(EUIScrollViewStyleOverride::kHorizontalMouseWheelScrollDelta))   style._horizontal_mouse_wheel_scroll_delta = _horizontal_mouse_wheel_scroll_delta;
            if (HasOverride(EUIScrollViewStyleOverride::kReserveHorizontalScrollbarSpace))   style._reserve_horizontal_scrollbar_space = _reserve_horizontal_scrollbar_space;
            if (HasOverride(EUIScrollViewStyleOverride::kReserveVerticalScrollbarSpace))     style._reserve_vertical_scrollbar_space = _reserve_vertical_scrollbar_space;
        }

    }// namespace UI
}// namespace Ailu
