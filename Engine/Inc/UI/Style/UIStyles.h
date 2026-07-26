#pragma once

#include "UIStyleBasic.h"
#include "generated/UIStyles.gen.h"

namespace Ailu
{
    namespace UI
    {
        // ================================================================
        // UIControlVisual - base visual state
        // ================================================================
        ASTRUCT()
        struct AILU_API UIControlVisual
        {
            GENERATED_BODY()

            APROPERTY()
            UIBrush _background;

            APROPERTY()
            Color _content_color = Colors::kWhite;

            APROPERTY()
            Color _border_color = Colors::kTransparent;

            APROPERTY()
            f32 _border_width = 0.0f;

            APROPERTY()
            Vector4f _corner_radius = Vector4f::kZero;
        };

        // ================================================================
        // EUIControlVisualOverride - visual property override flags
        // ================================================================
        AENUM()
        enum class EUIControlVisualOverride : u32
        {
            kNone = 0u,
            kBackground = 1u << 0u,
            kForeground = 1u << 1u,
            kTextColor = 1u << 2u,
            kBorderWidth = 1u << 3u,
            kCornerRadius = 1u << 4u,
            kBorderColor = 1u << 5u,
            kOpacity = 1u << 6u,
            kPadding = 1u << 7u,
            kFontSize = 1u << 8u,
        };

        // ================================================================
        // UIControlVisualOverride - visual property override data
        // ================================================================
        ASTRUCT()
        struct AILU_API UIControlVisualOverride
        {
            GENERATED_BODY()
        public:
            // Setters: update value and mask together.
            void SetBackground(const UIBrush &brush);
            void SetForeground(const UIBrush &brush);
            void SetContentColor(const Color &c);
            void SetBorderColor(const Color &c);
            void SetBorderWidth(f32 w);
            void SetCornerRadius(const Vector4f &r);
            void SetCornerRadius(f32 uniform);
            void SetOpacity(f32 o);
            void SetPadding(const Padding &p);
            void SetFontSize(f32 s);

            // Mask management.
            void ClearOverride(EUIControlVisualOverride flag);
            void ClearAllOverrides();
            bool HasOverride(EUIControlVisualOverride flag) const;
            u32 GetOverrideMask() const { return _override_mask; }

            // Apply overrides.
            void ApplyTo(UIControlVisual &visual) const;

        public:
            APROPERTY()
            u32 _override_mask = 0u;

            APROPERTY()
            UIBrush _background;

            APROPERTY()
            UIBrush _foreground;

            APROPERTY()
            Color _content_color = Colors::kWhite;

            APROPERTY()
            Color _border_color = Colors::kTransparent;

            APROPERTY()
            f32 _border_width = 0.0f;

            APROPERTY()
            Vector4f _corner_radius = Vector4f::kZero;

            APROPERTY()
            f32 _opacity = 1.0f;

            APROPERTY()
            Padding _padding = Padding(0.0f);

            APROPERTY()
            f32 _font_size = 14.0f;
        };

        // ================================================================
        // UIButtonStyle - button style
        // ================================================================
        ASTRUCT()
        struct AILU_API UIButtonStyle
        {
            GENERATED_BODY()

            APROPERTY()
            UIControlVisual _normal;

            APROPERTY()
            UIControlVisual _hovered;

            APROPERTY()
            UIControlVisual _pressed;

            APROPERTY()
            UIControlVisual _focused;

            APROPERTY()
            UIControlVisual _disabled;

            APROPERTY()
            Padding _padding = Padding(6.0f, 3.0f, 6.0f, 3.0f);

            APROPERTY()
            Vector2f _min_size = Vector2f(80.0f, 20.0f);

            APROPERTY()
            f32 _font_size = 14.0f;
        };

        // UIButtonStyle override
        AENUM()
        enum class EUIButtonStyleOverride : u32
        {
            kNone = 0u,
            kNormal = 1u << 0u,
            kHovered = 1u << 1u,
            kPressed = 1u << 2u,
            kFocused = 1u << 3u,
            kDisabled = 1u << 4u,
            kPadding = 1u << 5u,
            kMinSize = 1u << 6u,
            kFontSize = 1u << 7u,
        };

        ASTRUCT()
        struct AILU_API UIButtonStyleOverride
        {
            GENERATED_BODY()
        public:
            void SetNormal(const UIControlVisual &v);
            void SetHovered(const UIControlVisual &v);
            void SetPressed(const UIControlVisual &v);
            void SetFocused(const UIControlVisual &v);
            void SetDisabled(const UIControlVisual &v);
            void SetPadding(const Padding &p);
            void SetMinSize(const Vector2f &s);
            void SetFontSize(f32 s);

            void ClearOverride(EUIButtonStyleOverride flag);
            void ClearAllOverrides();
            bool HasOverride(EUIButtonStyleOverride flag) const;
            u32 GetOverrideMask() const { return _override_mask; }

            void ApplyTo(UIButtonStyle &style) const;

        public:
            APROPERTY()
            u32 _override_mask = 0u;

            APROPERTY()
            UIControlVisual _normal;
            APROPERTY()
            UIControlVisual _hovered;
            APROPERTY()
            UIControlVisual _pressed;
            APROPERTY()
            UIControlVisual _focused;
            APROPERTY()
            UIControlVisual _disabled;

            APROPERTY()
            Padding _padding = Padding(6.0f, 3.0f, 6.0f, 3.0f);
            APROPERTY()
            Vector2f _min_size = Vector2f(80.0f, 20.0f);
            APROPERTY()
            f32 _font_size = 14.0f;
        };

        // ================================================================
        // UISliderStyle - slider style
        // ================================================================
        ASTRUCT()
        struct AILU_API UISliderStyle
        {
            GENERATED_BODY()

            APROPERTY()
            UIControlVisual _normal;

            APROPERTY()
            UIControlVisual _hovered;

            APROPERTY()
            UIControlVisual _pressed;

            APROPERTY()
            UIControlVisual _focused;

            APROPERTY()
            UIControlVisual _disabled;

            APROPERTY()
            UIBrush _track_background;

            APROPERTY()
            UIBrush _track_fill;

            APROPERTY()
            UIBrush _thumb;

            APROPERTY()
            UIBrush _thumb_hovered;

            APROPERTY()
            UIBrush _thumb_pressed;

            APROPERTY()
            UIBrush _thumb_disabled;

            APROPERTY()
            Vector2f _thumb_size = Vector2f(14.0f, 14.0f);

            APROPERTY()
            f32 _track_thickness = 4.0f;

            APROPERTY()
            f32 _track_corner_radius = 2.0f;

            APROPERTY()
            f32 _thumb_corner_radius = 3.0f;

            APROPERTY()
            Padding _padding = Padding(4.0f);

            APROPERTY()
            Vector2f _min_size = Vector2f(80.0f, 20.0f);
        };

        // UISliderStyle override
        AENUM()
        enum class EUISliderStyleOverride : u32
        {
            kNone = 0u,
            kNormal = 1u << 0u,
            kHovered = 1u << 1u,
            kPressed = 1u << 2u,
            kFocused = 1u << 3u,
            kDisabled = 1u << 4u,
            kTrackBackground = 1u << 5u,
            kTrackFill = 1u << 6u,
            kThumb = 1u << 7u,
            kThumbHovered = 1u << 8u,
            kThumbPressed = 1u << 9u,
            kThumbDisabled = 1u << 10u,
            kThumbSize = 1u << 11u,
            kTrackThickness = 1u << 12u,
            kTrackCornerRadius = 1u << 13u,
            kPadding = 1u << 14u,
            kMinSize = 1u << 15u,
            kThumbCornerRadius = 1u << 16u,
        };

        ASTRUCT()
        struct AILU_API UISliderStyleOverride
        {
            GENERATED_BODY()
        public:
            void SetNormal(const UIControlVisual &v);
            void SetHovered(const UIControlVisual &v);
            void SetPressed(const UIControlVisual &v);
            void SetFocused(const UIControlVisual &v);
            void SetDisabled(const UIControlVisual &v);
            void SetTrackBackground(const UIBrush &b);
            void SetTrackFill(const UIBrush &b);
            void SetThumb(const UIBrush &b);
            void SetThumbHovered(const UIBrush &b);
            void SetThumbPressed(const UIBrush &b);
            void SetThumbDisabled(const UIBrush &b);
            void SetThumbSize(const Vector2f &s);
            void SetTrackThickness(f32 t);
            void SetTrackCornerRadius(f32 r);
            void SetThumbCornerRadius(f32 r);
            void SetPadding(const Padding &p);
            void SetMinSize(const Vector2f &s);

            void ClearOverride(EUISliderStyleOverride flag);
            void ClearAllOverrides();
            bool HasOverride(EUISliderStyleOverride flag) const;
            u32 GetOverrideMask() const { return _override_mask; }

            void ApplyTo(UISliderStyle &style) const;

        public:
            APROPERTY()
            u32 _override_mask = 0u;

            APROPERTY()
            UIControlVisual _normal;
            APROPERTY()
            UIControlVisual _hovered;
            APROPERTY()
            UIControlVisual _pressed;
            APROPERTY()
            UIControlVisual _focused;
            APROPERTY()
            UIControlVisual _disabled;

            APROPERTY()
            UIBrush _track_background;
            APROPERTY()
            UIBrush _track_fill;
            APROPERTY()
            UIBrush _thumb;
            APROPERTY()
            UIBrush _thumb_hovered;
            APROPERTY()
            UIBrush _thumb_pressed;
            APROPERTY()
            UIBrush _thumb_disabled;

            APROPERTY()
            Vector2f _thumb_size = Vector2f(14.0f, 14.0f);
            APROPERTY()
            f32 _track_thickness = 4.0f;
            APROPERTY()
            f32 _track_corner_radius = 2.0f;
            APROPERTY()
            f32 _thumb_corner_radius = 3.0f;
            APROPERTY()
            Padding _padding = Padding(4.0f);
            APROPERTY()
            Vector2f _min_size = Vector2f(80.0f, 20.0f);
        };

        // ================================================================
        // UICheckBoxStyle - checkbox style
        // ================================================================
        ASTRUCT()
        struct AILU_API UICheckBoxStyle
        {
            GENERATED_BODY()

            APROPERTY()
            UIControlVisual _normal;

            APROPERTY()
            UIControlVisual _hovered;

            APROPERTY()
            UIControlVisual _pressed;

            APROPERTY()
            UIControlVisual _focused;

            APROPERTY()
            UIControlVisual _disabled;

            APROPERTY()
            UIBrush _unchecked;

            APROPERTY()
            UIBrush _unchecked_hovered;

            APROPERTY()
            UIBrush _unchecked_pressed;

            APROPERTY()
            UIBrush _checked;

            APROPERTY()
            UIBrush _checked_hovered;

            APROPERTY()
            UIBrush _checked_pressed;

            APROPERTY()
            UIBrush _indeterminate;

            APROPERTY()
            UIBrush _disabled_mark;

            APROPERTY()
            Color _mark_color = Colors::kWhite;

            APROPERTY()
            Vector2f _box_size = Vector2f(16.0f, 16.0f);

            APROPERTY()
            f32 _label_spacing = 6.0f;

            APROPERTY()
            Padding _padding = Padding(2.0f);

            APROPERTY()
            f32 _font_size = 14.0f;
        };

        // UICheckBoxStyle override
        AENUM()
        enum class EUICheckBoxStyleOverride : u32
        {
            kNone = 0u,
            kNormal = 1u << 0u,
            kHovered = 1u << 1u,
            kPressed = 1u << 2u,
            kFocused = 1u << 3u,
            kDisabled = 1u << 4u,
            kUnchecked = 1u << 5u,
            kUncheckedHovered = 1u << 6u,
            kUncheckedPressed = 1u << 7u,
            kChecked = 1u << 8u,
            kCheckedHovered = 1u << 9u,
            kCheckedPressed = 1u << 10u,
            kIndeterminate = 1u << 11u,
            kDisabledMark = 1u << 12u,
            kMarkColor = 1u << 13u,
            kBoxSize = 1u << 14u,
            kLabelSpacing = 1u << 15u,
            kPadding = 1u << 16u,
            kFontSize = 1u << 17u,
        };

        ASTRUCT()
        struct AILU_API UICheckBoxStyleOverride
        {
            GENERATED_BODY()
        public:
            void SetNormal(const UIControlVisual &v);
            void SetHovered(const UIControlVisual &v);
            void SetPressed(const UIControlVisual &v);
            void SetFocused(const UIControlVisual &v);
            void SetDisabled(const UIControlVisual &v);
            void SetUnchecked(const UIBrush &b);
            void SetUncheckedHovered(const UIBrush &b);
            void SetUncheckedPressed(const UIBrush &b);
            void SetChecked(const UIBrush &b);
            void SetCheckedHovered(const UIBrush &b);
            void SetCheckedPressed(const UIBrush &b);
            void SetIndeterminate(const UIBrush &b);
            void SetDisabledMark(const UIBrush &b);
            void SetMarkColor(const Color &c);
            void SetBoxSize(const Vector2f &s);
            void SetLabelSpacing(f32 s);
            void SetPadding(const Padding &p);
            void SetFontSize(f32 s);

            void ClearOverride(EUICheckBoxStyleOverride flag);
            void ClearAllOverrides();
            bool HasOverride(EUICheckBoxStyleOverride flag) const;
            u32 GetOverrideMask() const { return _override_mask; }

            void ApplyTo(UICheckBoxStyle &style) const;

        public:
            APROPERTY()
            u32 _override_mask = 0u;

            APROPERTY()
            UIControlVisual _normal;
            APROPERTY()
            UIControlVisual _hovered;
            APROPERTY()
            UIControlVisual _pressed;
            APROPERTY()
            UIControlVisual _focused;
            APROPERTY()
            UIControlVisual _disabled;

            APROPERTY()
            UIBrush _unchecked;
            APROPERTY()
            UIBrush _unchecked_hovered;
            APROPERTY()
            UIBrush _unchecked_pressed;
            APROPERTY()
            UIBrush _checked;
            APROPERTY()
            UIBrush _checked_hovered;
            APROPERTY()
            UIBrush _checked_pressed;
            APROPERTY()
            UIBrush _indeterminate;
            APROPERTY()
            UIBrush _disabled_mark;

            APROPERTY()
            Color _mark_color = Colors::kWhite;
            APROPERTY()
            Vector2f _box_size = Vector2f(16.0f, 16.0f);
            APROPERTY()
            f32 _label_spacing = 6.0f;
            APROPERTY()
            Padding _padding = Padding(2.0f);
            APROPERTY()
            f32 _font_size = 14.0f;
        };

        // ================================================================
        // UIInputStyle - input style
        // ================================================================
        ASTRUCT()
        struct AILU_API UIInputStyle
        {
            GENERATED_BODY()

            APROPERTY()
            UIControlVisual _normal;

            APROPERTY()
            UIControlVisual _hovered;

            APROPERTY()
            UIControlVisual _focused;

            APROPERTY()
            UIControlVisual _read_only;

            APROPERTY()
            UIControlVisual _disabled;

            APROPERTY()
            UIControlVisual _invalid;

            APROPERTY()
            Color _placeholder_color = Colors::kGray;

            APROPERTY()
            Color _selection_color = Color(0.15f, 0.4f, 0.8f, 0.65f);

            APROPERTY()
            Color _caret_color = Colors::kWhite;

            APROPERTY()
            Padding _padding = Padding(6.0f, 3.0f, 6.0f, 3.0f);

            APROPERTY()
            Vector2f _min_size = Vector2f(120.0f, 24.0f);

            APROPERTY()
            f32 _font_size = 14.0f;

            APROPERTY()
            f32 _caret_width = 1.0f;

            APROPERTY()
            f32 _selection_corner_radius = 0.0f;
        };

        // UIInputStyle override
        AENUM()
        enum class EUIInputStyleOverride : u32
        {
            kNone = 0u,
            kNormal = 1u << 0u,
            kHovered = 1u << 1u,
            kFocused = 1u << 2u,
            kReadOnly = 1u << 3u,
            kDisabled = 1u << 4u,
            kInvalid = 1u << 5u,
            kPlaceholderColor = 1u << 6u,
            kSelectionColor = 1u << 7u,
            kCaretColor = 1u << 8u,
            kPadding = 1u << 9u,
            kMinSize = 1u << 10u,
            kFontSize = 1u << 11u,
            kCaretWidth = 1u << 12u,
            kSelectionCornerRadius = 1u << 13u,
        };

        ASTRUCT()
        struct AILU_API UIInputStyleOverride
        {
            GENERATED_BODY()
        public:
            void SetNormal(const UIControlVisual &v);
            void SetHovered(const UIControlVisual &v);
            void SetFocused(const UIControlVisual &v);
            void SetReadOnly(const UIControlVisual &v);
            void SetDisabled(const UIControlVisual &v);
            void SetInvalid(const UIControlVisual &v);
            void SetPlaceholderColor(const Color &c);
            void SetSelectionColor(const Color &c);
            void SetCaretColor(const Color &c);
            void SetPadding(const Padding &p);
            void SetMinSize(const Vector2f &s);
            void SetFontSize(f32 s);
            void SetCaretWidth(f32 w);
            void SetSelectionCornerRadius(f32 r);

            void ClearOverride(EUIInputStyleOverride flag);
            void ClearAllOverrides();
            bool HasOverride(EUIInputStyleOverride flag) const;
            u32 GetOverrideMask() const { return _override_mask; }

            void ApplyTo(UIInputStyle &style) const;

        public:
            APROPERTY()
            u32 _override_mask = 0u;

            APROPERTY()
            UIControlVisual _normal;
            APROPERTY()
            UIControlVisual _hovered;
            APROPERTY()
            UIControlVisual _focused;
            APROPERTY()
            UIControlVisual _read_only;
            APROPERTY()
            UIControlVisual _disabled;
            APROPERTY()
            UIControlVisual _invalid;

            APROPERTY()
            Color _placeholder_color = Colors::kGray;
            APROPERTY()
            Color _selection_color = Color(0.15f, 0.4f, 0.8f, 0.65f);
            APROPERTY()
            Color _caret_color = Colors::kWhite;

            APROPERTY()
            Padding _padding = Padding(6.0f, 3.0f, 6.0f, 3.0f);
            APROPERTY()
            Vector2f _min_size = Vector2f(120.0f, 24.0f);
            APROPERTY()
            f32 _font_size = 14.0f;
            APROPERTY()
            f32 _caret_width = 1.0f;
            APROPERTY()
            f32 _selection_corner_radius = 0.0f;
        };

        // ================================================================
        // UIScrollBarStyle - scrollbar style
        // ================================================================
        ASTRUCT()
        struct AILU_API UIScrollBarStyle
        {
            GENERATED_BODY()

            APROPERTY()
            UIControlVisual _normal;

            APROPERTY()
            UIControlVisual _hovered;

            APROPERTY()
            UIControlVisual _focused;

            APROPERTY()
            UIControlVisual _disabled;

            APROPERTY()
            UIBrush _track;

            APROPERTY()
            UIBrush _track_hovered;

            APROPERTY()
            UIBrush _track_disabled;

            APROPERTY()
            UIBrush _thumb;

            APROPERTY()
            UIBrush _thumb_hovered;

            APROPERTY()
            UIBrush _thumb_pressed;

            APROPERTY()
            UIBrush _thumb_disabled;

            APROPERTY()
            UIBrush _decrease_button;

            APROPERTY()
            UIBrush _decrease_button_hovered;

            APROPERTY()
            UIBrush _decrease_button_pressed;

            APROPERTY()
            UIBrush _decrease_button_disabled;

            APROPERTY()
            UIBrush _increase_button;

            APROPERTY()
            UIBrush _increase_button_hovered;

            APROPERTY()
            UIBrush _increase_button_pressed;

            APROPERTY()
            UIBrush _increase_button_disabled;

            APROPERTY()
            f32 _thickness = 10.0f;

            APROPERTY()
            f32 _min_thumb_length = 20.0f;

            APROPERTY()
            f32 _thumb_corner_radius = 5.0f;

            APROPERTY()
            f32 _track_corner_radius = 5.0f;

            APROPERTY()
            f32 _button_length = 0.0f;

            APROPERTY()
            Padding _padding = Padding(0.0f);
        };

        // UIScrollBarStyle override
        AENUM()
        enum class EUIScrollBarStyleOverride : u32
        {
            kNone = 0u,
            kNormal = 1u << 0u,
            kHovered = 1u << 1u,
            kFocused = 1u << 2u,
            kDisabled = 1u << 3u,
            kTrack = 1u << 4u,
            kTrackHovered = 1u << 5u,
            kTrackDisabled = 1u << 6u,
            kThumb = 1u << 7u,
            kThumbHovered = 1u << 8u,
            kThumbPressed = 1u << 9u,
            kThumbDisabled = 1u << 10u,
            kDecreaseButton = 1u << 11u,
            kDecreaseButtonHovered = 1u << 12u,
            kDecreaseButtonPressed = 1u << 13u,
            kDecreaseButtonDisabled = 1u << 14u,
            kIncreaseButton = 1u << 15u,
            kIncreaseButtonHovered = 1u << 16u,
            kIncreaseButtonPressed = 1u << 17u,
            kIncreaseButtonDisabled = 1u << 18u,
            kThickness = 1u << 19u,
            kMinThumbLength = 1u << 20u,
            kThumbCornerRadius = 1u << 21u,
            kTrackCornerRadius = 1u << 22u,
            kButtonLength = 1u << 23u,
            kPadding = 1u << 24u,
        };

        ASTRUCT()
        struct AILU_API UIScrollBarStyleOverride
        {
            GENERATED_BODY()
        public:
            void SetNormal(const UIControlVisual &v);
            void SetHovered(const UIControlVisual &v);
            void SetFocused(const UIControlVisual &v);
            void SetDisabled(const UIControlVisual &v);
            void SetTrack(const UIBrush &b);
            void SetTrackHovered(const UIBrush &b);
            void SetTrackDisabled(const UIBrush &b);
            void SetThumb(const UIBrush &b);
            void SetThumbHovered(const UIBrush &b);
            void SetThumbPressed(const UIBrush &b);
            void SetThumbDisabled(const UIBrush &b);
            void SetDecreaseButton(const UIBrush &b);
            void SetDecreaseButtonHovered(const UIBrush &b);
            void SetDecreaseButtonPressed(const UIBrush &b);
            void SetDecreaseButtonDisabled(const UIBrush &b);
            void SetIncreaseButton(const UIBrush &b);
            void SetIncreaseButtonHovered(const UIBrush &b);
            void SetIncreaseButtonPressed(const UIBrush &b);
            void SetIncreaseButtonDisabled(const UIBrush &b);
            void SetThickness(f32 t);
            void SetMinThumbLength(f32 l);
            void SetThumbCornerRadius(f32 r);
            void SetTrackCornerRadius(f32 r);
            void SetButtonLength(f32 l);
            void SetPadding(const Padding &p);

            void ClearOverride(EUIScrollBarStyleOverride flag);
            void ClearAllOverrides();
            bool HasOverride(EUIScrollBarStyleOverride flag) const;
            u32 GetOverrideMask() const { return _override_mask; }

            void ApplyTo(UIScrollBarStyle &style) const;

        public:
            APROPERTY()
            u32 _override_mask = 0u;

            APROPERTY()
            UIControlVisual _normal;
            APROPERTY()
            UIControlVisual _hovered;
            APROPERTY()
            UIControlVisual _focused;
            APROPERTY()
            UIControlVisual _disabled;

            APROPERTY()
            UIBrush _track;
            APROPERTY()
            UIBrush _track_hovered;
            APROPERTY()
            UIBrush _track_disabled;
            APROPERTY()
            UIBrush _thumb;
            APROPERTY()
            UIBrush _thumb_hovered;
            APROPERTY()
            UIBrush _thumb_pressed;
            APROPERTY()
            UIBrush _thumb_disabled;
            APROPERTY()
            UIBrush _decrease_button;
            APROPERTY()
            UIBrush _decrease_button_hovered;
            APROPERTY()
            UIBrush _decrease_button_pressed;
            APROPERTY()
            UIBrush _decrease_button_disabled;
            APROPERTY()
            UIBrush _increase_button;
            APROPERTY()
            UIBrush _increase_button_hovered;
            APROPERTY()
            UIBrush _increase_button_pressed;
            APROPERTY()
            UIBrush _increase_button_disabled;

            APROPERTY()
            f32 _thickness = 10.0f;
            APROPERTY()
            f32 _min_thumb_length = 20.0f;
            APROPERTY()
            f32 _thumb_corner_radius = 5.0f;
            APROPERTY()
            f32 _track_corner_radius = 5.0f;
            APROPERTY()
            f32 _button_length = 0.0f;
            APROPERTY()
            Padding _padding = Padding(0.0f);
        };

        // ================================================================
        // UIScrollViewStyle - scroll view style
        // ================================================================
        ASTRUCT()
        struct AILU_API UIScrollViewStyle
        {
            GENERATED_BODY()

            APROPERTY()
            UIControlVisual _normal;

            APROPERTY()
            UIControlVisual _hovered;

            APROPERTY()
            UIControlVisual _focused;

            APROPERTY()
            UIControlVisual _disabled;

            APROPERTY()
            UIScrollBarStyle _horizontal_scrollbar;

            APROPERTY()
            UIScrollBarStyle _vertical_scrollbar;

            APROPERTY()
            Padding _content_padding = Padding(0.0f);

            APROPERTY()
            f32 _scrollbar_spacing = 2.0f;

            APROPERTY()
            f32 _mouse_wheel_scroll_delta = 40.0f;

            APROPERTY()
            f32 _horizontal_mouse_wheel_scroll_delta = 40.0f;

            APROPERTY()
            bool _reserve_horizontal_scrollbar_space = false;

            APROPERTY()
            bool _reserve_vertical_scrollbar_space = false;
        };

        // UIScrollViewStyle override
        AENUM()
        enum class EUIScrollViewStyleOverride : u32
        {
            kNone = 0u,
            kNormal = 1u << 0u,
            kHovered = 1u << 1u,
            kFocused = 1u << 2u,
            kDisabled = 1u << 3u,
            kHorizontalScrollbar = 1u << 4u,
            kVerticalScrollbar = 1u << 5u,
            kContentPadding = 1u << 6u,
            kScrollbarSpacing = 1u << 7u,
            kMouseWheelScrollDelta = 1u << 8u,
            kHorizontalMouseWheelScrollDelta = 1u << 9u,
            kReserveHorizontalScrollbarSpace = 1u << 10u,
            kReserveVerticalScrollbarSpace = 1u << 11u,
        };

        ASTRUCT()
        struct AILU_API UIScrollViewStyleOverride
        {
            GENERATED_BODY()
        public:
            void SetNormal(const UIControlVisual &v);
            void SetHovered(const UIControlVisual &v);
            void SetFocused(const UIControlVisual &v);
            void SetDisabled(const UIControlVisual &v);
            void SetHorizontalScrollbar(const UIScrollBarStyle &s);
            void SetVerticalScrollbar(const UIScrollBarStyle &s);
            void SetContentPadding(const Padding &p);
            void SetScrollbarSpacing(f32 s);
            void SetMouseWheelScrollDelta(f32 d);
            void SetHorizontalMouseWheelScrollDelta(f32 d);
            void SetReserveHorizontalScrollbarSpace(bool b);
            void SetReserveVerticalScrollbarSpace(bool b);

            void ClearOverride(EUIScrollViewStyleOverride flag);
            void ClearAllOverrides();
            bool HasOverride(EUIScrollViewStyleOverride flag) const;
            u32 GetOverrideMask() const { return _override_mask; }

            void ApplyTo(UIScrollViewStyle &style) const;

        public:
            APROPERTY()
            u32 _override_mask = 0u;

            APROPERTY()
            UIControlVisual _normal;
            APROPERTY()
            UIControlVisual _hovered;
            APROPERTY()
            UIControlVisual _focused;
            APROPERTY()
            UIControlVisual _disabled;

            APROPERTY()
            UIScrollBarStyle _horizontal_scrollbar;
            APROPERTY()
            UIScrollBarStyle _vertical_scrollbar;

            APROPERTY()
            Padding _content_padding = Padding(0.0f);
            APROPERTY()
            f32 _scrollbar_spacing = 2.0f;
            APROPERTY()
            f32 _mouse_wheel_scroll_delta = 40.0f;
            APROPERTY()
            f32 _horizontal_mouse_wheel_scroll_delta = 40.0f;
            APROPERTY()
            bool _reserve_horizontal_scrollbar_space = false;
            APROPERTY()
            bool _reserve_vertical_scrollbar_space = false;
        };

    }// namespace UI
}// namespace Ailu
