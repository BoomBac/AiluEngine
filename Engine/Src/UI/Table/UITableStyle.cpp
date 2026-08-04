#include "UI/Table/UITableStyle.h"

namespace Ailu
{
    namespace UI
    {
        void UITableStyleOverride::SetBackground(const UIControlVisual &v) { _background = v; _override_mask |= (u32)EUITableStyleOverride::kBackground; }
        void UITableStyleOverride::SetHeader(const UIControlVisual &v) { _header = v; _override_mask |= (u32)EUITableStyleOverride::kHeader; }
        void UITableStyleOverride::SetRow(const UIControlVisual &v) { _row = v; _override_mask |= (u32)EUITableStyleOverride::kRow; }
        void UITableStyleOverride::SetRowHovered(const UIControlVisual &v) { _row_hovered = v; _override_mask |= (u32)EUITableStyleOverride::kRowHovered; }
        void UITableStyleOverride::SetRowSelected(const UIControlVisual &v) { _row_selected = v; _override_mask |= (u32)EUITableStyleOverride::kRowSelected; }
        void UITableStyleOverride::SetCell(const UIControlVisual &v) { _cell = v; _override_mask |= (u32)EUITableStyleOverride::kCell; }
        void UITableStyleOverride::SetRowHeight(f32 h) { _row_height = h; _override_mask |= (u32)EUITableStyleOverride::kRowHeight; }
        void UITableStyleOverride::SetHeaderHeight(f32 h) { _header_height = h; _override_mask |= (u32)EUITableStyleOverride::kHeaderHeight; }
        void UITableStyleOverride::SetBorderWidth(f32 w) { _border_width = w; _override_mask |= (u32)EUITableStyleOverride::kBorderWidth; }
        void UITableStyleOverride::SetColumnSeparatorWidth(f32 w)
        {
            _column_separator_width = w;
            _override_mask |= (u32)EUITableStyleOverride::kColumnSeparatorWidth;
        }

        void UITableStyleOverride::SetColumnSeparatorColor(Color c)
        {
            _column_separator_color = c;
            _override_mask |= (u32)EUITableStyleOverride::kColumnSeparatorColor;
        }
        void UITableStyleOverride::SetFontSize(f32 s) { _font_size = s; _override_mask |= (u32)EUITableStyleOverride::kFontSize; }
        void UITableStyleOverride::SetCellPadding(const Padding &p) { _cell_padding = p; _override_mask |= (u32)EUITableStyleOverride::kCellPadding; }

        void UITableStyleOverride::ClearOverride(EUITableStyleOverride flag) { _override_mask &= ~(u32)flag; }
        void UITableStyleOverride::ClearAllOverrides() { _override_mask = 0u; }
        bool UITableStyleOverride::HasOverride(EUITableStyleOverride flag) const { return (_override_mask & (u32)flag) != 0u; }

        void UITableStyleOverride::ApplyTo(UITableStyle &style) const
        {
            if (HasOverride(EUITableStyleOverride::kBackground)) style._background = _background;
            if (HasOverride(EUITableStyleOverride::kHeader)) style._header = _header;
            if (HasOverride(EUITableStyleOverride::kRow)) style._row = _row;
            if (HasOverride(EUITableStyleOverride::kRowHovered)) style._row_hovered = _row_hovered;
            if (HasOverride(EUITableStyleOverride::kRowSelected)) style._row_selected = _row_selected;
            if (HasOverride(EUITableStyleOverride::kCell)) style._cell = _cell;
            if (HasOverride(EUITableStyleOverride::kRowHeight)) style._row_height = _row_height;
            if (HasOverride(EUITableStyleOverride::kHeaderHeight)) style._header_height = _header_height;
            if (HasOverride(EUITableStyleOverride::kBorderWidth)) style._border_width = _border_width;
            if (HasOverride(EUITableStyleOverride::kColumnSeparatorWidth))
                style._column_separator_width = _column_separator_width;
            if (HasOverride(EUITableStyleOverride::kColumnSeparatorColor))
                style._column_separator_color = _column_separator_color;
            if (HasOverride(EUITableStyleOverride::kFontSize)) style._font_size = _font_size;
            if (HasOverride(EUITableStyleOverride::kCellPadding)) style._cell_padding = _cell_padding;
        }

    }// namespace UI
}// namespace Ailu
