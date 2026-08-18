#include "UI/TreeView.h"
#include "UI/Basic.h"
#include "UI/UIFramework.h"
#include "UI/UIRenderer.h"
#include "Framework/Common/Input.h"
#include "Framework/Common/Application.h"

namespace Ailu
{
    namespace UI
    {
        // =========================================================================
        // TreeViewRow - internal row widget
        // =========================================================================
        namespace
        {
            class TreeViewRow : public Border
            {
            public:
                TreeViewRow(TreeView* tree, TreeItemId id, const TreeItemPresentation& pres, u32 depth, bool has_children, bool expanded)
                    : _tree(tree), _item_id(id), _depth(depth), _has_children(has_children)
                {
                    _name = std::format("TreeRow_{}", id);
                    _bg_color = tree->_normal_color;
                    Thickness(0.0f);

                    auto* hb = AddChild<HorizontalBox>();
                    hb->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto);

                    // Indent via left margin on the horizontal box
                    const Padding &padding = tree->GetStylePadding();
                    f32 indent = depth * tree->_indent_width + padding._l;
                    hb->GetSlotAs<LinearSlot>().Margin(Padding(indent, padding._t, padding._r, padding._b));

                    // Expand button
                    if (_has_children)
                    {
                        _expand_btn = hb->AddChild<Text>(expanded ? "v" : ">");
                        _expand_btn->GetSlotAs<LinearSlot>().Size({tree->_expand_button_width, tree->_row_height}).SizePolicy(ESizePolicy::kFixed, ESizePolicy::kFixed);
                        _expand_btn->FontSize(tree->GetStyleFontSize(), false);
                        _expand_btn->GetSlotAs<LinearSlot>().Margin(Padding(0.0f, 0.0f, 2.0f, 0.0f));
                    }
                    else
                    {
                        // Spacer for alignment with items that have expand buttons
                        auto* spacer = hb->AddChild<Text>(" ");
                        spacer->FontSize(tree->GetStyleFontSize(), false);
                        spacer->GetSlotAs<LinearSlot>().Size({tree->_expand_button_width, tree->_row_height}).SizePolicy(ESizePolicy::kFixed, ESizePolicy::kFixed)
                                .Margin(Padding(0.0f, 0.0f, 2.0f, 0.0f));
                    }

                    // Icon
                    if (pres._icon)
                    {
                        auto* icon = hb->AddChild<Image>();
                        icon->SetTexture(pres._icon);
                        icon->GetSlotAs<LinearSlot>().Size({16.0f, 16.0f}).SizePolicy(ESizePolicy::kFixed, ESizePolicy::kFixed)
                                .Margin(Padding(0.0f, 0.0f, 4.0f, 0.0f));
                    }

                    // Label
                    _label = hb->AddChild<Text>(pres._label);
                    _label->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto);
                    _label->_color = pres._text_color;
                    _label->GetStyleOverride().SetContentColor(pres._text_color);
                    _label->FontSize(tree->GetStyleFontSize(), false);
                }

                TreeItemId GetItemId() const { return _item_id; }
                u32 GetDepth() const { return _depth; }
                bool HasChildren() const { return _has_children; }
                Text* GetExpandButton() const { return _expand_btn; }
                Text* GetLabel() const { return _label; }

            private:
                TreeView* _tree;
                TreeItemId _item_id;
                u32 _depth;
                bool _has_children;
                Text* _expand_btn = nullptr;
                Text* _label = nullptr;
            };

            TreeViewRow* FindTreeViewRow(UIElement* target, UIElement* content_box)
            {
                UIElement* node = target;
                while (node && node != content_box)
                {
                    if (auto* row = dynamic_cast<TreeViewRow*>(node))
                        return row;
                    node = node->GetParent();
                }
                return nullptr;
            }

            UIBrush MakeColorBrush(const Color& color)
            {
                UIBrush brush;
                brush._type = EUIBrushType::kColor;
                brush._tint = color;
                return brush;
            }

            void UpdateTreeViewRowBackground(TreeView* tree, TreeViewRow* row, bool is_hovered)
            {
                Color bg_color = tree->_normal_color;
                if (row->GetItemId() == tree->GetSelectedItem())
                    bg_color = tree->IsFocused() ? tree->_selected_color : tree->_selected_unfocused_color;
                else if (is_hovered)
                    bg_color = tree->_hover_color;

                if (NearbyEqual(row->_bg_color, bg_color))
                    return;

                row->_bg_color = bg_color;
                row->GetStyleOverride().SetBackground(MakeColorBrush(bg_color));
                row->InvalidateStyle(EStyleInvalidation::kPaintOnly);
            }
        } // anonymous namespace

        // =========================================================================
        // TreeView
        // =========================================================================
        TreeView::TreeView() : ScrollView()
        {
            _name = "TreeView";
            _content_box = AddChild<VerticalBox>();
            _content_box->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto);

            // Mouse tracking for hover
            OnMouseMove() += [this](UIEvent& e)
            {
                UIElement* hovered_row = FindTreeViewRow(e._target, _content_box);
                if (_hovered_row != hovered_row)
                {
                    UIElement* previous_hovered_row = _hovered_row;
                    _hovered_row = hovered_row;

                    if (auto* row = dynamic_cast<TreeViewRow*>(previous_hovered_row))
                        UpdateTreeViewRowBackground(this, row, false);
                    if (auto* row = dynamic_cast<TreeViewRow*>(_hovered_row))
                        UpdateTreeViewRowBackground(this, row, true);
                }
            };
            OnMouseExit() += [this](UIEvent& e)
            {
                if (_hovered_row == nullptr)
                    return;
                UIElement* previous_hovered_row = _hovered_row;
                _hovered_row = nullptr;
                if (auto* row = dynamic_cast<TreeViewRow*>(previous_hovered_row))
                    UpdateTreeViewRowBackground(this, row, false);
            };

            _on_focus_gained += [this]()
            {
                if (auto* row = dynamic_cast<TreeViewRow*>(FindRowForItem(_selected_item)))
                    UpdateTreeViewRowBackground(this, row, row == _hovered_row);
            };
            _on_focus_lost += [this]()
            {
                if (auto* row = dynamic_cast<TreeViewRow*>(FindRowForItem(_selected_item)))
                    UpdateTreeViewRowBackground(this, row, row == _hovered_row);
            };

            // Click: walk up from target to find the TreeViewRow
            OnMouseClick() += [this](UIEvent& e)
            {
                // If a drag was initiated, don't process click
                if (_is_drag_started)
                    return;

                UIElement* click_target = e._target;
                if (auto* row = FindTreeViewRow(click_target, _content_box))
                {
                    if (click_target == row->GetExpandButton())
                    {
                        ToggleExpanded(row->GetItemId());
                        e._is_handled = true;
                        return;
                    }
                    OnRowClicked(row->GetItemId());
                    if (_expand_on_row_click && row->HasChildren())
                        ToggleExpanded(row->GetItemId());
                    e._is_handled = true;
                }
            };

            // Double-click
            OnMouseDoubleClick() += [this](UIEvent& e)
            {
                if (_is_drag_started)
                    return;
                if (auto* row = FindTreeViewRow(e._target, _content_box))
                {
                    OnRowDoubleClicked(row->GetItemId());
                    e._is_handled = true;
                }
            };

            // MouseDown: initiate drag (left button) + right-click context menu
            OnMouseDown() += [this](UIEvent& e)
            {
                if (e._key_code == EKey::kLBUTTON)
                {
                    if (auto* row = FindTreeViewRow(e._target, _content_box))
                    {
                        TreeItemId id = row->GetItemId();
                        _drag_pending_item = id;
                        _drag_pending_mouse_pos = e._mouse_position;
                        _is_drag_started = false;

                        // Immediately begin drag (DragDropManager has its own 5px threshold)
                        if (_can_drag_callback && !_can_drag_callback(id))
                            return;
                        auto* data_source = _data_source;
                        if (data_source && data_source->IsValid(id))
                        {
                            auto pres = data_source->GetPresentation(id);
                            if (pres._draggable)
                            {
                                _drag_payload._source_tree = this;
                                _drag_payload._item = id;

                                DragPayload payload;
                                payload._type = EDragType::kTreeItem;
                                payload._data = &_drag_payload;

                                DragDropManager::Get().BeginDrag(payload, pres._label);
                                _is_drag_started = true;
                            }
                        }
                    }
                }
                else if (e._key_code == EKey::kRBUTTON)
                {
                    if (auto* row = FindTreeViewRow(e._target, _content_box))
                    {
                        TreeItemId id = row->GetItemId();
                        // Update selection first if needed
                        if (_selected_item != id)
                            SetSelectedItem(id, true);
                        OnRowContextMenu(id, e._mouse_position);
                        e._is_handled = true;
                    }
                }
            };

            // MouseUp: reset drag tracking
            OnMouseUp() += [this](UIEvent& e)
            {
                if (e._key_code == EKey::kLBUTTON)
                {
                    _drag_pending_item = kInvalidTreeItemId;
                    _is_drag_started = false;
                }
            };

            // Empty area drop handler (keep Ref for re-adding after RebuildRows)
            {
                auto border = MakeRef<Border>();
                _empty_drop_handler = border.get();
                _empty_drop_handler_ref = border;
                _content_box->AddChild(std::move(border));
                _empty_drop_handler->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFill);
            }
            _empty_drop_handler->Thickness(0.0f);
            {
                DropHandler dh;
                dh._can_drop = [this](const DragPayload& p) -> bool
                {
                    if (p._type != EDragType::kTreeItem) return false;
                    if (!_drop_callback) return false;
                    auto* tdp = static_cast<const TreeViewDragPayload*>(p._data);
                    if (!tdp) return false;
                    if (_can_drop_callback)
                        return _can_drop_callback(tdp->_source_tree, tdp->_item, kInvalidTreeItemId);
                    return true;
                };
                dh._on_drop = [this](const DragPayload& p, f32 x, f32 y)
                {
                    HandleDrop(p, x, y, kInvalidTreeItemId);
                };
                _empty_drop_handler->SetDropHandler(dh);
            }
        }

        void TreeView::SetStyleId(const UIStyleId &id)
        {
            if (_style_id == id)
                return;
            _style_id = id;
            InvalidateStyle();
        }

        void TreeView::ResolveStyle(const UIStyleContext &context)
        {
            ScrollView::ResolveStyle(context);
            UITreeViewStyle resolved_style;
            if (context._theme)
            {
                const UITreeViewStyle *theme_style = context._theme->FindTreeViewStyle(_style_id);
                resolved_style = theme_style != nullptr ? *theme_style : context._theme->_tree_view_style;
            }
            else
            {
                static UITheme s_default_theme = UITheme::DefaultDark();
                resolved_style = s_default_theme._tree_view_style;
            }
            _style_override.ApplyTo(resolved_style);

            const bool style_changed = _row_height != resolved_style._row_height ||
                                       _indent_width != resolved_style._indent_width ||
                                       _expand_button_width != resolved_style._expand_button_width ||
                                       _normal_color != resolved_style._normal_color ||
                                       _hover_color != resolved_style._hover_color ||
                                       _selected_color != resolved_style._selected_color ||
                                       _selected_unfocused_color != resolved_style._selected_unfocused_color ||
                                       _padding._l != resolved_style._padding._l ||
                                       _padding._t != resolved_style._padding._t ||
                                       _padding._r != resolved_style._padding._r ||
                                       _padding._b != resolved_style._padding._b ||
                                       _font_size != resolved_style._font_size;
            _resolved_style = resolved_style;
            _row_height = resolved_style._row_height;
            _indent_width = resolved_style._indent_width;
            _expand_button_width = resolved_style._expand_button_width;
            _normal_color = resolved_style._normal_color;
            _hover_color = resolved_style._hover_color;
            _selected_color = resolved_style._selected_color;
            _selected_unfocused_color = resolved_style._selected_unfocused_color;
            _padding = resolved_style._padding;
            _font_size = resolved_style._font_size;
            _content_box->GetSlotAs<LinearSlot>().Margin(_padding);

            if (style_changed && _data_source != nullptr)
                Refresh();
        }

        // =========================================================================
        // Data Source
        // =========================================================================
        void TreeView::SetDataSource(ITreeViewDataSource* data_source)
        {
            _data_source = data_source;
            ClearSelection(false);
            ClearExpansionState();
            _hovered_row = nullptr;
            _visible_items.clear();
            _item_rows.clear();
            _content_box->ClearChildren();
            if (_data_source)
                Refresh();
            else
                InvalidateHierarchy();
        }

        // =========================================================================
        // Refresh
        // =========================================================================
        void TreeView::Refresh()
        {
            if (!_data_source)
                return;

            // Preserve still-valid expanded items
            std::unordered_set<TreeItemId> valid_expanded;
            for (TreeItemId id : _expanded_items)
            {
                if (_data_source->IsValid(id))
                    valid_expanded.insert(id);
            }
            _expanded_items = std::move(valid_expanded);

            // Clear current state
            TreeItemId hovered_item = kInvalidTreeItemId;
            if (auto* row = dynamic_cast<TreeViewRow*>(_hovered_row))
                hovered_item = row->GetItemId();
            _hovered_row = nullptr;
            _visible_items.clear();
            _item_rows.clear();
            _content_box->ClearChildren();

            // Collect visible items
            u32 count = 0;
            std::unordered_set<TreeItemId> visited;
            auto roots = _data_source->GetRootItems();
            for (TreeItemId root : roots)
                CollectVisibleItems(root, 0, count, visited);

            // Create rows
            RebuildRows();

            if (hovered_item != kInvalidTreeItemId)
            {
                if (auto* row = dynamic_cast<TreeViewRow*>(FindRowForItem(hovered_item)))
                {
                    _hovered_row = row;
                    UpdateTreeViewRowBackground(this, row, true);
                }
            }

            // Validate selection
            if (_selected_item != kInvalidTreeItemId && !_data_source->IsValid(_selected_item))
            {
                _selected_item = kInvalidTreeItemId;
                if (!_is_suppress_selection_notify)
                    _on_selection_changed_delegate.Invoke(kInvalidTreeItemId);
            }
            InvalidateHierarchy();
        }

        void TreeView::CollectVisibleItems(TreeItemId item_id, u32 depth, u32& count,
                                           std::unordered_set<TreeItemId>& visited)
        {
            constexpr u32 kMaxDepth = 256;
            constexpr u32 kMaxNodes = 10000;

            if (depth > kMaxDepth || count >= kMaxNodes)
                return;
            if (!_data_source->IsValid(item_id))
                return;
            if (!visited.insert(item_id).second)
            {
                LOG_WARNING("TreeView: duplicate/cycle item id {}", item_id);
                return;
            }

            auto children = _data_source->GetChildren(item_id);
            bool has_children = !children.empty();

            VisibleTreeItem vi;
            vi._id = item_id;
            vi._depth = depth;
            vi._has_children = has_children;
            _visible_items.push_back(vi);
            ++count;

            if (_expanded_items.contains(item_id))
            {
                for (TreeItemId child : children)
                    CollectVisibleItems(child, depth + 1, count, visited);
            }
        }

        void TreeView::RebuildRows()
        {
            for (auto& vi : _visible_items)
            {
                if (!_data_source) break;
                auto pres = _data_source->GetPresentation(vi._id);
                bool expanded = _expanded_items.contains(vi._id);

                auto row = MakeRef<TreeViewRow>(this, vi._id, pres, vi._depth, vi._has_children, expanded);

                // Set up drop handler if this item accepts drops
                if (pres._drop_target)
                {
                     DropHandler dh;
                     dh._can_drop = [this, id = vi._id](const DragPayload& p) -> bool
                     {
                         if (p._type == EDragType::kTreeItem)
                         {
                             if (!_drop_callback) return false;
                             auto* tdp = static_cast<const TreeViewDragPayload*>(p._data);
                             if (!tdp) return false;
                             if (_can_drop_callback)
                                 return _can_drop_callback(tdp->_source_tree, tdp->_item, id);
                             return true;
                         }
                         return _external_can_drop_callback && _external_can_drop_callback(p, id);
                     };
                     dh._on_drop = [this, id = vi._id](const DragPayload& p, f32 x, f32 y)
                     {
                         if (p._type == EDragType::kTreeItem)
                             HandleDrop(p, x, y, id);
                         else if (_external_drop_callback)
                             _external_drop_callback(p, id, {x, y});
                     };
                    row->SetDropHandler(dh);
                }

                _content_box->AddChild(row);
                row->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFixed).Size({0.0f, _row_height});
                _item_rows[vi._id] = row.get();
                UpdateTreeViewRowBackground(this, row.get(), row.get() == _hovered_row);
            }

            // Re-add empty drop handler area at the bottom
            if (_empty_drop_handler)
            {
                _content_box->RemoveChild(_empty_drop_handler);
                // Clone the drop handler to a new Border (the existing _empty_drop_handler
                // was removed and its owning Ref was consumed by the previous AddChild)
                auto border = MakeRef<Border>();
                _empty_drop_handler = border.get();
                _empty_drop_handler_ref = std::move(border);
                _empty_drop_handler->Thickness(0.0f);
                {
                     DropHandler dh;
                     dh._can_drop = [this](const DragPayload& p) -> bool
                     {
                         if (p._type == EDragType::kTreeItem)
                         {
                             if (!_drop_callback) return false;
                             auto* tdp = static_cast<const TreeViewDragPayload*>(p._data);
                             if (!tdp) return false;
                             if (_can_drop_callback)
                                 return _can_drop_callback(tdp->_source_tree, tdp->_item, kInvalidTreeItemId);
                             return true;
                         }
                         return _external_can_drop_callback && _external_can_drop_callback(p, kInvalidTreeItemId);
                     };
                     dh._on_drop = [this](const DragPayload& p, f32 x, f32 y)
                     {
                         if (p._type == EDragType::kTreeItem)
                             HandleDrop(p, x, y, kInvalidTreeItemId);
                         else if (_external_drop_callback)
                             _external_drop_callback(p, kInvalidTreeItemId, {x, y});
                     };
                    _empty_drop_handler->SetDropHandler(dh);
                }
                _content_box->AddChild(_empty_drop_handler_ref);
                _empty_drop_handler->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFill);
            }
        }

        UIElement* TreeView::FindRowForItem(TreeItemId item) const
        {
            auto it = _item_rows.find(item);
            return it != _item_rows.end() ? it->second : nullptr;
        }

        UIElement* TreeView::GetRowForItem(TreeItemId item) const
        {
            return FindRowForItem(item);
        }

        // =========================================================================
        // Selection
        // =========================================================================
        void TreeView::SetSelectedItem(TreeItemId item, bool notify)
        {
            if (!_data_source) return;
            if (item != kInvalidTreeItemId)
            {
                if (!_data_source->IsValid(item)) return;
                auto pres = _data_source->GetPresentation(item);
                if (!pres._selectable) return;
            }
            if (item == _selected_item) return;

            TreeItemId previous_selected_item = _selected_item;
            _selected_item = item;

            if (auto* row = dynamic_cast<TreeViewRow*>(FindRowForItem(previous_selected_item)))
                UpdateTreeViewRowBackground(this, row, row == _hovered_row);
            if (auto* row = dynamic_cast<TreeViewRow*>(FindRowForItem(_selected_item)))
                UpdateTreeViewRowBackground(this, row, row == _hovered_row);

            if (notify && !_is_suppress_selection_notify)
                _on_selection_changed_delegate.Invoke(item);
        }

        void TreeView::ClearSelection(bool notify)
        {
            SetSelectedItem(kInvalidTreeItemId, notify);
        }

        // =========================================================================
        // Expansion
        // =========================================================================
        void TreeView::SetExpanded(TreeItemId item, bool expanded)
        {
            if (!_data_source || !_data_source->IsValid(item)) return;

            if (expanded)
            {
                if (_expanded_items.insert(item).second)
                {
                    _on_expansion_changed_delegate.Invoke(item, true);
                    Refresh();
                }
            }
            else
            {
                if (_expanded_items.erase(item) > 0)
                {
                    _on_expansion_changed_delegate.Invoke(item, false);
                    Refresh();
                }
            }
        }

        bool TreeView::IsExpanded(TreeItemId item) const
        {
            return _expanded_items.contains(item);
        }

        void TreeView::ToggleExpanded(TreeItemId item)
        {
            SetExpanded(item, !IsExpanded(item));
        }

        void TreeView::ExpandAll()
        {
            if (!_data_source) return;
            for (auto& vi : _visible_items)
            {
                if (vi._has_children)
                    _expanded_items.insert(vi._id);
            }
            Refresh();
        }

        void TreeView::CollapseAll()
        {
            ClearExpansionState();
            Refresh();
        }

        void TreeView::ClearExpansionState()
        {
            _expanded_items.clear();
        }

        void TreeView::ExpandParents(TreeItemId item)
        {
            if (!_data_source || item == kInvalidTreeItemId) return;

            std::unordered_set<TreeItemId> visited;
            TreeItemId current = item;
            constexpr u32 kMaxIter = 256;
            u32 iter = 0;

            while (current != kInvalidTreeItemId && iter < kMaxIter)
            {
                if (!visited.insert(current).second)
                {
                    LOG_WARNING("TreeView::ExpandParents: cycle at item {}", current);
                    break;
                }
                current = _data_source->GetParent(current);
                if (current != kInvalidTreeItemId)
                    _expanded_items.insert(current);
                ++iter;
            }

            Refresh();
        }

        void TreeView::ScrollItemIntoView(TreeItemId item)
        {
            if (!_data_source || item == kInvalidTreeItemId) return;

            // Find the item in visible items
            i32 index = -1;
            for (i32 i = 0; i < (i32)_visible_items.size(); ++i)
            {
                if (_visible_items[i]._id == item)
                {
                    index = i;
                    break;
                }
            }
            if (index < 0) return;

            // Compute the vertical position of the row
            f32 row_top = (f32)index * _row_height;
            f32 row_bottom = row_top + _row_height;
            f32 content_height = (f32)_visible_items.size() * _row_height;

            // Check viewport
            f32 view_h = _content_rect.w;
            f32 current_offset = _current_offset.y;
            f32 target_y = _target_offset.y;

            if (row_top < -target_y)
            {
                // Row is above viewport - scroll up
                _target_offset.y = -row_top;
            }
            else if (row_bottom > -target_y + view_h)
            {
                // Row is below viewport - scroll down
                _target_offset.y = -(row_bottom - view_h);
            }

            _target_offset.y = std::clamp(_target_offset.y, _max_offset.y, 0.0f);
        }

        // =========================================================================
        // Row Events
        // =========================================================================
        void TreeView::OnRowClicked(TreeItemId item)
        {
            SetSelectedItem(item, true);
        }

        void TreeView::OnRowDoubleClicked(TreeItemId item)
        {
            _on_item_double_clicked_delegate.Invoke(item);
        }

        void TreeView::OnRowContextMenu(TreeItemId item, Vector2f pos)
        {
            _on_item_context_menu_delegate.Invoke(item, pos);
        }

        // =========================================================================
        // Drag & Drop
        // =========================================================================
        void TreeView::HandleDrop(const DragPayload& payload, f32 x, f32 y, TreeItemId target)
        {
            if (payload._type != EDragType::kTreeItem) return;
            auto* tdp = static_cast<const TreeViewDragPayload*>(payload._data);
            if (!tdp || !_drop_callback) return;

            _drop_callback(tdp->_source_tree, tdp->_item, target);
        }

        // =========================================================================
        // Render
        // =========================================================================
        void TreeView::RenderImpl(UIRenderer& r)
        {
            ScrollView::RenderImpl(r);
        }

    }// namespace UI
}// namespace Ailu
