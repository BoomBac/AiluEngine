#include "Dock/DockManager.h"
#include "Framework/Common/Allocator.hpp"
#include "Common/EditorStyle.h"
#include "EditorApp.h"
#include "Framework/Common/Input.h"
#include "Framework/Events/Event.h"
#include "UI/Basic.h"
#include "UI/DragDrop.h"
#include "UI/UIFramework.h"
#include "UI/UILayer.h"
#include "UI/UIRenderer.h"
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <ranges>

#include "Objects/JsonArchive.h"
#include "Render/GraphicsContext.h"

namespace Ailu
{
    namespace Editor
    {
        namespace
        {
            UI::UIBrush ColorBrush(const Color &color)
            {
                UI::UIBrush brush;
                brush._type = UI::EUIBrushType::kColor;
                brush._tint = color;
                return brush;
            }

            void SaveDockLayoutState(DockWindow *window, String &out_state)
            {
                out_state.clear();
                if (window == nullptr)
                    return;

                JsonArchive ar;
                window->SaveDockLayoutState(ar);
                out_state = ar.SaveToString();
                if (out_state == "{}")
                    out_state.clear();
            }

            void LoadDockLayoutState(DockWindow *window, const String &saved_state)
            {
                if (window == nullptr)
                    return;

                if (!saved_state.empty())
                {
                    JsonArchive ar;
                    if (ar.LoadFromString(saved_state))
                    {
                        window->LoadDockLayoutState(ar);
                    }
                    else
                    {
                        LOG_WARNING("DockManager: failed to parse dock layout state for {}", window->GetType()->FullName());
                    }
                }
                window->OnDockLayoutLoaded();
            }
        }

        struct DockNode : public std::enable_shared_from_this<DockNode>
        {
            inline static constexpr f32 kSplitLineWidth = 4.0f;
            enum class EType
            {
                kSplit,//左右/上下分割
                kTab,  //包含多个dockwindow
                kLeaf  //只有一页的tab
            };
            static void SafeRemove(Ref<DockNode> node)
            {
                if (node)
                    node->RemoveNode();
            }
            DockNode(Window *win = Application::FocusedWindow()) : _own_window(win), _id(s_global_id++) {}
            //other为空时，则表示停靠至主窗口
            void DockTo(DockNode *other, EDockArea area);
            u32 HoverEdge(Vector2f pos, DockNode **out);
            bool IsHover(Vector2f pos) const;
            bool HoverDragArea(Vector2f pos) const;
            bool IsHoverSplitLine(Vector2f pos) const;
            void AdjustSplitRatio(f32 ratio);
            void SetDockedWindowState(bool is_docked);
            void NormalizeTabNode();
            Ref<DockNode> Clone() const
            {
                auto n = MakeRef<DockNode>();
                n->_type = _type;
                n->_left = _left;
                n->_right = _right;
                n->_is_vertical_split = _is_vertical_split;
                n->_split_ratio = _split_ratio;
                n->_parent = _parent;
                n->_position = _position;
                n->_size = _size;
                n->_own_window = _own_window;
                if (_type == EType::kLeaf)
                    n->_window = _window;
                else if (_type == EType::kTab)
                    n->_tab = _tab;
                return n;
            }
            void ResetContent()
            {
                _type = EType::kLeaf;
                _left = nullptr;
                _right = nullptr;
                _window = nullptr;
                _tab = nullptr;
                _is_vertical_split = true;
                _split_ratio = 0.5f;
            }
            void MoveContentFrom(DockNode *other)
            {
                if (other == nullptr || other == this)
                    return;
                _type = other->_type;
                _left = std::move(other->_left);
                _right = std::move(other->_right);
                _is_vertical_split = other->_is_vertical_split;
                _split_ratio = other->_split_ratio;
                _window = std::move(other->_window);
                _tab = std::move(other->_tab);
                if (_left)
                    _left->_parent = this;
                if (_right)
                    _right->_parent = this;
                other->ResetContent();
            }
            Ref<DockNode> ExtractContent(DockNode *new_parent)
            {
                auto extracted = MakeRef<DockNode>(_own_window);
                extracted->_parent = new_parent;
                extracted->_position = _position;
                extracted->_size = _size;
                extracted->_flags = _flags & ~EDockWindowFlag::kFullSize;
                extracted->_own_window = _own_window;
                extracted->_is_valid = _is_valid;
                extracted->MoveContentFrom(this);
                return extracted;
            }
            static void ConfigureSplitNode(DockNode *parent, Ref<DockNode> first, Ref<DockNode> second, bool is_vertical, bool is_second_prime)
            {
                AL_ASSERT(parent != nullptr);
                parent->_type = EType::kSplit;
                parent->_window = nullptr;
                parent->_tab = nullptr;
                parent->_is_vertical_split = is_vertical;
                parent->_split_ratio = 0.5f;
                if (is_second_prime)
                {
                    parent->_left = second;
                    parent->_right = first;
                }
                else
                {
                    parent->_left = first;
                    parent->_right = second;
                }
                if (parent->_left)
                    parent->_left->_parent = parent;
                if (parent->_right)
                    parent->_right->_parent = parent;
            }
            bool ContainsNode(const DockNode *node) const;
            bool ContainsWidget(UI::Widget *widget) const
            {
                if (widget == nullptr)
                    return false;
                if (_type == EType::kLeaf)
                {
                    return _window && (_window->TitleWidget() == widget || _window->ContentWidget() == widget);
                }
                if (_type == EType::kTab)
                {
                    if (_tab && _tab->TitleWidget() == widget)
                        return true;
                    if (_tab)
                    {
                        for (const auto &tab_item : *_tab)
                        {
                            if (tab_item->ContainsWidget(widget))
                                return true;
                        }
                    }
                    return false;
                }
                return (_left && _left->ContainsWidget(widget)) || (_right && _right->ContainsWidget(widget));
            }
            void UpdateLayout(f32 dt)
            {
                auto inset_panel_rect = [](Vector2f position, Vector2f size) -> Vector4f
                {
                    const f32 gap = g_editor_style._dock_panel_gap;
                    const Vector2f inset_pos = position + Vector2f(gap, gap);
                    const Vector2f inset_size = Max(size - Vector2f(gap * 2.0f, gap * 2.0f), DockWindow::kMinSize);
                    return {inset_pos, inset_size};
                };
                if (_type == EType::kLeaf)
                {
                    if (!_window)
                        return;
                    const bool is_docked = _parent != nullptr || (_flags & EDockWindowFlag::kFullSize) != 0u;
                    const u32 docked_flags = EDockWindowFlag::kNoMove | EDockWindowFlag::kNoResize;
                    const u32 old_flags = _window->_flags;
                    if (is_docked)
                        _window->_flags |= docked_flags;
                    else
                        _window->_flags &= ~docked_flags;
                    if (_window->_flags != old_flags)
                        _window->InvalidateDockState();
                    _window->SetRect(inset_panel_rect(_position, _size));
                }
                else if (_type == EType::kSplit)
                {
                    if (_left)
                    {
                        _left->_position = _position;
                        if (_is_vertical_split)
                        {
                            _left->_size = Vector2f(_size.x * _split_ratio, _size.y);
                        }
                        else
                        {
                            _left->_size = Vector2f(_size.x, _size.y * _split_ratio);
                        }
                        _left->UpdateLayout(dt);
                    }
                    if (_right)
                    {
                        Vector2f left_size = _size * _split_ratio;
                        if (_is_vertical_split)
                        {
                            _right->_position = Vector2f(_position.x + left_size.x, _position.y);
                            _right->_size = Vector2f(_size.x - left_size.x, _size.y);
                        }
                        else
                        {
                            _right->_position = Vector2f(_position.x, _position.y + left_size.y);
                            _right->_size = Vector2f(_size.x, _size.y - left_size.y);
                        }
                        _right->UpdateLayout(dt);
                    }
                }
                else
                {
                    if (!_tab)
                        return;
                    const Vector4f rect = inset_panel_rect(_position, _size);
                    _tab->_position = rect.xy;
                    _tab->_size = rect.zw;
                    _tab->Update(dt);
                }
            }

            void Update(f32 dt)
            {
                if (_type == EType::kSplit)
                {
                        if (_left)
                            _left->Update(dt);
                        if (_right)
                            _right->Update(dt);
                }
                else if (_type == EType::kTab && _tab)
                    _tab->Update(dt);
                else
                {
                    if (_window)
                        _window->Update(dt);
                }
            }

            void SetOwnWindow(Window *w)
            {
                _own_window = w;
                if (_type == EType::kLeaf)
                {
                    if (_window)
                    {
                        auto new_target = RenderTexture::WindowBackBuffer(w);
                        _window->ContentWidget()->BindOutput(new_target);
                        _window->TitleWidget()->BindOutput(new_target);
                        _window->ContentWidget()->SetParent(w);
                        _window->TitleWidget()->SetParent(w);
                    }
                }
                else if (_type == EType::kSplit)
                {
                        if (_left)
                            _left->SetOwnWindow(w);
                        if (_right)
                            _right->SetOwnWindow(w);
                }
                else
                {
                    auto new_target = RenderTexture::WindowBackBuffer(w);
                    _tab->TitleWidget()->BindOutput(new_target);
                    _tab->TitleWidget()->SetParent(w);
                    for (auto &t : *_tab)
                        t->AttachToWindow(w);
                }
            }
            void RemoveNode()
            {
                if (!_parent)
                {
                    // 根节点删除，需要特殊处理
                    HandleRootRemoval();
                    return;
                }
                if (_parent->_type != EType::kSplit)
                {
                    // 父节点不是split类型，直接从tab中移除
                    HandleTabRemoval();
                    return;
                }
                // 父节点是split类型，需要重新整理结构
                HandleSplitNodeRemoval();
            }
            // 检查节点是否为空（用于清理空节点）
            bool IsEmpty() const
            {
                switch (_type)
                {
                    case EType::kLeaf:
                        return _window == nullptr;
                    case EType::kTab:
                        return _tab == nullptr;// 或者 _tab->IsEmpty()
                    case EType::kSplit:
                        return (_left == nullptr || _left->IsEmpty()) &&
                               (_right == nullptr || _right->IsEmpty());
                }
                return true;
            }
            // 递归清理空节点
            void CleanupEmptyNodes()
            {
                if (_type == EType::kSplit)
                {
                    if (_left && _left->IsEmpty())
                    {
                        _left = nullptr;
                    }
                    if (_right && _right->IsEmpty())
                    {
                        _right = nullptr;
                    }

                    // 如果只剩一个子节点，提升它
                    if (_left && !_right)
                    {
                        PromoteSibling(this, _left);
                    }
                    else if (_right && !_left)
                    {
                        PromoteSibling(this, _right);
                    }
                    else if (!_left && !_right)
                    {
                        // 两个子节点都为空，删除当前节点
                        RemoveNode();
                    }
                    else
                    {
                        // 递归清理子节点
                        if (_left) _left->CleanupEmptyNodes();
                        if (_right) _right->CleanupEmptyNodes();
                    }
                }
            }
            bool ContainsWindow(DockWindow *w) const
            {
                if (_type == EType::kLeaf)
                {
                    return _window.get() == w;
                }
                else if (_type == EType::kTab)
                {
                    return _tab->Contains(w);
                }
                else if (_type == EType::kSplit)
                {
                    return (_left && _left->ContainsWindow(w)) ||
                           (_right && _right->ContainsWindow(w));
                }
                return false;
            }
        private:
            // 处理根节点被移除的情况
            void HandleRootRemoval()
            {
                LOG_INFO("{}", "HandleRootRemoval not impl yet");
            }
            void HandleTabRemoval()
            {
                LOG_INFO("{}", "HandleTabRemoval not impl yet");
            }
            void HandleSplitNodeRemoval()
            {
                DockNode *parent = _parent;
                Ref<DockNode> sibling = nullptr;
                // 找到兄弟节点
                if (parent->_left.get() == this)
                {
                    sibling = parent->_right;
                }
                else if (parent->_right.get() == this)
                {
                    sibling = parent->_left;
                }
                if (!sibling)
                {
                    // 没有兄弟节点，删除父节点
                    parent->RemoveNode();
                    return;
                }

                // 将兄弟节点提升到父节点的位置
                PromoteSibling(parent, sibling);
            }

            void PromoteSibling(DockNode *parent, Ref<DockNode> sibling)
            {
                // 保存父节点的信息
                DockNode *grandParent = parent->_parent;
                Vector2f parentPosition = parent->_position;
                Vector2f parentSize = parent->_size;

                // 将兄弟节点的内容复制到父节点
                parent->_type = sibling->_type;
                parent->_position = parentPosition;
                parent->_size = parentSize;

                switch (sibling->_type)
                {
                    case EType::kLeaf:
                        parent->_window = sibling->_window;
                        parent->_left = nullptr;
                        parent->_right = nullptr;
                        break;

                    case EType::kTab:
                        parent->_tab = sibling->_tab;
                        parent->_left = nullptr;
                        parent->_right = nullptr;
                        break;

                    case EType::kSplit:
                        parent->_left = sibling->_left;
                        parent->_right = sibling->_right;
                        parent->_is_vertical_split = sibling->_is_vertical_split;
                        parent->_split_ratio = sibling->_split_ratio;

                        // 更新子节点的父指针
                        if (parent->_left)
                            parent->_left->_parent = parent;
                        if (parent->_right)
                            parent->_right->_parent = parent;
                        break;
                }

                // 清理兄弟节点引用，避免循环引用
                sibling->_left = nullptr;
                sibling->_right = nullptr;
                sibling->_parent = nullptr;
            }

        public:
            EType _type = EType::kLeaf;
            // 分割布局
            Ref<DockNode> _left = nullptr;
            Ref<DockNode> _right = nullptr;
            bool _is_vertical_split = true;// true表示左右分割，false表示上下分割
            f32 _split_ratio = 0.5f;       // 左右/上下分割比
            DockNode *_parent = nullptr;
            Ref<DockWindow> _window = nullptr;
            Ref<DockTab> _tab = nullptr;
            Vector2f _position = Vector2f::kZero;
            Vector2f _size = Vector2f::kZero;
            bool _is_valid = true;
            Window *_own_window = nullptr;
            u32 _flags = 0u;
            u32 _id = 0u;
            inline static u32 s_global_id = 0u;
        };

        static DockWindow *FindPrimaryWindow(const DockNode *node)
        {
            if (node == nullptr)
                return nullptr;
            if (node->_type == DockNode::EType::kLeaf)
                return node->_window.get();
            if (node->_type == DockNode::EType::kSplit)
            {
                if (auto *left_window = FindPrimaryWindow(node->_left.get()))
                    return left_window;
                return FindPrimaryWindow(node->_right.get());
            }
            return node->_tab ? node->_tab->ActivePrimaryWindow() : nullptr;
        }

        static void SetNodeSubtreeVisibility(DockNode *node, bool is_visible)
        {
            if (node == nullptr)
                return;
            if (node->_type == DockNode::EType::kLeaf)
            {
                if (node->_window)
                {
                    node->_window->SetTitleBarVisibility(is_visible);
                    node->_window->SetContentVisibility(is_visible);
                }
                return;
            }
            if (node->_type == DockNode::EType::kSplit)
            {
                SetNodeSubtreeVisibility(node->_left.get(), is_visible);
                SetNodeSubtreeVisibility(node->_right.get(), is_visible);
                return;
            }
            if (node->_tab)
                node->_tab->SetVisible(is_visible);
        }

        class DockNodeTabItem final : public IDockTabItem
        {
        public:
            explicit DockNodeTabItem(Ref<DockNode> node) : _node(std::move(node)) {}

            String GetTitle() const override
            {
                if (_node && _node->_type == DockNode::EType::kSplit)
                    return "Workspace";
                if (auto *primary_window = PrimaryWindow())
                    return primary_window->GetTitle();
                return "DockNode";
            }
            Vector2f Position() const override
            {
                return _node ? _node->_position : Vector2f::kZero;
            }
            Vector2f Size() const override
            {
                return _node ? _node->_size : Vector2f::kZero;
            }
            void SetRect(Vector4f rect) override
            {
                if (!_node)
                    return;
                const f32 content_offset = DockWindow::kTitleBarHeight - DockWindow::kTitleContentOverlap;
                _node->_position = rect.xy + Vector2f(0.0f, content_offset);
                _node->_size = {rect.z, std::max(0.0f, rect.w - content_offset)};
                _node->UpdateLayout(0.0f);
            }
            void Update(f32 dt) override
            {
                if (!_node)
                    return;
                _node->UpdateLayout(dt);
                _node->Update(dt);
            }
            bool IsHover(Vector2f pos) const override
            {
                return _node && _node->IsHover(pos);
            }
            void SetFocus(bool is_focus) override
            {
                if (auto *primary_window = PrimaryWindow())
                    primary_window->SetFocus(is_focus);
            }
            void CaptureStandaloneRect() override
            {
                if (!_node)
                    return;
                _standalone_position = _node->_position;
                _standalone_size = _node->_size;
                _has_standalone_rect = true;
            }
            void SetTabActive(bool is_active) override
            {
                SetNodeSubtreeVisibility(_node.get(), is_active);
            }
            void RestoreStandaloneFromTab() override
            {
                if (_node && _has_standalone_rect)
                {
                    _node->_position = _standalone_position;
                    _node->_size = _standalone_size;
                    _node->UpdateLayout(0.0f);
                }
                SetNodeSubtreeVisibility(_node.get(), true);
            }
            bool ContainsWindow(DockWindow *w) const override
            {
                return _node && _node->ContainsWindow(w);
            }
            bool ContainsWidget(UI::Widget *widget) const override
            {
                return _node && _node->ContainsWidget(widget);
            }
            DockWindow *PrimaryWindow() const override
            {
                return FindPrimaryWindow(_node.get());
            }
            void AttachToWindow(Window *w) override
            {
                if (_node)
                    _node->SetOwnWindow(w);
            }
            bool CanClose() const override { return false; }
            void RequestClose() override {}

            Ref<DockNode> Node() const { return _node; }

        private:
            Ref<DockNode> _node;
            Vector2f _standalone_position;
            Vector2f _standalone_size;
            bool _has_standalone_rect = false;
        };

        bool DockNode::ContainsNode(const DockNode *node) const
        {
            if (node == nullptr)
                return false;
            if (this == node)
                return true;
            if (_type == EType::kSplit)
                return (_left && _left->ContainsNode(node)) || (_right && _right->ContainsNode(node));
            if (_type == EType::kTab && _tab)
            {
                for (const auto &tab_item : *_tab)
                {
                    auto split_item = std::dynamic_pointer_cast<DockNodeTabItem>(tab_item);
                    if (split_item && split_item->Node() && split_item->Node()->ContainsNode(node))
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        void DockNode::SetDockedWindowState(bool is_docked)
        {
            constexpr u32 kDockedFlags = EDockWindowFlag::kNoMove | EDockWindowFlag::kNoResize;
            auto apply_flags = [is_docked](DockWindow *window)
            {
                if (window == nullptr)
                    return;
                if (is_docked)
                    window->_flags |= kDockedFlags;
                else
                    window->_flags &= ~kDockedFlags;
                window->InvalidateDockState();
            };
            if (_type == EType::kLeaf)
            {
                apply_flags(_window.get());
            }
            else if (_type == EType::kTab)
            {
                if (_tab)
                {
                    for (const auto &tab_item : *_tab)
                    {
                        if (auto split_item = std::dynamic_pointer_cast<DockNodeTabItem>(tab_item))
                            split_item->Node()->SetDockedWindowState(is_docked);
                        else
                            apply_flags(tab_item->PrimaryWindow());
                    }
                }
            }
            else
            {
                if (_left)
                    _left->SetDockedWindowState(is_docked);
                if (_right)
                    _right->SetDockedWindowState(is_docked);
            }
        }

        void DockNode::NormalizeTabNode()
        {
            if (_type != EType::kTab || _tab == nullptr || _tab->TabCount() != 1 ||
                (_flags & EDockWindowFlag::kKeepTabBar) != 0u)
                return;
            auto remaining_item = _tab->ActiveItem();
            if (!remaining_item)
                return;

            if (auto remaining_window = std::dynamic_pointer_cast<DockWindow>(remaining_item))
            {
                _type = EType::kLeaf;
                _window = remaining_window;
                _tab = nullptr;
                if (_window)
                    _window->RestoreStandaloneFromTab();
            }
            else if (auto split_item = std::dynamic_pointer_cast<DockNodeTabItem>(remaining_item))
            {
                Ref<DockNode> remaining_node = split_item->Node();
                _tab = nullptr;
                if (remaining_node)
                    MoveContentFrom(remaining_node.get());
                SetNodeSubtreeVisibility(this, true);
            }
            const bool is_docked = _parent != nullptr || (_flags & EDockWindowFlag::kFullSize) != 0u;
            SetDockedWindowState(is_docked);
        }

        void DockNode::DockTo(DockNode *other, EDockArea area)
        {
            if (area == EDockArea::kFloat)
                return;
            auto &dock_mgr = DockManager::Get();
            auto self = shared_from_this();
            auto append_to_tab = [&](DockNode *target_node, DockTab *target_tab, Window *owner) -> bool
            {
                if (target_node == nullptr || target_tab == nullptr)
                    return false;
                dock_mgr.UntrackFloatNode(this);
                _flags &= ~EDockWindowFlag::kFullSize;
                SetOwnWindow(owner);
                SetDockedWindowState(true);

                bool is_added = false;
                if (_type == EType::kLeaf && _window)
                {
                    _window->_flags |= EDockWindowFlag::kNoMove | EDockWindowFlag::kNoResize;
                    is_added = target_tab->AddTab(_window);
                }
                else if (_type == EType::kTab && _tab)
                {
                    for (auto &tab_item : *_tab)
                    {
                        if (auto split_item = std::dynamic_pointer_cast<DockNodeTabItem>(tab_item))
                            split_item->Node()->_parent = target_node;
                        is_added = target_tab->AddTabItem(tab_item) || is_added;
                    }
                }
                else if (_type == EType::kSplit)
                {
                    _parent = target_node;
                    is_added = target_tab->AddTabItem(MakeRef<DockNodeTabItem>(self));
                }
                else
                {
                    LOG_WARNING("DockNode::DockTo: center dock source is invalid");
                }
                return is_added;
            };
            auto dock_as_split_child = [&](DockNode *target, bool is_vertical, bool is_second_prime)
            {
                AL_ASSERT(target != nullptr);
                Ref<DockNode> existing = target->ExtractContent(target);
                dock_mgr.UntrackFloatNode(this);
                _flags &= ~EDockWindowFlag::kFullSize;
                SetOwnWindow(target->_own_window);
                ConfigureSplitNode(target, existing, self, is_vertical, is_second_prime);
                target->SetDockedWindowState(true);
                target->UpdateLayout(0.0f);
            };
            if (other == nullptr)//停靠至主窗口
            {
                auto &main_window = Application::Get().GetWindow();
                const Vector4f main_area = dock_mgr.MainDockArea();
                dock_mgr.UntrackFloatNode(this);
                if (area == EDockArea::kCenter)
                {
                    SetOwnWindow(&main_window);
                    _size = main_area.zw;
                    _position = main_area.xy;
                    SetDockedWindowState(true);
                }
                else
                {
                    Ref<DockNode> current_root = ExtractContent(this);
                    auto placeholder = MakeRef<DockNode>(&main_window);
                    placeholder->_parent = this;
                    placeholder->_own_window = &main_window;
                    _size = main_area.zw;
                    _position = main_area.xy;
                    ConfigureSplitNode(this,
                                       placeholder,
                                       current_root,
                                       area == EDockArea::kLeft || area == EDockArea::kRight,
                                       area == EDockArea::kLeft || area == EDockArea::kTop);
                    SetOwnWindow(&main_window);
                    SetDockedWindowState(true);
                }
                _flags |= EDockWindowFlag::kFullSize;
                dock_mgr.TryAddFloatNode(self);
                dock_mgr._roots.push_back(this);
                UpdateLayout(0.0f);
            }
            else
            {
                if (area == EDockArea::kCenter)
                {
                    if (other->_type == EType::kLeaf && other->_window == nullptr)
                    {
                        dock_mgr.UntrackFloatNode(this);
                        SetOwnWindow(other->_own_window);
                        other->MoveContentFrom(this);
                        other->SetDockedWindowState(other->_parent != nullptr || (other->_flags & EDockWindowFlag::kFullSize) != 0u);
                        other->UpdateLayout(0.0f);
                        return;
                    }
                    if (other->_type == EType::kLeaf)
                    {
                        Window *owner = other->_own_window;
                        Ref<DockWindow> other_window = std::move(other->_window);
                        other->_type = EType::kTab;
                        other->_tab = MakeRef<DockTab>();
                        other->_window = nullptr;
                        other->_tab->AddTab(other_window);
                        append_to_tab(other, other->_tab.get(), owner);
                        other->SetDockedWindowState(true);
                        other->UpdateLayout(0.0f);
                        return;
                    }
                    if (other->_type == EType::kTab)
                    {
                        append_to_tab(other, other->_tab.get(), other->_own_window);
                        other->SetDockedWindowState(true);
                        other->UpdateLayout(0.0f);
                        return;
                    }
                    LOG_ERROR("DockNode::DockTo: center dock target is invalid");
                    return;
                }

                dock_as_split_child(other,
                                    area == EDockArea::kLeft || area == EDockArea::kRight,
                                    area == EDockArea::kLeft || area == EDockArea::kTop);
            }
        }

        static bool IsFocusedNodeBlockingInteraction(DockNode *candidate, DockNode *focused_node, Vector2f local_pos)
        {
            if (candidate == nullptr || focused_node == nullptr)
                return false;
            if (candidate->ContainsNode(focused_node))
                return false;
            for (DockNode *node = focused_node; node != nullptr; node = node->_parent)
            {
                if ((node->_flags & EDockWindowFlag::kFullSize) != 0u)
                    return false;
            }
            return focused_node->IsHover(local_pos);
        }

        static void FindHoverDragNode(const Ref<DockNode> &node, Vector2f pos, DockNode **out)
        {
            if (node == nullptr || !node->_is_valid || out == nullptr || *out != nullptr)
                return;
            if (node->_type == DockNode::EType::kSplit)
            {
                FindHoverDragNode(node->_left, pos, out);
                FindHoverDragNode(node->_right, pos, out);
                return;
            }
            if (node->HoverDragArea(pos))
                *out = node.get();
        }

        static bool IsDockedNode(const DockNode *node)
        {
            return node == nullptr || node->_parent != nullptr || (node->_flags & EDockWindowFlag::kFullSize) != 0u;
        }

        u32 DockNode::HoverEdge(Vector2f pos, DockNode **out)
        {
            u32 edge = 0;
            if (_type == EType::kLeaf)
            {
                edge = _window->HoverEdge(pos);
                if (edge)
                    *out = static_cast<DockNode *>(this);
                return _window->HoverEdge(pos);
            }
            else if (_type == EType::kSplit)
            {
                edge = DockWindow::HoverEdge(_position, _size, pos);
                if (edge)
                    *out = static_cast<DockNode *>(this);
                return edge;
            }
            edge = _tab->HoverEdge(pos);
            if (edge)
                *out = static_cast<DockNode *>(this);
            return _tab->HoverEdge(pos);
        }

        bool DockNode::IsHover(Vector2f pos) const
        {
            if (_type == EType::kSplit)
            {
                bool ret = false;
                if (_left) ret |= _left->IsHover(pos);
                if (_right) ret |= _right->IsHover(pos);
                return ret;
            }
            return UI::UIElement::IsPointInside(pos, {_position, _size});
        }

        bool DockNode::HoverDragArea(Vector2f pos) const
        {
            if (_type == EType::kLeaf)
            {
                if (_window == nullptr)
                    return false;
                else
                    return UI::UIElement::IsPointInside(pos, _window->DragArea());
            }
            else if (_type == EType::kSplit)
            {
                return false;
            }
            return _tab->HoverDragArea(pos);
        }

        bool DockNode::IsHoverSplitLine(Vector2f pos) const
        {
            if (_type != EType::kSplit)
                return false;
            Vector4f rect{_position, _size};
            if (!UI::UIElement::IsPointInside(pos, rect))
                return false;
            if (_is_vertical_split)
            {
                f32 line_x = rect.x + rect.z * _split_ratio;
                if (pos.x >= line_x - kSplitLineWidth && pos.x <= line_x + kSplitLineWidth)
                    return true;
            }
            else
            {
                f32 line_y = rect.y + rect.w * _split_ratio;
                if (pos.y >= line_y - kSplitLineWidth && pos.y <= line_y + kSplitLineWidth)
                    return true;
            }
            return false;
        }

        void DockNode::AdjustSplitRatio(f32 ratio)
        {
            if (_type != EType::kSplit)
                return;

            // 限制 ratio 避免越界
            _split_ratio = std::clamp(ratio, 0.05f, 0.95f);

            Vector2f left_size = _size;
            Vector2f right_size = _size;
            Vector2f left_pos = _position;
            Vector2f right_pos = _position;

            if (_is_vertical_split)
            {
                left_size.x = _size.x * _split_ratio;
                right_size.x = _size.x - left_size.x;

                right_pos.x += left_size.x;
            }
            else
            {
                left_size.y = _size.y * _split_ratio;
                right_size.y = _size.y - left_size.y;

                right_pos.y += left_size.y;
            }

            if (_left)
            {
                _left->_position = left_pos;
                _left->_size = left_size;
            }
            if (_right)
            {
                _right->_position = right_pos;
                _right->_size = right_size;
            }
        }

        static void UpdateResizeMouseCursor(u32 resize_dir)
        {
            if (!resize_dir)
                return;
            ECursorType cursor_type = ECursorType::kArrow;
            // 左右
            if (resize_dir == 1 || resize_dir == 4)
                cursor_type = ECursorType::kSizeEW;
            // 上下
            else if (resize_dir == 2 || resize_dir == 8)
                cursor_type = ECursorType::kSizeNS;
            // 对角 ↘↖
            else if ((resize_dir & 1 && resize_dir & 2) || (resize_dir & 4 && resize_dir & 8))
                cursor_type = ECursorType::kSizeNWSE;
            // 对角 ↗↙
            else if ((resize_dir & 4 && resize_dir & 2) || (resize_dir & 1 && resize_dir & 8))
                cursor_type = ECursorType::kSizeNESW;
            Application::Get().SetCursor(cursor_type);
            Application::Get().SetCursor(cursor_type, ECursorPriority::kHigh);
        }

        static void UpdateSplitResizeMouseCursor(const DockNode *split_node, const DockNode *cross_node = nullptr)
        {
            if (split_node != nullptr && cross_node != nullptr &&
                split_node->_is_vertical_split != cross_node->_is_vertical_split)
            {
                Application::Get().SetCursor(ECursorType::kSizeAll, ECursorPriority::kHigh);
                return;
            }
            if (split_node != nullptr)
                UpdateResizeMouseCursor(split_node->_is_vertical_split ? EHoverEdgeDir::kLeft : EHoverEdgeDir::kTop);
        }
        //输入屏幕鼠标坐标
        static bool IsMouseInWindow(Window *window, Vector2f pos)
        {
            if (!window)
                return false;
            auto [x, y] = window->GetClientPosition();
            auto [w, h] = window->GetClientSize();
            pos.x = pos.x - (f32) x;
            pos.y = pos.y - (f32) y;
            Vector2f wsize = Vector2f{(f32) w, (f32) h};
            return pos.x >= 0.0f && pos.x <= w && pos.y >= 0.0f && pos.y <= h;
        }

        static Vector2f GlobalToWindowPos(Window *window, Vector2f pos)
        {
            if (!window)
                return pos;
            auto [x, y] = window->GetClientPosition();
            return {pos.x - (f32) x, pos.y - (f32) y};
        }

        static UI::Widget *FindTopHoverWidget(Window *window, Vector2f local_pos)
        {
            auto &widgets = UI::UIManager::Get()->_widgets;
            for (i32 index = static_cast<i32>(widgets.size()) - 1; index >= 0; --index)
            {
                UI::Widget *widget = widgets[index].get();
                if (widget->_visibility != UI::EVisibility::kVisible || !widget->_is_receive_event ||
                    widget->Parent() != window)
                {
                    continue;
                }
                if (widget->IsHover(local_pos))
                    return widget;
            }
            return nullptr;
        }

        static Window *FindDockPreviewWindow(const HashMap<Window *, Vector<Ref<DockNode>>> &float_nodes, Vector2f pos)
        {
            Window *focused_window = Application::FocusedWindow();
            if (IsMouseInWindow(focused_window, pos))
                return focused_window;
            for (auto &it: float_nodes)
            {
                if (IsMouseInWindow(it.first, pos))
                    return it.first;
            }
            Window *main_window = &Application::Get().GetWindow();
            return IsMouseInWindow(main_window, pos) ? main_window : nullptr;
        }


        static DockManager *g_pDockMgr = nullptr;
        void DockManager::Init()
        {
            AL_ASSERT(g_pDockMgr == nullptr);
            g_pDockMgr = AL_NEW_TAG(EMemoryTag::kEditor, DockManager);
            if (UI::UIRenderer::Get() != nullptr)
            {
                UI::UIRenderer::Get()->SetOverlayDrawCallback([]()
                {
                    DockManager::Get().DrawFloatingShadow();
                    DockManager::Get().DrawFloatingPreview();
                });
            }
        }
        void DockManager::Shutdown()
        {
            if (UI::UIRenderer::Get() != nullptr)
                UI::UIRenderer::Get()->SetOverlayDrawCallback(nullptr);
            AL_DELETE(g_pDockMgr);
        }
        DockManager &DockManager::Get()
        {
            return *g_pDockMgr;
        }

        DockManager::DockManager()
        {
            Application::Get()._on_window_resize += [this](Window* w,Vector2f size)
            {
                for (auto &n: _roots)
                {
                    if (n->_own_window == w)
                    {
                        const Vector4f area = (w == &Application::Get().GetWindow()) ? MainDockArea() : Vector4f{Vector2f::kZero, size};
                        n->_position = area.xy;
                        n->_size = area.zw;
                    }
                }
            };
            _dock_layout_path = EditorApp::GetEditorRootPath() + L"dock_layout.json";
            JsonArchive ar;
            ar.Load(_dock_layout_path);
            for (auto p: DockNodeDataArray::StaticType()->GetProperties())
                p.Deserialize(&_node_data_array, ar);
            for (const auto &placement : _node_data_array._window_placements)
            {
                if (!placement._dock_id.empty())
                    _window_placement_map[placement._dock_id] = placement;
            }
            HashMap<u32, Ref<DockNode>> nodes_by_id;
            u32 max_node_id = 0u;
            for (const auto &n : _node_data_array._node_data)
            {
                max_node_id = std::max(max_node_id, n._id);
                auto node = MakeRef<DockNode>(Application::Get().GetWindowPtr());
                node->_id = n._id;
                node->_position = n._position;
                node->_size = n._size;
                node->_flags = n._flags;
                node->_is_vertical_split = n._is_vertical_split;
                node->_split_ratio = n._split_ratio;
                node->_type = static_cast<DockNode::EType>(n._type);

                if (node->_type == DockNode::EType::kLeaf)
                {
                    auto t = Type::Find(n._type_id);
                    if (!t)
                    {
                        LOG_ERROR("DockManager::DockManager: can't find type {}", n._type_id);
                        continue;
                    }
                    auto w = std::shared_ptr<DockWindow>(t->CreateInstance<DockWindow>());
                    w->SetTitle(n._title);
                    w->SetPosition(n._position);
                    w->SetSize(n._size);
                    w->_on_get_focus += [this](DockWindow *w)
                    {
                        RequestFocus(w);
                        //LOG_INFO("Focused dock node change to {}", _focused_node && _focused_node->_window ? _focused_node->_window->GetTitle() : "split/tab node");
                    };
                    //same with AddDock
                    UI::UIManager::Get()->RegisterWidget(w->TitleWidgetRef());
                    UI::UIManager::Get()->RegisterWidget(w->ContentWidgetRef());
                    LoadDockLayoutState(w.get(), n._window_state);
                    node->_window = w;
                }
                else if (node->_type == DockNode::EType::kTab)
                {
                    node->_tab = MakeRef<DockTab>();
                }
                else if (node->_type != DockNode::EType::kSplit)
                {
                    LOG_WARNING("DockManager::DockManager: {} unknown node type {}", n._title, n._type);
                    continue;
                }
                nodes_by_id[n._id] = node;
            }
            DockNode::s_global_id = std::max(DockNode::s_global_id, max_node_id + 1u);

            auto attach_split_child = [](DockNode *parent, const Ref<DockNode> &child)
            {
                if (parent == nullptr || child == nullptr)
                    return;
                child->_parent = parent;
                const bool place_left = parent->_is_vertical_split
                                             ? child->_position.x <= parent->_position.x + parent->_size.x * parent->_split_ratio
                                             : child->_position.y <= parent->_position.y + parent->_size.y * parent->_split_ratio;
                if (place_left)
                {
                    if (!parent->_left)
                        parent->_left = child;
                    else
                        parent->_right = child;
                }
                else
                {
                    if (!parent->_right)
                        parent->_right = child;
                    else
                        parent->_left = child;
                }
            };

            for (const auto &n : _node_data_array._node_data)
            {
                auto node_it = nodes_by_id.find(n._id);
                if (node_it == nodes_by_id.end())
                    continue;
                auto node = node_it->second;
                if (n._parent_id == UINT32_MAX)
                    continue;

                auto parent_it = nodes_by_id.find(n._parent_id);
                if (parent_it == nodes_by_id.end())
                {
                    LOG_WARNING("DockManager::DockManager: missing parent {} for node {}", n._parent_id, n._id);
                    continue;
                }
                auto parent = parent_it->second;
                if (parent->_type == DockNode::EType::kSplit)
                {
                    attach_split_child(parent.get(), node);
                }
                else if (parent->_type == DockNode::EType::kTab)
                {
                    node->_parent = parent.get();
                    if (node->_type == DockNode::EType::kLeaf && node->_window)
                    {
                        parent->_tab->AddTab(node->_window);
                    }
                    else
                    {
                        parent->_tab->AddTabItem(MakeRef<DockNodeTabItem>(node));
                    }
                }
            }

            for (const auto &n : _node_data_array._node_data)
            {
                if (n._parent_id != UINT32_MAX)
                    continue;
                auto node_it = nodes_by_id.find(n._id);
                if (node_it == nodes_by_id.end())
                    continue;
                auto node = node_it->second;
                if (n._window_id != 0)
                {
                    auto window = CreateNewWindow(n._title.empty() ? "DockWindow" : n._title,
                                                  (u16) n._window_size.x,
                                                  (u16) n._window_size.y,
                                                  true);
                    node->SetOwnWindow(window);
                    window->SetPosition((i32) n._window_position.x, (i32) n._window_position.y);
                }
                else
                {
                    node->SetOwnWindow(Application::Get().GetWindowPtr());
                }
                if ((node->_flags & EDockWindowFlag::kFullSize) != 0u)
                {
                    const Vector4f area = node->_own_window == &Application::Get().GetWindow() ? MainDockArea() : Vector4f{Vector2f::kZero, Vector2f{(f32) node->_own_window->GetWidth(), (f32) node->_own_window->GetHeight()}};
                    node->_position = area.xy;
                    node->_size = area.zw;
                }
                TryAddFloatNode(node);
                if (node->_flags & EDockWindowFlag::kFullSize)
                    _roots.push_back(node.get());
            }

            for (const auto &n : _node_data_array._node_data)
            {
                if (n._type != (u32) DockNode::EType::kTab || n._tab_id.empty())
                    continue;
                auto node_it = nodes_by_id.find(n._id);
                if (node_it == nodes_by_id.end() || !node_it->second->_tab || node_it->second->_tab->TabCount() == 0)
                    continue;
                const i32 max_index = node_it->second->_tab->TabCount() - 1;
                const i32 active_index = std::clamp((i32) std::atoi(n._tab_id.c_str()), 0, max_index);
                node_it->second->_tab->SetActiveIndex(active_index);
            }
            for (auto &[window, nodes] : _float_nodes)
                NormalizeWindowWidgetOrder(window);
        }
        
        DockManager::~DockManager()
        {
            _node_data_array._node_data.clear();
            _next_serialize_node_id = DockNode::s_global_id;
            for (auto& it: _float_nodes)
            {
                auto &[w, nodes] = it;
                for (auto& n : nodes)
                {
                    if (n && n->_is_valid)
                        WriteNodeData(n.get());
                }
            }
            SyncWindowPlacementArray();
            JsonArchive ar;
            for (auto p : DockNodeDataArray::StaticType()->GetProperties())
            {
                p.Serialize(&_node_data_array, ar);
            }
            ar.Save(_dock_layout_path);
        }

        Vector4f DockManager::MainDockArea() const
        {
            auto &main_window = Application::Get().GetWindow();
            if (_main_dock_size.x <= 0.0f || _main_dock_size.y <= 0.0f)
                return {Vector2f::kZero, Vector2f{(f32) main_window.GetWidth(), (f32) main_window.GetHeight()}};
            return {_main_dock_position, _main_dock_size};
        }

        void DockManager::SetMainDockArea(Vector2f position, Vector2f size)
        {
            size = {std::max(0.0f, size.x), std::max(0.0f, size.y)};
            if (NearbyEqual(_main_dock_position, position) && NearbyEqual(_main_dock_size, size))
                return;
            _main_dock_position = position;
            _main_dock_size = size;
            EnsureMainWorkspaceTabs();
            for (auto &n: _roots)
            {
                if (n && n->_own_window == &Application::Get().GetWindow())
                {
                    n->_position = _main_dock_position;
                    n->_size = _main_dock_size;
                }
            }
        }

        void DockManager::EnsureMainWorkspaceTabs()
        {
            Window *main_window = Application::Get().GetWindowPtr();
            auto roots_it = _float_nodes.find(main_window);
            if (roots_it == _float_nodes.end())
                return;

            DockNode *main_root = nullptr;
            for (DockNode *root : _roots)
            {
                if (root && root->_is_valid && root->_own_window == main_window &&
                    (root->_flags & EDockWindowFlag::kFullSize) != 0u)
                {
                    main_root = root;
                    break;
                }
            }
            if (main_root == nullptr)
                return;
            if (main_root->_type == DockNode::EType::kTab)
            {
                main_root->_flags |= EDockWindowFlag::kKeepTabBar;
                return;
            }

            auto root_it = std::find_if(roots_it->second.begin(), roots_it->second.end(),
                                        [main_root](const Ref<DockNode> &node) { return node.get() == main_root; });
            if (root_it == roots_it->second.end())
                return;

            Ref<DockNode> workspace_item = *root_it;
            auto workspace_root = MakeRef<DockNode>(main_window);
            workspace_root->_type = DockNode::EType::kTab;
            workspace_root->_tab = MakeRef<DockTab>();
            workspace_root->_position = _main_dock_position;
            workspace_root->_size = _main_dock_size;
            workspace_root->_flags = EDockWindowFlag::kFullSize | EDockWindowFlag::kKeepTabBar;

            workspace_item->_flags &= ~EDockWindowFlag::kFullSize;
            workspace_item->_parent = workspace_root.get();
            workspace_item->SetOwnWindow(main_window);
            workspace_item->SetDockedWindowState(true);
            workspace_root->_tab->AddTabItem(MakeRef<DockNodeTabItem>(workspace_item));

            TryRemoveFloatNode(main_root);
            std::erase(_roots, main_root);
            TryAddFloatNode(workspace_root);
            _roots.push_back(workspace_root.get());
            workspace_root->UpdateLayout(0.0f);
        }

        void DockManager::AddDock(Ref<DockWindow> dock)
        {
            UI::UIManager::Get()->RegisterWidget(dock->TitleWidgetRef());
            UI::UIManager::Get()->RegisterWidget(dock->ContentWidgetRef());

            auto leaf_node = MakeRef<DockNode>();
            leaf_node->_window = dock;
            auto placement_it = _window_placement_map.find(dock->GetDockPersistenceId());
            if (placement_it != _window_placement_map.end())
            {
                const DockWindowPlacement &placement = placement_it->second;
                dock->SetPosition(placement._position);
                dock->SetSize(Max(placement._size, DockWindow::kMinSize));
                leaf_node->_position = dock->Position();
                leaf_node->_size = dock->Size();
                if (placement._is_external_window && leaf_node->_own_window != nullptr &&
                    leaf_node->_own_window != Application::Get().GetWindowPtr())
                {
                    const Vector2f native_size = Max(placement._native_window_size, DockWindow::kMinSize);
                    leaf_node->_own_window->SetPosition(static_cast<i32>(placement._native_window_position.x),
                                                        static_cast<i32>(placement._native_window_position.y));
                    leaf_node->_own_window->SetWindowSize(static_cast<u16>(std::clamp(native_size.x, 1.0f, 65535.0f)),
                                                          static_cast<u16>(std::clamp(native_size.y, 1.0f, 65535.0f)));
                }
            }
            else
            {
                leaf_node->_position = dock->Position();
                leaf_node->_size = dock->Size();
            }
            dock->_on_get_focus += [this](DockWindow* w) {
                RequestFocus(w);
                //LOG_INFO("Focused dock node changed!");
            };
            TryAddFloatNode(leaf_node);
        }

        void DockManager::RemoveDock(DockWindow *dock)
        {
            if (dock == nullptr)
                return;
            std::erase(_pending_remove_docks, dock);

            DockNode *node = FindNodeByWindow(dock);
            if (node == nullptr)
                return;

            SaveWindowPlacement(dock, node);
            Window *own_window = node->_own_window;
            UI::UIManager::Get()->UnRegisterWidget(dock->TitleWidget());
            UI::UIManager::Get()->UnRegisterWidget(dock->ContentWidget());
            if (_focused_window == dock)
            {
                _focused_node = nullptr;
                _focused_window = nullptr;
            }

            if (node->_type == DockNode::EType::kLeaf)
            {
                node->_window.reset();
                if (node->_parent)
                    node->RemoveNode();
                else
                {
                    TryRemoveFloatNode(node);
                    std::erase_if(_roots, [node](DockNode *root) -> bool { return root == node; });
                }
                MarkDeleteNode(node);
            }
            else if (node->_type == DockNode::EType::kTab && node->_tab)
            {
                if (node->_tab->RemoveTab(dock))
                {
                    node->_tab.reset();
                    if (node->_parent)
                        node->RemoveNode();
                    else
                    {
                        TryRemoveFloatNode(node);
                        std::erase_if(_roots, [node](DockNode *root) -> bool { return root == node; });
                    }
                    MarkDeleteNode(node);
                }
            }

            CleanupWindowIfEmpty(own_window);
        }

        bool DockManager::ActivateDock(StringView type_name)
        {
            DockWindow *target_window = nullptr;
            std::function<bool(DockNode *)> find_window = [&](DockNode *node) -> bool
            {
                if (node == nullptr)
                    return false;
                if (node->_type == DockNode::EType::kLeaf && node->_window && node->_window->GetType()->FullName() == type_name)
                {
                    target_window = node->_window.get();
                    return true;
                }
                if (node->_type == DockNode::EType::kTab && node->_tab)
                {
                    i32 index = 0;
                    for (const auto &item : *node->_tab)
                    {
                        if (auto *window = item->PrimaryWindow(); window != nullptr && window->GetType()->FullName() == type_name)
                        {
                            node->_tab->SetActiveIndex(index);
                            target_window = window;
                            return true;
                        }
                        if (auto split_item = std::dynamic_pointer_cast<DockNodeTabItem>(item);
                            split_item && find_window(split_item->Node().get()))
                        {
                            node->_tab->SetActiveIndex(index);
                            return true;
                        }
                        ++index;
                    }
                }
                return find_window(node->_left.get()) || find_window(node->_right.get());
            };

            for (DockNode *root : _roots)
            {
                if (!find_window(root))
                    continue;
                RequestFocus(target_window);
                return true;
            }
            return false;
        }

        void DockManager::RequestRemoveDock(DockWindow *dock)
        {
            if (dock == nullptr)
                return;
            if (std::find(_pending_remove_docks.begin(), _pending_remove_docks.end(), dock) != _pending_remove_docks.end())
                return;
            _pending_remove_docks.push_back(dock);
        }

        static void DrawTreeNode(DockNode *node, int depth, int &row)
        {
            return;
            //if (!node) return;

            static const char *s_type_str[] = {"Split", "Tab", "Leaf"};

            // 先画当前节点
            String label = "null";
            if (node)
            {
                if (node->_window)
                    label = node->_window->GetTitle();
                else
                    label = s_type_str[(int) node->_type];
                if (node->_tab)
                {
                    label.append(": ");
                    for (auto t: *node->_tab)
                    {
                        label.append(t->GetTitle());
                        label.append(",");
                    }
                }
            }
            Vector2f pos(50.0f + depth * 50.0f, 200.0f + row * 30.0f);
            UI::UIRenderer::Get()->DrawText(label, pos);

            row++;// 下一行
            if (!node)
                return;
            // 再递归左右子
            DrawTreeNode(node->_left.get(), depth + 1, row);
            DrawTreeNode(node->_right.get(), depth + 1, row);
        }

        void DockManager::Update(f32 dt)
        {
            if (!_pending_remove_docks.empty())
            {
                Vector<DockWindow *> pending_remove_docks = std::move(_pending_remove_docks);
                _pending_remove_docks.clear();
                for (DockWindow *dock: pending_remove_docks)
                {
                    if (FindNodeByWindow(dock) != nullptr)
                        RemoveDock(dock);
                }
            }
            if (_is_any_float_node_invalid)
            {
                for (auto& it: _float_nodes)
                {
                    std::erase_if(it.second, [](Ref<DockNode> e) -> bool
                                  { return !e->_is_valid; });
                }
                _is_any_float_node_invalid = false;
            }
            if (!_is_any_floating)
            {
                HandleNodeResize();
            }

            if (_is_any_floating)
            {
                Vector2f global_pos = Input::GetGlobalMousePos();
                if (_can_draw_float_preview)
                {
                    OnWindowFloat(false);
                    Window *preview_window = _dock_preview_window ? _dock_preview_window : FindDockPreviewWindow(_float_nodes, global_pos);
                    if (!preview_window && _floating_preview_node)
                        preview_window = _floating_preview_node->_own_window;
                    if (!Input::IsKeyDown(EKey::kLBUTTON) && _floating_preview_node)
                    {
                        Window *drop_window = _dock_quad_hover_node ? _dock_quad_hover_node->_own_window : _dock_preview_window;
                        if (!drop_window)
                            drop_window = preview_window;
                        EndFloatWindow(GlobalToWindowPos(drop_window, global_pos));
                        _can_draw_float_preview = false;
                    }
                }
                else
                {
                    f32 dx = abs(global_pos.x - _float_node_start_pos.x), dy = abs(global_pos.y - _float_node_start_pos.y);
                    if (dx >= 10.0f || dy >= 10.0f)
                    {
                        _can_draw_float_preview = true;
                    }
                    else
                        _can_draw_float_preview = false;
                    LOG_INFO("_can_draw_float_preview: {}", _can_draw_float_preview ? "true" : "false");
                }
            }
            f32 offset_y = 10.0f;
            i32 row = 0;
            for (auto &it: _float_nodes)
            {
                auto &[window, nodes] = it;
                for (auto &node: nodes)
                {
                    node->UpdateLayout(dt);
                    node->Update(dt);
                    DrawTreeNode(node.get(), 0u, row);
                    //UI::UIRenderer::Get()->DrawText(node->_window->GetTitle(), Vector2f(500.0f, offset_y), 16u, Vector2f::kOne, Colors::kWhite);
                    offset_y += 20.0f;
                }
            }
        }

        static void FindFloatWindow(DockNode *node, DockWindow *w, DockNode **out)
        {
            if (!node || !node->_is_valid || out == nullptr || *out != nullptr)
                return;
            if (node->_type == DockNode::EType::kLeaf)
            {
                if (node->_window.get() == w)
                    *out = node;
            }
            else if (node->_type == DockNode::EType::kSplit)
            {
                if (node->_left)
                    FindFloatWindow(node->_left.get(), w, out);
                if (*out == nullptr && node->_right)
                    FindFloatWindow(node->_right.get(), w, out);
            }
            else
            {
                if (node->_tab)
                {
                    if (auto split_item = std::dynamic_pointer_cast<DockNodeTabItem>(node->_tab->ActiveItem()))
                        FindFloatWindow(split_item->Node().get(), w, out);
                    if (*out != nullptr)
                        return;
                    if (node->_tab->Contains(w))
                        *out = node;
                }
            }
        }

        void DockManager::BeginFloatWindow(DockWindow *w)
        {
            if (w == nullptr || _floating_preview_node != nullptr || _resizing_node != nullptr)
                return;
            DockNode *source_node = nullptr;
            for (auto &it: _float_nodes)
            {
                auto &[window, nodes] = it;
                for (auto &n: nodes)
                {
                    FindFloatWindow(n.get(), w, &source_node);
                    if (source_node)
                        break;
                }
                if (source_node)
                    break;
            }
            if (source_node == nullptr)
            {
                LOG_ERROR("DockManager::BeginFloatWindow: window not belong to any node!");
                return;
            }
            _floating_preview_window = w;
            _is_any_floating = true;
            _is_floating_whole_node = false;
            _is_float_on_cancel_area = false;
            // A title drag belongs to the node that owns the hit title bar. Do not promote a leaf to an
            // ancestor split here: that makes dragging the empty part of one dock title move every sibling.
            _floating_preview_node = source_node;
            _float_node_start_pos = Input::GetGlobalMousePos();
            _is_float_node_external_window = _floating_preview_node->_own_window != &Application::Get().GetWindow();
            LOG_INFO("DockManager::BeginFloatWindow: {}{}", w->GetTitle(), _is_floating_whole_node ? " (whole split)" : "");
        }

        void DockManager::BeginFloatNode(DockNode *w)
        {
            if (_floating_preview_node || _resizing_edge_dir)
                return;
            if (w == nullptr || w->IsEmpty())
                return;
            if (w->_type == DockNode::EType::kLeaf)
                _floating_preview_window = w->_window.get();
            else if (w->_type == DockNode::EType::kTab && w->_tab && w->_tab->TabCount() > 0)
                _floating_preview_window = w->_tab->ActivePrimaryWindow();
            else
                _floating_preview_window = nullptr;
            _is_any_floating = true;
            _is_floating_whole_node = true;
            _floating_preview_node = w;
            _is_float_on_cancel_area = false;
            _float_node_start_pos = Input::GetGlobalMousePos();
            _is_float_node_external_window = w->_own_window != &Application::Get().GetWindow();
            LOG_INFO("DockManager::BeginFloatNode: {}", _floating_preview_window ? _floating_preview_window->GetTitle() : "composite");
        }

        void DockManager::EndFloatWindow(Vector2f drop_pos)
        {
            if (!_floating_preview_node)
            {
                _floating_preview_window = nullptr;
                _is_any_floating = false;
                _is_float_on_cancel_area = false;
                _can_draw_float_preview = false;
                _is_floating_whole_node = false;
                return;
            }
            Window *source_window = _floating_preview_node->_own_window;
            auto make_node_from_tab_item = [&](const Ref<IDockTabItem> &tab_item) -> Ref<DockNode>
            {
                if (!tab_item)
                    return nullptr;
                if (auto split_item = std::dynamic_pointer_cast<DockNodeTabItem>(tab_item))
                {
                    auto new_node = split_item->Node();
                    new_node->_parent = nullptr;
                    new_node->_position = drop_pos;
                    new_node->_size = split_item->Size();
                    new_node->SetDockedWindowState(false);
                    return new_node;
                }
                if (auto window_item = std::dynamic_pointer_cast<DockWindow>(tab_item))
                {
                    auto new_node = MakeRef<DockNode>();
                    new_node->_position = drop_pos;
                    new_node->_size = window_item->Size();
                    new_node->_window = window_item;
                    new_node->SetDockedWindowState(false);
                    return new_node;
                }
                return nullptr;
            };
            auto detach_active_tab = [&](DockNode *tab_node, bool &out_is_tab_empty) -> Ref<DockNode>
            {
                AL_ASSERT(tab_node != nullptr && tab_node->_type == DockNode::EType::kTab && tab_node->_tab != nullptr);
                Ref<IDockTabItem> active_item = tab_node->_tab->RemoveActiveItem();
                out_is_tab_empty = tab_node->_tab->TabCount() == 0;
                tab_node->NormalizeTabNode();
                return make_node_from_tab_item(active_item);
            };
            auto finish_float = [&]()
            {
                _floating_preview_window = nullptr;
                _floating_preview_node = nullptr;
                _is_any_floating = false;
                _is_float_on_cancel_area = false;
                _can_draw_float_preview = false;
                _is_floating_whole_node = false;
                CleanupWindowIfEmpty(source_window);
                LOG_INFO("DockManager::EndFloatWindow: at pos: {}", drop_pos.ToString());
            };
            const bool is_hover_self = _dock_quad_hover_node != nullptr &&
                                       _floating_preview_node->ContainsNode(_dock_quad_hover_node);
            if (is_hover_self && !_is_float_on_cancel_area && IsDockedNode(_floating_preview_node))
            {
                if (_is_floating_whole_node || _floating_preview_node->_type == DockNode::EType::kLeaf)
                {
                    DetachFromTree(_floating_preview_node);
                }
                else if (_floating_preview_node->_type == DockNode::EType::kTab)
                {
                    bool is_tab_empty = false;
                    Ref<DockNode> new_node = detach_active_tab(_floating_preview_node, is_tab_empty);
                    if (new_node)
                        TryAddFloatNode(new_node);
                    if (is_tab_empty)
                        DetachFromTree(_floating_preview_node);
                }
                finish_float();
                return;
            }
            if (_is_floating_whole_node)
            {
                if (_dock_quad_hover_node && !is_hover_self)
                {
                    if (_dock_quad_hover_area != EDockArea::kFloat)
                    {
                        if (_floating_preview_node->_parent)
                            DetachFromTree(_floating_preview_node);
                        _floating_preview_node->DockTo(_dock_quad_hover_node, _dock_quad_hover_area);
                    }
                }
                else if (_dock_quad_hover_area != EDockArea::kFloat)
                {
                    _floating_preview_node->DockTo(nullptr, _dock_quad_hover_area);
                }
                finish_float();
                return;
            }
            if (_dock_quad_hover_node)
            {
                if (_floating_preview_node->_type == DockNode::EType::kLeaf)
                {
                    //在当前拖动节点上释放
                    if (_dock_quad_hover_node == _floating_preview_node)
                    {
                        //没有释放在标题栏
                        if (!_is_float_on_cancel_area)
                        {
                            LOG_INFO("DockManager::EndFloatWindow: detach self...");
                            DetachFromTree(_floating_preview_node);
                        }
                    }
                    else//释放在其他节点上
                    {
                        String src_title = _floating_preview_window->GetTitle();
                        String dst_title = _dock_quad_hover_node->_window ? _dock_quad_hover_node->_window->GetTitle() : "null";
                        if (_dock_quad_hover_area == EDockArea::kFloat)
                            return;
                        auto t = StaticEnum<EDockArea>();
                        LOG_INFO("Window: {} dock to the {} of {}", src_title, t->GetNameByEnum(_dock_quad_hover_area), dst_title);
                        DetachFromTree(_floating_preview_node);
                        _floating_preview_node->DockTo(_dock_quad_hover_node, _dock_quad_hover_area);
                    }
                }
                else if (_floating_preview_node->_type == DockNode::EType::kTab)
                {
                    //在当前拖动节点上释放
                    if (_dock_quad_hover_node == _floating_preview_node)
                    {
                        //没有释放在标题栏
                        if (!_is_float_on_cancel_area)
                        {
                            LOG_INFO("DockManager::EndFloatWindow: detach self...");
                            bool is_tab_empty = false;
                            auto new_node = detach_active_tab(_floating_preview_node, is_tab_empty);
                            TryAddFloatNode(new_node);
                            if (is_tab_empty)
                                DetachFromTree(_floating_preview_node);
                        }
                    }
                    else
                    {
                        bool is_tab_empty = false;
                        auto new_node = detach_active_tab(_floating_preview_node, is_tab_empty);
                        new_node->DockTo(_dock_quad_hover_node, _dock_quad_hover_area);
                        if (is_tab_empty)
                            DetachFromTree(_floating_preview_node);
                    }
                    LOG_INFO("DockManager::EndFloatWindow dock to tab");
                }
                else
                {
                    LOG_ERROR("DockManager::EndFloatWindow dock to split node not allow");
                }
            }
            else
            {
                //没有停靠至任意一个节点，通用的逻辑的detach当前节点，将其附加到主窗口（可选），如果父节点存在的话，则需要将子节点置空
                if (auto parent = _floating_preview_node->_parent; parent != nullptr)
                {
                    if (_floating_preview_node->_type == DockNode::EType::kLeaf)
                    {
                        DetachFromTree(_floating_preview_node);
                        if (_dock_quad_hover_area != EDockArea::kFloat)
                            _floating_preview_node->DockTo(nullptr, _dock_quad_hover_area);
                    }
                    else if (_floating_preview_node->_type == DockNode::EType::kTab)
                    {
                        bool is_tab_empty = false;
                        auto new_node = detach_active_tab(_floating_preview_node, is_tab_empty);
                        if (is_tab_empty)
                            DetachFromTree(_floating_preview_node);
                        if (_dock_quad_hover_area != EDockArea::kFloat)
                            new_node->DockTo(nullptr, _dock_quad_hover_area);
                    }
                    else
                    {
                        AL_ASSERT(false);
                    }
                }
                else
                {
                    if (_floating_preview_node->_type == DockNode::EType::kTab)
                    {
                        bool is_tab_empty = false;
                        auto new_node = detach_active_tab(_floating_preview_node, is_tab_empty);
                        if (is_tab_empty)
                            MarkDeleteNode(_floating_preview_node);
                        if (_dock_quad_hover_area != EDockArea::kFloat)
                            new_node->DockTo(nullptr, _dock_quad_hover_area);
                        TryAddFloatNode(new_node);
                    }
                    else if (_floating_preview_node->_type == DockNode::EType::kLeaf)
                    {
                        if (_dock_quad_hover_area != EDockArea::kFloat)
                            _floating_preview_node->DockTo(nullptr, _dock_quad_hover_area);
                    }
                    else if (_floating_preview_node->_type == DockNode::EType::kSplit)
                    {
                        if (_dock_quad_hover_area != EDockArea::kFloat)
                            _floating_preview_node->DockTo(nullptr, _dock_quad_hover_area);
                    }
                    else { AL_ASSERT(false); }
                }
            }
            finish_float();
        }

        void DockManager::MarkDeleteNode(DockNode *node)
        {
            node->_is_valid = false;
            _is_any_float_node_invalid = true;
        }

        static DockNode *FindDockNodeByWidget(DockNode *node, const UI::Widget *widget)
        {
            if (node == nullptr || widget == nullptr || !node->_is_valid)
                return nullptr;
            if (node->_type == DockNode::EType::kLeaf)
            {
                const bool contains_widget = node->_window &&
                                             (node->_window->TitleWidget() == widget ||
                                              node->_window->ContentWidget() == widget);
                return contains_widget ? node : nullptr;
            }
            if (node->_type == DockNode::EType::kSplit)
            {
                if (DockNode *left_node = FindDockNodeByWidget(node->_left.get(), widget))
                    return left_node;
                return FindDockNodeByWidget(node->_right.get(), widget);
            }

            if (node->_tab == nullptr)
                return nullptr;
            if (node->_tab->TitleWidget() == widget)
                return node;
            if (auto split_item = std::dynamic_pointer_cast<DockNodeTabItem>(node->_tab->ActiveItem()))
                return FindDockNodeByWidget(split_item->Node().get(), widget);
            if (DockWindow *active_window = node->_tab->ActivePrimaryWindow())
            {
                if (active_window->TitleWidget() == widget || active_window->ContentWidget() == widget)
                    return node;
            }
            return nullptr;
        }

        void DockManager::DrawFloatingShadow()
        {
            auto *renderer = UI::UIRenderer::Get();
            if (renderer == nullptr)
                return;

            Window *main_window = &Application::Get().GetWindow();
            constexpr f32 kShadowSpread = 16.0f;
            constexpr f32 kShadowOffset = 0.0f;

            for (auto &[window, nodes] : _float_nodes)
            {
                if (window == nullptr)
                    continue;

                for (auto &node : nodes)
                {
                    if (!node || !node->_is_valid)
                        continue;
                    if (window == main_window && ((node->_flags & EDockWindowFlag::kFullSize) != 0u || node->_parent != nullptr))
                        continue;

                    const Vector2f shadow_pos = node->_position + Vector2f{kShadowOffset, kShadowOffset};
                    const Vector2f shadow_size = node->_size + Vector2f{kShadowSpread * 2.0f, kShadowSpread * 2.0f};
                    renderer->DrawWindowShadow(window,
                                               {shadow_pos - Vector2f{kShadowSpread, kShadowSpread}, shadow_size},
                                               Color(0.0f, 0.0f, 0.0f, 0.26f),
                                               Vector4f(g_editor_style._window_corner_radius));
                }
            }
        }

        void DockManager::DrawFloatingPreview()
        {
            if (!_is_any_floating || !_can_draw_float_preview || _floating_preview_node == nullptr)
                return;
            const Vector2f global_pos = Input::GetGlobalMousePos();
            OnWindowFloat(true);
            Window *preview_window = _dock_preview_window ? _dock_preview_window : FindDockPreviewWindow(_float_nodes, global_pos);
            if (!preview_window && _floating_preview_node)
                preview_window = _floating_preview_node->_own_window;
            const Vector2f pos = GlobalToWindowPos(preview_window, global_pos);
            UI::UIRenderer::Get()->DrawWindowQuad(preview_window, {pos - Vector2f{100.0f, 70.0f}, Vector2f{200.0f, 140.0f}},
                                                  ColorBrush(g_editor_style._dock_hint_color));
        }

        void DockManager::OnWindowFloat(bool draw_preview)
        {
            Vector2f pos = Input::GetGlobalMousePos();
            //Vector2f pos = Input::GetMousePos();
            Vector2f size = Vector2f::kZero, start_pos = Vector2f::kZero;
            AL_ASSERT(_floating_preview_node != nullptr);
            _dock_quad_hover_node = nullptr;
            _dock_preview_window = nullptr;
            _dock_quad_hover_area = EDockArea::kFloat;
            _is_float_on_cancel_area = false;
            Window *hover_window = FindDockPreviewWindow(_float_nodes, pos);
            UI::Widget *top_hover_widget = nullptr;
            if (hover_window != nullptr)
            {
                const Vector2f local_pos = GlobalToWindowPos(hover_window, pos);
                top_hover_widget = FindTopHoverWidget(hover_window, local_pos);
                if (top_hover_widget != nullptr)
                {
                    auto nodes_it = _float_nodes.find(hover_window);
                    if (nodes_it != _float_nodes.end())
                    {
                        for (auto &node : nodes_it->second)
                        {
                            _dock_quad_hover_node = FindDockNodeByWidget(node.get(), top_hover_widget);
                            if (_dock_quad_hover_node != nullptr)
                                break;
                        }
                    }
                }
            }
            if (_dock_quad_hover_node != nullptr && _floating_preview_node->ContainsNode(_dock_quad_hover_node))
                _dock_quad_hover_node = _floating_preview_node;
            if (_dock_quad_hover_node != nullptr)
            {
                start_pos = _dock_quad_hover_node->_position;
                size = _dock_quad_hover_node->_size;
                _dock_preview_window = _dock_quad_hover_node->_own_window;
            }

            if (_dock_quad_hover_node == nullptr)
            {
                Window *main_window = &Application::Get().GetWindow();
                if (hover_window == main_window && top_hover_widget == nullptr)
                {
                    const Vector4f main_area = MainDockArea();
                    start_pos = main_area.xy;
                    size = main_area.zw;
                    _dock_preview_window = main_window;
                    DrawPreviewDockArea(main_window, GlobalToWindowPos(main_window, pos), size, start_pos, draw_preview);
                }
            }
            else
            {
                Window *target_window = _dock_quad_hover_node->_own_window;
                const Vector2f local_pos = GlobalToWindowPos(target_window, pos);
                if (_dock_quad_hover_node == _floating_preview_node)
                {
                    const bool is_docked = IsDockedNode(_floating_preview_node);
                    _is_float_on_cancel_area = !is_docked;
                    if (draw_preview && is_docked)
                    {
                        UI::UIRenderer *renderer = UI::UIRenderer::Get();
                        const Vector2f center = start_pos + size * 0.5f;
                        const Vector2f text_size = renderer->CalculateTextSize("Drop to detach");
                        renderer->DrawWindowText(target_window, "Drop to detach", center - text_size * 0.5f);
                    }
                    return;
                }
                if (_dock_quad_hover_node->_type == DockNode::EType::kTab &&
                    _dock_quad_hover_node->_tab->HoverTabBar(local_pos))
                {
                    _dock_quad_hover_area = EDockArea::kCenter;
                    _is_float_on_cancel_area = _dock_quad_hover_node == _floating_preview_node;
                    if (draw_preview)
                    {
                        const Vector4f preview_rect = {_dock_quad_hover_node->_position, _dock_quad_hover_node->_size};
                        const UI::UIBrush preview_brush = ColorBrush(g_editor_style._dock_hint_color);
                        UI::UIRenderer::Get()->DrawWindowQuad(target_window, preview_rect, preview_brush);
                    }
                    return;
                }
                DrawPreviewDockArea(target_window, GlobalToWindowPos(target_window, pos), size, start_pos, draw_preview);
            }
        }

        void DockManager::DrawPreviewDockArea(Window *window, Vector2f pos, Vector2f w_size, Vector2f start_pos, bool draw_preview)
        {
            auto r = UI::UIRenderer::Get();
            //auto &cur_window = Application::Get().GetWindow();
            //Vector2f w_size = {(f32) cur_window.GetWidth(), (f32) cur_window.GetHeight()};

            static const f32 s_main_axis_scale = 0.67f; // 主轴缩放（矩形细长）
            static const f32 s_cross_axis_scale = 0.12f;// 副轴缩放
            static const f32 s_gap = 0.0f;              // 与中心区域间隙
            bool is_area_changed = false;
            auto drawDockRect = [&](Vector2f center, Vector2f size, bool &is_area_changed) -> bool
            {
                center += start_pos;
                Vector4f rect = {center - size * 0.5f, size};
                bool is_hover = UI::UIElement::IsPointInside(pos, rect);
                Color c = is_hover
                                  ? _dock_quad_hover_color
                                  : _dock_quad_normal_color;
                if (draw_preview)
                    r->DrawWindowQuad(window, rect, ColorBrush(c));
                is_area_changed |= is_hover;
                return is_hover;
            };

            // --- Center ---
            {
                Vector2f size = w_size * 0.5f;
                Vector2f center = w_size * 0.5f;
                _dock_quad_hover_area = drawDockRect(center, size, is_area_changed) ? EDockArea::kCenter : _dock_quad_hover_area;
            }
            if (_dock_quad_hover_node && _dock_quad_hover_node->_type == DockNode::EType::kTab)
                return;
            // --- Top ---
            {
                Vector2f size = {w_size.x * s_main_axis_scale, w_size.y * s_cross_axis_scale};
                Vector2f center = {w_size.x * 0.5f, size.y * 0.5f + s_gap};
                _dock_quad_hover_area = drawDockRect(center, size, is_area_changed) ? EDockArea::kTop : _dock_quad_hover_area;
            }

            // --- Bottom ---
            {
                Vector2f size = {w_size.x * s_main_axis_scale, w_size.y * s_cross_axis_scale};
                Vector2f center = {w_size.x * 0.5f, w_size.y - size.y * 0.5f - s_gap};
                _dock_quad_hover_area = drawDockRect(center, size, is_area_changed) ? EDockArea::kBottom : _dock_quad_hover_area;
            }

            // --- Left ---
            {
                Vector2f size = {w_size.x * s_cross_axis_scale, w_size.y * s_main_axis_scale};
                Vector2f center = {size.x * 0.5f + s_gap, w_size.y * 0.5f};
                _dock_quad_hover_area = drawDockRect(center, size, is_area_changed) ? EDockArea::kLeft : _dock_quad_hover_area;
            }

            // --- Right ---
            {
                Vector2f size = {w_size.x * s_cross_axis_scale, w_size.y * s_main_axis_scale};
                Vector2f center = {w_size.x - size.x * 0.5f - s_gap, w_size.y * 0.5f};
                _dock_quad_hover_area = drawDockRect(center, size, is_area_changed) ? EDockArea::kRight : _dock_quad_hover_area;
            }
            if (!is_area_changed)
                _dock_quad_hover_area = EDockArea::kFloat;
        }

        void DockManager::UpdateDockNode(DockNode *node)
        {
        }

        void DockManager::ReleaseMouseCapture()
        {
            const bool had_capture = HasMouseCapture();
            _mouse_interaction = EDockMouseInteraction::kNone;
            _resizing_node = nullptr;
            _resizing_edge_dir = 0u;
            _adj_split_node = nullptr;
            _adj_split_node_cross = nullptr;
            _drag_move_node = nullptr;
            InputRouteState::Get().ReleaseMouseCapture(EInputOwner::kEngine);
            if (had_capture)
                InputRouteState::Get().ClearOwner(InputChannel::kMouse, EInputOwner::kEngine);
        }

        static void FindHoverSplitNodes(Ref<DockNode> node, Vector2f pos, DockNode **vertical_out, DockNode **horizontal_out);

        void DockManager::OnEvent(Event &e)
        {
            if (e.GetEventType() == EEventType::kWindowLostFocus)
            {
                ReleaseMouseCapture();
                return;
            }

            if (e.GetEventType() == EEventType::kMouseButtonReleased)
            {
                auto &mouse_event = static_cast<MouseButtonReleasedEvent &>(e);
                if (mouse_event.GetButton() == EKey::kLBUTTON && HasMouseCapture())
                {
                    ReleaseMouseCapture();
                    e.SetHandled();
                    InputRouteState::Get().Consume(InputChannel::kMouse);
                }
                return;
            }

            if (e.GetEventType() == EEventType::kMouseMoved && HasMouseCapture())
            {
                e.SetHandled();
                InputRouteState::Get().Consume(InputChannel::kMouse);
                return;
            }

            if (e.GetEventType() != EEventType::kMouseButtonPressed || e.Handled() || HasMouseCapture())
                return;

            auto &mouse_event = static_cast<MouseButtonPressedEvent &>(e);
            if (mouse_event.GetButton() != EKey::kLBUTTON ||
                InputRouteState::Get().IsOwnedBy(InputChannel::kMouse, EInputOwner::kImGui) || _is_any_floating)
                return;

            Window *window = e._window != nullptr ? e._window : Application::FocusedWindow();
            if (window == nullptr)
                return;
            auto nodes_it = _float_nodes.find(window);
            if (nodes_it == _float_nodes.end())
                return;

            const Vector2f mouse_pos = Input::GetMousePosAccurate(window);
            UI::Widget *top_hover_widget = FindTopHoverWidget(window, mouse_pos);
            DockNode *hover_vertical_split = nullptr;
            DockNode *hover_horizontal_split = nullptr;
            for (auto &node : nodes_it->second)
            {
                if (!node || !node->_is_valid ||
                    (top_hover_widget != nullptr && !node->ContainsWidget(top_hover_widget)) ||
                    IsFocusedNodeBlockingInteraction(node.get(), _focused_node, mouse_pos))
                    continue;
                FindHoverSplitNodes(node, mouse_pos, &hover_vertical_split, &hover_horizontal_split);
            }

            if (hover_vertical_split != nullptr || hover_horizontal_split != nullptr)
            {
                _adj_split_node = hover_vertical_split != nullptr ? hover_vertical_split : hover_horizontal_split;
                _adj_split_node_cross = hover_vertical_split != nullptr && hover_horizontal_split != nullptr
                                            ? hover_horizontal_split
                                            : nullptr;
                _mouse_interaction = EDockMouseInteraction::kSplit;
            }
            else
            {
                DockNode *hover_edge_node = nullptr;
                u32 hover_resize_dir = 0u;
                for (auto &node : nodes_it->second)
                {
                    if (!node || !node->_is_valid || (node->_flags & EDockWindowFlag::kFullSize) != 0u ||
                        (node->_flags & EDockWindowFlag::kNoResize) != 0u ||
                        IsFocusedNodeBlockingInteraction(node.get(), _focused_node, mouse_pos))
                        continue;
                    hover_resize_dir = node->HoverEdge(mouse_pos, &hover_edge_node);
                    if (hover_resize_dir != 0u)
                        break;
                }
                if (hover_edge_node != nullptr)
                {
                    _resizing_node = hover_edge_node;
                    _resizing_edge_dir = hover_resize_dir;
                    _mouse_interaction = EDockMouseInteraction::kResizeEdge;
                }
                else
                {
                    for (auto &node : nodes_it->second)
                    {
                        if (!node || !node->_is_valid || (node->_flags & EDockWindowFlag::kNoMove) != 0u ||
                            IsFocusedNodeBlockingInteraction(node.get(), _focused_node, mouse_pos))
                            continue;
                        DockNode *drag_hover_node = nullptr;
                        FindHoverDragNode(node, mouse_pos, &drag_hover_node);
                        if (drag_hover_node == nullptr || IsDockedNode(drag_hover_node))
                            continue;
                        _drag_move_node = node->_type == DockNode::EType::kSplit ? node.get() : drag_hover_node;
                        _drag_start_offset = _drag_move_node->_position - mouse_pos;
                        _float_node_start_pos = Input::GetGlobalMousePosAccurate();
                        if (drag_hover_node->_window)
                        {
                            drag_hover_node->_window->SetFocus(true);
                            _focused_node = drag_hover_node;
                        }
                        _mouse_interaction = EDockMouseInteraction::kMove;
                        break;
                    }
                }
            }

            if (HasMouseCapture())
            {
                InputRouteState::Get().SetOwner(InputChannel::kMouse, EInputOwner::kEngine);
                InputRouteState::Get().CaptureMouse(EInputOwner::kEngine);
                UI::UIManager::Get()->ClearMouseCapture();
                InputRouteState::Get().Consume(InputChannel::kMouse);
                e.SetHandled();
            }
        }

        static void FindHoverSplitNodes(Ref<DockNode> node, Vector2f pos, DockNode **vertical_out, DockNode **horizontal_out)
        {
            if (node == nullptr || !node->_is_valid || vertical_out == nullptr || horizontal_out == nullptr)
                return;
            if (node->_type == DockNode::EType::kSplit)
            {
                FindHoverSplitNodes(node->_left, pos, vertical_out, horizontal_out);
                FindHoverSplitNodes(node->_right, pos, vertical_out, horizontal_out);
                if (node->IsHoverSplitLine(pos))
                {
                    DockNode **out = node->_is_vertical_split ? vertical_out : horizontal_out;
                    if (*out == nullptr)
                        *out = node.get();
                }
                return;
            }
            if (node->_type == DockNode::EType::kTab && node->_tab)
            {
                if (auto split_item = std::dynamic_pointer_cast<DockNodeTabItem>(node->_tab->ActiveItem()))
                    FindHoverSplitNodes(split_item->Node(), pos, vertical_out, horizontal_out);
            }
        }

        void DockManager::HandleNodeResize()
        {
            const Vector2f mouse_global_pos = Input::GetGlobalMousePos();
            DockNode *hover_vertical_split = nullptr;
            DockNode *hover_horizontal_split = nullptr;
            for (auto &it : _float_nodes)
            {
                auto &[window, nodes] = it;
                if (Application::FocusedWindow() != window)
                    continue;
                const Vector2f pos = GlobalToWindowPos(window, mouse_global_pos);
                UI::Widget *top_hover_widget = FindTopHoverWidget(window, pos);
                for (auto &n : nodes)
                {
                    if (!n || !n->_is_valid)
                        continue;
                    if (top_hover_widget != nullptr && !n->ContainsWidget(top_hover_widget))
                        continue;
                    if (IsFocusedNodeBlockingInteraction(n.get(), _focused_node, pos))
                        continue;
                    FindHoverSplitNodes(n, pos, &hover_vertical_split, &hover_horizontal_split);
                }
            }
            const bool has_hover_split = hover_vertical_split != nullptr || hover_horizontal_split != nullptr;
            const bool prioritize_split_resize = has_hover_split && _resizing_node == nullptr &&
                                                 _adj_split_node == nullptr && _drag_move_node == nullptr;
            auto find_resize_edge = [&](DockNode **out) -> u32
            {
                u32 resize_dir = 0u;
                for (auto &it: _float_nodes)
                {
                    auto &[window, nodes] = it;
                    if (Application::FocusedWindow() != window)
                        continue;
                    for (auto &n: nodes)
                    {
                        if (!n || !n->_is_valid)
                            continue;
                        if (n->_flags & EDockWindowFlag::kFullSize)
                            continue;
                        if (n->_flags & EDockWindowFlag::kNoResize)
                            continue;
                        Vector2f pos = GlobalToWindowPos(window, mouse_global_pos);
                        // Resize handles are deliberately outside the node rectangle. The widget under
                        // that strip belongs to the window behind it, so it must not veto this hit test.
                        resize_dir = n->HoverEdge(pos, out);
                        if (resize_dir == 0u)
                            continue;
                        if (IsFocusedNodeBlockingInteraction(n.get(), _focused_node, pos))
                            continue;
                        return resize_dir;
                    }
                }
                return 0u;
            };

            DockNode *hover_edge_node = nullptr;
            const u32 hover_resize_dir = prioritize_split_resize ? 0u : find_resize_edge(&hover_edge_node);
            const bool mouse_available_for_resize = UI::UIManager::Get()->_capture_target == nullptr;
            if (InputRouteState::Get().IsOwnedBy(InputChannel::kMouse, EInputOwner::kImGui) && _resizing_node == nullptr &&
                !prioritize_split_resize && _adj_split_node == nullptr)
            {
                if (hover_resize_dir == 0u)
                {
                    _resizing_edge_dir = 0u;
                    UpdateResizeMouseCursor(0u);
                    return;
                }
            }
            if (UI::DragDropManager::Get().GetPayload().has_value())
            {
                ReleaseMouseCapture();
                return;
            }
            bool can_resize = _adj_split_node == nullptr && _drag_move_node == nullptr &&
                              (_resizing_node != nullptr || mouse_available_for_resize);
            bool can_adjust_split = _resizing_node == nullptr && _drag_move_node == nullptr;
            bool can_drag_move = _resizing_node == nullptr && _adj_split_node == nullptr;
            if (can_resize)
            {
                if (_resizing_node)
                {
                    UpdateResizeMouseCursor(_resizing_edge_dir);
                    auto delta = Input::GetMousePosDelta();
                    if (_resizing_edge_dir & EHoverEdgeDir::kRight)
                        _resizing_node->_size = _resizing_node->_size + delta * Vector2f{1.0f, 0.0f};
                    if (_resizing_edge_dir & EHoverEdgeDir::kBottom)
                        _resizing_node->_size = _resizing_node->_size + delta * Vector2f{0.0f, 1.0f};
                    if (_resizing_edge_dir & EHoverEdgeDir::kLeft)
                    {
                        _resizing_node->_position = _resizing_node->_position + delta * Vector2f{1.0f, 0.0f};
                        _resizing_node->_size = _resizing_node->_size - delta * Vector2f{1.0f, 0.0f};
                    }
                    if (_resizing_edge_dir & EHoverEdgeDir::kTop)
                    {
                        _resizing_node->_position = _resizing_node->_position + delta * Vector2f{0.0f, 1.0f};
                        _resizing_node->_size = _resizing_node->_size - delta * Vector2f{0.0f, 1.0f};
                    }
                    if (!Input::IsKeyDown(EKey::kLBUTTON))
                        ReleaseMouseCapture();
                }
                else
                {
                    static DockNode *s_last_edge_hover_node = nullptr;
                    DockNode *edge_hover_node = hover_edge_node;
                    _resizing_edge_dir = hover_resize_dir;
                    UpdateResizeMouseCursor(_resizing_edge_dir);
                    if (edge_hover_node != s_last_edge_hover_node)
                    {
                        LOG_INFO("UpdateResizeMouseCursor: {}", _resizing_edge_dir);
                    }
                    s_last_edge_hover_node = edge_hover_node;
                }
            }
            can_adjust_split = _resizing_node == nullptr && _drag_move_node == nullptr;
            if (can_adjust_split)
            {
                //adjust split ratio
                if (_adj_split_node)
                {
                    if (Input::IsKeyDown(EKey::kLBUTTON))
                    {
                        const Vector2f pos = Input::GetMousePos(_adj_split_node->_own_window);
                        auto adjust_split = [pos](DockNode *split_node)
                        {
                            if (split_node == nullptr)
                                return;
                            const Vector2f node_pos = split_node->_position;
                            const Vector2f node_size = split_node->_size;
                            const f32 axis_size = split_node->_is_vertical_split ? node_size.x : node_size.y;
                            if (axis_size <= 0.0f)
                                return;
                            const f32 axis_pos = split_node->_is_vertical_split ? pos.x - node_pos.x : pos.y - node_pos.y;
                            split_node->AdjustSplitRatio(axis_pos / axis_size);
                        };
                        adjust_split(_adj_split_node);
                        adjust_split(_adj_split_node_cross);
                        UpdateSplitResizeMouseCursor(_adj_split_node, _adj_split_node_cross);
                    }
                    else
                    {
                        _adj_split_node = nullptr;
                        _adj_split_node_cross = nullptr;
                    }
                }
                else
                {
                    if (has_hover_split)
                    {
                        DockNode *split_node = hover_vertical_split != nullptr ? hover_vertical_split : hover_horizontal_split;
                        DockNode *cross_node = hover_vertical_split != nullptr && hover_horizontal_split != nullptr
                                                    ? hover_horizontal_split : nullptr;
                        UpdateSplitResizeMouseCursor(split_node, cross_node);
                    }
                }
            }
            //handle node move
            can_drag_move = _resizing_node == nullptr && _adj_split_node == nullptr;
            if (can_drag_move && _drag_move_node != nullptr)
            {
                Vector2f pos = Input::GetMousePosAccurate(_drag_move_node->_own_window);
                if (Input::IsKeyDown(EKey::kLBUTTON))
                {
                    _drag_move_node->_position = pos + _drag_start_offset;
                    //处理节点拖出窗口
                    Vector2f wsize = {(f32) _drag_move_node->_own_window->GetWidth(), (f32) _drag_move_node->_own_window->GetHeight()};
                    if (pos.x < 0.0f || pos.y < 0.0f || pos.x > wsize.x || pos.y > wsize.y)
                    {
                        auto drag_node = _drag_move_node->shared_from_this();
                        f32 dx = pos.x - _drag_move_node->_position.x;
                        u16 ww = (u16) _drag_move_node->_size.x, wh = (u16) _drag_move_node->_size.y;
                        auto new_window = CreateNewWindow("DockWindow", ww, wh);
                        auto gm_pos = Input::GetGlobalMousePos();
                        new_window->SetPosition((i32) (gm_pos.x - dx), (i32) gm_pos.y);
                        UntrackFloatNode(_drag_move_node);
                        _drag_move_node->SetOwnWindow(new_window);
                        _drag_move_node->_position = Vector2f::kZero;
                        _drag_move_node->_flags |= EDockWindowFlag::kNoMove | EDockWindowFlag::kNoResize | EDockWindowFlag::kFullSize;
                        TryAddFloatNode(drag_node);
                        _roots.push_back(_drag_move_node);
                        if (_drag_move_node->_window)
                        {
                            _drag_move_node->_window->SetFocus(false);
                            _focused_node = nullptr;
                        }
                        ReleaseMouseCapture();
                    }
                }
                else
                    ReleaseMouseCapture();
            }
        }
        void DockManager::TryAddFloatNode(Ref<DockNode> &n)
        {
            if (!_float_nodes.contains(n->_own_window))
                _float_nodes[n->_own_window] = {};
            auto& nodes = _float_nodes[n->_own_window];
            auto it = std::ranges::find_if(nodes, [&](Ref<DockNode> e) -> bool
                                           { return n.get() == e.get(); });
            if (it == nodes.end())
                nodes.push_back(n);
            NormalizeWindowWidgetOrder(n->_own_window);
        }
        void DockManager::TryRemoveFloatNode(DockNode *n)
        {
            if (_float_nodes.contains(n->_own_window))
            {
                std::erase_if(_float_nodes[n->_own_window], [&](Ref<DockNode> e) -> bool
                                               { return n == e.get(); });
            }
        }
        void DockManager::CleanupWindowIfEmpty(Window *window)
        {
            if (window == nullptr || window == Application::Get().GetWindowPtr())
                return;

            auto nodes_it = _float_nodes.find(window);
            if (nodes_it != _float_nodes.end())
            {
                std::erase_if(nodes_it->second, [](const Ref<DockNode> &node) -> bool
                              { return !node || !node->_is_valid || node->IsEmpty(); });
                if (!nodes_it->second.empty())
                    return;
                _float_nodes.erase(nodes_it);
            }

            Render::GraphicsContext::Get().UnRegisterWindow(window);
            auto window_it = std::find_if(_float_windows.begin(), _float_windows.end(), [window](const Scope<Window> &item) -> bool
                                          { return item.get() == window; });
            if (window_it != _float_windows.end())
            {
                (*window_it)->SetEventHandler([](Event &) {});
                _float_windows.erase(window_it);
            }
        }
        void DockManager::UntrackFloatNode(DockNode *n)
        {
            if (n == nullptr)
                return;
            TryRemoveFloatNode(n);
            std::erase_if(_roots, [&](DockNode *root) -> bool
                          { return root == n; });
            n->_flags &= ~EDockWindowFlag::kFullSize;
        }
        void DockManager::DetachFromTree(DockNode *n)
        {
            auto parent = n->_parent;
            if (parent == nullptr)//悬浮节点，标记移除即可
            {
                if (auto it = std::find_if(_roots.begin(), _roots.end(), [=](DockNode* item){return item == n;}); it != _roots.end())
                {
                    n->_flags &= ~EDockWindowFlag::kFullSize;
                    _roots.erase(it);
                }
                //MarkDeleteNode(n);
            }
            else
            {
                Vector2f mouse_pos = Input::GetMousePos();
                //移除leaf节点，由于根为空之前处理了，所以他的父节点一定是split，分两种情况处理
                //1. 兄弟节点非空：将当前节点添加至悬浮，兄弟节点提升至父节点
                //2. 兄弟为空节点：将当前节点添加至悬浮，移除父节点的引用
                if (n->_type == DockNode::EType::kLeaf || n->_type == DockNode::EType::kTab)
                {
                    AL_ASSERT(parent->_type == DockNode::EType::kSplit);
                    if (!parent->_left->IsEmpty() && !parent->_right->IsEmpty())
                    {
                        bool is_cur_left = (parent->_left.get() == n);
                        Ref<DockNode> sibling = is_cur_left ? parent->_right : parent->_left;
                        Ref<DockNode> cur_ref = is_cur_left ? parent->_left : parent->_right;
                        Vector2f total_size = parent->_size;
                        Vector2f left_pos = is_cur_left ? cur_ref->_position : sibling->_position;
                        Vector2f right_pos = is_cur_left ? sibling->_position : cur_ref->_position;
                        parent->_left.reset();
                        parent->_right.reset();
                        cur_ref->_parent = nullptr;
                        cur_ref->_flags &= ~EDockWindowFlag::kFullSize;
                        cur_ref->SetDockedWindowState(false);
                        TryAddFloatNode(cur_ref);
                        if (auto grand_parent = parent->_parent)
                        {
                            if (grand_parent->_left.get() == parent)
                                grand_parent->_left = sibling;
                            else if (grand_parent->_right.get() == parent)
                                grand_parent->_right = sibling;
                            else {}
                            sibling->_parent = grand_parent;
                        }
                        else// parent is root
                        {
                            sibling->_parent = nullptr;
                            sibling->_flags |= parent->_flags & EDockWindowFlag::kFullSize;
                            sibling->SetDockedWindowState((sibling->_flags & EDockWindowFlag::kFullSize) != 0u);
                            if (!sibling->IsEmpty())
                            {
                                TryAddFloatNode(sibling);
                                if ((sibling->_flags & EDockWindowFlag::kFullSize) != 0u)
                                    _roots.push_back(sibling.get());
                            }
                            TryRemoveFloatNode(parent);
                            parent->_flags &= ~EDockWindowFlag::kFullSize;
                            std::erase_if(_roots, [&](DockNode *n) -> bool
                                          { return n == parent; });
                        }
                        sibling->_size = total_size;
                        if (is_cur_left)
                            sibling->_position = left_pos;
                        sibling->UpdateLayout(0.0f);
                        cur_ref->_position = mouse_pos;
                        cur_ref->UpdateLayout(0.0f);
                    }
                    else// only one child
                    {
                        Ref<DockNode> cur_ref = parent->_left ? parent->_left : parent->_right;
                        if (parent->_left)
                            parent->_left.reset();
                        else
                            parent->_right.reset();
                        if (auto grand_parent = parent->_parent; grand_parent != nullptr)
                        {
                            if (grand_parent->_left.get() == parent)
                                grand_parent->_left.reset();
                            else if (grand_parent->_right.get() == parent)
                                grand_parent->_right.reset();
                        }
                        else// parent is root
                        {
                            TryRemoveFloatNode(parent);
                        }
                        cur_ref->_parent = nullptr;
                        cur_ref->_flags &= ~EDockWindowFlag::kFullSize;
                        cur_ref->SetDockedWindowState(false);
                        TryAddFloatNode(cur_ref);
                        cur_ref->_position = mouse_pos;
                    }
                    parent->_parent = nullptr;// 避免悬空
                }
                else
                {
                    AL_ASSERT(false);
                }
            }
        }

        void DockManager::WindowEventHandler(Event &e)
        {
            Application::Get().UpdatePlatformEventState(e);
            static Vector2f s_pre_mouse_down_pos = Input::GetGlobalMousePos();
            if (e.GetEventType() == EEventType::kWindowClose)
            {
                auto close_event = dynamic_cast<WindowCloseEvent *>(&e);
                auto it = std::find_if(_float_windows.begin(), _float_windows.end(), [&](Scope<Window> &w) -> bool
                                       { return w->GetNativeWindowPtr() == close_event->_handle; });
                if (it != _float_windows.end())
                {
                    AL_ASSERT(false);
                    //Render::GraphicsContext::Get().UnRegisterWindow(it->get());
                    //auto itt = std::find_if(_float_nodes.begin(), _float_nodes.end(), [&](Ref<DockNode> &n) -> bool
                    //                        { return n->_own_window == it->get(); });
                    //TryRemoveFloatNode(itt->get());
                    //_float_windows.erase(it);
                }
            }
            else if (e.GetEventType() == EEventType::kWindowResize)
            {
                auto resize_event = dynamic_cast<WindowResizeEvent *>(&e);
                auto it = std::find_if(_float_windows.begin(), _float_windows.end(), [&](Scope<Window> &w) -> bool
                                       { return w->GetNativeWindowPtr() == resize_event->_handle; });
                if (it != _float_windows.end())
                {
                    Render::GraphicsContext::Get().ResizeSwapChain(it->get()->GetNativeWindowPtr(), resize_event->_width, resize_event->_height);
                }
                for (auto &n: _roots)
                {
                    const Vector4f area = n->_own_window == &Application::Get().GetWindow() ? MainDockArea() : Vector4f{Vector2f::kZero, Vector2f{(f32) n->_own_window->GetWidth(), (f32) n->_own_window->GetHeight()}};
                    n->_position = area.xy;
                    n->_size = area.zw;
                }
            }
            else if (e.GetEventType() == EEventType::kMouseButtonPressed)
            {
                s_pre_mouse_down_pos = Input::GetGlobalMousePos();
            }
            else if (e.GetEventType() == EEventType::kMouseMoved && !HasMouseCapture())
            {
                if (Distance(Input::GetGlobalMousePos(), s_pre_mouse_down_pos) > 20.0f)
                {
                    Window *main_window = &Application::Get().GetWindow();
                    auto [mw_x, mw_y] = main_window->GetClientPosition();
                    auto [mw_w, mw_h] = main_window->GetWindowSize();
                    for (auto &n: _float_nodes[e._window])
                    {
                        if (!n || !n->_is_valid)
                            continue;
                        if (n->_own_window != main_window && n->_own_window == Application::FocusedWindow())
                        {
                            if (Input::IsKeyDown(EKey::kLBUTTON))
                            {
                                auto [x, y] = n->_own_window->GetClientPosition();
                                if (x >= mw_x && x <= mw_x + mw_w && y >= mw_y && y <= mw_y + mw_h)
                                {
                                    BeginFloatNode(n.get());
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            else {}
            OnEvent(e);
            static UI::UILayer *layer = nullptr;
            if (layer == nullptr)
            {
                for (Layer *l: Application::Get().GetLayerStack())
                {
                    if (dynamic_cast<UI::UILayer *>(l))
                    {
                        layer = dynamic_cast<UI::UILayer *>(l);
                        break;
                    }
                }
            }
            if (!e.Handled())
                layer->OnEvent(e);
        }
        
        void DockManager::WriteNodeData(DockNode *n)
        {
            if (!n || !n->_is_valid)
                return;
            DockNodeData data;
            data._type_id = n->_type == DockNode::EType::kLeaf && n->_window ? n->_window->GetType()->FullName() : "null";
            data._id = n->_id;
            data._parent_id = n->_parent ? n->_parent->_id : UINT32_MAX;
            data._type = static_cast<u32>(n->_type);
            data._is_vertical_split = n->_is_vertical_split;
            data._split_ratio = n->_split_ratio;
            data._position = n->_position;
            data._size = n->_size;
            data._flags = n->_flags;
            auto [wx, wy] = n->_own_window->GetWindowPosition();
            data._window_position = {(f32) wx, (f32)wy};
            auto [ww, wh] = n->_own_window->GetWindowSize();
            data._window_size = {(f32) ww, (f32) wh};
            data._title = n->_type == DockNode::EType::kLeaf && n->_window ? n->_window->GetTitle() : "null";
            data._window_id = n->_own_window == Application::Get().GetWindowPtr() ? 0u : 999u;
            data._tab_id = n->_tab ? std::to_string(n->_tab->ActiveIndex()) : "";
            if (n->_type == DockNode::EType::kLeaf && n->_window)
            {
                SaveWindowPlacement(n->_window.get(), n);
                SaveDockLayoutState(n->_window.get(), data._window_state);
            }
            _node_data_array._node_data.push_back(data);
            if (n->_type == DockNode::EType::kSplit)
            {
                WriteNodeData(n->_left.get());
                WriteNodeData(n->_right.get());
            }
            else if (n->_type == DockNode::EType::kTab && n->_tab)
            {
                for (const auto &tab_item : *n->_tab)
                {
                    if (auto tab_window = std::dynamic_pointer_cast<DockWindow>(tab_item))
                    {
                        DockNodeData child;
                        child._type_id = tab_window->GetType()->FullName();
                        child._id = _next_serialize_node_id++;
                        child._parent_id = n->_id;
                        child._type = static_cast<u32>(DockNode::EType::kLeaf);
                        child._is_vertical_split = true;
                        child._split_ratio = 0.5f;
                        child._title = tab_window->GetTitle();
                        child._window_id = data._window_id;
                        child._tab_id = "";
                        child._window_position = data._window_position;
                        child._window_size = data._window_size;
                        child._position = n->_position;
                        child._size = n->_size;
                        child._flags = tab_window->_flags;
                        SaveWindowPlacement(tab_window.get(), n);
                        SaveDockLayoutState(tab_window.get(), child._window_state);
                        _node_data_array._node_data.push_back(child);
                    }
                    else if (auto split_item = std::dynamic_pointer_cast<DockNodeTabItem>(tab_item))
                    {
                        auto split_node = split_item->Node();
                        DockNode *old_parent = split_node->_parent;
                        split_node->_parent = n;
                        WriteNodeData(split_node.get());
                        split_node->_parent = old_parent;
                    }
                }
            }
        }

        void DockManager::SaveWindowPlacement(DockWindow *dock, DockNode *node)
        {
            if (dock == nullptr || node == nullptr || node->_own_window == nullptr)
                return;

            DockWindowPlacement placement;
            placement._dock_id = dock->GetDockPersistenceId();
            if (placement._dock_id.empty())
                return;
            placement._position = node->_position;
            placement._size = node->_size;
            auto [window_x, window_y] = node->_own_window->GetWindowPosition();
            placement._native_window_position = {(f32) window_x, (f32) window_y};
            auto [window_width, window_height] = node->_own_window->GetWindowSize();
            placement._native_window_size = {(f32) window_width, (f32) window_height};
            placement._is_external_window = node->_own_window != Application::Get().GetWindowPtr();
            _window_placement_map[placement._dock_id] = std::move(placement);
        }

        void DockManager::SyncWindowPlacementArray()
        {
            _node_data_array._window_placements.clear();
            for (const auto &[dock_id, placement] : _window_placement_map)
                _node_data_array._window_placements.push_back(placement);
        }

        Window *DockManager::CreateNewWindow(String title, u16 ww, u16 wh, bool is_sync)
        {
            if (!is_sync && !Application::IsMainThread())
            {
                std::future<Scope<Window>> w = Application::Get()._dispatcher.Enqueue([&]()
                                                                                      { return WindowFactory::Create(ToWChar(title), ww, wh, EWindowFlags::kWindow_NoTitleBar); });
                _float_windows.emplace_back(std::move(w.get()));
            }
            else
            {
                _float_windows.emplace_back(WindowFactory::Create(ToWChar(title), ww, wh, EWindowFlags::kWindow_NoTitleBar));
            }
            auto new_window = _float_windows.back().get();
            new_window->SetEventHandler(std::bind(&DockManager::WindowEventHandler, this, std::placeholders::_1));
            Render::GraphicsContext::Get().RegisterWindow(new_window);
            return new_window;
        }

        void DockManager::BringNodeWidgetsToFront(DockNode *node)
        {
            if (node == nullptr)
                return;
            auto *ui_manager = UI::UIManager::Get();
            switch (node->_type)
            {
            case DockNode::EType::kLeaf:
                if (node->_window)
                {
                    ui_manager->BringToFrontSilently(node->_window->ContentWidget());
                    ui_manager->BringToFrontSilently(node->_window->TitleWidget());
                }
                break;
            case DockNode::EType::kSplit:
                BringNodeWidgetsToFront(node->_left.get());
                BringNodeWidgetsToFront(node->_right.get());
                break;
            case DockNode::EType::kTab:
                if (node->_tab)
                {
                    if (auto active_item = node->_tab->ActiveItem())
                    {
                        if (auto split_item = std::dynamic_pointer_cast<DockNodeTabItem>(active_item))
                            BringNodeWidgetsToFront(split_item->Node().get());
                        else if (auto *primary_window = active_item->PrimaryWindow())
                        {
                            ui_manager->BringToFrontSilently(primary_window->ContentWidget());
                            ui_manager->BringToFrontSilently(primary_window->TitleWidget());
                        }
                    }
                    ui_manager->BringToFrontSilently(node->_tab->TitleWidget());
                }
                break;
            default:
                break;
            }
        }

        void DockManager::SendNodeWidgetsToBack(DockNode *node)
        {
            if (node == nullptr)
                return;
            auto *ui_manager = UI::UIManager::Get();
            switch (node->_type)
            {
            case DockNode::EType::kLeaf:
                if (node->_window)
                {
                    ui_manager->SendToBack(node->_window->TitleWidget());
                    ui_manager->SendToBack(node->_window->ContentWidget());
                }
                break;
            case DockNode::EType::kSplit:
                SendNodeWidgetsToBack(node->_right.get());
                SendNodeWidgetsToBack(node->_left.get());
                break;
            case DockNode::EType::kTab:
                if (node->_tab)
                {
                    ui_manager->SendToBack(node->_tab->TitleWidget());
                    if (auto active_item = node->_tab->ActiveItem())
                    {
                        if (auto split_item = std::dynamic_pointer_cast<DockNodeTabItem>(active_item))
                            SendNodeWidgetsToBack(split_item->Node().get());
                        else if (auto *primary_window = active_item->PrimaryWindow())
                        {
                            ui_manager->SendToBack(primary_window->TitleWidget());
                            ui_manager->SendToBack(primary_window->ContentWidget());
                        }
                    }
                }
                break;
            default:
                break;
            }
        }

        void DockManager::NormalizeWindowWidgetOrder(Window *window)
        {
            if (window == nullptr)
                return;
            auto it = _float_nodes.find(window);
            if (it == _float_nodes.end())
                return;
            for (auto &node : it->second)
            {
                if (node && (node->_flags & EDockWindowFlag::kFullSize) != 0u)
                    SendNodeWidgetsToBack(node.get());
            }
        }
        
        void DockManager::RequestFocus(DockWindow *w)
        {
            if (w == nullptr)
                return;

            if (_focused_window != nullptr && _focused_window != w)
                _focused_window->SetFocus(false);

            _focused_node = FindNodeByWindow(w);
            _focused_window = _focused_node ? w : nullptr;
            if (_focused_node == nullptr)
                return;
            if ((_focused_node->_flags & EDockWindowFlag::kFullSize) == 0u)
                BringNodeWidgetsToFront(_focused_node);
            NormalizeWindowWidgetOrder(_focused_node->_own_window);
        }

        bool DockManager::IsFocused(DockWindow *w) const
        {
            return w != nullptr && _focused_window == w;
        }

        bool DockManager::IsTabItemFocused(const IDockTabItem *item) const
        {
            return item != nullptr && _focused_window != nullptr && item->ContainsWindow(_focused_window);
        }

        DockNode *DockManager::FindNodeByWindow(DockWindow *w)
        {
            std::function<DockNode *(DockNode *)> find_node = [&](DockNode *node) -> DockNode *
            {
                if (node == nullptr || !node->_is_valid)
                    return nullptr;
                if (node->_type == DockNode::EType::kLeaf)
                    return node->_window.get() == w ? node : nullptr;
                if (node->_type == DockNode::EType::kSplit)
                {
                    if (DockNode *found = find_node(node->_left.get()))
                        return found;
                    return find_node(node->_right.get());
                }
                if (node->_tab)
                {
                    for (const auto &item : *node->_tab)
                    {
                        if (auto window_item = std::dynamic_pointer_cast<DockWindow>(item); window_item.get() == w)
                            return node;
                        if (auto split_item = std::dynamic_pointer_cast<DockNodeTabItem>(item))
                        {
                            if (DockNode *found = find_node(split_item->Node().get()))
                                return found;
                        }
                    }
                }
                return nullptr;
            };
            for (auto &it: _float_nodes)
            {
                auto &[window, nodes] = it;
                for (auto &n: nodes)
                {
                    if (DockNode *found = find_node(n.get()))
                        return found;
                }
            }
            return nullptr;
        }
    }// namespace Editor
}// namespace Ailu
