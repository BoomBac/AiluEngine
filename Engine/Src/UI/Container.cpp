#include "UI/Container.h"
#include "Framework/Common/Application.h"
#include "Framework/Common/Input.h"
#include "UI/Basic.h"
#include "UI/UIFramework.h"
#include "UI/UIRenderer.h"
#include "pch.h"

namespace Ailu
{
    namespace UI
    {
#undef max
#pragma region Canvas
        Canvas::Canvas() : UIElement("Canvas")
        {
            _desired_rect = Vector4f(0.0f, 0.0f, 100.0f, 100.0f);
        }
        Canvas::~Canvas()
        {
        }

        Vector2f Canvas::MeasureDesiredSize()
        {
            if (_slot._size_policy_h == ESizePolicy::kFixed)
                return _slot._size;
            else
            {
                f32 w = 0.0f, h = 0.0f;
                for (auto &c: _children)
                {
                    auto p = c->SlotPosition();
                    auto s = c->SlotSize();
                    w = std::max(w, p.x + s.x);
                    h = std::max(h, p.y + s.y);
                }
                return {w, h};
            }
        }

        void Canvas::MeasureAndArrange(f32 dt)
        {
            for (auto &c: _children)
            {
                Vector2f pos = c->SlotPosition();
                Vector2f s = c->MeasureDesiredSize();
                if (c->SlotSizePolicy() == ESizePolicy::kFill)
                    s.x = _content_rect.z - pos.x;
                if (c->SlotSizePolicy(false) == ESizePolicy::kFill)
                    s.y = _content_rect.w - pos.y;
                c->Arrange(pos.x, pos.y, s.x, s.y);
            }
        }

        void Canvas::RenderImpl(UIRenderer &r)
        {
            for (auto &child: _children)
            {
                child->Render(r);
            }
        }

#pragma endregion

#pragma region LinearBox
        LinearBox::LinearBox(EOrientation orientation) : _orientation(orientation)
        {
            _name = std::format("{}_{}", orientation == EOrientation::kHorizontal ? "HorizontalBox" : "VerticalBox", _id);
        }
        Vector2f LinearBox::MeasureDesiredSize()
        {
            if (_slot._size_policy_h == ESizePolicy::kFixed && _slot._size_policy_v == ESizePolicy::kFixed)
                return _slot._size;
            f32 total_len = 0.0f;
            f32 max_cross = 0.0f;
            Vector2f desired_size = Vector2f::kZero;
            for (auto &c: _children)
            {
                const auto &margin = c->SlotMargin();
                Vector2f child_size = c->MeasureDesiredSize();

                if (_orientation == EOrientation::kVertical)
                {
                    total_len += margin._t + child_size.y + margin._b;
                    max_cross = std::max(max_cross, child_size.x + margin._l + margin._r);
                }
                else
                {
                    total_len += margin._l + child_size.x + margin._r;
                    max_cross = std::max(max_cross, child_size.y + margin._t + margin._b);
                }
            }

            if (_orientation == EOrientation::kVertical)
            {
                desired_size.x = max_cross + _padding._l + _padding._r;
                desired_size.y = total_len + _padding._t + _padding._b;
            }
            else
            {
                desired_size.x = total_len + _padding._l + _padding._r;
                desired_size.y = max_cross + _padding._t + _padding._b;
            }
            return desired_size;
        }
        void LinearBox::RenderImpl(UIRenderer &r)
        {
            //if (_orientation == EOrientation::kVertical)
            //    r.DrawBox(_arrange_rect.xy, _arrange_rect.zw,_matrix, 1.0f, Colors::kRed);
            for (auto &child: _children)
            {
                child->Render(r);
                //auto rect = child->GetArrangeRect();
                //if (_orientation == EOrientation::kVertical)
                //    r.DrawBox(rect.xy, rect.zw, 1.0f, Colors::kGreen);
            }
        }
        void LinearBox::MeasureAndArrange(f32 dt)
        {
            const f32 element_count = static_cast<f32>(_children.size());
            if (element_count <= 0.0f)
                return;

            // ---------- SizeToContent 模式 ----------
            Vector2f content_size = MeasureDesiredSize();

            // ---------- 可用区域 ----------
            f32 available_w = _content_rect.z;
            f32 available_h = _content_rect.w;

            // 统计 fill 元素
            f32 fixed_total = 0.0f;
            f32 fill_rate_total = 0.0f;
            f32 margin_total = 0.0f;
            for (auto &c: _children)
            {
                Vector2f child_desired_size = c->MeasureDesiredSize();
                const auto &margin = c->SlotMargin();
                if (_orientation == EOrientation::kVertical)
                {
                    if (c->SlotSizePolicy(false) == ESizePolicy::kFill)
                    {
                        fill_rate_total += std::max(0.0f, c->SlotFillRate());
                    }
                    else
                    {
                        fixed_total += margin._t + child_desired_size.y + margin._b;
                    }
                    margin_total += margin._t + margin._b;
                }
                else
                {
                    if (c->SlotSizePolicy(true) == ESizePolicy::kFill)
                    {
                        fill_rate_total += std::max(0.0f, c->SlotFillRate());
                    }
                    else
                    {
                        fixed_total += margin._l + child_desired_size.x + margin._r;
                    }
                    margin_total += margin._l + margin._r;
                }
            }

            f32 remaining_h = available_h - fixed_total;
            f32 remaining_w = available_w - fixed_total;
            if (_orientation == EOrientation::kHorizontal)
                remaining_w -= margin_total;
            else
                remaining_h -= margin_total;
            // ---------- 子元素布局 ----------
            f32 offset = 0.0f;

            for (auto &c: _children)
            {
                const auto &margin = c->SlotMargin();
                Vector2f desired = c->MeasureDesiredSize();
                f32 child_w = 0.0f;
                f32 child_h = 0.0f;
                f32 x = _padding._l;
                f32 y = _padding._t;

                if (_orientation == EOrientation::kVertical)
                {
                    const auto &margin = c->SlotMargin();
                    f32 inner_w = available_w - _padding._l - _padding._r;

                    // --- SizePolicy (决定尺寸) ---
                    switch (c->SlotSizePolicy(false))
                    {
                        case ESizePolicy::kFixed:
                        case ESizePolicy::kAuto:
                            child_h = desired.y;
                            break;
                        case ESizePolicy::kFill:
                            child_h = (fill_rate_total > 0.0f) ? remaining_h * (c->SlotFillRate() / fill_rate_total) : 0.0f;
                            break;
                    }
                    switch (c->SlotSizePolicy(true))
                    {
                        case ESizePolicy::kFixed:
                        case ESizePolicy::kAuto:
                            child_w = desired.x;
                            break;
                        case ESizePolicy::kFill:
                            child_w = std::max(0.0f, inner_w - margin._l - margin._r);
                            break;
                    }

                    // --- Alignment (决定位置) ---
                    switch (c->SlotAlignmentH())
                    {
                        case EAlignment::kFill:
                        case EAlignment::kLeft:
                            x = _padding._l + margin._l;
                            break;
                        case EAlignment::kCenter:
                            x = _padding._l + margin._l + (inner_w - child_w - margin._l - margin._r) * 0.5f;
                            break;
                        case EAlignment::kRight:
                            x = _padding._l + inner_w - child_w - margin._r;
                            break;
                    }

                    y = _padding._t + offset + margin._t;
                    c->Arrange(x, y, child_w, child_h);

                    offset += margin._t + child_h + margin._b;
                }
                else
                {
                    const auto &margin = c->SlotMargin();
                    f32 inner_h = available_h - _padding._t - _padding._b;
                    f32 child_w = 0.0f;
                    f32 child_h = 0.0f;
                    f32 x = 0.0f;
                    f32 y = 0.0f;

                    // ---------------- 宽度（主轴方向）SizePolicy ----------------
                    switch (c->SlotSizePolicy(true))// true = horizontal
                    {
                        case ESizePolicy::kFixed:
                        case ESizePolicy::kAuto:
                            child_w = desired.x;
                            break;

                        case ESizePolicy::kFill:
                            child_w = (fill_rate_total > 0.0f)
                                              ? remaining_w * (c->SlotFillRate() / fill_rate_total)
                                              : 0.0f;
                            break;
                    }

                    // ---------------- 高度（交叉方向）SizePolicy ----------------
                    switch (c->SlotSizePolicy(false))// false = vertical
                    {
                        case ESizePolicy::kFixed:
                        case ESizePolicy::kAuto:
                            child_h = desired.y;
                            break;

                        case ESizePolicy::kFill:
                            child_h = std::max(0.0f, inner_h - margin._t - margin._b);
                            break;
                    }

                    // ---------------- 对齐（纵向）AlignmentV ----------------
                    switch (c->SlotAlignmentV())
                    {
                        case EAlignment::kFill:
                        case EAlignment::kTop:
                            y = _padding._t + margin._t;
                            break;

                        case EAlignment::kCenter:
                            y = _padding._t + margin._t +
                                (inner_h - child_h - margin._t - margin._b) * 0.5f;
                            break;

                        case EAlignment::kBottom:
                            y = _padding._t + inner_h - child_h - margin._b;
                            break;
                    }

                    // ---------------- 主轴位置 ----------------
                    x = _padding._l + offset + margin._l;

                    // ---------------- 布局与累计 ----------------
                    c->Arrange(x, y, child_w, child_h);
                    offset += margin._l + child_w + margin._r;
                }


                c->Update(dt);
            }
        }
#pragma endregion

#pragma region ScrollView
        ScrollView::ScrollView() : UIElement("ScrollView")
        {
            OnMouseScroll() += [this](UIEvent &e)
            {
                if (_is_vertical)
                {
                    _target_offset.y += e._scroll_delta;
                    _target_offset.y = std::clamp(_target_offset.y, _max_offset.y, 0.0f);
                }
                else
                {
                    _target_offset.x += e._scroll_delta;
                    _target_offset.x = std::clamp(_target_offset.x, _max_offset.x, 0.0f);
                }
                e._is_handled = true;
            };
            OnMouseMove() += [this](UIEvent &e)
            {
                if (!_is_dragging_bar)
                {
                    _is_hover_hbar = IsPointInside(e._mouse_position, _hbar_rect);
                    _is_hover_vbar = IsPointInside(e._mouse_position, _vbar_rect);
                    _is_vertical = !_is_hover_hbar;
                }
            };

            OnMouseDown() += [this](UIEvent &e)
            {
                if (IsPointInside(e._mouse_position, _vbar_rect))
                {
                    _is_dragging_bar = true;
                    _drag_start_mouse = Input::GetGlobalMousePosAccurate();
                    _drag_start_offset = _target_offset.y;
                    e._is_handled = true;
                    _scroll_speed *= 100.0f;
                }
                else if (IsPointInside(e._mouse_position, _hbar_rect))
                {
                    _is_dragging_bar = true;
                    _drag_start_mouse = Input::GetGlobalMousePosAccurate();
                    _drag_start_offset = _target_offset.x;
                    e._is_handled = true;
                    _scroll_speed *= 100.0f;
                }
                else {}
            };
            OnMouseUp() += [this](UIEvent &e)
            {
                if (_is_dragging_bar)
                {
                    _is_dragging_bar = false;
                    e._is_handled = true;
                    _scroll_speed *= 0.01f;
                }
            };
            OnMouseExit() += [this](UIEvent &e)
            {
                if (!_is_dragging_bar)
                {
                    _is_hover_hbar = false;
                    _is_hover_vbar = false;
                }
            };
        }
        void ScrollView::PreUpdate(f32 dt)
        {
            UIElement::PreUpdate(dt);
            if (_is_dragging_bar)
            {
                auto mpos = Input::GetGlobalMousePosAccurate();
                if (_is_vertical)
                {
                    f32 delta = mpos.y - _drag_start_mouse.y;
                    // 根据滚动条比例转换到内容偏移
                    f32 scrollable_height = _content_size.y - _arrange_rect.w;
                    if (scrollable_height > 0.0f)
                    {
                        f32 bar_movable_height = _arrange_rect.w - _vbar_rect.w;// _bar_rect.w = 滚动条高度
                        f32 offset_delta = -(delta * (scrollable_height / bar_movable_height));
                        _target_offset.y = std::clamp(_drag_start_offset + offset_delta, -scrollable_height, 0.0f);
                    }
                }
                else
                {
                    f32 delta = mpos.x - _drag_start_mouse.x;
                    f32 scrollable_width = _content_size.x - _arrange_rect.z;
                    if (scrollable_width > 0.0f)
                    {
                        f32 bar_movable_width = _arrange_rect.z - _hbar_rect.z;// _bar_rect.z = 滚动条宽度
                        f32 offset_delta = -(delta * (scrollable_width / bar_movable_width));
                        _target_offset.x = std::clamp(_drag_start_offset + offset_delta, -scrollable_width, 0.0f);
                    }
                }
                if (!Input::IsKeyPressed(EKey::kLBUTTON))
                {
                    _is_hover_hbar = false;
                    _is_hover_vbar = false;
                    if (_is_dragging_bar)
                    {
                        _is_dragging_bar = false;
                        _scroll_speed *= 0.01f;
                    }
                }
            }
            _current_offset = Lerp(_current_offset, _target_offset, std::clamp(dt * _scroll_speed, 0.0f, 1.0f));
            for (auto &c: _children)
            {
                c->Translate(_current_offset);
            }
        }

        void ScrollView::RenderImpl(UIRenderer &r)
        {
            r.DrawQuad(_content_rect, _matrix, Vector4f{0.12f, 0.12f, 0.21f, 1.0f});
            r.PushScissor(_abs_rect);
            for (auto &child: _children)
            {
                child->Render(r);
            }
            r.PopScissor();
            if (_content_size.y > _content_rect.w)
            {
                f32 bar_height = _content_rect.w * (_content_rect.w / _content_size.y);
                f32 bar_y = -_current_offset.y * (_content_rect.w / _content_size.y) + _content_rect.y;
                _vbar_rect = {_content_rect.x + _content_rect.z - 6.0f, bar_y, kScrollBarWidth, bar_height};
                r.DrawQuad(_vbar_rect, _matrix, Vector4f{0.8f, 0.8f, 0.9f, _is_hover_vbar ? 1.0f : 0.6f});
            }
            if (_content_size.x > _content_rect.z)
            {
                f32 bar_width = _content_rect.z * (_content_rect.z / _content_size.x);
                f32 bar_x = -_current_offset.x * (_content_rect.z / _content_size.x) + _content_rect.x;
                _hbar_rect = {bar_x, _content_rect.y + _content_rect.w - 6.0f, bar_width, kScrollBarWidth};
                r.DrawQuad(_hbar_rect, _matrix, Vector4f{0.8f, 0.8f, 0.9f, _is_hover_hbar ? 1.0f : 0.6f});
            }
        }
        void ScrollView::PostDeserialize()
        {
            UIElement::PostDeserialize();
            _slot._size.y = _slot._size.y;
            _content_size = Vector2f::kZero;
            for (auto &c: _children)
                _content_size = Max(c->MeasureDesiredSize(), _content_size);
            _max_offset = Min(Vector2f::kZero, _slot._size - _content_size);
        }
        void ScrollView::MeasureAndArrange(f32 dt)
        {
            _content_size = Vector2f::kZero;
            for (auto &c: _children)
            {
                const auto &p = c->SlotPosition();
                const auto &s = c->MeasureDesiredSize();
                _content_size += s;
                c->Arrange(p.x, p.y, c->SlotSizePolicy(_is_vertical) == ESizePolicy::kFill ? _content_rect.z : s.x, s.y);
                c->Translate(_current_offset);
                c->MeasureAndArrange(dt);
            }
            _max_offset = Min(Vector2f::kZero, _arrange_rect.zw - _content_size);
        }
        void ScrollView::PostArrange()
        {
            //_max_offset = Min(Vector2f::kZero, _arrange_rect.zw - _content_size);
            //_max_offset = Min(Vector2f::kZero, _arrange_rect.zw - _content_size);
            //LOG_INFO("ScrollView::PostArrange: view{} update max_offset, y is {}",_name, _max_offset.y);
        }
        Vector2f ScrollView::MeasureDesiredSize()
        {
            //if (_slot._size_policy == ESizePolicy::kAuto)
            //{
            //    Vector2f content_size = Vector2f::kZero;
            //    for (auto c: _children)
            //    {
            //        const auto& s = c->MeasureDesiredSize();
            //        content_size.x = std::max(content_size.x, s.x);
            //        content_size.y = std::max(content_size.y, s.y);
            //    }
            //    return content_size + Vector2f(kScrollBarWidth, kScrollBarWidth);
            //}
            return _slot._size;
        }
#pragma endregion

#pragma region ListView
        ListView::ListView()
        {
            _content_box = AddChild<VerticalBox>();
            _content_box->SlotSizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto);
            //_content_box->SlotSizeToContent(true);
            OnMouseMove() += [this](UIEvent &e)
            {
                _hovered_item = nullptr;
                for (int i = 0; i < _content_box->GetChildren().size(); i++)
                {
                    auto &child = _content_box->GetChildren()[i];
                    if (child->IsPointInside(e._mouse_position))
                    {
                        _hovered_item = child.get();
                        break;
                    }
                }
            };

            OnMouseExit() += [this](UIEvent &e)
            {
                _hovered_item = nullptr;
            };

            //OnMouseClick() += [this](UIEvent &e)
            //{
            //    if (_hovered_item)
            //    {
            //        _selected_item = _hovered_item;
            //        if (i32 index = _content_box->IndexOf(_selected_item); index != -1)
            //            _on_item_clicked_delegate.Invoke(_selected_item, index);
            //        e._is_handled = true;
            //    }
            //};
        }
        void ListView::AddItem(Ref<UIElement> item)
        {
            _content_box->AddChild(item);
            //Vector2f desired_size = _content_box->MeasureDesiredSize();
            //desired_size.x = _content_box->SlotSizePolicy() == ESizePolicy::kFill ? _slot._size.x - kScrollBarWidth : desired_size.x;
            //if (_is_size_to_content)
            //    _slot._size = desired_size;
            //_slot._size.x = desired_size.x + kScrollBarWidth;
            //_content_size.y = desired_size.y;
            //_max_offset.y = std::min(0.0f, _slot._size.y - _content_size.y);
        }
        void ListView::ClearItems()
        {
            _content_box->ClearChildren();
        }
        void ListView::SizeToContent(bool enable)
        {
            //_is_size_to_content = enable;
            //if (_is_size_to_content)
            //{
            //    _slot._size = MeasureDesiredSize();
            //    _slot._size += kScrollBarWidth;
            //    _content_size = _slot._size;
            //    _max_offset = Min(Vector2f::kZero, _slot._size - _content_size);
            //}
            //else
            //{
            //    _slot._size = {100.0f, 100.0f};
            //    _content_size = _content_box->MeasureDesiredSize();
            //    _max_offset = Min(Vector2f::kZero, _slot._size - _content_size);
            //}
        }
        Vector2f ListView::MeasureDesiredSize()
        {
            return ScrollView::MeasureDesiredSize();
            //if (_slot._size_policy_v == ESizePolicy::kAuto)
            //{
            //    Vector2f desired_size = _content_box->MeasureDesiredSize();
            //    desired_size.x += kScrollBarWidth;
            //    return desired_size;
            //}
            //else
            //{
            //    return UIElement::MeasureDesiredSize();
            //}
        }

        void ListView::RenderImpl(UIRenderer &r)
        {
            ScrollView::RenderImpl(r);

            for (auto &c: _content_box->GetChildren())
            {
                // hover 高亮
                if (c.get() == _hovered_item)
                {
                    r.DrawQuad(c->GetArrangeRect(), _matrix, {0.2f, 0.2f, 0.4f, 0.5f});
                }
                // selected 高亮
                if (c.get() == _selected_item)
                {
                    r.DrawQuad(c->GetArrangeRect(), _matrix, {0.3f, 0.3f, 0.6f, 0.8f});
                }
            }
        }

#pragma endregion

#pragma region Dropdown
        Dropdown::Dropdown() : Dropdown(Vector<String>())
        {
        }
        Dropdown::Dropdown(const Vector<String> &items) : UIElement("Dropdown")
        {
            _root = AddChild<HorizontalBox>()->SlotSizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto).As<HorizontalBox>();
            _text = _root->AddChild<Text>();
            _text->SlotSizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto);
            _button = _root->AddChild<Button>();
            _button->SlotSizePolicy(ESizePolicy::kFixed, ESizePolicy::kFill).SlotSize({20.0f, 20.0f});
            _items = items;
            _button->OnMouseClick() += [this](UIEvent &e)
            {
                if (_is_dropdown_open)
                {
                    UIManager::Get()->HidePopup();
                    _is_dropdown_open = false;
                    return;
                }
                auto list_view = MakeRef<ListView>();
                list_view->SlotSizePolicy(ESizePolicy::kFixed, ESizePolicy::kAuto);
                list_view->Name(std::format("Dropdown_{}", _name));
                auto abs_rect = _text->GetArrangeRect();
                list_view->SlotSize(abs_rect.z, list_view->SlotSize().y);
                for (u16 i = 0; i < (u16) _items.size(); i++)
                {
                    auto text = MakeRef<Text>(_items[i]);
                    text->OnMouseClick() += [this, i](UIEvent &e)
                    {
                        LOG_INFO("Dropdown item clicked: {}", _items[i]);
                        SetSelectedIndex(i);
                        UIManager::Get()->HidePopup();
                    };
                    list_view->AddItem(text);
                }
                Vector2f size = list_view->MeasureDesiredSize();
                if (size.y > 200.0f)
                {
                    list_view->SetViewportHeight(200.0f);
                }
                else
                    list_view->SizeToContent(true);
                UIManager::Get()->ShowPopupAt(abs_rect.x, abs_rect.y + abs_rect.w, list_view, [this]()
                                              { _is_dropdown_open = false; });
                _is_dropdown_open = true;
            };
        }
        void Dropdown::SetSelectedIndex(i32 index)
        {
            _selected_index = index;
            _on_selected_changed_delegate.Invoke(_selected_index);
            if (_selected_index >= 0 && _selected_index < static_cast<i32>(_items.size()))
                _text->SetText(_items[_selected_index]);
            if (_is_dropdown_open)
                _is_dropdown_open = false;
        }

        String Dropdown::GetSelectedText() const
        {
            if (_selected_index >= 0 && _selected_index < static_cast<i32>(_items.size()))
                return _items[_selected_index];
            return _items.empty() ? "null" : _items[0];
        }
        Vector2f Dropdown::MeasureDesiredSize()
        {
            Vector2f size = _slot._size;
            if (_slot._size_policy_h == ESizePolicy::kAuto)
            {
                size.x = 0.0f;
                for (auto &c: _children)
                {
                    auto margin = c->SlotMargin();
                    auto c_size = c->MeasureDesiredSize();
                    size.x += c_size.x + margin._l + margin._r;
                }
            }
            if (_slot._size_policy_v == ESizePolicy::kAuto)
            {
                size.y = 0.0f;
                for (auto &c: _children)
                {
                    auto margin = c->SlotMargin();
                    auto c_size = c->MeasureDesiredSize();
                    size.y = std::max(size.y, c_size.y + margin._t + margin._b);
                }
            }
            return size;
        }
        void Dropdown::RenderImpl(UIRenderer &r)
        {
            _root->Render(r);
        }
        void Dropdown::PostDeserialize()
        {
            UIElement::PostDeserialize();
            _root->SlotSize(_slot._size);
            _text->SetText(GetSelectedText());
            _button->SlotSize(_slot._size.y, _slot._size.y);
        }
        UIElement *Dropdown::HitTest(Vector2f pos)
        {
            Vector2f lpos = TransformCoord(_inv_matrix, Vector3f{pos, 0.0f}).xy;
            //test text and button
            if (!IsPointInside(lpos))
                return nullptr;
            if (_text->HitTest(pos))
                return _text;
            return _button->HitTest(pos) ? (UIElement *) _button : this;
        }
        void Dropdown::PostArrange()
        {
            InvalidateTransform();
            _root->Arrange(0.0f, 0.0f, _content_rect.z, _content_rect.w);
        }
#pragma endregion

#pragma region CollapsibleView
        CollapsibleView::CollapsibleView() : CollapsibleView("CollapsibleView")
        {
            SlotSizePolicy(ESizePolicy::kAuto);
        }
        CollapsibleView::CollapsibleView(String title) : UIElement("CollapsibleView")
        {
            SlotSizePolicy(ESizePolicy::kAuto);
            _header = AddChild<HorizontalBox>();
            _header->SlotSize(_header->SlotSize().x, s_header_height);
            _header->SlotSizePolicy(ESizePolicy::kAuto);
            _title = _header->AddChild<Text>(title);
            _title->OnMouseClick() += [this](UIEvent &e)
            {
                SetCollapsed(!_is_collapsed, false);
                LOG_INFO("CollapsibleView: collapsed {}", _is_collapsed);
            };
            //auto bd = _header->AddChild<Border>();
            _content = AddChild<Canvas>();
            auto cp = _content->SlotPosition();
            cp.y += s_header_height;
            _content->SlotPosition(cp);
        }
        void CollapsibleView::Update(f32 dt)
        {
            UIElement::Update(dt);
            //_header->Update(dt);
            //_content->Update(dt);
        }
        void CollapsibleView::SetCollapsed(bool collapsed, bool animated)
        {
            _is_collapsed = collapsed;
            _is_animated = animated;
            _content->SetVisible(!_is_collapsed);
            InvalidateLayout();
        }
        Vector2f CollapsibleView::MeasureDesiredSize()
        {
            Vector2f sz;
            if (!_is_collapsed)
                sz = (*_content)[0]->MeasureDesiredSize();
            sz.x = std::max(sz.x, _header->MeasureDesiredSize().x);
            sz.y += s_header_height;
            return sz;
        }
        void CollapsibleView::SetTitle(const String &title)
        {
            _title->SetText(title);
        }
        String CollapsibleView::GetTitle() const
        {
            return _title->GetText();
        }
        void CollapsibleView::RenderImpl(UIRenderer &r)
        {
            _header->Render(r);
            if (!_is_collapsed)
                _content->Render(r);
        }
        void CollapsibleView::PostDeserialize()
        {
            _header = _children[0].get();
            _content = _children[1].get();
            _title = (*_header)[0]->As<UI::Text>();
            _title->OnMouseClick() += [this](UIEvent &e)
            {
                SetCollapsed(!_is_collapsed, false);
                LOG_INFO("CollapsibleView: collapsed {}", _is_collapsed);
            };
        }
        void CollapsibleView::MeasureAndArrange(f32 dt)
        {
            _header->Arrange(0.0f, 0.0f, _content_rect.z, s_header_height);
            if (!_is_collapsed)
            {
                Vector2f sz = (*_content)[0]->MeasureDesiredSize();
                _content->Arrange(0.0f, s_header_height, _content_rect.z, sz.y);
                (*_content)[0]->Arrange(0.0f, 0.0f, _content_rect.z, sz.y);
            }
        }
#pragma endregion
#pragma region SplitView
        SplitView::SplitView() : UIElement("SplitView")
        {
            _on_child_add += [this](UIElement *child)
            {
                if (_children.size() > 2)
                {
                    LOG_WARNING("SplitView: splitview({}) can only have two child!", _name);
                    RemoveChild(_children.back());
                }
            };
            OnMouseMove() += [this](UIEvent &e)
            {
                Vector4f bar_rect = _abs_rect;
                if (_is_horizontal)
                {
                    bar_rect.x += _abs_rect.z * _ratio - kSplitBarThickness * 0.5f;
                    bar_rect.z = kSplitBarThickness;
                }
                else
                {
                    bar_rect.y += _abs_rect.w * _ratio - kSplitBarThickness * 0.5f;
                    bar_rect.w = kSplitBarThickness;
                }
                bool is_hover_bar = IsPointInside(e._mouse_position, bar_rect);
                if (is_hover_bar != _is_hover_bar)
                {
                    _is_hover_bar = is_hover_bar;
                    if (_is_hover_bar)
                    {
                        // change cursor
                        if (_is_horizontal)
                            Application::Get().SetCursor(ECursorType::kSizeEW);
                        else
                            Application::Get().SetCursor(ECursorType::kSizeNS);
                    }
                    else
                    {
                        Application::Get().SetCursor(ECursorType::kArrow);
                    }
                }

                if (_is_dragging_bar)
                {
                    Vector2f local_pos = e._mouse_position - _abs_rect.xy;
                    if (_is_horizontal)
                    {
                        _ratio = local_pos.x / _abs_rect.z;
                    }
                    else
                    {
                        _ratio = local_pos.y / _abs_rect.w;
                    }
                    _ratio = std::clamp(_ratio, 0.1f, 0.9f);
                    InvalidateLayout();
                    e._is_handled = true;
                }
            };
            OnMouseDown() += [this](UIEvent &e)
            {
                if (_is_hover_bar)
                {
                    _is_dragging_bar = true;
                    e._is_handled = true;
                }
            };
            OnMouseUp() += [this](UIEvent &e)
            {
                if (_is_dragging_bar)
                {
                    _is_dragging_bar = false;
                    e._is_handled = true;
                }
            };
        }
        void SplitView::Update(f32 dt)
        {
            UIElement::Update(dt);
        }
        void SplitView::RenderImpl(UIRenderer &r)
        {
            for (auto &child: _children)
            {
                child->Render(r);
            }
            Vector4f bar_rect = _content_rect;
            if (_is_horizontal)
            {
                bar_rect.x += _content_rect.z * _ratio - kSplitBarThickness * 0.5f;
                bar_rect.z = kSplitBarThickness;
            }
            else
            {
                bar_rect.y += _content_rect.w * _ratio - kSplitBarThickness * 0.5f;
                bar_rect.w = kSplitBarThickness;
            }
            r.DrawQuad(bar_rect, _matrix);
        }
        void SplitView::PostDeserialize()
        {
        }
        void SplitView::MeasureAndArrange(f32 dt)
        {
            if (_children.size() != 2)
                return;
            if (_is_horizontal)
            {
                _children[0]->Arrange(0.0f, 0.0f, _content_rect.z * _ratio, _content_rect.w);
                _children[1]->Arrange(_content_rect.z * _ratio, 0.0f, _content_rect.z * (1.0f - _ratio), _content_rect.w);
            }
            else
            {
                _children[0]->Arrange(0.0f, 0.0f, _content_rect.z, _content_rect.w * _ratio);
                _children[1]->Arrange(0.0f, _content_rect.w * _ratio, _content_rect.z, _content_rect.w * (1.0f - _ratio));
            }
            for (auto &c: _children)
            {
                c->InvalidateLayout();
                c->InvalidateTransform();
            }
        }
#pragma endregion
    }// namespace UI
}// namespace Ailu
