#pragma once
#include "UI/Table/TableColumn.h"
#include "UI/Table/UITableStyle.h"
#include "Framework/Math/ALMath.hpp"

namespace Ailu
{
    namespace UI
    {
        class UIRenderer;

        /// <summary>
        /// 表格绘制器，Cell 不创建独立 UIElement，直接提交绘制指令
        /// </summary>
        class TableRenderer
        {
        public:
            /// <summary>
            /// 计算各列实际宽度：固定列取 _width，伸缩列按 _width 权重分配剩余宽度，最后一个伸缩列吸收余量
            /// </summary>
            static void CalculateColumnWidths(const Vector<TableColumn> &columns, f32 content_width, Vector<f32> &out_widths);
            static void DrawBackground(UIRenderer &r, const UITableStyle &style, Vector4f rect, Matrix4x4f matrix);
            static void DrawHeader(UIRenderer &r, const UITableStyle &style, const Vector<TableColumn> &columns, const Vector<f32> &widths, Vector4f header_rect, Matrix4x4f matrix, i32 sort_column = -1, bool sort_ascending = true, f32 content_offset_x = 0.0f);
            static void DrawRow(UIRenderer &r, const UITableStyle &style, ETableRowState state, Vector4f row_rect, Matrix4x4f matrix);
            static void DrawColumnSeparators(UIRenderer &r, const UITableStyle &style, const Vector<f32> &widths, Vector4f rect,
                                             Matrix4x4f matrix, f32 content_offset_x = 0.0f);
            static void DrawCellText(UIRenderer &r, const UITableStyle &style, const String &text, Vector4f cell_rect, Matrix4x4f matrix, ETableColumnAlignment alignment = ETableColumnAlignment::kLeft);
            static void DrawCheckbox(UIRenderer &r, Vector2f pos, f32 size, bool checked, Color color, f32 border_width = 1.0f);
            static void DrawProgressBar(UIRenderer &r, Vector4f rect, Matrix4x4f matrix, f32 progress, const UIBrush &background, const UIBrush &fill);

            static const UIControlVisual &ResolveRowVisual(const UITableStyle &style, ETableRowState state);
        };
    }// namespace UI
}// namespace Ailu
