#pragma once
#ifndef __TREEVIEW_H__
#define __TREEVIEW_H__
#include <unordered_set>
#include "UI/Basic.h"
#include "UI/Container.h"
#include "UI/DragDrop.h"
#include "generated/TreeView.gen.h"

namespace Ailu
{
    namespace UI
    {
        using TreeItemId = u64;
        inline constexpr TreeItemId kInvalidTreeItemId = 0u;

        struct AILU_API TreeItemPresentation
        {
            String _label;
            Render::Texture* _icon = nullptr;
            Color _text_color = Colors::kWhite;
            bool _selectable = true;
            bool _draggable = false;
            bool _drop_target = false;
        };

        class AILU_API ITreeViewDataSource
        {
        public:
            virtual ~ITreeViewDataSource() = default;

            virtual Vector<TreeItemId> GetRootItems() const = 0;
            virtual Vector<TreeItemId> GetChildren(TreeItemId parent) const = 0;
            virtual TreeItemId GetParent(TreeItemId item) const = 0;
            virtual TreeItemPresentation GetPresentation(TreeItemId item) const = 0;
            virtual bool IsValid(TreeItemId item) const = 0;
        };

        struct VisibleTreeItem
        {
            TreeItemId _id = kInvalidTreeItemId;
            u32 _depth = 0;
            bool _has_children = false;
        };

        struct TreeViewDragPayload
        {
            TreeView* _source_tree = nullptr;
            TreeItemId _item = kInvalidTreeItemId;
        };

        using TreeCanDragCallback = std::function<bool(TreeItemId)>;
        using TreeCanDropCallback = std::function<bool(TreeView*, TreeItemId, TreeItemId)>;
        using TreeDropCallback = std::function<void(TreeView*, TreeItemId, TreeItemId)>;

        ACLASS()
        class AILU_API TreeView : public ScrollView
        {
            GENERATED_BODY()
            DECLARE_DELEGATE(on_selection_changed, TreeItemId);
            DECLARE_DELEGATE(on_item_double_clicked, TreeItemId);
            DECLARE_DELEGATE(on_item_context_menu, TreeItemId, Vector2f);
            DECLARE_DELEGATE(on_expansion_changed, TreeItemId, bool);

        public:
            TreeView();
            ~TreeView() override = default;

            void SetDataSource(ITreeViewDataSource* data_source);
            ITreeViewDataSource* GetDataSource() const { return _data_source; }
            UIElement* GetRowForItem(TreeItemId item) const;

            void Refresh();

            void SetSelectedItem(TreeItemId item, bool notify = true);
            TreeItemId GetSelectedItem() const { return _selected_item; }
            void ClearSelection(bool notify = true);

            void SetExpanded(TreeItemId item, bool expanded);
            bool IsExpanded(TreeItemId item) const;
            void ToggleExpanded(TreeItemId item);
            void SetExpandOnRowClick(bool enabled) { _expand_on_row_click = enabled; }
            void ExpandAll();
            void CollapseAll();
            void ClearExpansionState();

            void ExpandParents(TreeItemId item);
            void ScrollItemIntoView(TreeItemId item);

            void SetCanDragCallback(TreeCanDragCallback callback) { _can_drag_callback = std::move(callback); }
            void SetCanDropCallback(TreeCanDropCallback callback) { _can_drop_callback = std::move(callback); }
            void SetDropCallback(TreeDropCallback callback) { _drop_callback = std::move(callback); }

            // ── Style ────────────────────────────────────────────
            void SetStyleId(const UIStyleId &id);
            const UIStyleId &GetStyleId() const { return _style_id; }
            UITreeViewStyleOverride &GetStyleOverride() { return _style_override; }
            const Padding &GetStylePadding() const { return _padding; }
            f32 GetStyleFontSize() const { return _font_size; }

            f32 _row_height = 22.0f;
            f32 _indent_width = 16.0f;
            f32 _expand_button_width = 16.0f;
            f32 _font_size = 14.0f;
            Color _normal_color = Color(0.15f, 0.15f, 0.2f, 1.0f);
            Color _hover_color = Color(0.2f, 0.2f, 0.4f, 1.0f);
            Color _selected_color = Color(0.3f, 0.3f, 0.6f, 1.0f);
            Color _selected_unfocused_color = Color(0.2f, 0.2f, 0.35f, 1.0f);

        protected:
            void RenderImpl(UIRenderer& r) override;

        private:
            void ResolveStyle(const UIStyleContext &context) override;
            void RebuildRows();
            void CollectVisibleItems(TreeItemId parent_id, u32 depth, u32& count,
                                     std::unordered_set<TreeItemId>& visited);
            UIElement* FindRowForItem(TreeItemId item) const;
            void OnRowClicked(TreeItemId item);
            void OnRowDoubleClicked(TreeItemId item);
            void OnRowContextMenu(TreeItemId item, Vector2f pos);
            void HandleDrop(const DragPayload& payload, f32 x, f32 y, TreeItemId target);

            ITreeViewDataSource* _data_source = nullptr;
            Vector<VisibleTreeItem> _visible_items;
            std::unordered_set<TreeItemId> _expanded_items;
            HashMap<TreeItemId, UIElement*> _item_rows;
            TreeItemId _selected_item = kInvalidTreeItemId;
            VerticalBox* _content_box = nullptr;
            UIElement* _hovered_row = nullptr;
            Border* _empty_drop_handler = nullptr;
            Ref<Border> _empty_drop_handler_ref;
            TreeViewDragPayload _drag_payload;

            TreeCanDragCallback _can_drag_callback;
            TreeCanDropCallback _can_drop_callback;
            TreeDropCallback _drop_callback;
            bool _expand_on_row_click = false;

            // Drag initiation tracking
            TreeItemId _drag_pending_item = kInvalidTreeItemId;
            Vector2f _drag_pending_mouse_pos = Vector2f::kZero;
            bool _is_drag_started = false;

            bool _is_suppress_selection_notify = false;

            UIStyleId _style_id;
            UITreeViewStyleOverride _style_override;
            UITreeViewStyle _resolved_style;
        };
    }// namespace UI
}// namespace Ailu
#endif// !__TREEVIEW_H__
