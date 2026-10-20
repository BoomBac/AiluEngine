#include "UI/Element/UITableElement.h"
#include "UI/TextRenderer.h"
#include "UI/UIRenderer.h"
#include "UI/Style/UITheme.h"
#include "UI/Table/TableRenderer.h"
#include "Framework/Common/Application.h"
#include "Framework/Common/Input.h"

#include <algorithm>
#include <cmath>
#include <imgui.h>

namespace Ailu
{
    namespace UI
    {
        namespace
        {
            bool IsControlDown()
            {
                return Input::IsKeyDown(EKey::kCONTROL) || Input::IsKeyDown(EKey::kLCONTROL) ||
                       Input::IsKeyDown(EKey::kRCONTROL);
            }
        }

        UITableElement::UITableElement() : ScrollView()
        {
            _name = "UITableElement";
            SetWantsMouseEvents(true);

            OnMouseMove() += [this](UIEvent &e)
            {
                const Vector2f local_mouse = TransformCoord(_inv_matrix, Vector3f{e._mouse_position, 0.0f}).xy;
                if (_is_text_selecting)
                {
                    const TextPosition position = TextPositionAtLocalMouse(local_mouse);
                    if (position._row >= 0 && position._column >= 0)
                    {
                        if (position._row != _text_selection_caret._row || position._column != _text_selection_caret._column ||
                            position._offset != _text_selection_caret._offset)
                        {
                            _text_selection_caret = position;
                            _text_selection_dragged = true;
                            _suppress_row_click = true;
                            InvalidatePaint();
                        }
                        e._is_handled = true;
                    }
                    return;
                }
                if (_is_row_selecting)
                {
                    const i32 row = RowAtLocalY(local_mouse.y);
                    if (row >= 0)
                    {
                        SelectRowRange(_row_selection_anchor, row);
                        e._is_handled = true;
                    }
                    return;
                }
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
                const bool is_resize_handle = IsPointInside(local_mouse, header_rect) && ResizeColumnAtLocalX(local_mouse.x) != -1;
                if (is_resize_handle)
                    Application::Get().SetCursor(ECursorType::kSizeEW, ECursorPriority::kHigh);
                const i32 hovered_row = RowAtLocalY(local_mouse.y);
                const i32 hovered_column = hovered_row == -1 ? -1 : ColumnAtLocalX(local_mouse.x);
                if (hovered_row != _hovered_row || hovered_column != _hovered_column)
                {
                    _hovered_row = hovered_row;
                    _hovered_column = hovered_column;
                    InvalidatePaint();
                }
                if (is_resize_handle)
                    return;
                if (_data_source != nullptr && _data_source->IsCellLink(hovered_row, hovered_column) && IsControlDown())
                    Application::Get().SetCursor(ECursorType::kHand, ECursorPriority::kHigh);
                else if (!_is_resizing_column)
                    Application::Get().SetCursor(ECursorType::kArrow);
            };

            OnMouseExit() += [this](UIEvent &e)
            {
                if (!_is_resizing_column)
                    Application::Get().SetCursor(ECursorType::kArrow);
                if (_hovered_row != -1)
                {
                    _hovered_row = -1;
                    _hovered_column = -1;
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
                    const i32 column = row == -1 ? -1 : ColumnAtLocalX(local_mouse.x);
                    if (_data_source != nullptr && IsControlDown() && _data_source->IsCellLink(row, column) &&
                        _data_source->OnCellClicked(row, column))
                    {
                        e._is_handled = true;
                        return;
                    }
                    if (_suppress_row_click)
                    {
                        _suppress_row_click = false;
                        e._is_handled = true;
                        return;
                    }
                    if (row != -1)
                    {
                        if (_data_source != nullptr && _data_source->OnCellClicked(row, column))
                        {
                            e._is_handled = true;
                            return;
                        }
                        RequestFocus();
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
                {
                    const i32 row = RowAtLocalY(local_mouse.y);
                    if (row >= 0)
                    {
                        const i32 column = ColumnAtLocalX(local_mouse.x);
                        if (_data_source != nullptr && IsControlDown() && _data_source->IsCellLink(row, column))
                        {
                            e._is_handled = true;
                            return;
                        }
                        const TextPosition position = TextPositionAtLocalMouse(local_mouse);
                        if (position._row >= 0 && position._column >= 0)
                        {
                            RequestFocus();
                            _is_text_selecting = true;
                            _text_selection_dragged = false;
                            _text_selection_anchor = position;
                            _text_selection_caret = position;
                            _selection_anchor_row = row;
                            e._is_handled = true;
                            return;
                        }
                        RequestFocus();
                        _is_row_selecting = true;
                        _suppress_row_click = true;
                        _row_selection_anchor = row;
                        _selection_anchor_row = row;
                        SelectRowRange(row, row);
                        e._is_handled = true;
                    }
                    return;
                }
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
                if (e._key_code != EKey::kLBUTTON)
                    return;
                if (_is_text_selecting)
                {
                    _is_text_selecting = false;
                    if (_text_selection_dragged)
                        e._is_handled = true;
                }
                if (_is_row_selecting)
                {
                    _is_row_selecting = false;
                    e._is_handled = true;
                }
                if (!_is_resizing_column)
                    return;
                _is_resizing_column = false;
                _resize_column = -1;
                Application::Get().SetCursor(ECursorType::kArrow);
                e._is_handled = true;
            };

            OnKeyDown() += [this](UIEvent &e)
            {
                if (!IsFocused())
                    return;
                const bool control_down = Input::IsKeyDown(EKey::kCONTROL) || Input::IsKeyDown(EKey::kLCONTROL) ||
                                          Input::IsKeyDown(EKey::kRCONTROL);
                if (!control_down)
                    return;
                if (e._key_code == EKey::kA)
                {
                    SelectAllText();
                    e._is_handled = true;
                }
                else if (e._key_code == EKey::kC)
                {
                    if (HasTextSelection())
                        CopyTextSelectionToClipboard();
                    else
                        CopySelectedRowsToClipboard();
                    e._is_handled = true;
                }
            };
        }

        void UITableElement::Update(f32 dt)
        {
            UIElement::Update(dt);
            const bool control_down = IsControlDown();
            if (control_down != _control_down_last)
            {
                _control_down_last = control_down;
                if (_hovered_row != -1)
                    InvalidatePaint();
            }
            if (_hovered_row >= 0 && _hovered_column >= 0 && _data_source != nullptr &&
                _data_source->IsCellLink(_hovered_row, _hovered_column) && control_down)
                Application::Get().SetCursor(ECursorType::kHand, ECursorPriority::kHigh);
        }

        UITableElement::TextPosition UITableElement::TextPositionAtLocalMouse(Vector2f local_mouse) const
        {
            TextPosition position;
            if (_data_source == nullptr)
                return position;
            position._row = RowAtLocalY(local_mouse.y);
            position._column = position._row < 0 ? -1 : ColumnAtLocalX(local_mouse.x);
            if (position._row < 0 || position._column < 0)
                return {};

            Vector<f32> widths;
            TableRenderer::CalculateColumnWidths(_columns, _content_size.x, widths);
            f32 cell_x = _content_rect.x + _current_offset.x;
            for (i32 column = 0; column < position._column; ++column)
                cell_x += widths[column];
            const Vector4f cell_rect = {cell_x, _content_rect.y + _header_height + position._row * _row_height + _current_offset.y,
                                         widths[position._column], _row_height};
            const String text = _data_source->GetCellText(position._row, position._column);
            if (text.empty())
                return position;

            const f32 font_size = _resolved_table_style._font_size;
            const f32 text_width = TextRenderer::CalculateTextSize(text, font_size).x;
            f32 text_x = cell_rect.x + _resolved_table_style._cell_padding._l;
            switch (_columns[position._column]._alignment)
            {
                case ETableColumnAlignment::kCenter:
                    text_x = cell_rect.x + (cell_rect.z - text_width) * 0.5f;
                    break;
                case ETableColumnAlignment::kRight:
                    text_x = cell_rect.x + cell_rect.z - _resolved_table_style._cell_padding._r - text_width;
                    break;
                case ETableColumnAlignment::kLeft:
                default:
                    break;
            }
            const f32 local_x = local_mouse.x - text_x;
            if (local_x <= 0.0f)
                return position;
            if (local_x >= text_width)
            {
                position._offset = static_cast<u32>(text.size());
                return position;
            }
            u32 low = 0u;
            u32 high = static_cast<u32>(text.size());
            while (low < high)
            {
                const u32 middle = low + (high - low) / 2u;
                const f32 middle_width = TextRenderer::CalculateTextSize(text.substr(0u, middle), font_size).x;
                if (middle_width < local_x)
                    low = middle + 1u;
                else
                    high = middle;
            }
            position._offset = low;
            return position;
        }

        bool UITableElement::IsTextPositionBefore(const TextPosition &left, const TextPosition &right)
        {
            if (left._row != right._row)
                return left._row < right._row;
            if (left._column != right._column)
                return left._column < right._column;
            return left._offset < right._offset;
        }

        bool UITableElement::HasTextSelection() const
        {
            return _text_selection_anchor._row >= 0 && _text_selection_caret._row >= 0 &&
                   (_text_selection_anchor._row != _text_selection_caret._row ||
                    _text_selection_anchor._column != _text_selection_caret._column ||
                    _text_selection_anchor._offset != _text_selection_caret._offset);
        }

        void UITableElement::SelectAllText()
        {
            if (_data_source == nullptr || _data_source->GetRowCount() <= 0 || _columns.empty())
                return;
            const i32 last_row = _data_source->GetRowCount() - 1;
            const i32 last_column = static_cast<i32>(_columns.size()) - 1;
            _text_selection_anchor = {0, 0, 0u};
            _text_selection_caret = {last_row, last_column,
                                     static_cast<u32>(_data_source->GetCellText(last_row, last_column).size())};
            RequestFocus();
            InvalidatePaint();
        }

        void UITableElement::CopyTextSelectionToClipboard() const
        {
            if (!HasTextSelection() || _data_source == nullptr || ImGui::GetCurrentContext() == nullptr)
                return;
            TextPosition begin = _text_selection_anchor;
            TextPosition end = _text_selection_caret;
            if (IsTextPositionBefore(end, begin))
                std::swap(begin, end);

            String copied;
            const i32 last_column = static_cast<i32>(_columns.size()) - 1;
            for (i32 row = begin._row; row <= end._row; ++row)
            {
                bool has_cell = false;
                const i32 first_column = row == begin._row ? begin._column : 0;
                const i32 final_column = row == end._row ? end._column : last_column;
                for (i32 column = first_column; column <= final_column; ++column)
                {
                    const String cell_text = _data_source->GetCellText(row, column);
                    const u32 first_offset = row == begin._row && column == begin._column ? begin._offset : 0u;
                    const u32 final_offset = row == end._row && column == end._column
                                                 ? end._offset
                                                 : static_cast<u32>(cell_text.size());
                    if (final_offset < first_offset)
                        continue;
                    if (has_cell)
                        copied += '\t';
                    copied += cell_text.substr(std::min(first_offset, static_cast<u32>(cell_text.size())),
                                               std::min(final_offset, static_cast<u32>(cell_text.size())) -
                                                   std::min(first_offset, static_cast<u32>(cell_text.size())));
                    has_cell = true;
                }
                if (row != end._row && has_cell)
                    copied += '\n';
            }
            ImGui::SetClipboardText(copied.c_str());
        }

        void UITableElement::DrawTextSelection(UIRenderer &r, i32 row, i32 column, Vector4f cell_rect, const String &text)
        {
            if (!HasTextSelection() || text.empty())
                return;
            TextPosition begin = _text_selection_anchor;
            TextPosition end = _text_selection_caret;
            if (IsTextPositionBefore(end, begin))
                std::swap(begin, end);
            if (IsTextPositionBefore({row, column, static_cast<u32>(text.size())}, begin) ||
                IsTextPositionBefore(end, {row, column, 0u}))
                return;
            const u32 first_offset = row == begin._row && column == begin._column ? begin._offset : 0u;
            const u32 final_offset = row == end._row && column == end._column ? end._offset : static_cast<u32>(text.size());
            if (final_offset <= first_offset)
                return;
            const f32 font_size = _resolved_table_style._font_size;
            const f32 text_width = TextRenderer::CalculateTextSize(text, font_size).x;
            f32 text_x = cell_rect.x + _resolved_table_style._cell_padding._l;
            switch (_columns[column]._alignment)
            {
                case ETableColumnAlignment::kCenter:
                    text_x = cell_rect.x + (cell_rect.z - text_width) * 0.5f;
                    break;
                case ETableColumnAlignment::kRight:
                    text_x = cell_rect.x + cell_rect.z - _resolved_table_style._cell_padding._r - text_width;
                    break;
                case ETableColumnAlignment::kLeft:
                default:
                    break;
            }
            const f32 start_x = text_x + TextRenderer::CalculateTextSize(text.substr(0u, first_offset), font_size).x;
            const f32 end_x = text_x + TextRenderer::CalculateTextSize(text.substr(0u, final_offset), font_size).x;
            UIBrush selection;
            selection._tint = _resolved_table_style._row_selected._background._tint;
            selection._tint.a = std::max(selection._tint.a, 0.55f);
            r.DrawQuad({start_x, cell_rect.y + _resolved_table_style._cell_padding._t,
                        std::max(0.0f, end_x - start_x),
                        std::max(0.0f, cell_rect.w - _resolved_table_style._cell_padding._t -
                                                _resolved_table_style._cell_padding._b)},
                       _matrix, selection);
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

        void UITableElement::SelectRowRange(i32 begin_row, i32 end_row)
        {
            if (_data_source == nullptr)
                return;
            const i32 row_count = _data_source->GetRowCount();
            if (row_count <= 0)
                return;
            begin_row = std::clamp(begin_row, 0, row_count - 1);
            end_row = std::clamp(end_row, 0, row_count - 1);
            const i32 first = std::min(begin_row, end_row);
            const i32 last = std::max(begin_row, end_row);
            _selected_rows.clear();
            _selected_rows.reserve(static_cast<size_t>(last - first + 1));
            for (i32 row = first; row <= last; ++row)
                _selected_rows.push_back(row);
            InvalidatePaint();
            _on_selection_changed_delegate.Invoke(GetSelectedRow());
        }

        void UITableElement::CopySelectedRowsToClipboard() const
        {
            if (ImGui::GetCurrentContext() == nullptr || _data_source == nullptr || _selected_rows.empty())
                return;
            String text;
            for (i32 row : _selected_rows)
            {
                if (!text.empty())
                    text += '\n';
                for (i32 column = 0; column < static_cast<i32>(_columns.size()); ++column)
                {
                    if (column != 0)
                        text += '\t';
                    text += _data_source->GetCellText(row, column);
                }
            }
            ImGui::SetClipboardText(text.c_str());
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
            f32 fixed_width = 0.0f;
            for (const auto &column: _columns)
            {
                has_stretch_column |= column._stretch;
                if (!column._stretch)
                    fixed_width += column._width;
            }
            if (!has_stretch_column)
                fixed_width = 0.0f;
            if (!has_stretch_column)
                for (const auto &column: _columns)
                    fixed_width += column._width;
            f32 total_height = _header_height;
            if (_data_source != nullptr)
                total_height += static_cast<f32>(_data_source->GetRowCount()) * _row_height;

            _content_size = {std::max(fixed_width, _content_rect.z), total_height};
            UpdateScrollBarVisibility();
            if (!_allow_horizontal_scroll)
            {
                _has_horizontal_bar = false;
                _has_vertical_bar = _content_size.y > _content_rect.w;
            }
            if (has_stretch_column)
                _content_size.x = std::max(fixed_width, _content_rect.z - (_has_vertical_bar ? kScrollBarWidth : 0.0f));
            if (_allow_horizontal_scroll)
                UpdateScrollBarVisibility();
            else
            {
                _has_horizontal_bar = false;
                _has_vertical_bar = _content_size.y > _content_rect.w;
            }
            const Vector2f viewport_size = GetScrollViewportSize();
            _max_offset = Min(Vector2f::kZero, viewport_size - _content_size);
            _target_offset = Max(_max_offset, Min(Vector2f::kZero, _target_offset));
            _current_offset = Max(_max_offset, Min(Vector2f::kZero, _current_offset));
            if (_scroll_to_bottom_requested)
            {
                _target_offset.y = _max_offset.y;
                _scroll_to_bottom_requested = false;
            }
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
            TableRenderer::CalculateColumnWidths(_columns, _content_size.x, widths);
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
            TableRenderer::CalculateColumnWidths(_columns, _content_size.x, widths);
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

            const Vector2f viewport_size = GetScrollViewportSize();
            const Vector4f content_rect = GetContentRect();
            const Vector4f viewport_rect = {content_rect.x, content_rect.y, viewport_size.x, viewport_size.y};
            r.PushScissor(viewport_rect);
            DrawHeader(r);
            r.PopScissor();

            Vector4f body_rect = viewport_rect;
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
            TableRenderer::CalculateColumnWidths(_columns, _content_size.x, widths);
            const Vector4f header_rect = {_content_rect.x, _content_rect.y, GetScrollViewportSize().x, _header_height};
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
            const f32 view_height = GetScrollViewportSize().y - _header_height;
            const f32 scroll_y = _current_offset.y;
            const i32 first_row = std::max(static_cast<i32>(std::floor(-scroll_y / _row_height)), 0);
            const i32 last_row = std::min(static_cast<i32>(std::ceil((-scroll_y + view_height) / _row_height)), row_count);
            const i32 col_count = static_cast<i32>(_columns.size());
            Vector<f32> widths;
            TableRenderer::CalculateColumnWidths(_columns, _content_size.x, widths);

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
                    DrawTextSelection(r, row, col, cell_rect, _data_source->GetCellText(row, col));
                    const bool is_hovered = row == _hovered_row && col == _hovered_column;
                    if (!_data_source->DrawCell(r, row, col, cell_rect, _matrix, is_hovered))
                        TableRenderer::DrawCellText(r, _resolved_table_style, _data_source->GetCellText(row, col), cell_rect,
                                                    _matrix, _columns[col]._alignment);
                    x += widths[col];
                }
            }
        }

        void UITableElement::DrawScrollbars(UIRenderer &r)
        {
            if (HasVerticalBar())
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
            if (HasHorizontalBar())
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
