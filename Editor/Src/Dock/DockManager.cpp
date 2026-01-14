#include "Dock/DockManager.h"
#include "Common/EditorStyle.h"
#include "EditorApp.h"
#include "Framework/Common/Input.h"
#include "UI/Basic.h"
#include "UI/UIFramework.h"
#include "UI/UILayer.h"
#include "UI/UIRenderer.h"
#include <ranges>

#include "Objects/JsonArchive.h"

namespace Ailu
{
    namespace Editor
    {
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
            void UpdateLayout(f32 dt)
            {
                if (_type == EType::kLeaf)
                {
                    if (!_window)
                        return;
                    _window->SetPosition(_position);
                    _window->SetSize(_size);
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
                    _tab->_position = _position;
                    _tab->_size = _size;
                    _tab->Update(dt);
                }
            }

            void Update(f32 dt)
            {
                if (_type == EType::kSplit)
                {
                    _left->Update(dt);
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
                    _left->SetOwnWindow(w);
                    _right->SetOwnWindow(w);
                }
                else
                {
                    auto new_target = RenderTexture::WindowBackBuffer(w);
                    _tab->TitleWidget()->BindOutput(new_target);
                    _tab->TitleWidget()->SetParent(w);
                    for (auto &t : *_tab)
                    {
                        t->ContentWidget()->BindOutput(new_target);
                        t->TitleWidget()->BindOutput(new_target);
                        t->ContentWidget()->SetParent(w);
                        t->TitleWidget()->SetParent(w);
                    }
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

        void DockNode::DockTo(DockNode *other, EDockArea area)
        {
            //if (_type != EType::kLeaf)
            //{
            //    LOG_WARNING("Dock to: self must be leaf node!");
            //    return;
            //}
            if (area == EDockArea::kFloat)
                return;
            if (other == nullptr)//停靠至主窗口
            {
                auto make_split_node = [](DockNode *n, bool is_vertical, bool is_prime)
                {
                    auto &main_window = Application::Get().GetWindow();
                    auto size = Vector2f{(f32) main_window.GetWidth(), (f32) main_window.GetHeight()};
                    auto new_node = n->Clone();
                    if (is_prime)
                    {
                        n->_left = new_node;
                        n->_right = MakeRef<DockNode>();
                        n->_right->_parent = n;
                    }
                    else
                    {
                        n->_right = new_node;
                        n->_left = MakeRef<DockNode>();
                        n->_left->_parent = n;
                    }
                    n->_type = EType::kSplit;
                    n->_size = size;
                    n->_position = Vector2f::kZero;
                    n->_is_vertical_split = is_vertical;
                    n->_window = nullptr;
                    new_node->_parent = n;
                };
                if (area == EDockArea::kLeft)
                    make_split_node(this, true, true);
                else if (area == EDockArea::kRight)
                    make_split_node(this, true, false);
                else if (area == EDockArea::kTop)
                    make_split_node(this, false, true);
                else if (area == EDockArea::kBottom)
                    make_split_node(this, false, false);
                else if (area == EDockArea::kCenter)
                {
                    auto &main_window = Application::Get().GetWindow();
                    _size = Vector2f{(f32) main_window.GetWidth(), (f32) main_window.GetHeight()};
                    _position = Vector2f::kZero;
                }
                _flags |= EDockWindowFlag::kFullSize;
                DockManager::Get()._roots.push_back(this);
                UpdateLayout(0.0f);
            }
            else
            {
                const static auto make_split_node = [](DockNode *first, DockNode *second, bool is_vertical, bool is_second_prime)
                {
                    first->_type = EType::kSplit;
                    first->_split_ratio = 0.5f;
                    first->_is_vertical_split = is_vertical;

                    first->_left = MakeRef<DockNode>();
                    first->_left->_type = EType::kLeaf;
                    first->_left->_window = first->_window;
                    first->_left->_parent = first;
                    first->_left->_split_ratio = 0.5f;
                    first->_left->_is_vertical_split = is_vertical;
                    if (first->_left->_window)
                        first->_left->_window->_flags |= EDockWindowFlag::kNoMove | EDockWindowFlag::kNoResize;

                    first->_right = MakeRef<DockNode>();
                    first->_right->_type = EType::kLeaf;
                    first->_right->_window = second->_window;
                    if (first->_right->_window)
                        first->_right->_window->_flags |= EDockWindowFlag::kNoMove | EDockWindowFlag::kNoResize;
                    first->_right->_parent = first;
                    first->_right->_split_ratio = 0.5f;
                    first->_right->_is_vertical_split = is_vertical;
                    first->_window = nullptr;
                };

                if (other->_type == EType::kLeaf)
                {
                    if (area == EDockArea::kCenter)
                    {
                        if (other->_window == nullptr)
                        {
                            other->_window = this->_window;
                            DockManager::Get().MarkDeleteNode(this);
                        }
                        else//将目标node转为tab
                        {
                            Ref<DockWindow> other_window = std::move(other->_window);
                            other->_type = EType::kTab;
                            other->_tab = MakeRef<DockTab>();
                            other->_window = nullptr;
                            other->_tab->AddTab(other_window);
                            other->_tab->AddTab(_window);
                            DockManager::Get().MarkDeleteNode(this);
                        }
                    }
                    else if (area == EDockArea::kLeft)
                    {
                        make_split_node(other, this, true, true);
                    }
                    else if (area == EDockArea::kRight)
                    {
                        make_split_node(other, this, true, false);
                    }
                    else if (area == EDockArea::kTop)
                    {
                        make_split_node(other, this, false, true);
                    }
                    else if (area == EDockArea::kBottom)
                    {
                        make_split_node(other, this, false, false);
                    }
                }
                else if (other->_type == EType::kTab)
                {
                    if (other->_tab)
                    {
                        if (other->_tab->AddTab(_window))
                        {
                            _parent = other;
                        }
                    }
                    else
                        LOG_ERROR("DockNode::DockTo: other's tab is null");
                }
                else
                {
                    AL_ASSERT(false);
                }
            }
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
                bool ret = false;
                if (_left) ret |= _left->HoverDragArea(pos);
                if (_right) ret |= _right->HoverDragArea(pos);
                return ret;
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
            ECursorType cursor_type = ECursorType::kArrow;
            if (!resize_dir)
            {
                Application::Get().SetCursor(cursor_type);
                return;
            }
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


        static DockManager *g_pDockMgr = nullptr;
        void DockManager::Init()
        {
            AL_ASSERT(g_pDockMgr == nullptr);
            g_pDockMgr = new DockManager();
        }
        void DockManager::Shutdown()
        {
            DESTORY_PTR(g_pDockMgr);
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
                        n->_size = {(f32) n->_own_window->GetWidth(), (f32) n->_own_window->GetHeight()};
                }
            };
            _dock_layout_path = EditorApp::GetEditorRootPath() + L"dock_layout.json";
            JsonArchive ar;
            ar.Load(_dock_layout_path);
            for (auto p: DockNodeDataArray::StaticType()->GetProperties())
                p.Deserialize(&_node_data_array, ar);
            for (auto n: _node_data_array._node_data)
            {
                auto t = Type::Find(n._type_id);
                if (!t)
                {
                    LOG_ERROR("DockManager::DockManager: can't find type {}", n._type_id);
                    continue;
                }
                if (n._type == (u32)DockNode::EType::kLeaf)
                {
                    auto w = std::shared_ptr<DockWindow>(t->CreateInstance<DockWindow>());
                    w->SetTitle(n._title);
                    w->SetPosition(n._position);
                    w->SetSize(n._size);
                    w->_on_get_focus += [this](DockWindow *w)
                    {
                        _focused_node = FindNodeByWindow(w);
                        LOG_INFO("Focused dock node change to {}", _focused_node->_window ? _focused_node->_window->GetTitle() : "split/tab node");
                    };
                    w->_on_lost_focus += [this](DockWindow *w)
                    {
                        if (_focused_node && _focused_node->_window.get() == w)
                        {
                            _focused_node = nullptr;
                            LOG_INFO("Focused dock node lost!");
                        }
                    };
                    //same with AddDock
                    UI::UIManager::Get()->RegisterWidget(w->_title_widget);
                    UI::UIManager::Get()->RegisterWidget(w->_content_widget);
                    auto leaf_node = MakeRef<DockNode>(Application::Get().GetWindowPtr());
                    leaf_node->_window = w;
                    leaf_node->_position = w->_position;
                    leaf_node->_size = w->_size;
                    leaf_node->_flags = n._flags;
                    if (n._window_id != 0)
                    {
                        auto window = CreateNewWindow(w->GetTitle(), (u16) n._window_size.x, (u16) n._window_size.y, true);
                        leaf_node->SetOwnWindow(window);
                        window->SetPosition((i32) n._window_position.x, (i32) n._window_position.y);
                    }
                    TryAddFloatNode(leaf_node);
                    if (leaf_node->_flags & EDockWindowFlag::kFullSize)
                        _roots.push_back(leaf_node.get());
                }
                else
                    LOG_WARNING("DockManager::DockManager: {} unknown node type {}", n._title,n._type);
            }
        }
        
        DockManager::~DockManager()
        {
            _node_data_array._node_data.clear();
            for (auto& it: _float_nodes)
            {
                auto &[w, nodes] = it;
                for (auto& n : nodes)
                    WriteNodeData(n.get());
            }
            JsonArchive ar;
            for (auto p : DockNodeDataArray::StaticType()->GetProperties())
            {
                p.Serialize(&_node_data_array, ar);
            }
            ar.Save(_dock_layout_path);
        }
        void DockManager::AddDock(Ref<DockWindow> dock)
        {
            UI::UIManager::Get()->RegisterWidget(dock->_title_widget);
            UI::UIManager::Get()->RegisterWidget(dock->_content_widget);

            auto leaf_node = MakeRef<DockNode>();
            leaf_node->_window = dock;
            leaf_node->_position = dock->_position;
            leaf_node->_size = dock->_size;
            dock->_on_get_focus += [this](DockWindow* w) {
                _focused_node = FindNodeByWindow(w);
                LOG_INFO("Focused dock node changed!");
            };
            TryAddFloatNode(leaf_node);
        }

        void DockManager::RemoveDock(DockWindow *dock)
        {
            AL_ASSERT(false);
            //if (auto it = std::find_if(_docks.begin(), _docks.end(), [&](DockWindow *d) -> bool
            //                           { return d == dock; });
            //    it != _docks.end())
            //{
            //    UI::UIRenderer::Get()->RemoveWidget(dock->_title_bar.get());
            //    UI::UIRenderer::Get()->RemoveWidget(dock->_content.get());
            //    _uiLayer->UnRegisterWidget(dock->_title_bar.get());
            //    _uiLayer->UnRegisterWidget(dock->_content.get());
            //    _docks.erase(it);
            //}
            ////if (auto it = std::find_if(_float_nodes.begin(), _float_nodes.end(), [&](Ref<DockNode> d) -> bool
            ////                           { return d->wi == dock; });
            ////    it != _space._float_windows.end())
            ////{
            ////    _space._float_windows.erase(it);
            ////}
            ////RemoveDockFromTree(dock, _space._root.get());
        }
        static void DrawTreeNode(DockNode *node, int depth, int &row)
        {
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
                Vector2f pos = Input::GetMousePos();
                if (_can_draw_float_preview)
                {
                    UI::UIRenderer::Get()->DrawQuad({pos - Vector2f{100.0f, 70.0f}, Vector2f{200.0f, 140.0f}}, g_editor_style._dock_hint_color);
                    OnWindowFloat();
                    if (!Input::IsKeyPressed(EKey::kLBUTTON) && _floating_preview_node)
                    {
                        EndFloatWindow(pos);
                        _can_draw_float_preview = false;
                    }
                }
                else
                {
                    f32 dx = abs(pos.x - _float_node_start_pos.x), dy = abs(pos.y - _float_node_start_pos.y);
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
            if (!node)
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
                if (node->_right)
                    FindFloatWindow(node->_right.get(), w, out);
            }
            else
            {
                if (node->_tab)
                {
                    if (node->_tab->Contains(w))
                        *out = node;
                }
            }
        }

        void DockManager::BeginFloatWindow(DockWindow *w)
        {
            if (_floating_preview_node || _resizing_edge_dir)
                return;
            _floating_preview_window = w;
            _is_any_floating = true;
            _floating_preview_node = nullptr;
            _is_float_on_cancel_area = false;
            for (auto &it: _float_nodes)
            {
                auto &[window, nodes] = it;
                for (auto &n: nodes)
                {
                    FindFloatWindow(n.get(), w, &_floating_preview_node);
                    if (_floating_preview_node)
                        break;
                }
            }
            if (_floating_preview_node)
            {
                _float_node_start_pos = Input::GetMousePos();
                _is_float_node_external_window = _floating_preview_node->_own_window != &Application::Get().GetWindow();
                LOG_INFO("DockManager::BeginFloatWindow: {}", w->_title->GetText());
            }
            else
            {
                LOG_ERROR("DockManager::BeginFloatWindow: window not belong to any node!");
            }
        }

        void DockManager::BeginFloatNode(DockNode *w)
        {
            if (_floating_preview_node || _resizing_edge_dir)
                return;
            if (w->_type != DockNode::EType::kLeaf)
                return;
            _floating_preview_window = w->_window.get();
            _is_any_floating = true;
            _floating_preview_node = w;
            _is_float_on_cancel_area = false;
            _float_node_start_pos = Input::GetMousePos(w->_own_window);
            _is_float_node_external_window = w->_own_window != &Application::Get().GetWindow();
            LOG_INFO("DockManager::BeginFloatWindow: {}", w->_window->_title->GetText());
        }

        void DockManager::EndFloatWindow(Vector2f drop_pos)
        {
            if (!_floating_preview_node)
                return;
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
                            Ref<DockWindow> active_tab = _floating_preview_node->_tab->ActiveTab();
                            const bool is_tab_empty = _floating_preview_node->_tab->RemoveTab(active_tab.get());
                            auto new_node = MakeRef<DockNode>();
                            new_node->_position = drop_pos;
                            new_node->_size = active_tab->_size;
                            new_node->_window = active_tab;
                            TryAddFloatNode(new_node);
                            if (is_tab_empty)
                                DetachFromTree(_floating_preview_node);
                        }
                    }
                    else
                    {
                        Ref<DockWindow> active_tab = _floating_preview_node->_tab->ActiveTab();
                        const bool is_tab_empty = _floating_preview_node->_tab->RemoveTab(active_tab.get());
                        auto new_node = MakeRef<DockNode>();
                        new_node->_position = drop_pos;
                        new_node->_size = active_tab->_size;
                        new_node->_window = active_tab;
                        new_node->DockTo(_dock_quad_hover_node, _dock_quad_hover_area);
                        if (is_tab_empty)
                            DetachFromTree(_floating_preview_node);
                    }
                    LOG_INFO("DockManager::EndFloatWindow dock to tab");
                }
                else
                {
                    LOG_ERROR("DockManager::EndFloatWindow dock to split node not allow")
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
                        Ref<DockWindow> active_tab = _floating_preview_node->_tab->ActiveTab();
                        const bool is_tab_empty = _floating_preview_node->_tab->RemoveTab(active_tab.get());
                        auto new_node = MakeRef<DockNode>();
                        new_node->_position = drop_pos;
                        new_node->_size = active_tab->_size;
                        new_node->_window = active_tab;
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
                        Ref<DockWindow> active_tab = _floating_preview_node->_tab->ActiveTab();
                        const bool is_tab_empty = _floating_preview_node->_tab->RemoveTab(active_tab.get());
                        auto new_node = MakeRef<DockNode>();
                        new_node->_position = drop_pos;
                        new_node->_size = active_tab->_size;
                        new_node->_window = active_tab;
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
                    else { AL_ASSERT(false); }
                }
            }
            _floating_preview_window = nullptr;
            _floating_preview_node = nullptr;
            _is_any_floating = false;
            _is_float_on_cancel_area = false;
            _can_draw_float_preview = false;
            LOG_INFO("DockManager::EndFloatWindow: at pos: {}", drop_pos.ToString());
        }

        void DockManager::MarkDeleteNode(DockNode *node)
        {
            node->_is_valid = false;
            _is_any_float_node_invalid = true;
        }

        static void FindHoverLeafNode(DockNode *node, Vector2f screen_mpos, DockNode **ret)
        {
            static auto gpos_to_wpos = [](DockNode *n, Vector2f gpos) -> Vector2f
            {
                auto [x, y] = n->_own_window->GetClientPosition();
                gpos.x -= (f32) x;
                gpos.y -= (f32) y;
                return gpos;
            };
            if (!node)
                return;
            if (node->_type == DockNode::EType::kLeaf)
            {
                if (node->IsHover(gpos_to_wpos(node, screen_mpos)))
                    *ret = node;
            }
            else if (node->_type == DockNode::EType::kSplit)
            {
                if (node->_left)
                    FindHoverLeafNode(node->_left.get(), screen_mpos, ret);
                if (node->_right)
                    FindHoverLeafNode(node->_right.get(), screen_mpos, ret);
            }
            else
            {
                if (node->_tab->IsHover(gpos_to_wpos(node, screen_mpos)))
                    *ret = node;
            }
        };

        void DockManager::OnWindowFloat()
        {
            Vector2f pos = Input::GetGlobalMousePos();
            //Vector2f pos = Input::GetMousePos();
            Vector2f size = Vector2f::kZero, start_pos = Vector2f::kZero;
            AL_ASSERT(_floating_preview_node != nullptr);
            _dock_quad_hover_node = nullptr;
            for (auto &it: _float_nodes)
            {
                auto &[window, nodes] = it;
                for (auto &node: nodes)
                {
                    FindHoverLeafNode(node.get(), pos, &_dock_quad_hover_node);
                    if (_dock_quad_hover_node)
                    {
                        if (_is_float_node_external_window && _dock_quad_hover_node == _floating_preview_node)
                        {
                            _dock_quad_hover_node = nullptr;
                            continue;
                        }
                        start_pos = _dock_quad_hover_node->_position;
                        size = _dock_quad_hover_node->_size;
                        break;
                    }
                }
            }

            if (_dock_quad_hover_node == nullptr)
            {
                auto &cur_window = Application::Get().GetWindow();
                size = {(f32) cur_window.GetWidth(), (f32) cur_window.GetHeight()};
                DrawPreviewDockArea(Input::GetMousePos(), size, start_pos);
            }
            else
            {
                if (_dock_quad_hover_node == _floating_preview_node)
                {
                    auto size = _dock_quad_hover_node->_size;
                    size.y = DockWindow::kTitleBarHeight * 2.0f;
                    Vector4f rect = {start_pos, size};
                    const Vector2f lpos = Input::GetMousePos(_dock_quad_hover_node->_own_window);
                    if (UI::UIElement::IsPointInside(lpos, rect))
                    {
                        UI::UIRenderer::Get()->DrawQuad({start_pos, size}, Vector4f(1.0f, 1.0f, 1.0f, 0.5f));
                        _is_float_on_cancel_area = true;
                    }
                    else
                    {
                        auto size = _dock_quad_hover_node->_size;
                        size.y -= DockWindow::kTitleBarHeight * 2.0f;
                        Vector2f pos = _dock_quad_hover_node->_position;
                        pos.y += DockWindow::kTitleBarHeight * 2.0f;
                        Vector2f center = pos + size * 0.5f;
                        auto r = UI::UIRenderer::Get();
                        Vector2f text_size = r->CalculateTextSize("Drop to detach");
                        r->DrawText("Drop to detach", center - text_size * 0.5f);
                        _is_float_on_cancel_area = false;
                    }
                }
                else
                {
                    DrawPreviewDockArea(Input::GetMousePos(), size, start_pos);
                }
            }
        }

        void DockManager::DrawPreviewDockArea(Vector2f pos, Vector2f w_size, Vector2f start_pos)
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
                r->DrawQuad(rect, c);
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

        static void FindHoverSplitNode(Ref<DockNode> node, Vector2f pos, DockNode **out)
        {
            if (node == nullptr || node->_type != DockNode::EType::kSplit)
                return;
            FindHoverSplitNode(node->_left, pos, out);
            if (*out != nullptr)
                return;
            FindHoverSplitNode(node->_right, pos, out);
            if (node->IsHoverSplitLine(pos))
                *out = node.get();
        }

        void DockManager::HandleNodeResize()
        {
            if (Input::IsInputBlock())
                return;
            bool can_resize = _adj_split_node == nullptr && _drag_move_node == nullptr && UI::UIManager::Get()->_capture_target == nullptr;
            bool can_adjust_split = _resizing_node == nullptr && _drag_move_node == nullptr;
            bool can_drag_move = _resizing_node == nullptr && _adj_split_node == nullptr;
            const Vector2f mouse_global_pos = Input::GetGlobalMousePos();
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
                    if (!Input::IsKeyPressed(EKey::kLBUTTON))
                    {
                        _resizing_node = nullptr;
                        _resizing_edge_dir = 0u;
                    }
                }
                else
                {
                    static DockNode *s_last_edge_hover_node = nullptr;
                    DockNode *edge_hover_node = nullptr;
                    _resizing_edge_dir = 0u;
                    for (auto &it: _float_nodes)
                    {
                        auto &[window, nodes] = it;
                        if (Application::FocusedWindow() != window)
                            continue;
                        for (auto &n: nodes)
                        {
                            if (n->_flags & EDockWindowFlag::kNoResize)
                                continue;
                            Vector2f pos = Input::GetMousePos(window);
                            if (_focused_node && _focused_node != n.get() && _focused_node->IsHover(pos))
                                continue;
                            _resizing_edge_dir = n->HoverEdge(pos, &edge_hover_node);
                            if (_resizing_edge_dir != 0u)
                                break;
                        }
                    }
                    if (edge_hover_node != s_last_edge_hover_node)
                    {
                        UpdateResizeMouseCursor(_resizing_edge_dir);
                        LOG_INFO("UpdateResizeMouseCursor: {}", _resizing_edge_dir);
                    }
                    if (edge_hover_node && Input::IsKeyPressed(EKey::kLBUTTON))
                    {
                        _resizing_node = edge_hover_node;
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
                    if (Input::IsKeyPressed(EKey::kLBUTTON))
                    {
                        Vector2f pos = Input::GetMousePos(_adj_split_node->_own_window);
                        Vector2f node_pos = _adj_split_node->_position;
                        Vector2f node_size = _adj_split_node->_size;
                        u32 dir = 0u;
                        if (_adj_split_node->_is_vertical_split)
                        {
                            f32 ratio = (pos.x - node_pos.x) / node_size.x;
                            _adj_split_node->AdjustSplitRatio(ratio);
                            dir = 1u;// 左右
                            UpdateResizeMouseCursor(dir);
                        }
                        else
                        {
                            f32 ratio = (pos.y - node_pos.y) / node_size.y;
                            _adj_split_node->AdjustSplitRatio(ratio);
                            dir = 2u;// 上下
                            UpdateResizeMouseCursor(dir);
                        }
                    }
                    else
                    {
                        _adj_split_node = nullptr;
                    }
                }
                else
                {
                    for (auto &it: _float_nodes)
                    {
                        auto &[window, nodes] = it;
                        if (Application::FocusedWindow() != window)
                            continue;
                        for (auto &n: nodes)
                        {
                            DockNode *tmp = nullptr;
                            Vector2f pos = Input::GetMousePos(window);
                            if (_focused_node && _focused_node != n.get() && _focused_node->IsHover(pos))
                                continue;
                            FindHoverSplitNode(n, pos, &tmp);
                            if (tmp)
                            {
                                UpdateResizeMouseCursor(tmp->_is_vertical_split ? EHoverEdgeDir::kLeft : EHoverEdgeDir::kTop);
                                if (Input::IsKeyPressed(EKey::kLBUTTON))
                                {
                                    _adj_split_node = tmp;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            //handle node move
            can_drag_move = _resizing_node == nullptr && _adj_split_node == nullptr;
            if (can_drag_move)
            {
                if (_drag_move_node)
                {
                    Vector2f pos = Input::GetMousePosAccurate(_drag_move_node->_own_window);
                    if (Input::IsKeyPressed(EKey::kLBUTTON))
                    {
                        _drag_move_node->_position = pos + _drag_start_offset;
                        //处理节点拖出窗口
                        Vector2f wsize = {(f32) _drag_move_node->_own_window->GetWidth(), (f32) _drag_move_node->_own_window->GetHeight()};
                        if (pos.x < 0.0f || pos.y < 0.0f || pos.x > wsize.x || pos.y > wsize.y)
                        {
                            f32 dx = pos.x - _drag_move_node->_position.x;
                            u16 ww = (u16) _drag_move_node->_size.x, wh = (u16) _drag_move_node->_size.y;
                            auto new_window = CreateNewWindow("DockWindow", ww, wh);
                            auto gm_pos = Input::GetGlobalMousePos();
                            new_window->SetPosition((i32) (gm_pos.x - dx), (i32) gm_pos.y);
                            _drag_move_node->SetOwnWindow(new_window);
                            _drag_move_node->_position = Vector2f::kZero;
                            _drag_move_node->_flags |= EDockWindowFlag::kNoMove | EDockWindowFlag::kNoResize | EDockWindowFlag::kFullSize;
                            _roots.push_back(_drag_move_node);
                            if (_drag_move_node->_window)
                            {
                                _drag_move_node->_window->SetFocus(false);
                                _focused_node = nullptr;
                            }
                            _drag_move_node = nullptr;
                        }
                    }
                    else
                    {
                        //if (_drag_move_node->_window)
                        //{
                        //    _drag_move_node->_window->SetFocus(false);
                        //}
                        _focused_node = nullptr;
                        _drag_move_node = nullptr;
                    }
                }
                else
                {
                    for (auto &it: _float_nodes)
                    {
                        auto &[window, nodes] = it;
                        if (Application::FocusedWindow() != window)
                            continue;
                        for (auto &n: nodes)
                        {
                            if (n->_flags & EDockWindowFlag::kNoMove)
                                continue;
                            Vector2f pos = Input::GetMousePos(n->_own_window);
                            if (_focused_node && _focused_node != n.get() && _focused_node->IsHover(pos))
                                continue;
                            if (n->HoverDragArea(pos) && Input::IsKeyPressed(EKey::kLBUTTON))
                            {
                                _drag_move_node = n.get();
                                _drag_start_offset = n->_position - pos;
                                if (n->_window)
                                {
                                    n->_window->SetFocus(true);
                                    _focused_node = n.get();
                                }
                                break;
                            }
                        }
                    }
                }
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
        }
        void DockManager::TryRemoveFloatNode(DockNode *n)
        {
            if (_float_nodes.contains(n->_own_window))
            {
                std::erase_if(_float_nodes[n->_own_window], [&](Ref<DockNode> e) -> bool
                                               { return n == e.get(); });
            }
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
                            if (!sibling->IsEmpty())
                                TryAddFloatNode(sibling);
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
                    n->_size = {(f32) n->_own_window->GetWidth(), (f32) n->_own_window->GetHeight()};
            }
            else if (e.GetEventType() == EEventType::kMouseButtonPressed)
            {
                s_pre_mouse_down_pos = Input::GetGlobalMousePos();
            }
            else if (e.GetEventType() == EEventType::kMouseMoved)
            {
                if (Distance(Input::GetGlobalMousePos(), s_pre_mouse_down_pos) > 20.0f)
                {
                    Window *main_window = &Application::Get().GetWindow();
                    auto [mw_x, mw_y] = main_window->GetClientPosition();
                    auto [mw_w, mw_h] = main_window->GetWindowSize();
                    for (auto &n: _float_nodes[e._window])
                    {
                        if (n->_own_window != main_window && n->_own_window == Application::FocusedWindow())
                        {
                            if (Input::IsKeyPressed(EKey::kLBUTTON))
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
            layer->OnEvent(e);
        }
        
        void DockManager::WriteNodeData(DockNode *n)
        {
            if (!n)
                return;
            DockNodeData data;
            data._type_id = n->_type == DockNode::EType::kLeaf ? n->_window->GetType()->FullName() : "null";
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
            data._title = n->_type == DockNode::EType::kLeaf ? n->_window->GetTitle() : "null";
            data._window_id = n->_own_window == Application::Get().GetWindowPtr() ? 0u : 999u;
            data._tab_id = n->_tab ? "tab" : "";
            _node_data_array._node_data.push_back(data);
            if (n->_type == DockNode::EType::kSplit)
            {
                WriteNodeData(n->_left.get());
                WriteNodeData(n->_right.get());
            }
        }
        Window *DockManager::CreateNewWindow(String title, u16 ww, u16 wh, bool is_sync)
        {
            if (!is_sync)
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
        
        void DockManager::RequestFocus(DockWindow *w)
        {
            _focused_node = FindNodeByWindow(w);
        }
        DockNode *DockManager::FindNodeByWindow(DockWindow *w)
        {
            for (auto &it: _float_nodes)
            {
                auto &[window, nodes] = it;
                for (auto &n: nodes)
                {
                    if (n->ContainsWindow(w))
                        return n.get();
                }
            }
            return nullptr;
        }
    }// namespace Editor
}// namespace Ailu