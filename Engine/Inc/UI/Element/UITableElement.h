#pragma once
#include "UI/Container.h"
#include "UI/Table/TableColumn.h"
#include "UI/Table/TableDataSource.h"
#include "UI/Table/UITableStyle.h"
#include "generated/UITableElement.gen.h"

namespace Ailu
{
    namespace UI
    {
        /// <summary>
        /// 通用表格控件，数据与 UI 分离，Cell 不创建独立 UIElement，使用虚拟化渲染
        /// </summary>
        ACLASS()
        class AILU_API UITableElement : public ScrollView
        {
            GENERATED_BODY()
            DECLARE_DELEGATE(on_selection_changed, i32);
            DECLARE_DELEGATE(on_sort_changed, i32, bool);

        public:
            UITableElement();
            ~UITableElement() override = default;

            void SetDataSource(TableDataSource *source);
            TableDataSource *GetDataSource() const { return _data_source; }
            void Update(f32 dt) override;

            void AddColumn(const TableColumn &column);
            void ClearColumns();

            void SortByColumn(i32 column_index);
            i32 GetSortColumn() const { return _sort_column; }
            bool IsSortAscending() const { return _sort_ascending; }

            void SetSelectedRow(i32 row);
            i32 GetSelectedRow() const { return _selected_rows.empty() ? -1 : _selected_rows.front(); }
            const Vector<i32> &GetSelectedRows() const { return _selected_rows; }
            void SetMultiSelectEnabled(bool enabled);
            bool IsMultiSelectEnabled() const { return _is_multi_select_enabled; }
            void ClearSelection();
            void CopySelectedRowsToClipboard() const;
            void SelectAllText();
            bool HasTextSelection() const;
            void CopyTextSelectionToClipboard() const;

            void Refresh();
            void RequestScrollToBottom() { _scroll_to_bottom_requested = true; InvalidateLayout(); }
            bool IsAtBottom() const { return NearbyEqual(_target_offset.y, _max_offset.y); }
            void SetHorizontalScrollEnabled(bool enabled)
            {
                if (_allow_horizontal_scroll == enabled)
                    return;
                _allow_horizontal_scroll = enabled;
                InvalidateLayout();
            }
            bool IsHorizontalScrollEnabled() const { return _allow_horizontal_scroll; }

            // ── Style ────────────────────────────────────────────
            void SetStyleId(const UIStyleId &id);
            const UIStyleId &GetStyleId() const { return _style_id; }
            UITableStyleOverride &GetStyleOverride() { return _style_override; }
            void SetStyle(const UITableStyle &style);
            const UITableStyle &GetStyle() const { return _resolved_table_style; }

        protected:
            void ResolveStyle(const UIStyleContext &context) override;
            void RenderImpl(UIRenderer &r) override;
            void MeasureAndArrange(f32 dt) override;

        private:
            bool IsRowSelected(i32 row) const;
            i32 RowAtLocalY(f32 local_y) const;
            i32 ColumnAtLocalX(f32 local_x) const;
            i32 ResizeColumnAtLocalX(f32 local_x) const;
            void SelectRowFromMouse(i32 row);
            void SelectRowRange(i32 begin_row, i32 end_row);
            struct TextPosition
            {
                i32 _row = -1;
                i32 _column = -1;
                u32 _offset = 0u;
            };
            TextPosition TextPositionAtLocalMouse(Vector2f local_mouse) const;
            static bool IsTextPositionBefore(const TextPosition &left, const TextPosition &right);
            void DrawTextSelection(UIRenderer &r, i32 row, i32 column, Vector4f cell_rect, const String &text);
            void ClearSelection(bool notify);
            void DrawHeader(UIRenderer &r);
            void DrawVisibleRows(UIRenderer &r);
            void DrawScrollbars(UIRenderer &r);

        private:
            TableDataSource *_data_source = nullptr;
            Vector<TableColumn> _columns;
            Vector<i32> _selected_rows;
            i32 _hovered_row = -1;
            i32 _hovered_column = -1;
            i32 _selection_anchor_row = -1;
            i32 _sort_column = -1;
            i32 _resize_column = -1;
            bool _sort_ascending = true;
            bool _is_multi_select_enabled = false;
            bool _is_resizing_column = false;
            bool _is_row_selecting = false;
            bool _is_text_selecting = false;
            bool _text_selection_dragged = false;
            bool _suppress_row_click = false;
            bool _suppress_header_click = false;
            bool _scroll_to_bottom_requested = false;
            bool _allow_horizontal_scroll = true;
            bool _control_down_last = false;
            f32 _resize_start_mouse_x = 0.0f;
            f32 _resize_start_width = 0.0f;
            i32 _row_selection_anchor = -1;
            TextPosition _text_selection_anchor;
            TextPosition _text_selection_caret;
            f32 _row_height = 22.0f;
            f32 _header_height = 24.0f;

            UIStyleId _style_id;
            UITableStyleOverride _style_override;
            UITableStyle _instance_style;
            bool _has_instance_style = false;
            UITableStyle _resolved_table_style;
        };
    }// namespace UI
}// namespace Ailu
