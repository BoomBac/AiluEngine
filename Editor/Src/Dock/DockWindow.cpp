#include "Dock/DockWindow.h"
#include "Common/EditorStyle.h"
#include "Dock/DockManager.h"
#include "Framework/Common/Input.h"
#include "Render/GraphicsContext.h"
#include "UI/Basic.h"
#include "UI/Container.h"
#include "UI/UIFramework.h"

#include "Objects/JsonArchive.h"

namespace Ailu
{
    namespace Editor
    {
#pragma region DockWindow
        u32 DockWindow::HoverEdge(Vector2f position, Vector2f size, Vector2f pos, f32 thickness)
        {
            // 首先检查鼠标是否在窗口范围内
            if (pos.x < position.x || pos.x > position.x + size.x ||
                pos.y < position.y || pos.y > position.y + size.y)
                return 0;

            u32 resize_dir = 0;
            // 判断边缘
            if (pos.x >= position.x && pos.x <= position.x + thickness)
                resize_dir |= 1;// 左
            if (pos.x >= position.x + size.x - thickness && pos.x <= position.x + size.x)
                resize_dir |= 4;// 右
            if (pos.y >= position.y && pos.y <= position.y + thickness)
                resize_dir |= 2;// 上
            if (pos.y >= position.y + size.y - thickness && pos.y <= position.y + size.y)
                resize_dir |= 8;// 下
            return resize_dir;
        }
        DockWindow::DockWindow() : DockWindow("noname")
        {
        }
        DockWindow::DockWindow(const String &title, Vector2f size) : _is_focused(false), _is_dragging(false), _size(size), _position(Vector2f::kZero)
        {
            _drag_start_mouse_pos = {-1.0f, 0.0f};
            _title_bar = MakeRef<UI::Widget>();
            auto c = MakeRef<UI::Canvas>();
            c->SlotSize(Vector2f(size.x, kTitleBarHeight));
            _title_bar_root = c->AddChild<UI::Border>();
            _title_bar_root->SlotSizePolicy(UI::ESizePolicy::kFixed);
            _title_bar_root->Thickness(0.0f);
            auto hb = _title_bar_root->AddChild<UI::HorizontalBox>();
            hb->SlotSizePolicy(UI::ESizePolicy::kFill);
            _title = hb->AddChild<UI::Text>();
            _title->OnMouseMove() += [&](UI::UIEvent &e)
            {
                if (e._current_target->_state._is_pressed)
                    DockManager::Get().BeginFloatWindow(this);
            };
            _title_drag_area = hb->AddChild<UI::Border>();
            _title_drag_area->Thickness(0.0f);
            _title_drag_area->_bg_color = Colors::kBlue;
            _title_drag_area->SlotSize({300.0f, kTitleBarHeight});
            _title_drag_area->SlotSizePolicy(UI::ESizePolicy::kFill);
            _title_drag_area->OnMouseDown() += [&](UI::UIEvent &e)
            {
                _is_dragging = true;
                _drag_start_mouse_pos = e._mouse_position;
                _drag_start_offset = _position - _drag_start_mouse_pos;
                //e._is_handled = true;
                SetFocus(true);
            };
            _title_drag_area->OnMouseUp() += [&](UI::UIEvent &e)
            {
                _is_dragging = false;
                _drag_start_mouse_pos = {-1.0f, 0.0f};
                SetFocus(false);
            };
            _title_drag_area->OnMouseMove() += [&](UI::UIEvent &e)
            {
                if (_flags & EDockWindowFlag::kNoMove)
                    return;
                if (_is_dragging)
                {
                    _position = e._mouse_position + _drag_start_offset;
                }
            };
            _title_drag_area->SetVisible(false);

            _btn_close = hb->AddChild<UI::Button>();
            _btn_close->SlotSizePolicy(UI::ESizePolicy::kFixed);
            _btn_close->SlotSize(kTitleBarHeight, kTitleBarHeight);
            _btn_close->OnMouseClick() += [this](UI::UIEvent &e)
            {
                LOG_INFO("DockWindow({}) close...", _title->GetText());
            };
            _title_bar->AddToWidget(c);
            _content = MakeRef<UI::Widget>();
            _content->SetPosition(_position + Vector2f(0.0f, kTitleBarHeight));
            c = MakeRef<UI::Canvas>();
            c->SlotSize(Vector2f(size.x, size.y - kTitleBarHeight));
            _content_root = c->AddChild<UI::Border>();
            _content_root->Thickness({1.0f,0.0f,1.0f,1.0f});
            _content_root->_border_color = g_editor_style._window_border_color;
            _content_root->_bg_color = g_editor_style._window_bg_color;
            _content_root->SlotSizePolicy(UI::ESizePolicy::kFixed);
            _content->AddToWidget(c);
            _content->_on_get_focus += [this]()
            {
                UI::UIManager::Get()->BringToFront(_title_bar.get());
            };
            SetTitle(title);
        }
        DockWindow::~DockWindow()
        {
        }
        void DockWindow::SetRect(Vector4f rect)
        {
            _position = rect.xy;
            SetSize(rect.zw);
        }
        void DockWindow::Update(f32 dt)
        {
            if (_is_dirty)
            {
                _title_bar->SetPosition(_position);
                _title_bar->SetSize({_size.x, kTitleBarHeight});
                _title_bar->Root()->SlotSize(Vector2f(_size.x, kTitleBarHeight));
                _title_bar_root->SlotSize(Vector2f(_size.x, kTitleBarHeight));
                _title_bar_root->GetChildren()[0]->SlotSize(_title_bar_root->SlotSize());
                {
                    auto s = _title->SlotSize();
                    s.y = kTitleBarHeight;
                    _title->SlotSize(s);
                }
                _btn_close->SlotSize(Vector2f(kTitleBarHeight, kTitleBarHeight));
                _content->SetPosition(_position + Vector2f(0.0f, kTitleBarHeight));
                _content->SetSize({_size.x, _size.y - kTitleBarHeight});
                _content->Root()->SlotSize(Vector2f(_size.x, _size.y - kTitleBarHeight));
                _content_root->SlotSize(Vector2f(_size.x, _size.y - kTitleBarHeight));
                _is_dirty = false;
            }
            _title_bar_root->_bg_color = _is_focused ? Colors::kBlue : g_editor_style._window_title_bar_color;
            _content_root->_border_color = g_editor_style._window_border_color;
            _content_root->_bg_color = g_editor_style._window_bg_color;
        }

        bool DockWindow::IsHover(Vector2f pos) const
        {
            return UI::UIElement::IsPointInside(pos, {_position, _size});
        }

        void DockWindow::SetTitleBarVisibility(bool is_visibility, bool is_expand_content)
        {
            _title_bar->_visibility = is_visibility ? UI::EVisibility::kVisible : UI::EVisibility::kHide;
            if (is_visibility)
            {
                _content->SetPosition(_position + Vector2f(0.0f, kTitleBarHeight));
                _content->SetSize({_size.x, _size.y - kTitleBarHeight});
                _content->Root()->SlotSize(Vector2f(_size.x, _size.y - kTitleBarHeight));
                _content_root->SlotSize(Vector2f(_size.x, _size.y - kTitleBarHeight));
            }
            else
            {
                if (is_expand_content)
                {
                    _content->SetPosition(_position);
                    _content->SetSize({_size.x, _size.y});
                    _content->Root()->SlotSize(Vector2f(_size.x, _size.y));
                    _content_root->SlotSize(Vector2f(_size.x, _size.y));
                }
            }
        }
        void DockWindow::SetContentVisibility(bool is_visibility)
        {
            _content->_visibility = is_visibility ? UI::EVisibility::kVisible : UI::EVisibility::kHide;
        }
        String DockWindow::GetTitle() const
        {
            return _title->GetText();
        }
        void DockWindow::SetTitle(String title)
        {
            _content->Name(std::format("{}_content", title));
            _title_bar->Name(std::format("{}_title", title));
            _title->SetText(title);
            _name = title;
        }
        void DockWindow::SetFocus(bool is_focus)
        {
            //if (is_focus == _is_focused)
            //    return;
            _is_focused = is_focus;
            if (is_focus)
            {
                UI::UIManager::Get()->BringToFront(_title_bar.get());
                UI::UIManager::Get()->BringToFront(_content.get());
                //_title_bar->_on_sort_order_changed.Invoke();
            }
        };

        u32 DockWindow::HoverEdge(Vector2f pos) const
        {
            // 首先检查鼠标是否在窗口范围内
            if (pos.x < _position.x || pos.x > _position.x + _size.x ||
                pos.y < _position.y || pos.y > _position.y + _size.y)
                return 0;

            u32 resize_dir = 0;
            // 判断边缘
            if (pos.x >= _position.x && pos.x <= _position.x + kBorderThickness)
                resize_dir |= 1;// 左
            if (pos.x >= _position.x + _size.x - kBorderThickness && pos.x <= _position.x + _size.x)
                resize_dir |= 4;// 右
            if (pos.y >= _position.y && pos.y <= _position.y + kBorderThickness && (pos.x - _position.x) < (_size.x - kTitleBarHeight))
                resize_dir |= 2;// 上
            if (pos.y >= _position.y + _size.y - kBorderThickness && pos.y <= _position.y + _size.y)
                resize_dir |= 8;// 下
            return resize_dir;
        }

        Vector4f DockWindow::DragArea() const
        {
            return _title_drag_area->GetArrangeRect();
        };

        void DockWindow::SetSize(Vector2f size)
        {
            if (NearbyEqual(_size, size))
                return;
            size = Max(size, kMinSize);
            _size = size;
            _on_size_change_delegate.Invoke(size);
            _is_dirty = true;
        }

        void DockWindow::SetPosition(Vector2f position)
        {
            if (NearbyEqual(_position, position))
                return;
            _position = position;
            _is_dirty = true;
        }

        void DockWindow::UpdateResizeState(Vector2f mouse_pos, bool is_mouse_down)
        {
            _resize_dir = 0;

            // 判断边缘
            if (mouse_pos.x >= _position.x && mouse_pos.x <= _position.x + kBorderThickness)
                _resize_dir |= 1;// 左
            if (mouse_pos.x >= _position.x + _size.x - kBorderThickness && mouse_pos.x <= _position.x + _size.x)
                _resize_dir |= 4;// 右
            if (mouse_pos.y >= _position.y && mouse_pos.y <= _position.y + kBorderThickness)
                _resize_dir |= 2;// 上
            if (mouse_pos.y >= _position.y + _size.y - kBorderThickness && mouse_pos.y <= _position.y + _size.y)
                _resize_dir |= 8;// 下

            _is_resizing = is_mouse_down && _resize_dir != 0;
            if (_is_resizing)
            {
                s_cur_resizing_window = this;
                _pre_mouse_pos = mouse_pos;
            }
        }
#pragma endregion

#pragma region DockTab
        DockTab::DockTab()
        {
            _size = {400.0f, DockWindow::kTitleBarHeight};
            _tab_bar = MakeRef<UI::Widget>();
            _tab_bar->Name("DockTab");
            _tab_bar->SetSize(_size);
            auto c = MakeRef<UI::Canvas>();
            _tab_bar->AddToWidget(c);
            _tab_root = c->AddChild<UI::Border>();
            _tab_root->Thickness(0.0f);
            _tab_hb = _tab_root->AddChild<UI::HorizontalBox>();
            {
                auto s = _tab_hb->SlotSize();
                s.y = DockWindow::kTitleBarHeight;
                _tab_hb->SlotSize(s);
            }
            //标签区
            _tab_titles = _tab_hb->AddChild<UI::HorizontalBox>();
            {
                auto s = _tab_titles->SlotSize();
                s.y = DockWindow::kTitleBarHeight;
                _tab_titles->SlotSize(s);
            }
            //拖拽区
            _drag_area = _tab_hb->AddChild<UI::Border>();
            _drag_area->Thickness(0.0f);
            _drag_area->_bg_color = Colors::kBlue;
            _drag_area->SlotSize({300.0f, DockWindow::kTitleBarHeight});
            _drag_area->SlotSizePolicy(UI::ESizePolicy::kFill);
            _drag_area->SetVisible(false);
            _drag_area->OnMouseDown() += [this](UI::UIEvent &e)
            {
                _tabs[_active_index]->SetFocus(true);
            };
            //右侧按钮
            _btn_close = _tab_hb->AddChild<UI::Button>();
            _btn_close->SlotSize({DockWindow::kTitleBarHeight, DockWindow::kTitleBarHeight});
            _btn_close->OnMouseClick() += [this](UI::UIEvent &e)
            {
                LOG_INFO("DockTab close...");
            };
            UI::UIManager::Get()->RegisterWidget(_tab_bar);
        }

        DockTab::~DockTab()
        {
            UI::UIManager::Get()->UnRegisterWidget(_tab_bar.get());
        }

        bool DockTab::AddTab(const Ref<DockWindow> &w)
        {
            if (auto it = std::find_if(_tabs.begin(), _tabs.end(), [&](Ref<DockWindow> e) -> bool
                                       { return e.get() == w.get(); });
                it != _tabs.end())
                return false;
            if (_tabs.empty())
            {
                _size = w->Size();
                _position = w->Position();
            }
            _tabs.push_back(w);
            w->SetTitleBarVisibility(false);
            w->SetRect({_position.x, _position.y, _size.x, _size.y});
            auto bg = _tab_titles->AddChild<UI::Border>();
            bg->_bg_color = g_editor_style._tab_bg_color;
            {
                auto s = bg->SlotSize();
                s.y = DockWindow::kTitleBarHeight;
                bg->SlotSize(s);
            }
            {
                auto s = _tab_titles->SlotSize();
                s.x = 100.0f * _tab_titles->GetChildren().size();
                _tab_titles->SlotSize(s);
            }
            bg->AddChild<UI::Text>()->SetText(w->GetTitle());
            bg->OnMouseClick() += [this, w](UI::UIEvent &e)
            {
                i32 new_index = static_cast<int>(std::distance(_tabs.begin(), std::find_if(_tabs.begin(), _tabs.end(), [&](Ref<DockWindow> e) -> bool
                                                                                           { return e.get() == w.get(); })));
                OnActiveTabChanged(new_index);
            };
            bg->OnMouseDown() += [this, w](UI::UIEvent &e)
            {
                i32 new_index = static_cast<int>(std::distance(_tabs.begin(), std::find_if(_tabs.begin(), _tabs.end(), [&](Ref<DockWindow> e) -> bool
                                                                                           { return e.get() == w.get(); })));
                OnActiveTabChanged(new_index);
            };
            bg->OnMouseMove() += [&](UI::UIEvent &e)
            {
                if (e._current_target->_state._is_pressed)
                {
                    DockManager::Get().BeginFloatWindow(_tabs[_active_index].get());
                }
            };
            bg->OnMouseEnter() += [this](UI::UIEvent &e)
            {
                if (!IsActiveTab(e._current_target))
                    e._current_target->As<UI::Border>()->_bg_color = g_editor_style._tab_hover_bg_color;
            };
            bg->OnMouseExit() += [this](UI::UIEvent &e)
            {
                if (!IsActiveTab(e._current_target))
                    e._current_target->As<UI::Border>()->_bg_color = g_editor_style._tab_bg_color;
            };
            OnActiveTabChanged(static_cast<i32>(_tabs.size() - 1u));
            return true;
        }

        bool DockTab::RemoveTab(DockWindow *w)
        {
            // 找到对应的 index
            auto it = std::find_if(_tabs.begin(), _tabs.end(), [&](const Ref<DockWindow> &e)
                                   { return e.get() == w; });

            if (it == _tabs.end())
                return false;// 没找到

            i32 remove_index = static_cast<i32>(std::distance(_tabs.begin(), it));

            // ===== 切换焦点逻辑放在前面 =====
            if (_tabs.size() > 1)
            {
                bool new_is_right = false;
                // 如果删掉的是最后一个，就激活前一个，否则激活同位置的下一个
                i32 new_index = remove_index;
                if (new_index >= static_cast<i32>(_tabs.size()) - 1)
                    new_index = static_cast<i32>(_tabs.size()) - 2;// 删掉最后一个 → 激活前一个
                else
                {
                    // 激活右边的,OnActiveTabChanged使用new_index,实际设置时使用new_index-1，new_inex是相对于未移除元素
                    //考虑012，移除1，new_index=2，首先使用2来修改下一个窗口属性，移除后会变为02，此时index其实还是1
                    new_index = remove_index + 1;
                    new_is_right = true;
                }
                OnActiveTabChanged(new_index);
                if (new_is_right)
                    _active_index = new_index - 1;
            }

            // 恢复窗口状态
            (*it)->SetTitleBarVisibility(true);
            (*it)->SetContentVisibility(true);

            // 移除 DockWindow
            _tabs.erase(it);

            // 移除 UI tab title
            if (remove_index < static_cast<i32>(_tab_titles->GetChildren().size()))
            {
                auto child = _tab_titles->GetChildren()[remove_index];
                _tab_titles->RemoveChild(child);
            }

            // 调整 tab_titles 宽度
            {
                auto s = _tab_titles->SlotSize();
                s.x = 100.0f * _tab_titles->GetChildren().size();
                _tab_titles->SlotSize(s);
            }

            // 如果删完了，返回 true 让外部知道这个 DockTab 已经空了
            return _tabs.empty();
        }


        bool DockTab::IsActiveTab(UI::UIElement *e) const
        {
            if (_active_index == -1)
                return false;
            const auto &tabs = _tab_titles->GetChildren();
            return tabs[_active_index].get() == e;
        }

        void DockTab::OnActiveTabChanged(i32 new_index)
        {
            if (_active_index != -1)
            {
                dynamic_cast<UI::Border *>(_tab_titles->GetChildren()[_active_index].get())->_bg_color = g_editor_style._tab_bg_color;
                _tabs[_active_index]->SetContentVisibility(false);
                _tabs[_active_index]->ContentWidget()->_on_get_focus -= _content_focus_handle;
            }
            _active_index = new_index;
            _tab_titles->GetChildren()[_active_index]->As<UI::Border>()->_bg_color = g_editor_style._tab_active_bg_color;
            _tabs[_active_index]->SetContentVisibility(true);
            _content_focus_handle = _tabs[_active_index]->ContentWidget()->_on_get_focus += [this]()
            {
                UI::UIManager::Get()->BringToFront(_tab_bar.get());
            };
            LOG_INFO("DockTab::AddTab: active index {}", _active_index);
        }

        u32 DockTab::HoverEdge(Vector2f pos) const
        {
            return DockWindow::HoverEdge(_position, _size, pos);
        }

        bool DockTab::HoverDragArea(Vector2f pos) const
        {
            AL_ASSERT(false);//这里要转屏幕空间？
            return UI::UIElement::IsPointInside(pos, _drag_area->GetArrangeRect());
        }

        bool DockTab::IsHover(Vector2f pos) const
        {
            return _tabs[_active_index]->IsHover(pos);
        }

        bool DockTab::Contains(DockWindow *w) const
        {
            auto it = std::find_if(_tabs.begin(), _tabs.end(), [&](const Ref<DockWindow> &e)
                                   { return e.get() == w; });
            return it != _tabs.end();
        }

        void DockTab::SetFocus(bool is_focus)
        {
            if (is_focus == _is_focused)
                return;
            _is_focused = is_focus;
            UI::UIManager::Get()->BringToFront(_tab_bar.get());
            _tabs[_active_index]->SetFocus(is_focus);
        }

        void DockTab::Update(f32 dt)
        {
            _tab_bar->SetPosition(_position);
            _tab_bar->SetSize({_size.x, DockWindow::kTitleBarHeight});
            _tab_root->SlotSize({_size.x, DockWindow::kTitleBarHeight});
            _tab_hb->SlotSize(_tab_root->SlotSize());
            for (auto &w: _tabs)
            {
                w->SetRect({_position.x, _position.y, _size.x, _size.y});
                w->Update(dt);
            }
        }
#pragma endregion
    }// namespace Editor
}// namespace Ailu