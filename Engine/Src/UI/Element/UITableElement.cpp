#include "UI/Element/UITableElement.h"
#include "UI/UIRenderer.h"
#include "UI/Style/UITheme.h"
#include "UI/Table/TableRenderer.h"
#include "Framework/Common/Application.h"
#include "Framework/Common/Input.h"

#include <algorithm>
#include <cmath>

namespace Ailu
{
    namespace UI
    {
        UITableElement::UITableElement() : ScrollView()
        {
            _name = "UITableElement";
            SetWantsMouseEvents(true);

            OnMouseMove() += [this](UIEvent &e)
            {
                const Vector2f local_mouse = TransformCoord(_inv_matrix, Vector3f{e._mouse_position, 0.0f}).xy;
                if (_is_resizing_column)
                {
                    const f32 width = std::max(24.0f, _resize_start_width + e._mouse_position.x - _resize_start_mouse_x);
                    if (!NearbyEqual(_columns[_resize_column]._width, width))
                    {
                        _columns[_resize_column]._width = width;
                        _columns[_resize_column]._stretch = false;
                        InvalidateLayout();
                        InvalidatePaint();
                    }
                    Application::Get().SetCursor(ECursorType::kSizeEW, ECursorPriority::kHigh);
                    e._is_handled = true;
                    return;
                }
                const Vector4f header_rect = {_content_rect.x, _content_rect.y, _content_rect.z, _header_height};
                if (IsPointInside(local_mouse, header_rect) && ResizeColumnAtLocalX(local_mouse.x) != -1)
                    Application::Get().SetCursor(ECursorType::kSizeEW, ECursorPriority::kHigh);
                const i32 hovered_row = RowAtLocalY(local_mouse.y);
                if (hovered_row != _hovered_row)
                {
                    _hovered_row = hovered_row;
                    InvalidatePaint();
                }
            };

            OnMouseExit() += [this](UIEvent &e)
            {
                if (!_is_resizing_column)
                    Application::Get().SetCursor(ECursorType::kArrow);
                if (_hovered_row != -1)
                {
                    _hovered_row = -1;
                    InvalidatePaint();
                }
            };

            OnMouseClick() += [this](UIEvent &e)
            {
                const Vector2f local_mouse = TransformCoord(_inv_matrix, Vector3f{e._mouse_position, 0.0f}).xy;
                if ((HasVerticalBar() && IsPointInside(local_mouse, CalculateVerticalBarRect())) ||
                    (HasHorizontalBar() && IsPointInside(local_mouse, CalculateHorizontalBarRect())))
                    return;
                if (_suppress_header_click)
                {
                    _suppress_header_click = false;
                    e._is_handled = true;
                    return;
                }
                const Vector4f header_rect = {_content_rect.x, _content_rect.y, _content_rect.z, _header_height};
                if (IsPointInside(local_mouse, header_rect))
                {
                    const i32 column = ColumnAtLocalX(local_mouse.x);
                    if (column != -1)
                        SortByColumn(column);
                }
                else
                {
                    const i32 row = RowAtLocalY(local_mouse.y);
                    if (row != -1)
                    {
                        SelectRowFromMouse(row);
                        e._is_handled = true;
                    }
                }
            };

            OnMouseDown() += [this](UIEvent &e)
            {
                if (e._key_code != EKey::kLBUTTON)
                    return;
                const Vector2f local_mouse = TransformCoord(_inv_matrix, Vector3f{e._mouse_position, 0.0f}).xy;
                const Vector4f header_rect = {_content_rect.x, _content_rect.y, _content_rect.z, _header_height};
                if (!IsPointInside(local_mouse, header_rect))
                    return;
                const i32 column = ResizeColumnAtLocalX(local_mouse.x);
                if (column == -1)
                    return;
                Vector<f32> widths;
                TableRenderer::CalculateColumnWidths(_columns, _content_rect.z, widths);
                _resize_column = column;
                _resize_start_mouse_x = e._mouse_position.x;
                _resize_start_width = widths[column];
                _is_resizing_column = true;
                _suppress_header_click = true;
                Application::Get().SetCursor(ECursorType::kSizeEW, ECursorPriority::kHigh);
                e._is_handled = true;
            };

            OnMouseUp() += [this](UIEvent &e)
            {
                if (e._key_code != EKey::kLBUTTON || !_is_resizing_column)
                    return;
                _is_resizing_column = false;
                _resize_column = -1;
                Application::Get().SetCursor(ECursorType::kArrow);
                e._is_handled = true;
            };
        }

        // =========================================================================
        // Data
        // =========================================================================
        void UITableElement::SetDataSource(TableDataSource *source)
        {
            _data_source = source;
            ClearSelection(false);
            _selection_anchor_row = -1;
            if (_data_source == nullptr)
            {
                _sort_column = -1;
                _sort_ascending = true;
            }
            Refresh();
        }

        void UITableElement::AddColumn(const TableColumn &column)
        {
            _columns.push_back(column);
            InvalidateLayout();
        }

        void UITableElement::ClearColumns()
        {
            if (_columns.empty())
                return;
            _columns.clear();
            _sort_column = -1;
            _sort_ascending = true;
            InvalidateLayout();
        }

        void UITableElement::SortByColumn(i32 column_index)
        {
            if (_data_source == nullptr || column_index < 0 || column_index >= static_cast<i32>(_columns.size()))
                return;
            const TableColumn &column = _columns[column_index];
            if (!column._sortable)
                return;
            if (_sort_column == column_index)
            {
                _sort_ascending = !_sort_ascending;
            }
            else
            {
                _sort_column = column_index;
                _sort_ascending = true;
            }
            _data_source->Sort(column_index, _sort_ascending);
            InvalidatePaint();
            _on_sort_changed_delegate.Invoke(column_index, _sort_ascending);
        }

        void UITableElement::SetSelectedRow(i32 row)
        {
            if (_data_source != nullptr && row >= _data_source->GetRowCount())
                row = std::max(_data_source->GetRowCount() - 1, 0);
            else if (_data_source == nullptr)
                row = -1;
            const i32 old_row = GetSelectedRow();
            if (row == old_row)
                return;
            _selected_rows.clear();
            if (row >= 0)
                _selected_rows.push_back(row);
            _selection_anchor_row = row;
            InvalidatePaint();
            _on_selection_changed_delegate.Invoke(row);
        }

        void UITableElement::SetMultiSelectEnabled(bool enabled)
        {
            if (_is_multi_select_enabled == enabled)
                return;
            _is_multi_select_enabled = enabled;
            if (!enabled && _selected_rows.size() > 1u)
            {
                _selected_rows.resize(1u);
                _selection_anchor_row = _selected_rows.front();
                InvalidatePaint();
                _on_selection_changed_delegate.Invoke(_selected_rows.front());
            }
        }

        void UITableElement::ClearSelection()
        {
            ClearSelection(true);
        }

        void UITableElement::ClearSelection(bool notify)
        {
            if (_selected_rows.empty())
                return;
            _selected_rows.clear();
            _selection_anchor_row = -1;
            InvalidatePaint();
            if (notify)
                _on_selection_changed_delegate.Invoke(-1);
        }

        void UITableElement::Refresh()
        {
            if (_data_source == nullptr)
                return;
            const i32 row_count = _data_source->GetRowCount();
            const auto new_end = std::remove_if(_selected_rows.begin(), _selected_rows.end(), [row_count](i32 row)
            {
                return row < 0 || row >= row_count;
            });
            if (new_end != _selected_rows.end())
            {
                _selected_rows.erase(new_end, _selected_rows.end());
                _selection_anchor_row = _selected_rows.empty() ? -1 : _selected_rows.front();
                _on_selection_changed_delegate.Invoke(GetSelectedRow());
            }
            InvalidateLayout();
        }

        // =========================================================================
        // Style
        // =========================================================================
        void UITableElement::SetStyleId(const UIStyleId &id)
        {
            if (_style_id == id)
                return;
            _style_id = id;
            InvalidateStyle();
        }

        void UITableElement::SetStyle(const UITableStyle &style)
        {
            _instance_style = style;
            _has_instance_style = true;
            InvalidateStyle();
        }

        void UITableElement::ResolveStyle(const UIStyleContext &context)
        {
            ScrollView::ResolveStyle(context);
            UITableStyle resolved_style;
            if (context._theme)
            {
                if (const UITableStyle *named_style = context._theme->FindTableStyle(_style_id); named_style != nullptr)
                    resolved_style = *named_style;
                else
                    resolved_style = context._theme->_table_style;
            }
            else
            {
                static UITheme s_default_theme = UITheme::DefaultDark();
                resolved_style = s_default_theme._table_style;
            }
            if (_has_instance_style)
                resolved_style = _instance_style;
            _style_override.ApplyTo(resolved_style);

            const bool style_changed = !NearbyEqual(_row_height, resolved_style._row_height) ||
                                       !NearbyEqual(_header_height, resolved_style._header_height);
            _resolved_table_style = resolved_style;
            _row_height = resolved_style._row_height;
            _header_height = resolved_style._header_height;
            if (style_changed)
                InvalidateLayout();
        }

        // =========================================================================
        // Layout
        // =========================================================================
        void UITableElement::MeasureAndArrange(f32 dt)
        {
            EnsureStyleResolved();
            bool has_stretch_column = false;
            f32 total_width = 0.0f;
            for (const auto &column: _columns)
            {
                has_stretch_column |= column._stretch;
                if (!column._stretch)
                    total_width += column._width;
            }
            if (!has_stretch_column)
            {
                total_width = 0.0f;
                for (const auto &column: _columns)
                    total_width += column._width;
            }
            total_width = std::max(total_width, _content_rect.z);
            f32 total_height = _header_height;
            if (_data_source != nullptr)
                total_height += static_cast<f32>(_data_source->GetRowCount()) * _row_height;
            _content_size = {total_width, total_height};
            _max_offset = Min(Vector2f::kZero, _content_rect.zw - _content_size);
            _target_offset = Max(_max_offset, Min(Vector2f::kZero, _target_offset));
            _current_offset = Max(_max_offset, Min(Vector2f::kZero, _current_offset));
        }

        // =========================================================================
        // Hit / Query
        // =========================================================================
        bool UITableElement::IsRowSelected(i32 row) const
        {
            return std::find(_selected_rows.begin(), _selected_rows.end(), row) != _selected_rows.end();
        }

        i32 UITableElement::RowAtLocalY(f32 local_y) const
        {
            if (_data_source == nullptr)
                return -1;
            const f32 body_top = _content_rect.y + _header_height;
            if (local_y < body_top)
                return -1;
            const i32 row = static_cast<i32>(std::floor((local_y - body_top - _current_offset.y) / _row_height));
            if (row < 0 || row >= _data_source->GetRowCount())
                return -1;
            return row;
        }

        i32 UITableElement::ColumnAtLocalX(f32 local_x) const
        {
            if (_columns.empty())
                return -1;
            Vector<f32> widths;
            TableRenderer::CalculateColumnWidths(_columns, _content_rect.z, widths);
            f32 x = _content_rect.x + _current_offset.x;
            const i32 col_count = static_cast<i32>(_columns.size());
            for (i32 col = 0; col < col_count; ++col)
            {
                if (local_x >= x && local_x <= x + widths[col])
                    return col;
                x += widths[col];
            }
            return -1;
        }

        i32 UITableElement::ResizeColumnAtLocalX(f32 local_x) const
        {
            constexpr f32 kResizeHandleHalfWidth = 4.0f;
            if (_columns.size() < 2u)
                return -1;
            Vector<f32> widths;
            TableRenderer::CalculateColumnWidths(_columns, _content_rect.z, widths);
            f32 x = _content_rect.x + _current_offset.x;
            for (i32 col = 0; col + 1 < static_cast<i32>(_columns.size()); ++col)
            {
                x += widths[col];
                if (_columns[col]._resizable && std::abs(local_x - x) <= kResizeHandleHalfWidth)
                    return col;
            }
            return -1;
        }

        void UITableElement::SelectRowFromMouse(i32 row)
        {
            const bool is_ctrl_down = Input::IsKeyDown(EKey::kCONTROL) || Input::IsKeyDown(EKey::kLCONTROL) ||
                                      Input::IsKeyDown(EKey::kRCONTROL);
            const bool is_shift_down = Input::IsKeyDown(EKey::kSHIFT) || Input::IsKeyDown(EKey::kLSHIFT) ||
                                       Input::IsKeyDown(EKey::kRSHIFT);
            if (!_is_multi_select_enabled)
            {
                SetSelectedRow(row);
                return;
            }
            if (is_shift_down && _selection_anchor_row >= 0)
            {
                _selected_rows.clear();
                const i32 begin = std::min(_selection_anchor_row, row);
                const i32 end = std::max(_selection_anchor_row, row);
                for (i32 selected_row = begin; selected_row <= end; ++selected_row)
                    _selected_rows.push_back(selected_row);
            }
            else if (is_ctrl_down)
            {
                const auto it = std::find(_selected_rows.begin(), _selected_rows.end(), row);
                if (it == _selected_rows.end())
                    _selected_rows.push_back(row);
                else
                    _selected_rows.erase(it);
                _selection_anchor_row = row;
            }
            else
            {
                _selected_rows = {row};
                _selection_anchor_row = row;
            }
            InvalidatePaint();
            _on_selection_changed_delegate.Invoke(GetSelectedRow());
        }

        // =========================================================================
        // Render
        // =========================================================================
        void UITableElement::RenderImpl(UIRenderer &r)
        {
            TableRenderer::DrawBackground(r, _resolved_table_style, _content_rect, _matrix);

            r.PushScissor(GetContentRect());
            DrawHeader(r);
            r.PopScissor();

            Vector4f body_rect = GetContentRect();
            body_rect.y += _header_height;
            body_rect.w = std::max(0.0f, body_rect.w - _header_height);
            r.PushScissor(body_rect);
            DrawVisibleRows(r);
            r.PopScissor();

            DrawScrollbars(r);
        }

        void UITableElement::DrawHeader(UIRenderer &r)
        {
            if (_columns.empty())
                return;
            Vector<f32> widths;
            TableRenderer::CalculateColumnWidths(_columns, _content_rect.z, widths);
            const Vector4f header_rect = {_content_rect.x, _content_rect.y, _content_rect.z, _header_height};
            TableRenderer::DrawHeader(r, _resolved_table_style, _columns, widths, header_rect, _matrix, _sort_column, _sort_ascending, _current_offset.x);
        }

        void UITableElement::DrawVisibleRows(UIRenderer &r)
        {
            if (_data_source == nullptr || _columns.empty())
                return;
            const i32 row_count = _data_source->GetRowCount();
            if (row_count <= 0)
                return;
            const f32 body_top = _content_rect.y + _header_height;
            const f32 view_height = _content_rect.w - _header_height;
            const f32 scroll_y = _current_offset.y;
            const i32 first_row = std::max(static_cast<i32>(std::floor(-scroll_y / _row_height)), 0);
            const i32 last_row = std::min(static_cast<i32>(std::ceil((-scroll_y + view_height) / _row_height)), row_count);
            const i32 col_count = static_cast<i32>(_columns.size());
            Vector<f32> widths;
            TableRenderer::CalculateColumnWidths(_columns, _content_rect.z, widths);

            for (i32 row = first_row; row < last_row; ++row)
            {
                const f32 y = body_top + static_cast<f32>(row) * _row_height + scroll_y;
                const Vector4f row_rect = {_content_rect.x, y, _content_size.x, _row_height};

                ETableRowState state = ETableRowState::kNormal;
                if (IsRowSelected(row))
                    state = ETableRowState::kSelected;
                if (row == _hovered_row)
                    state = state == ETableRowState::kSelected ? ETableRowState::kSelectedHovered : ETableRowState::kHovered;
                if (!IsInteractiveEnabled())
                    state = ETableRowState::kDisabled;
                TableRenderer::DrawRow(r, _resolved_table_style, state, row_rect, _matrix);
                TableRenderer::DrawColumnSeparators(r, _resolved_table_style, widths, row_rect, _matrix, _current_offset.x);

                f32 x = _content_rect.x + _current_offset.x;
                for (i32 col = 0; col < col_count; ++col)
                {
                    const Vector4f cell_rect = {x, y, widths[col], _row_height};
                    if (!_data_source->DrawCell(r, row, col, cell_rect))
                        TableRenderer::DrawCellText(r, _resolved_table_style, _data_source->GetCellText(row, col), cell_rect,
                                                    _matrix, _columns[col]._alignment);
                    x += widths[col];
                }
            }
        }

        void UITableElement::DrawScrollbars(UIRenderer &r)
        {
            if (_content_size.y > _content_rect.w)
            {
                _vbar_rect = CalculateVerticalBarRect();
                const auto &sb_style = _resolved_style._vertical_scrollbar;
                const UIBrush *thumb = &sb_style._thumb;
                if (!IsInteractiveEnabled())
                    thumb = &sb_style._thumb_disabled;
                else if (_is_dragging_bar && _is_vertical)
                    thumb = &sb_style._thumb_pressed;
                else if (_is_hover_vbar)
                    thumb = &sb_style._thumb_hovered;
                r.DrawQuad(_vbar_rect, _matrix, *thumb);
            }
            if (_content_size.x > _content_rect.z)
            {
                _hbar_rect = CalculateHorizontalBarRect();
                const auto &sb_style = _resolved_style._horizontal_scrollbar;
                const UIBrush *thumb = &sb_style._thumb;
                if (!IsInteractiveEnabled())
                    thumb = &sb_style._thumb_disabled;
                else if (_is_dragging_bar && !_is_vertical)
                    thumb = &sb_style._thumb_pressed;
                else if (_is_hover_hbar)
                    thumb = &sb_style._thumb_hovered;
                r.DrawQuad(_hbar_rect, _matrix, *thumb);
            }
        }

    }// namespace UI
}// namespace Ailu
