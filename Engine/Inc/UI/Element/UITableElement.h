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

            void Refresh();

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
            void ClearSelection(bool notify);
            void DrawHeader(UIRenderer &r);
            void DrawVisibleRows(UIRenderer &r);
            void DrawScrollbars(UIRenderer &r);

        private:
            TableDataSource *_data_source = nullptr;
            Vector<TableColumn> _columns;
            Vector<i32> _selected_rows;
            i32 _hovered_row = -1;
            i32 _selection_anchor_row = -1;
            i32 _sort_column = -1;
            i32 _resize_column = -1;
            bool _sort_ascending = true;
            bool _is_multi_select_enabled = false;
            bool _is_resizing_column = false;
            bool _suppress_header_click = false;
            f32 _resize_start_mouse_x = 0.0f;
            f32 _resize_start_width = 0.0f;
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
