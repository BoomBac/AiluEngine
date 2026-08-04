#include "UI/Table/TableRenderer.h"
#include "UI/UIRenderer.h"

#include <algorithm>
#include <cmath>

namespace Ailu
{
    namespace UI
    {
        namespace
        {
            constexpr f32 kHeaderMaxLines = 2u;

            Vector<String> WrapText(UIRenderer &r, const String &text, f32 max_width, u16 font_size, u32 max_lines)
            {
                Vector<String> words;
                String::size_type start = 0u;
                while (start <= text.size())
                {
                    const String::size_type space = text.find(' ', start);
                    const String word = space == String::npos ? text.substr(start) : text.substr(start, space - start);
                    words.push_back(word);
                    if (space == String::npos)
                        break;
                    start = space + 1u;
                }
                Vector<String> lines;
                String current;
                f32 current_width = 0.0f;
                const f32 space_width = r.CalculateTextSize(" ", font_size).x;
                for (const String &word: words)
                {
                    const f32 word_width = r.CalculateTextSize(word, font_size).x;
                    const f32 sep_width = current.empty() ? 0.0f : space_width;
                    if (current_width + sep_width + word_width > max_width && !current.empty())
                    {
                        lines.push_back(current);
                        current = word;
                        current_width = word_width;
                    }
                    else
                    {
                        if (!current.empty())
                            current += " ";
                        current += word;
                        current_width += sep_width + word_width;
                    }
                }
                if (!current.empty())
                    lines.push_back(current);
                if (lines.size() > max_lines)
                {
                    lines.resize(max_lines);
                    lines.back() += "...";
                }
                return lines;
            }

            /// <summary>
            /// 在单元格范围内绘制文本：按单元格绝对区域裁剪，超宽自动截断，可多行
            /// </summary>
            void DrawClippedText(UIRenderer &r, const UITableStyle &style, const String &text, Vector4f cell_rect, Matrix4x4f matrix, Color color, ETableColumnAlignment alignment, u32 max_lines)
            {
                if (text.empty() || cell_rect.z <= 0.0f || cell_rect.w <= 0.0f)
                    return;
                const Vector3f tl = TransformCoord(matrix, {cell_rect.x, cell_rect.y, 1.0f});
                const Vector3f br = TransformCoord(matrix, {cell_rect.x + cell_rect.z, cell_rect.y + cell_rect.w, 1.0f});
                r.PushScissor({tl.x, tl.y, br.x - tl.x, br.y - tl.y});

                const f32 font_size = style._font_size;
                const u16 font_size_u16 = static_cast<u16>(font_size);
                const f32 line_height = std::max(r.CalculateTextSize("Ag", font_size_u16).y, font_size);
                const f32 text_max_width = std::max(cell_rect.z - style._cell_padding._l - style._cell_padding._r, 1.0f);
                const Vector<String> lines = WrapText(r, text, text_max_width, font_size_u16, max_lines);
                const f32 block_height = static_cast<f32>(lines.size()) * line_height;
                f32 ty = cell_rect.y + (cell_rect.w - block_height) * 0.5f;
                for (const String &line: lines)
                {
                    const Vector2f line_size = r.CalculateTextSize(line, font_size_u16);
                    f32 tx = cell_rect.x + style._cell_padding._l;
                    switch (alignment)
                    {
                        case ETableColumnAlignment::kCenter:
                            tx = cell_rect.x + (cell_rect.z - line_size.x) * 0.5f;
                            break;
                        case ETableColumnAlignment::kRight:
                            tx = cell_rect.x + cell_rect.z - style._cell_padding._r - line_size.x;
                            break;
                        case ETableColumnAlignment::kLeft:
                        default:
                            break;
                    }
                    r.DrawText(line, {tx, ty}, matrix, font_size, color);
                    ty += line_height;
                }
                r.PopScissor();
            }
        }// namespace

        void TableRenderer::CalculateColumnWidths(const Vector<TableColumn> &columns, f32 content_width, Vector<f32> &out_widths)
        {
            const i32 col_count = static_cast<i32>(columns.size());
            out_widths.resize(col_count);
            if (col_count == 0)
                return;
            f32 fixed_total = 0.0f;
            f32 stretch_weight_total = 0.0f;
            i32 last_stretch = -1;
            for (i32 c = 0; c < col_count; ++c)
            {
                if (columns[c]._stretch)
                {
                    stretch_weight_total += std::max(columns[c]._width, 1.0f);
                    last_stretch = c;
                }
                else
                {
                    fixed_total += columns[c]._width;
                }
            }
            const f32 remaining = std::max(content_width - fixed_total, 0.0f);
            if (last_stretch == -1)
            {
                for (i32 c = 0; c < col_count; ++c)
                    out_widths[c] = columns[c]._width;
                out_widths[col_count - 1] += remaining;
                return;
            }
            f32 assigned = 0.0f;
            for (i32 c = 0; c < col_count; ++c)
            {
                if (!columns[c]._stretch)
                {
                    out_widths[c] = columns[c]._width;
                }
                else if (c == last_stretch)
                {
                    out_widths[c] = remaining - assigned;
                }
                else
                {
                    out_widths[c] = remaining * (std::max(columns[c]._width, 1.0f) / stretch_weight_total);
                    assigned += out_widths[c];
                }
            }
        }

        void TableRenderer::DrawBackground(UIRenderer &r, const UITableStyle &style, Vector4f rect, Matrix4x4f matrix)
        {
            r.DrawVisual(rect, matrix, style._background);
        }

        void TableRenderer::DrawHeader(UIRenderer &r, const UITableStyle &style, const Vector<TableColumn> &columns, const Vector<f32> &widths, Vector4f header_rect, Matrix4x4f matrix, i32 sort_column, bool sort_ascending, f32 content_offset_x)
        {
            if (columns.empty())
                return;
            r.DrawQuad(header_rect, matrix, style._header._background);
            if (style._border_width > 0.0f)
            {
                r.DrawLine({header_rect.x, header_rect.y + header_rect.w},
                           {header_rect.x + header_rect.z, header_rect.y + header_rect.w},
                           matrix, style._border_width, style._header._border_color);
            }
            f32 x = header_rect.x + content_offset_x;
            const i32 col_count = static_cast<i32>(columns.size());
            for (i32 col = 0; col < col_count; ++col)
            {
                const f32 w = widths[col];
                String title = columns[col]._name;
                if (col == sort_column && columns[col]._sortable)
                    title += sort_ascending ? " ^" : " v";
                DrawClippedText(r, style, title, {x, header_rect.y, w, header_rect.w}, matrix, style._header._content_color, ETableColumnAlignment::kCenter, kHeaderMaxLines);
                x += w;
            }
            DrawColumnSeparators(r, style, widths, header_rect, matrix, content_offset_x);
        }

        void TableRenderer::DrawRow(UIRenderer &r, const UITableStyle &style, ETableRowState state, Vector4f row_rect, Matrix4x4f matrix)
        {
            const UIControlVisual &visual = ResolveRowVisual(style, state);
            r.DrawQuad(row_rect, matrix, visual._background);
            if (style._border_width > 0.0f)
            {
                Color separator_color = visual._border_color;
                separator_color.a *= 0.30f;
                r.DrawLine({row_rect.x, row_rect.y + row_rect.w},
                           {row_rect.x + row_rect.z, row_rect.y + row_rect.w},
                           matrix, style._border_width, separator_color);
            }
        }

        void TableRenderer::DrawColumnSeparators(UIRenderer &r, const UITableStyle &style, const Vector<f32> &widths, Vector4f rect,
                                                 Matrix4x4f matrix, f32 content_offset_x)
        {
            if (style._column_separator_width <= 0.0f || widths.size() < 2u)
                return;
            f32 x = rect.x + content_offset_x;
            for (size_t col = 0u; col + 1u < widths.size(); ++col)
            {
                x += widths[col];
                r.DrawLine({x, rect.y}, {x, rect.y + rect.w}, matrix, style._column_separator_width,
                           style._column_separator_color);
            }
        }

        void TableRenderer::DrawCellText(UIRenderer &r, const UITableStyle &style, const String &text, Vector4f cell_rect, Matrix4x4f matrix, ETableColumnAlignment alignment)
        {
            DrawClippedText(r, style, text, cell_rect, matrix, style._cell._content_color, alignment, 1u);
        }

        void TableRenderer::DrawCheckbox(UIRenderer &r, Vector2f pos, f32 size, bool checked, Color color, f32 border_width)
        {
            r.DrawBox(pos, {size, size}, border_width, color);
            if (checked)
            {
                const f32 inset = size * 0.25f;
                r.DrawLine({pos.x + inset, pos.y + size * 0.55f},
                           {pos.x + size * 0.42f, pos.y + size - inset}, border_width, color);
                r.DrawLine({pos.x + size * 0.42f, pos.y + size - inset},
                           {pos.x + size - inset, pos.y + inset}, border_width, color);
            }
        }

        void TableRenderer::DrawProgressBar(UIRenderer &r, Vector4f rect, Matrix4x4f matrix, f32 progress, const UIBrush &background, const UIBrush &fill)
        {
            progress = std::clamp(progress, 0.0f, 1.0f);
            r.DrawQuad(rect, matrix, background);
            const f32 fill_width = rect.z * progress;
            if (fill_width > 0.0f)
                r.DrawQuad({rect.x, rect.y, fill_width, rect.w}, matrix, fill);
        }

        const UIControlVisual &TableRenderer::ResolveRowVisual(const UITableStyle &style, ETableRowState state)
        {
            switch (state)
            {
                case ETableRowState::kHovered:
                    return style._row_hovered;
                case ETableRowState::kSelected:
                case ETableRowState::kSelectedHovered:
                    return style._row_selected;
                case ETableRowState::kDisabled:
                    return style._row;
                case ETableRowState::kNormal:
                default:
                    return style._row;
            }
        }

    }// namespace UI
}// namespace Ailu
