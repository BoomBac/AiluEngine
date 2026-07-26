#include "Dock/DockWindow.h"
#include "Common/EditorStyle.h"
#include "Dock/DockManager.h"
#include "Framework/Common/Input.h"
#include "Render/GraphicsContext.h"
#include "UI/Basic.h"
#include "UI/Container.h"
#include "UI/UIFramework.h"
#include "UI/UIRenderer.h"
#include <memory>

#include "Objects/JsonArchive.h"

namespace Ailu
{
    namespace Editor
    {
        namespace
        {
            Vector4f TopRadius(f32 radius)
            {
                return Vector4f(radius, radius, 0.0f, 0.0f);
            }

            Vector4f BottomRadius(f32 radius)
            {
                return Vector4f(0.0f, 0.0f, radius, radius);
            }

            void SetBorderCornerRadius(UI::Border *border, Vector4f radius)
            {
                auto &style_override = border->GetStyleOverride();
                style_override._corner_radius = radius;
                style_override._override_mask |= (u32) UI::EUIControlVisualOverride::kCornerRadius;
            }

            void ConfigureDockCloseButton(UI::Button *button, f32 radius)
            {
                auto make_visual = [](Color bg, Color text, Vector4f corner_radius) -> UI::UIControlVisual
                {
                    UI::UIControlVisual visual;
                    visual._background._type = UI::EUIBrushType::kColor;
                    visual._background._tint = bg;
                    visual._content_color = text;
                    visual._border_color = Colors::kTransparent;
                    visual._border_width = 0.0f;
                    visual._corner_radius = corner_radius;
                    return visual;
                };

                const Vector4f close_radius = Vector4f(0.0f, radius, 0.0f, 0.0f);
                auto &style = button->GetStyleOverride();
                style._normal = make_visual(Colors::kTransparent, g_editor_style._window_title_text_color, close_radius);
                style._hovered = make_visual(Color(0.30f, 0.33f, 0.38f, 1.0f), g_editor_style._tab_active_text_color, close_radius);
                style._pressed = make_visual(Color(0.22f, 0.24f, 0.28f, 1.0f), g_editor_style._tab_active_text_color, close_radius);
                style._focused = style._hovered;
                style._disabled = make_visual(Colors::kTransparent, Color(0.45f, 0.48f, 0.54f, 1.0f), close_radius);
                style._padding = UI::Padding(0.0f);
                style._override_mask |= (u32) UI::EUIButtonStyleOverride::kNormal |
                                        (u32) UI::EUIButtonStyleOverride::kHovered |
                                        (u32) UI::EUIButtonStyleOverride::kPressed |
                                        (u32) UI::EUIButtonStyleOverride::kFocused |
                                        (u32) UI::EUIButtonStyleOverride::kDisabled |
                                        (u32) UI::EUIButtonStyleOverride::kPadding;
            }

            bool SetColorIfChanged(Color &dst, const Color &src)
            {
                if (NearbyEqual(dst, src))
                    return false;
                dst = src;
                return true;
            }
        }

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
            _title_widget = MakeRef<UI::Widget>();
            auto c = MakeRef<UI::Canvas>();
            c->GetSlot()->Size(Vector2f(size.x, kTitleBarHeight));
            _title_bar_root = c->AddChild<UI::Border>();
            _title_bar_root->GetSlotAs<UI::CanvasSlot>().Size(Vector2f(size.x, kTitleBarHeight));
            _title_bar_root->Thickness({1.0f, 1.0f, 1.0f, 0.0f});
            SetBorderCornerRadius(_title_bar_root, TopRadius(g_editor_style._window_corner_radius));
            auto hb = _title_bar_root->AddChild<UI::HorizontalBox>();
            hb->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            _title = hb->AddChild<UI::Text>();
            _title->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kAuto, UI::ESizePolicy::kFill);
            _title->FontSize(kTitleBarHeight * 0.7f);
            _title->_color = g_editor_style._window_title_text_color;
            _title->OnMouseDown() += [&](UI::UIEvent &e)
            {
                SetFocus(true);
            };
            _title->OnMouseMove() += [&](UI::UIEvent &e)
            {
                if (e._current_target->IsPressed())
                    DockManager::Get().BeginFloatWindow(this);
            };
            _title_drag_area = hb->AddChild<UI::Border>();
            _title_drag_area->Thickness(0.0f);
            _title_drag_area->_bg_color = g_editor_style._window_title_bar_color;
            _title_drag_area->GetSlotAs<UI::LinearSlot>().Size({300.0f, kTitleBarHeight}).SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
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
            //_title_drag_area->SetVisible(false);

            _btn_close = hb->AddChild<UI::Button>();
            _btn_close->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed).Size({kTitleBarHeight, kTitleBarHeight});
            _btn_close->SetText("x");
            ConfigureDockCloseButton(_btn_close, g_editor_style._window_corner_radius);
            _btn_close->OnMouseClick() += [this](UI::UIEvent &e)
            {
                LOG_INFO("DockWindow({}) close...", _title->GetText());
                e._is_handled = true;
                DockManager::Get().RequestRemoveDock(this);
            };
            _title_widget->AddToWidget(c);
            _content_widget = MakeRef<UI::Widget>();
            _content_widget->SetPosition(_position + Vector2f(0.0f, kTitleBarHeight));
            c = MakeRef<UI::Canvas>();
            c->GetSlot()->Size(Vector2f(size.x, size.y - kTitleBarHeight));
            _content_root = c->AddChild<UI::Border>();
            _content_root->Thickness({1.0f,0.0f,1.0f,1.0f});
            _content_root->_border_color = g_editor_style._window_border_color;
            _content_root->_bg_color = g_editor_style._window_bg_color;
            SetBorderCornerRadius(_content_root, BottomRadius(g_editor_style._window_corner_radius));
            _content_root->GetSlotAs<UI::CanvasSlot>().Size(Vector2f(size.x, size.y - kTitleBarHeight));
            _content_widget->AddToWidget(c);
            _content_widget->_on_get_focus += [this]()
            {
                UI::UIManager::Get()->BringToFront(_title_widget.get());
                _is_focused = true;
                _on_get_focus_delegate.Invoke(this);
            };
            _title_widget->_on_lost_focus += [this]()
            {
                SetFocus(false);
            };
            _content_widget->_on_lost_focus += [this]()
            {
                SetFocus(false);
            };
            SetTitle(title);
            _resize_zone_handles.push_back(UI::UIManager::Get()->RegisterInteractionZone(Vector4f::kZero));
            _resize_zone_handles.push_back(UI::UIManager::Get()->RegisterInteractionZone(Vector4f::kZero));
            _resize_zone_handles.push_back(UI::UIManager::Get()->RegisterInteractionZone(Vector4f::kZero));
            _resize_zone_handles.push_back(UI::UIManager::Get()->RegisterInteractionZone(Vector4f::kZero));
        }
        DockWindow::~DockWindow()
        {
            if (auto *ui_mgr = UI::UIManager::Get())
            {
                for (auto handle : _resize_zone_handles)
                    ui_mgr->UnRegisterInteractionZone(handle);
            }
        }
        void DockWindow::SetRect(Vector4f rect)
        {
            if (NearbyEqual(Vector4f{_position,_size}, rect))
                return;
            _position = rect.xy;
            SetSize(rect.zw);
            _is_dirty = true;
        }
        void DockWindow::Update(f32 dt)
        {
            if (_is_dirty)
            {
                const f32 content_offset_y = (_is_title_bar_visible || !_is_expand_content_when_title_hidden)
                                                 ? kTitleBarHeight
                                                 : 0.0f;
                const f32 content_height = std::max(0.0f, _size.y - content_offset_y);
                _title_widget->SetPosition(_position);
                _title_widget->SetSize({_size.x, kTitleBarHeight});
                _title_widget->Root()->GetSlot()->Size(Vector2f(_size.x, kTitleBarHeight));
                _title_bar_root->GetSlotAs<UI::CanvasSlot>().Size(Vector2f(_size.x, kTitleBarHeight));
                _title_bar_root->GetChildren()[0]->GetSlot()->Size(_title_bar_root->GetSlot()->_size);
                {
                    auto s = _title->GetSlot()->_size;
                    s.y = kTitleBarHeight;
                    _title->GetSlot()->Size(s);
                }
                _btn_close->GetSlot()->Size(Vector2f(kTitleBarHeight, kTitleBarHeight));
                _content_widget->SetPosition(_position + Vector2f(0.0f, content_offset_y));
                _content_widget->SetSize({_size.x, content_height});
                _content_widget->Root()->GetSlot()->Size(Vector2f(_size.x, content_height));
                _content_root->GetSlotAs<UI::CanvasSlot>().Size(Vector2f(_size.x, content_height));
                _content_widget->Root()->InvalidateLayout(true);
                auto ui_mgr = UI::UIManager::Get();
                const f32 t = kBorderThickness;
                const Vector2f &p = _position;
                const Vector2f &s = _size;
                ui_mgr->UpdateInteractionZone(
                        _resize_zone_handles[0],
                        Vector4f(p.x - t, p.y - t, s.x + t * 2.0f, t));// Top
                ui_mgr->UpdateInteractionZone(
                        _resize_zone_handles[1],
                        Vector4f(p.x - t, p.y + s.y, s.x + t * 2.0f, t));// Bottom
                ui_mgr->UpdateInteractionZone(
                        _resize_zone_handles[2],
                        Vector4f(p.x - t, p.y, t, s.y));// Left
                ui_mgr->UpdateInteractionZone(
                        _resize_zone_handles[3],
                        Vector4f(p.x + s.x, p.y, t, s.y));// Right

                _is_dirty = false;
            }
            bool title_style_changed = false;
            bool content_style_changed = false;
            title_style_changed |= SetColorIfChanged(_title_bar_root->_bg_color, g_editor_style._window_title_bar_color);
            title_style_changed |= SetColorIfChanged(_title_bar_root->_border_color, _is_focused ? g_editor_style._window_focus_border_color : g_editor_style._window_border_color);
            content_style_changed |= SetColorIfChanged(_content_root->_border_color, _is_focused ? g_editor_style._window_focus_border_color : g_editor_style._window_border_color);
            content_style_changed |= SetColorIfChanged(_content_root->_bg_color, g_editor_style._window_bg_color);
            if (SetColorIfChanged(_title->_color, g_editor_style._window_title_text_color))
                _title->InvalidatePaint();
            if (title_style_changed)
                _title_bar_root->InvalidateStyle(UI::EStyleInvalidation::kPaintOnly);
            if (content_style_changed)
                _content_root->InvalidateStyle(UI::EStyleInvalidation::kPaintOnly);
        }

        bool DockWindow::IsHover(Vector2f pos) const
        {
            return UI::UIElement::IsPointInside(pos, {_position, _size});
        }

        void DockWindow::SetTitleBarVisibility(bool is_visibility, bool is_expand_content)
        {
            _is_title_bar_visible = is_visibility;
            _is_expand_content_when_title_hidden = !is_visibility && is_expand_content;
            _title_widget->_visibility = is_visibility ? UI::EVisibility::kVisible : UI::EVisibility::kHide;
            _title_widget->InvalidatePaint(UI::EUIInvalidationReason::kVisibility | UI::EUIInvalidationReason::kPaint);
            _is_dirty = true;
        }
        void DockWindow::SetContentVisibility(bool is_visibility)
        {
            _content_widget->_visibility = is_visibility ? UI::EVisibility::kVisible : UI::EVisibility::kHide;
            _content_widget->InvalidatePaint(UI::EUIInvalidationReason::kVisibility | UI::EUIInvalidationReason::kPaint);
        }
        String DockWindow::GetTitle() const
        {
            return _title->GetText();
        }
        void DockWindow::SetTitle(String title)
        {
            _content_widget->Name(std::format("{}_content", title));
            _title_widget->Name(std::format("{}_title", title));
            _title->SetText(title);
            _name = title;
            _title_widget->InvalidatePaint(UI::EUIInvalidationReason::kPaint);
        }
        void DockWindow::SetFocus(bool is_focus)
        {
            if (!is_focus && !_is_focused)
                return;
            _is_focused = is_focus;
            if (is_focus)
            {
                UI::UIManager::Get()->BringToFront(_content_widget.get());//事件会自动将title_widget带到前面
            }
            else
                _on_lost_focus_delegate.Invoke(this);
        };

        void DockWindow::SetTabActive(bool is_active)
        {
            SetTitleBarVisibility(false);
            SetContentVisibility(is_active);
        }

        void DockWindow::RestoreStandaloneFromTab()
        {
            SetTitleBarVisibility(true);
            SetContentVisibility(true);
        }

        void DockWindow::AttachToWindow(Window *w)
        {
            auto new_target = RenderTexture::WindowBackBuffer(w);
            _content_widget->BindOutput(new_target);
            _title_widget->BindOutput(new_target);
            _content_widget->SetParent(w);
            _title_widget->SetParent(w);
        }

        u32 DockWindow::HoverEdge(Vector2f pos) const
        {
            // 首先检查鼠标是否在窗口范围内
            if (pos.x < _position.x - kBorderThickness || pos.x > _position.x + _size.x + kBorderThickness ||
                pos.y < _position.y - kBorderThickness || pos.y > _position.y + _size.y + kBorderThickness)
                return 0;

            u32 resize_dir = 0;
            // 判断边缘
            if (pos.x <= _position.x && pos.x >= _position.x - kBorderThickness)
                resize_dir |= 1;// 左
            if (pos.x <= _position.x + _size.x + kBorderThickness && pos.x >= _position.x + _size.x)
                resize_dir |= 4;// 右
            if (pos.y <= _position.y && pos.y >= _position.y - kBorderThickness && (pos.x - _position.x) < (_size.x - kTitleBarHeight))
                resize_dir |= 2;// 上
            if (pos.y >= _position.y + _size.y && pos.y <= _position.y + _size.y + kBorderThickness)
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
            _tab_root->Thickness({1.0f, 1.0f, 1.0f, 0.0f});
            _tab_root->_bg_color = g_editor_style._window_title_bar_color;
            _tab_root->_border_color = g_editor_style._window_border_color;
            SetBorderCornerRadius(_tab_root, TopRadius(g_editor_style._window_corner_radius));
            _tab_root->GetSlotAs<UI::CanvasSlot>().Size(_size);
            _tab_hb = _tab_root->AddChild<UI::HorizontalBox>();
            {
                auto s = _tab_hb->GetSlot()->_size;
                s.y = DockWindow::kTitleBarHeight;
                _tab_hb->GetSlot()->Size(s);
            }
            //标签区
            _tab_titles = _tab_hb->AddChild<UI::HorizontalBox>();
            {
                auto s = _tab_titles->GetSlot()->_size;
                s.y = DockWindow::kTitleBarHeight;
                _tab_titles->GetSlot()->Size(s);
            }
            _tab_titles->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kAuto, UI::ESizePolicy::kFill);
            //拖拽区
            _drag_area = _tab_hb->AddChild<UI::Border>();
            _drag_area->Thickness(0.0f);
            _drag_area->_bg_color = g_editor_style._window_title_bar_color;
            _drag_area->GetSlotAs<UI::LinearSlot>().Size({300.0f, DockWindow::kTitleBarHeight})
                    .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed);
            _drag_area->SetVisible(false);
            _drag_area->OnMouseDown() += [this](UI::UIEvent &e)
            {
                if (auto *primary_window = ActivePrimaryWindow())
                    primary_window->SetFocus(true);
            };
            //右侧按钮
            _btn_close = _tab_hb->AddChild<UI::Button>();
            _btn_close->GetSlotAs<UI::LinearSlot>().Size({DockWindow::kTitleBarHeight, DockWindow::kTitleBarHeight})
                    .SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed);
            _btn_close->SetText("x");
            ConfigureDockCloseButton(_btn_close, g_editor_style._window_corner_radius);
            _btn_close->GetSlotAs<UI::LinearSlot>().CrossAlignment(UI::EAlignment::kRight);
            _btn_close->OnMouseClick() += [this](UI::UIEvent &e)
            {
                LOG_INFO("DockTab close...");
                e._is_handled = true;
                if (auto *primary_window = ActivePrimaryWindow())
                    DockManager::Get().RequestRemoveDock(primary_window);
            };
            UI::UIManager::Get()->RegisterWidget(_tab_bar);
        }

        DockTab::~DockTab()
        {
            UI::UIManager::Get()->UnRegisterWidget(_tab_bar.get());
        }

        bool DockTab::AddTab(const Ref<DockWindow> &w)
        {
            return AddTabItem(std::static_pointer_cast<IDockTabItem>(w));
        }

        bool DockTab::AddTabItem(const Ref<IDockTabItem> &item)
        {
            if (!item)
                return false;
            if (auto it = std::find_if(_tabs.begin(), _tabs.end(), [&](const Ref<IDockTabItem> &e) -> bool
                                       { return e.get() == item.get(); });
                it != _tabs.end())
                return false;
            if (_tabs.empty())
            {
                _size = item->Size();
                _position = item->Position();
            }
            _tabs.push_back(item);
            f32 max_width = 0.0f;
            for (const auto &tab: _tabs)
                max_width = std::max(max_width, tab->Size().x);
            _tab_root->GetSlotAs<UI::CanvasSlot>().Size({max_width, DockWindow::kTitleBarHeight});
            item->SetRect({_position.x, _position.y, _size.x, _size.y});
            item->SetTabActive(false);
            auto bg = _tab_titles->AddChild<UI::Border>();
            bg->_bg_color = g_editor_style._tab_bg_color;
            bg->_border_color = Colors::kTransparent;
            SetBorderCornerRadius(bg, Vector4f(4.0f, 4.0f, 0.0f, 0.0f));
            const f32 tab_width = std::max(80.0f, UI::UIRenderer::Get()->CalculateTextSize(item->GetTitle()).x + DockWindow::kTitleBarHeight * 1.5f);
            bg->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kAuto, UI::ESizePolicy::kFill).Size({tab_width, DockWindow::kTitleBarHeight});
            auto *title = bg->AddChild<UI::Text>();
            title->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kAuto, UI::ESizePolicy::kFill);
            title->SlotPadding() = UI::Padding(5.0f, 0.0f, 5.0f, 0.0f);
            title->_color = g_editor_style._tab_text_color;
            title->InvalidateLayout();
            title->SetText(item->GetTitle());
            bg->OnMouseClick() += [this, item](UI::UIEvent &e)
            {
                i32 new_index = static_cast<int>(std::distance(_tabs.begin(), std::find_if(_tabs.begin(), _tabs.end(), [&](const Ref<IDockTabItem> &e) -> bool
                                                                                           { return e.get() == item.get(); })));
                OnActiveTabChanged(new_index);
            };
            bg->OnMouseDown() += [this, item](UI::UIEvent &e)
            {
                i32 new_index = static_cast<int>(std::distance(_tabs.begin(), std::find_if(_tabs.begin(), _tabs.end(), [&](const Ref<IDockTabItem> &e) -> bool
                                                                                           { return e.get() == item.get(); })));
                OnActiveTabChanged(new_index);
                if (auto *primary_window = ActivePrimaryWindow())
                    primary_window->SetFocus(true);
            }; 
            bg->OnMouseMove() += [&](UI::UIEvent &e)
            {
                if (e._current_target->IsPressed())
                {
                    if (auto *primary_window = ActivePrimaryWindow())
                        DockManager::Get().BeginFloatWindow(primary_window);
                }
            };
            bg->OnMouseEnter() += [this](UI::UIEvent &e)
            {
                if (!IsActiveTab(e._current_target))
                {
                    e._current_target->As<UI::Border>()->_bg_color = g_editor_style._tab_hover_bg_color;
                    e._current_target->InvalidateStyle(UI::EStyleInvalidation::kPaintOnly);
                    if (!e._current_target->GetChildren().empty())
                    {
                        e._current_target->GetChildren()[0]->As<UI::Text>()->_color = g_editor_style._tab_hover_text_color;
                        e._current_target->GetChildren()[0]->InvalidatePaint();
                    }
                }
            };
            bg->OnMouseExit() += [this](UI::UIEvent &e)
            {
                if (!IsActiveTab(e._current_target))
                {
                    e._current_target->As<UI::Border>()->_bg_color = g_editor_style._tab_bg_color;
                    e._current_target->InvalidateStyle(UI::EStyleInvalidation::kPaintOnly);
                    if (!e._current_target->GetChildren().empty())
                    {
                        e._current_target->GetChildren()[0]->As<UI::Text>()->_color = g_editor_style._tab_text_color;
                        e._current_target->GetChildren()[0]->InvalidatePaint();
                    }
                }
            };
            OnActiveTabChanged(static_cast<i32>(_tabs.size() - 1u));
            return true;
        }

        bool DockTab::RemoveTab(DockWindow *w)
        {
            // 找到对应的 index
            auto it = std::find_if(_tabs.begin(), _tabs.end(), [&](const Ref<IDockTabItem> &e)
                                   { return e->ContainsWindow(w); });

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
            (*it)->RestoreStandaloneFromTab();

            // 移除 DockWindow
            _tabs.erase(it);

            // 移除 UI tab title
            if (remove_index < static_cast<i32>(_tab_titles->GetChildren().size()))
            {
                auto child = _tab_titles->GetChildren()[remove_index];
                _tab_titles->RemoveChild(child);
            }

            // 如果删完了，返回 true 让外部知道这个 DockTab 已经空了
            if (_tabs.empty())
                _active_index = -1;
            return _tabs.empty();
        }

        Ref<IDockTabItem> DockTab::RemoveActiveItem()
        {
            if (_active_index < 0 || _active_index >= static_cast<i32>(_tabs.size()))
                return nullptr;
            Ref<IDockTabItem> removed_item = _tabs[_active_index];
            const i32 remove_index = _active_index;
            if (_tabs.size() > 1)
            {
                i32 new_index = remove_index;
                if (new_index >= static_cast<i32>(_tabs.size()) - 1)
                    new_index = static_cast<i32>(_tabs.size()) - 2;
                else
                    new_index = remove_index + 1;
                OnActiveTabChanged(new_index);
                if (new_index > remove_index)
                    _active_index = new_index - 1;
            }
            removed_item->RestoreStandaloneFromTab();
            _tabs.erase(_tabs.begin() + remove_index);
            if (remove_index < static_cast<i32>(_tab_titles->GetChildren().size()))
            {
                auto child = _tab_titles->GetChildren()[remove_index];
                _tab_titles->RemoveChild(child);
            }
            if (_tabs.empty())
                _active_index = -1;
            return removed_item;
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
            if (new_index < 0 || new_index >= static_cast<i32>(_tabs.size()))
                return;
            if (_active_index != -1)
            {
                auto *prev_title_bg = dynamic_cast<UI::Border *>(_tab_titles->GetChildren()[_active_index].get());
                prev_title_bg->_bg_color = g_editor_style._tab_bg_color;
                if (!prev_title_bg->GetChildren().empty())
                {
                    prev_title_bg->GetChildren()[0]->As<UI::Text>()->_color = g_editor_style._tab_text_color;
                    prev_title_bg->GetChildren()[0]->InvalidatePaint();
                }
                prev_title_bg->InvalidateStyle(UI::EStyleInvalidation::kPaintOnly);
                _tabs[_active_index]->SetTabActive(false);
                if (auto *previous_window = _tabs[_active_index]->PrimaryWindow())
                    previous_window->ContentWidget()->_on_get_focus -= _content_focus_handle;
            }
            _active_index = new_index;
            auto *active_title_bg = _tab_titles->GetChildren()[_active_index]->As<UI::Border>();
            active_title_bg->_bg_color = g_editor_style._tab_active_bg_color;
            if (!active_title_bg->GetChildren().empty())
            {
                active_title_bg->GetChildren()[0]->As<UI::Text>()->_color = g_editor_style._tab_active_text_color;
                active_title_bg->GetChildren()[0]->InvalidatePaint();
            }
            active_title_bg->InvalidateStyle(UI::EStyleInvalidation::kPaintOnly);
            _tabs[_active_index]->SetTabActive(_is_visible);
            _content_focus_handle = -1;
            if (auto *active_window = _tabs[_active_index]->PrimaryWindow())
            {
                _content_focus_handle = active_window->ContentWidget()->_on_get_focus += [this]()
                {
                    UI::UIManager::Get()->BringToFront(_tab_bar.get());
                };
            }
            LOG_INFO("DockTab::AddTab: active index {}", _active_index);
        }

        Ref<IDockTabItem> DockTab::ActiveItem() const
        {
            if (_active_index < 0 || _active_index >= static_cast<i32>(_tabs.size()))
                return nullptr;
            return _tabs[_active_index];
        }

        DockWindow *DockTab::ActivePrimaryWindow() const
        {
            if (auto active_item = ActiveItem())
                return active_item->PrimaryWindow();
            return nullptr;
        }

        void DockTab::SetActiveIndex(i32 new_index)
        {
            if (new_index == _active_index)
                return;
            OnActiveTabChanged(new_index);
        }

        void DockTab::SetVisible(bool is_visible)
        {
            _is_visible = is_visible;
            _tab_bar->_visibility = is_visible ? UI::EVisibility::kVisible : UI::EVisibility::kHide;
            _tab_bar->InvalidatePaint(UI::EUIInvalidationReason::kVisibility | UI::EUIInvalidationReason::kPaint);
            for (i32 i = 0; i < static_cast<i32>(_tabs.size()); ++i)
            {
                _tabs[i]->SetTabActive(is_visible && i == _active_index);
            }
        }

        u32 DockTab::HoverEdge(Vector2f pos) const
        {
            return DockWindow::HoverEdge(_position, _size, pos);
        }

        bool DockTab::HoverDragArea(Vector2f pos) const
        {
            if (!_is_visible || _tabs.empty() || _drag_area == nullptr)
                return false;
            return UI::UIElement::IsPointInside(pos, _drag_area->GetArrangeRect());
        }

        bool DockTab::IsHover(Vector2f pos) const
        {
            if (_tabs.empty() || _active_index < 0 || _active_index >= static_cast<i32>(_tabs.size()))
                return false;
            return _tabs[_active_index]->IsHover(pos);
        }

        bool DockTab::Contains(DockWindow *w) const
        {
            auto it = std::find_if(_tabs.begin(), _tabs.end(), [&](const Ref<IDockTabItem> &e)
                                   { return e->ContainsWindow(w); });
            return it != _tabs.end();
        }

        void DockTab::SetFocus(bool is_focus)
        {
            if (_tabs.empty() || _active_index < 0 || _active_index >= static_cast<i32>(_tabs.size()))
                return;
            if (!is_focus && !_is_focused)
                return;
            _is_focused = is_focus;
            if (is_focus)
                UI::UIManager::Get()->BringToFront(_tab_bar.get());
            _tabs[_active_index]->SetFocus(is_focus);
        }

        void DockTab::Update(f32 dt)
        {
            _tab_bar->SetPosition(_position);
            _tab_bar->SetSize({_size.x, DockWindow::kTitleBarHeight});
            _tab_root->GetSlotAs<UI::CanvasSlot>().Size({_size.x, DockWindow::kTitleBarHeight});
            _tab_hb->GetSlot()->Size(_tab_root->GetSlot()->_size);
            bool tab_style_changed = false;
            tab_style_changed |= SetColorIfChanged(_tab_root->_bg_color, g_editor_style._window_title_bar_color);
            tab_style_changed |= SetColorIfChanged(_tab_root->_border_color, _is_focused ? g_editor_style._window_focus_border_color : g_editor_style._window_border_color);
            if (tab_style_changed)
                _tab_root->InvalidateStyle(UI::EStyleInvalidation::kPaintOnly);
            if (_tabs.empty())
                return;
            for (auto &w: _tabs)
            {
                w->SetRect({_position.x, _position.y, _size.x, _size.y});
                w->Update(dt);
            }
        }
#pragma endregion
    }// namespace Editor
}// namespace Ailu
