#include "UI/Style/UITheme.h"

namespace Ailu
{
    namespace UI
    {
        namespace
        {
            UIBrush ColorBrush(const Color &color)
            {
                UIBrush brush;
                brush._type = EUIBrushType::kColor;
                brush._tint = color;
                return brush;
            }

            UIControlVisual Visual(const Color &background, const Color &content, const Color &border = Colors::kTransparent, f32 border_width = 0.0f)
            {
                UIControlVisual visual;
                visual._background = ColorBrush(background);
                visual._content_color = content;
                visual._border_color = border;
                visual._border_width = border_width;
                visual._corner_radius = Vector4f(3.0f);
                return visual;
            }

            void FillCommonControlStyles(UITheme &theme)
            {
                const auto &c = theme._colors;
                const auto &s = theme._spacing;
                const auto &t = theme._typography;

                theme._button_style._normal = Visual(c._surface, c._text_primary, c._border, 1.0f);
                theme._button_style._hovered = Visual(c._surface_hovered, c._text_primary, c._accent, 1.0f);
                theme._button_style._pressed = Visual(c._surface_pressed, c._text_primary, c._accent, 1.0f);
                theme._button_style._focused = Visual(c._surface_hovered, c._text_primary, c._accent, 1.0f);
                theme._button_style._disabled = Visual(c._surface, c._text_disabled, c._border, 1.0f);
                theme._button_style._padding = s._control_padding;
                theme._button_style._font_size = t._normal_font_size;

                theme._slider_style._normal = Visual(Colors::kTransparent, c._text_primary);
                theme._slider_style._hovered = theme._slider_style._normal;
                theme._slider_style._pressed = theme._slider_style._normal;
                theme._slider_style._focused = theme._slider_style._normal;
                theme._slider_style._disabled = Visual(Colors::kTransparent, c._text_disabled);
                theme._slider_style._track_background = ColorBrush(c._surface_pressed);
                theme._slider_style._track_fill = ColorBrush(c._accent);
                theme._slider_style._thumb = ColorBrush(c._text_primary);
                theme._slider_style._thumb_hovered = ColorBrush(c._accent);
                theme._slider_style._thumb_pressed = ColorBrush(c._accent);
                theme._slider_style._thumb_disabled = ColorBrush(c._text_disabled);
                theme._slider_style._track_corner_radius = 6.0f;
                theme._slider_style._thumb_corner_radius = 3.0f;
                theme._slider_style._padding = Padding(s._medium);

                theme._check_box_style._normal = Visual(Colors::kTransparent, c._text_primary);
                theme._check_box_style._hovered = theme._check_box_style._normal;
                theme._check_box_style._pressed = theme._check_box_style._normal;
                theme._check_box_style._focused = theme._check_box_style._normal;
                theme._check_box_style._disabled = Visual(Colors::kTransparent, c._text_disabled);
                theme._check_box_style._unchecked = ColorBrush(c._surface);
                theme._check_box_style._unchecked_hovered = ColorBrush(c._surface_hovered);
                theme._check_box_style._unchecked_pressed = ColorBrush(c._surface_pressed);
                theme._check_box_style._checked = ColorBrush(c._accent);
                theme._check_box_style._checked_hovered = ColorBrush(c._accent);
                theme._check_box_style._checked_pressed = ColorBrush(c._accent);
                theme._check_box_style._disabled_mark = ColorBrush(c._text_disabled);
                theme._check_box_style._mark_color = c._text_primary;
                theme._check_box_style._padding = Padding(s._small);
                theme._check_box_style._font_size = t._normal_font_size;

                theme._input_style._normal = Visual(c._surface, c._text_primary, c._border, 1.0f);
                theme._input_style._hovered = Visual(c._surface_hovered, c._text_primary, c._border, 1.0f);
                theme._input_style._focused = Visual(c._surface_hovered, c._text_primary, c._accent, 1.0f);
                theme._input_style._read_only = Visual(c._surface, c._text_disabled, c._border, 1.0f);
                theme._input_style._disabled = Visual(c._surface, c._text_disabled, c._border, 1.0f);
                theme._input_style._invalid = Visual(c._surface, c._text_primary, Colors::kRed, 1.0f);
                theme._input_style._placeholder_color = c._text_disabled;
                theme._input_style._selection_color = Color(c._accent.x, c._accent.y, c._accent.z, 0.35f);
                theme._input_style._caret_color = c._text_primary;
                theme._input_style._padding = s._control_padding;
                theme._input_style._font_size = t._normal_font_size;

                theme._scroll_view_style._normal = Visual(c._surface, c._text_primary);
                theme._scroll_view_style._hovered = theme._scroll_view_style._normal;
                theme._scroll_view_style._focused = theme._scroll_view_style._normal;
                theme._scroll_view_style._disabled = Visual(c._surface, c._text_disabled);
                theme._scroll_view_style._horizontal_scrollbar = UIScrollBarStyle{};
                theme._scroll_view_style._horizontal_scrollbar._track = ColorBrush(Colors::kTransparent);
                theme._scroll_view_style._horizontal_scrollbar._thumb = ColorBrush(c._border);
                theme._scroll_view_style._horizontal_scrollbar._thumb_hovered = ColorBrush(c._accent);
                theme._scroll_view_style._horizontal_scrollbar._thumb_pressed = ColorBrush(c._accent);
                theme._scroll_view_style._vertical_scrollbar = theme._scroll_view_style._horizontal_scrollbar;

                UIButtonStyle primary = theme._button_style;
                primary._normal._background = ColorBrush(c._accent);
                primary._hovered._background = ColorBrush(Color((std::min)(c._accent.x + 0.08f, 1.0f), (std::min)(c._accent.y + 0.08f, 1.0f), (std::min)(c._accent.z + 0.08f, 1.0f), c._accent.w));
                primary._pressed._background = ColorBrush(Color((std::max)(c._accent.x - 0.08f, 0.0f), (std::max)(c._accent.y - 0.08f, 0.0f), (std::max)(c._accent.z - 0.08f, 0.0f), c._accent.w));
                theme.SetButtonStyle("Primary", primary);
            }
        }

        UITheme::UITheme()
            : UITheme(true)
        {
        }

        UITheme::UITheme(bool init_default)
        {
            if (init_default)
                *this = DefaultDark();
        }

        UITheme UITheme::DefaultDark()
        {
            UITheme theme(false);
            theme._colors._text_primary = Color(0.92f, 0.94f, 0.96f, 1.0f);
            theme._colors._text_disabled = Color(0.50f, 0.54f, 0.58f, 1.0f);
            theme._colors._surface = Color(0.13f, 0.14f, 0.16f, 1.0f);
            theme._colors._surface_hovered = Color(0.18f, 0.20f, 0.23f, 1.0f);
            theme._colors._surface_pressed = Color(0.09f, 0.10f, 0.12f, 1.0f);
            theme._colors._border = Color(0.28f, 0.31f, 0.35f, 1.0f);
            theme._colors._accent = Color(0.18f, 0.52f, 0.86f, 1.0f);
            FillCommonControlStyles(theme);
            return theme;
        }

        UITheme UITheme::DefaultLight()
        {
            UITheme theme(false);
            theme._colors._text_primary = Color(0.12f, 0.14f, 0.16f, 1.0f);
            theme._colors._text_disabled = Color(0.52f, 0.56f, 0.60f, 1.0f);
            theme._colors._surface = Color(0.94f, 0.95f, 0.96f, 1.0f);
            theme._colors._surface_hovered = Color(0.88f, 0.91f, 0.94f, 1.0f);
            theme._colors._surface_pressed = Color(0.80f, 0.84f, 0.88f, 1.0f);
            theme._colors._border = Color(0.68f, 0.72f, 0.76f, 1.0f);
            theme._colors._accent = Color(0.10f, 0.42f, 0.76f, 1.0f);
            FillCommonControlStyles(theme);
            return theme;
        }

        const UIButtonStyle * UITheme::FindButtonStyle(const UIStyleId &style_id) const 
        {
            if (style_id.empty())
                return nullptr;
            auto it = _button_styles.find(style_id);
            return it == _button_styles.end() ? nullptr : &it->second;
        }

        void UITheme::SetButtonStyle(const UIStyleId &style_id, const UIButtonStyle &style)
        {
            if (style_id.empty())
                return;
            _button_styles[style_id] = style;
            BumpRevision();
        }

        const UISliderStyle *UITheme::FindSliderStyle(const UIStyleId &style_id) const
        {
            if (style_id.empty())
                return nullptr;
            auto it = _slider_styles.find(style_id);
            return it == _slider_styles.end() ? nullptr : &it->second;
        }

        void UITheme::SetSliderStyle(const UIStyleId &style_id, const UISliderStyle &style)
        {
            if (style_id.empty())
                return;
            _slider_styles[style_id] = style;
            BumpRevision();
        }

        const UICheckBoxStyle *UITheme::FindCheckBoxStyle(const UIStyleId &style_id) const
        {
            if (style_id.empty())
                return nullptr;
            auto it = _check_box_styles.find(style_id);
            return it == _check_box_styles.end() ? nullptr : &it->second;
        }

        void UITheme::SetCheckBoxStyle(const UIStyleId &style_id, const UICheckBoxStyle &style)
        {
            if (style_id.empty())
                return;
            _check_box_styles[style_id] = style;
            BumpRevision();
        }

        const UIInputStyle *UITheme::FindInputStyle(const UIStyleId &style_id) const
        {
            if (style_id.empty())
                return nullptr;
            auto it = _input_styles.find(style_id);
            return it == _input_styles.end() ? nullptr : &it->second;
        }

        void UITheme::SetInputStyle(const UIStyleId &style_id, const UIInputStyle &style)
        {
            if (style_id.empty())
                return;
            _input_styles[style_id] = style;
            BumpRevision();
        }

        void UITheme::PostDeserialize()
        {
            // Named style variants (like "Primary" button) are derived from
            // the accent color. Regenerate them so they stay in sync with
            // any color token changes, but don't overwrite explicitly-serialized
            // named styles that were loaded from file.
            UIButtonStyle primary = _button_style;
            primary._normal._background = ColorBrush(Color(
                (std::min)(_colors._accent.x + 0.08f, 1.0f),
                (std::min)(_colors._accent.y + 0.08f, 1.0f),
                (std::min)(_colors._accent.z + 0.08f, 1.0f),
                _colors._accent.w));
            primary._hovered._background = ColorBrush(Color(
                (std::min)(_colors._accent.x + 0.08f, 1.0f),
                (std::min)(_colors._accent.y + 0.08f, 1.0f),
                (std::min)(_colors._accent.z + 0.08f, 1.0f),
                _colors._accent.w));
            primary._pressed._background = ColorBrush(Color(
                (std::max)(_colors._accent.x - 0.08f, 0.0f),
                (std::max)(_colors._accent.y - 0.08f, 0.0f),
                (std::max)(_colors._accent.z - 0.08f, 0.0f),
                _colors._accent.w));
            // Only add Primary if it wasn't explicitly set from file
            if (_button_styles.find("Primary") == _button_styles.end())
                _button_styles["Primary"] = primary;

            BumpRevision();
        }
    } // namespace UI
} // namespace Ailu
