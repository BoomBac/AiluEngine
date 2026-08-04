#pragma once
#include "UI/Style/UIStyles.h"
#include "generated/UITableStyle.gen.h"

namespace Ailu
{
    namespace UI
    {
        // ================================================================
        // ETableRowState - 行视觉状态
        // ================================================================
        enum class ETableRowState : u8
        {
            kNormal,
            kHovered,
            kSelected,
            kSelectedHovered,
            kDisabled
        };

        // ================================================================
        // UITableStyle - 表格样式
        // ================================================================
        ASTRUCT()
        struct AILU_API UITableStyle
        {
            GENERATED_BODY()

            APROPERTY()
            UIControlVisual _background;

            APROPERTY()
            UIControlVisual _header;

            APROPERTY()
            UIControlVisual _row;

            APROPERTY()
            UIControlVisual _row_hovered;

            APROPERTY()
            UIControlVisual _row_selected;

            APROPERTY()
            UIControlVisual _cell;

            APROPERTY()
            f32 _row_height = 20.0f;

            APROPERTY()
            f32 _header_height = 22.0f;

            APROPERTY()
            f32 _border_width = 1.0f;

            APROPERTY()
            f32 _column_separator_width = 1.0f;

            APROPERTY()
            Color _column_separator_color = Colors::kTransparent;

            APROPERTY()
            f32 _font_size = 14.0f;

            APROPERTY()
            Padding _cell_padding = Padding(4.0f, 1.0f, 4.0f, 1.0f);
        };

        // UITableStyle override
        AENUM()
        enum class EUITableStyleOverride : u32
        {
            kNone = 0u,
            kBackground = 1u << 0u,
            kHeader = 1u << 1u,
            kRow = 1u << 2u,
            kRowHovered = 1u << 3u,
            kRowSelected = 1u << 4u,
            kCell = 1u << 5u,
            kRowHeight = 1u << 6u,
            kHeaderHeight = 1u << 7u,
            kBorderWidth = 1u << 8u,
            kColumnSeparatorWidth = 1u << 9u,
            kColumnSeparatorColor = 1u << 10u,
            kFontSize = 1u << 11u,
            kCellPadding = 1u << 12u,
        };

        ASTRUCT()
        struct AILU_API UITableStyleOverride
        {
            GENERATED_BODY()
        public:
            void SetBackground(const UIControlVisual &v);
            void SetHeader(const UIControlVisual &v);
            void SetRow(const UIControlVisual &v);
            void SetRowHovered(const UIControlVisual &v);
            void SetRowSelected(const UIControlVisual &v);
            void SetCell(const UIControlVisual &v);
            void SetRowHeight(f32 h);
            void SetHeaderHeight(f32 h);
            void SetBorderWidth(f32 w);
            void SetColumnSeparatorWidth(f32 w);
            void SetColumnSeparatorColor(Color c);
            void SetFontSize(f32 s);
            void SetCellPadding(const Padding &p);

            void ClearOverride(EUITableStyleOverride flag);
            void ClearAllOverrides();
            bool HasOverride(EUITableStyleOverride flag) const;
            u32 GetOverrideMask() const { return _override_mask; }

            void ApplyTo(UITableStyle &style) const;

        public:
            APROPERTY()
            u32 _override_mask = 0u;

            APROPERTY()
            UIControlVisual _background;
            APROPERTY()
            UIControlVisual _header;
            APROPERTY()
            UIControlVisual _row;
            APROPERTY()
            UIControlVisual _row_hovered;
            APROPERTY()
            UIControlVisual _row_selected;
            APROPERTY()
            UIControlVisual _cell;

            APROPERTY()
            f32 _row_height = 20.0f;
            APROPERTY()
            f32 _header_height = 22.0f;
            APROPERTY()
            f32 _border_width = 1.0f;
            APROPERTY()
            f32 _column_separator_width = 1.0f;
            APROPERTY()
            Color _column_separator_color = Colors::kTransparent;
            APROPERTY()
            f32 _font_size = 14.0f;
            APROPERTY()
            Padding _cell_padding = Padding(4.0f, 1.0f, 4.0f, 1.0f);
        };

    }// namespace UI
}// namespace Ailu
