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
        namespace
        {
            LinearSlot *GetLinearSlot(UIElement *element)
            {
                return dynamic_cast<LinearSlot *>(element->GetSlot().get());
            }

            const LinearSlot *GetLinearSlot(const UIElement *element)
            {
                return dynamic_cast<const LinearSlot *>(element->GetSlot().get());
            }

            ESizePolicy GetSizePolicy(const UIElement *element, bool is_horizontal)
            {
                const LinearSlot *slot = GetLinearSlot(element);
                if (slot == nullptr)
                    return ESizePolicy::kAuto;
                return is_horizontal ? slot->_size_policy_h : slot->_size_policy_v;
            }

            EAlignment GetCrossAlignment(const UIElement *element, bool vertical_layout)
            {
                const LinearSlot *slot = GetLinearSlot(element);
                if (slot == nullptr)
                    return EAlignment::kCenter;
                return slot->_cross_align;
            }

            UIBrush ColorBrush(const Color &color)
            {
                UIBrush brush;
                brush._type = EUIBrushType::kColor;
                brush._tint = color;
                return brush;
            }
        }

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
            if (GetSizePolicy(this, true) == ESizePolicy::kFixed)
                return GetSlot()->_size;
            else
            {
                f32 w = 0.0f, h = 0.0f;
                for (auto &c: _children)
                {
                    auto &slot = c->GetSlotAs<CanvasSlot>();
                    auto p = slot._position;
                    auto s = slot._size;
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
                auto &slot = c->GetSlotAs<CanvasSlot>();
                Vector2f pos = slot._position;
                Vector2f s = slot._size_to_content ? c->MeasureDesiredSize() : slot._size;
                c->Arrange(pos.x, pos.y, s.x, s.y);
            }
        }

        void Canvas::RenderImpl(UIRenderer &r)
        {
            r.DrawVisual(_arrange_rect, _matrix, _resolved_visual);
            for (auto &child: _children)
            {
                child->Render(r);
            }
        }

        void Canvas::ResolveStyle(const UIStyleContext &context)
        {
            _resolved_visual._background = UIBrush{};
            _resolved_visual._background._type = EUIBrushType::kColor;
            _resolved_visual._background._tint = Colors::kTransparent;
            _style_override.ApplyTo(_resolved_visual);
        }

        const UIControlVisual *Canvas::GetVisual(EUIVisualState state) const
        {
            return &_resolved_visual;
        }
        Ref<UISlot> Canvas::CreateSlotForChild()
        {
            return MakeRef<CanvasSlot>();
        }

#pragma endregion

#pragma region LinearBox
        LinearBox::LinearBox(EOrientation orientation) : _orientation(orientation)
        {
            _name = std::format("{}_{}", orientation == EOrientation::kHorizontal ? "HorizontalBox" : "VerticalBox", _id);
        }
        Ref<UISlot> LinearBox::CreateSlotForChild()
        {
            return MakeRef<LinearSlot>();
        }
        Vector2f LinearBox::MeasureDesiredSize()
        {
            if (GetSizePolicy(this, true) == ESizePolicy::kFixed && GetSizePolicy(this, false) == ESizePolicy::kFixed)
                return GetSlot()->_size;
            f32 total_len = 0.0f;
            f32 max_cross = 0.0f;
            Vector2f desired_size = Vector2f::kZero;
            for (auto &c: _children)
            {
                const auto &margin = c->GetSlot()->_margin;
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
            r.DrawVisual(_arrange_rect, _matrix, _resolved_visual);
            for (auto &child: _children)
            {
                child->Render(r);
            }
        }

        void LinearBox::ResolveStyle(const UIStyleContext &context)
        {
            _resolved_visual._background = UIBrush{};
            _resolved_visual._background._type = EUIBrushType::kColor;
            _resolved_visual._background._tint = Colors::kTransparent;
            _style_override.ApplyTo(_resolved_visual);
        }

        const UIControlVisual *LinearBox::GetVisual(EUIVisualState state) const
        {
            return &_resolved_visual;
        }
        void LinearBox::MeasureAndArrange(f32 dt)
        {
            if (_children.empty())
                return;

            const f32 inner_w = std::max(0.0f, _content_rect.z);
            const f32 inner_h = std::max(0.0f, _content_rect.w);
            const f32 content_x = _padding._l;
            const f32 content_y = _padding._t;
            f32 occupied_main_size = 0.0f;
            f32 fill_margin_size = 0.0f;
            f32 fill_rate_total = 0.0f;

            // 统计主轴上非 Fill 元素占用的空间，以及 Fill 元素的权重和 margin。
            for (auto &child: _children)
            {
                const auto &slot = child->GetSlotAs<LinearSlot>();
                const Padding margin = slot._margin;
                const Vector2f desired_size = child->MeasureDesiredSize();
                const Vector2f slot_size = slot._size;

                if (_orientation == EOrientation::kVertical)
                {
                    const ESizePolicy main_policy = slot._size_policy_v;

                    if (main_policy == ESizePolicy::kFill)
                    {
                        fill_rate_total += std::max(0.0f, slot._fill_rate);
                        fill_margin_size += margin._t + margin._b;
                    }
                    else
                    {
                        const f32 child_h = main_policy == ESizePolicy::kFixed
                                        ? slot_size.y
                                        : desired_size.y;

                        occupied_main_size +=
                                margin._t +
                                std::max(0.0f, child_h) +
                                margin._b;
                    }
                }
                else
                {
                    const ESizePolicy main_policy = slot._size_policy_h;

                    if (main_policy == ESizePolicy::kFill)
                    {
                        fill_rate_total += std::max(0.0f, slot._fill_rate);
                        fill_margin_size += margin._l + margin._r;
                    }
                    else
                    {
                        const f32 child_w =
                                main_policy == ESizePolicy::kFixed
                                        ? slot_size.x
                                        : desired_size.x;

                        occupied_main_size +=
                                margin._l +
                                std::max(0.0f, child_w) +
                                margin._r;
                    }
                }
            }

            const f32 available_main_size =
                    _orientation == EOrientation::kVertical
                            ? inner_h
                            : inner_w;

            const f32 remaining_main_size = std::max(
                    0.0f,
                    available_main_size -
                            occupied_main_size -
                            fill_margin_size);

            f32 offset = 0.0f;

            for (auto &child: _children)
            {
                const auto &slot = child->GetSlotAs<LinearSlot>();
                const Padding margin = slot._margin;
                const Vector2f desired_size = child->MeasureDesiredSize();
                const Vector2f slot_size = slot._size;

                f32 child_w = 0.0f;
                f32 child_h = 0.0f;
                f32 x = content_x;
                f32 y = content_y;

                if (_orientation == EOrientation::kVertical)
                {
                    // 主轴：高度
                    switch (slot._size_policy_v)
                    {
                        case ESizePolicy::kFixed:
                            child_h = std::max(0.0f, slot_size.y);
                            break;

                        case ESizePolicy::kAuto:
                            child_h = std::max(0.0f, desired_size.y);
                            break;

                        case ESizePolicy::kFill:
                        {
                            const f32 fill_rate =
                                    std::max(0.0f, slot._fill_rate);

                            child_h =
                                    fill_rate_total > 0.0f
                                            ? remaining_main_size *
                                                      (fill_rate / fill_rate_total)
                                            : 0.0f;
                            break;
                        }
                    }

                    // 交叉轴：宽度
                    switch (slot._size_policy_h)
                    {
                        case ESizePolicy::kFixed:
                            child_w = std::max(0.0f, slot_size.x);
                            break;

                        case ESizePolicy::kAuto:
                            child_w = std::max(0.0f, desired_size.x);
                            break;

                        case ESizePolicy::kFill:
                            child_w = std::max(
                                    0.0f,
                                    inner_w - margin._l - margin._r);
                            break;
                    }

                    const f32 available_cross_size = std::max(
                            0.0f,
                            inner_w - margin._l - margin._r);

                    switch (slot._cross_align)
                    {
                        case EAlignment::kFill:
                        case EAlignment::kLeft:
                            x = content_x + margin._l;
                            break;

                        case EAlignment::kCenter:
                            x = content_x +
                                margin._l +
                                (available_cross_size - child_w) * 0.5f;
                            break;

                        case EAlignment::kRight:
                            x = content_x +
                                inner_w -
                                child_w -
                                margin._r;
                            break;

                        default:
                            x = content_x + margin._l;
                            break;
                    }

                    y = content_y + offset + margin._t;

                    child->Arrange(x, y, child_w, child_h);

                    offset += margin._t + child_h + margin._b;
                }
                else
                {
                    // 主轴：宽度
                    switch (slot._size_policy_h)
                    {
                        case ESizePolicy::kFixed:
                            child_w = std::max(0.0f, slot_size.x);
                            break;

                        case ESizePolicy::kAuto:
                            child_w = std::max(0.0f, desired_size.x);
                            break;

                        case ESizePolicy::kFill:
                        {
                            const f32 fill_rate =
                                    std::max(0.0f, slot._fill_rate);

                            child_w =
                                    fill_rate_total > 0.0f
                                            ? remaining_main_size *
                                                      (fill_rate / fill_rate_total)
                                            : 0.0f;
                            break;
                        }
                    }

                    // 交叉轴：高度
                    switch (slot._size_policy_v)
                    {
                        case ESizePolicy::kFixed:
                            child_h = std::max(0.0f, slot_size.y);
                            break;

                        case ESizePolicy::kAuto:
                            child_h = std::max(0.0f, desired_size.y);
                            break;

                        case ESizePolicy::kFill:
                            child_h = std::max(
                                    0.0f,
                                    inner_h - margin._t - margin._b);
                            break;
                    }

                    const f32 available_cross_size = std::max(
                            0.0f,
                            inner_h - margin._t - margin._b);

                    switch (slot._cross_align)
                    {
                        case EAlignment::kFill:
                        case EAlignment::kTop:
                            y = content_y + margin._t;
                            break;

                        case EAlignment::kCenter:
                            y = content_y +
                                margin._t +
                                (available_cross_size - child_h) * 0.5f;
                            break;

                        case EAlignment::kBottom:
                            y = content_y +
                                inner_h -
                                child_h -
                                margin._b;
                            break;

                        default:
                            y = content_y + margin._t;
                            break;
                    }
                    x = content_x + offset + margin._l;
                    child->Arrange(x, y, child_w, child_h);
                    offset += margin._l + child_w + margin._r;
                }

                child->Update(dt);
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
                const Vector2f local_mouse = TransformCoord(_inv_matrix, Vector3f{e._mouse_position, 0.0f}).xy;
                if (!_is_dragging_bar)
                {
                    _is_hover_hbar = HasHorizontalBar() && IsPointInside(local_mouse, CalculateHorizontalBarRect());
                    _is_hover_vbar = HasVerticalBar() && IsPointInside(local_mouse, CalculateVerticalBarRect());
                    _is_vertical = !_is_hover_hbar;
                }
            };

            OnMouseDown() += [this](UIEvent &e)
            {
                const Vector2f local_mouse = TransformCoord(_inv_matrix, Vector3f{e._mouse_position, 0.0f}).xy;
                if (HasVerticalBar() && IsPointInside(local_mouse, CalculateVerticalBarRect()))
                {
                    _is_vertical = true;
                    _is_dragging_bar = true;
                    _drag_start_mouse = Input::GetGlobalMousePosAccurate();
                    _drag_start_offset = _target_offset.y;
                    e._is_handled = true;
                    _scroll_speed *= 100.0f;
                }
                else if (HasHorizontalBar() && IsPointInside(local_mouse, CalculateHorizontalBarRect()))
                {
                    _is_vertical = false;
                    _is_dragging_bar = true;
                    _drag_start_mouse = Input::GetGlobalMousePosAccurate();
                    _drag_start_offset = _target_offset.x;
                    e._is_handled = true;
                    _scroll_speed *= 100.0f;
                }
                else
                {
                }
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
                    f32 scrollable_height = _content_size.y - _content_rect.w;
                    if (scrollable_height > 0.0f)
                    {
                        f32 bar_movable_height = _content_rect.w - CalculateVerticalBarRect().w;// _bar_rect.w = 滚动条高度
                        f32 offset_delta = -(delta * (scrollable_height / bar_movable_height));
                        _target_offset.y = std::clamp(_drag_start_offset + offset_delta, -scrollable_height, 0.0f);
                    }
                }
                else
                {
                    f32 delta = mpos.x - _drag_start_mouse.x;
                    f32 scrollable_width = _content_size.x - _content_rect.z;
                    if (scrollable_width > 0.0f)
                    {
                        f32 bar_movable_width = _content_rect.z - CalculateHorizontalBarRect().z;// _bar_rect.z = 滚动条宽度
                        f32 offset_delta = -(delta * (scrollable_width / bar_movable_width));
                        _target_offset.x = std::clamp(_drag_start_offset + offset_delta, -scrollable_width, 0.0f);
                    }
                }
                if (!Input::IsKeyDown(EKey::kLBUTTON))
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
            if (const UIControlVisual *visual = GetCurrentVisual())
                r.DrawVisual(_arrange_rect, _matrix, *visual);
            r.PushScissor(_abs_rect);
            for (auto &child: _children)
            {
                child->Render(r);
            }
            r.PopScissor();
            // Vertical scrollbar
            if (_content_size.y > _content_rect.w)
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
            // Horizontal scrollbar
            if (_content_size.x > _content_rect.z)
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

        void ScrollView::ResolveStyle(const UIStyleContext &context)
        {
            if (context._theme)
                _resolved_style = context._theme->_scroll_view_style;
            else
            {
                static UITheme s_default_theme = UITheme::DefaultDark();
                _resolved_style = s_default_theme._scroll_view_style;
            }
            _style_override.ApplyTo(_resolved_style);
        }

        const UIControlVisual *ScrollView::GetVisual(EUIVisualState state) const
        {
            switch (state)
            {
                case EUIVisualState::kHovered:
                    return &_resolved_style._hovered;
                case EUIVisualState::kFocused:
                    return &_resolved_style._focused;
                case EUIVisualState::kDisabled:
                    return &_resolved_style._disabled;
                default:
                    return &_resolved_style._normal;
            }
        }
        void ScrollView::PostDeserialize()
        {
            UIElement::PostDeserialize();
            _content_size = Vector2f::kZero;
            for (auto &c: _children)
                _content_size = Max(c->MeasureDesiredSize(), _content_size);
            _max_offset = Min(Vector2f::kZero, GetSlot()->_size - _content_size);
        }
        void ScrollView::MeasureAndArrange(f32 dt)
        {
            _content_size = Vector2f::kZero;
            for (auto &c: _children)
            {
                const auto &slot = c->GetSlotAs<LinearSlot>();
                const auto &s = c->MeasureDesiredSize();
                _content_size += s;
                c->Arrange(0.0f, 0.0f, _is_vertical ? _content_rect.z : s.x, s.y);
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
            return GetSlot()->_size;
        }
        Ref<UISlot> ScrollView::CreateSlotForChild()
        {
            return MakeRef<LinearSlot>();
        }
        bool ScrollView::HasVerticalBar() const
        {
            return _content_size.y > _content_rect.w && _content_rect.w > 0.0f;
        }
        bool ScrollView::HasHorizontalBar() const
        {
            return _content_size.x > _content_rect.z && _content_rect.z > 0.0f;
        }
        Vector4f ScrollView::CalculateVerticalBarRect() const
        {
            if (!HasVerticalBar())
                return Vector4f::kZero;
            f32 bar_height = _content_rect.w * (_content_rect.w / _content_size.y);
            f32 bar_y = -_current_offset.y * (_content_rect.w / _content_size.y) + _content_rect.y;
            return {_content_rect.x + _content_rect.z - kScrollBarWidth, bar_y, kScrollBarWidth, bar_height};
        }
        Vector4f ScrollView::CalculateHorizontalBarRect() const
        {
            if (!HasHorizontalBar())
                return Vector4f::kZero;
            f32 bar_width = _content_rect.z * (_content_rect.z / _content_size.x);
            f32 bar_x = -_current_offset.x * (_content_rect.z / _content_size.x) + _content_rect.x;
            return {bar_x, _content_rect.y + _content_rect.w - kScrollBarWidth, bar_width, kScrollBarWidth};
        }
        UIElement *ScrollView::HitTest(Vector2f pos)
        {
            Vector2f local_pos = TransformCoord(_inv_matrix, Vector3f{pos, 0.0f}).xy;
            if (!IsPointInside(local_pos))
                return nullptr;

            if ((HasVerticalBar() && IsPointInside(local_pos, CalculateVerticalBarRect())) ||
                (HasHorizontalBar() && IsPointInside(local_pos, CalculateHorizontalBarRect())))
                return this;

            for (auto &child: _children)
                if (auto *hit = child->HitTest(pos))
                    return hit;

            return this;
        }
#pragma endregion

#pragma region ListView
        ListView::ListView()
        {
            _content_box = AddChild<VerticalBox>();
            _content_box->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto);
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
        }
        void ListView::ClearItems()
        {
            _content_box->ClearChildren();
        }
        void ListView::SizeToContent(bool enable)
        {
        }
        Vector2f ListView::MeasureDesiredSize()
        {
            return ScrollView::MeasureDesiredSize();
        }

        void ListView::RenderImpl(UIRenderer &r)
        {
            ScrollView::RenderImpl(r);

            for (auto &c: _content_box->GetChildren())
            {
                // hover 高亮
                if (c.get() == _hovered_item)
                {
                    r.DrawQuad(c->GetArrangeRect(), _matrix, ColorBrush({0.2f, 0.2f, 0.4f, 0.5f}));
                }
                // selected 高亮
                if (c.get() == _selected_item)
                {
                    r.DrawQuad(c->GetArrangeRect(), _matrix, ColorBrush({0.3f, 0.3f, 0.6f, 0.8f}));
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
            _root = AddChild<HorizontalBox>();
            _root->GetSlot()->Size(GetSlot()->_size);
            _text = _root->AddChild<Text>();
            _text->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto);
            _button = _root->AddChild<Button>();
            _button->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFixed, ESizePolicy::kFill).Size({20.0f, 20.0f});
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
                list_view->SetSlot(MakeRef<LinearSlot>());
                list_view->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFixed, ESizePolicy::kAuto);
                list_view->Name(std::format("Dropdown_{}", _name));
                auto abs_rect = _text->GetArrangeRect();
                list_view->GetSlot()->Size({abs_rect.z, list_view->GetSlot()->_size.y});
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
            Vector2f size = GetSlot()->_size;
            if (GetSizePolicy(this, true) == ESizePolicy::kAuto)
            {
                size.x = 0.0f;
                for (auto &c: _children)
                {
                    auto margin = c->GetSlot()->_margin;
                    auto c_size = c->MeasureDesiredSize();
                    size.x += c_size.x + margin._l + margin._r;
                }
            }
            if (GetSizePolicy(this, false) == ESizePolicy::kAuto)
            {
                size.y = 0.0f;
                for (auto &c: _children)
                {
                    auto margin = c->GetSlot()->_margin;
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
            _root->GetSlot()->Size(GetSlot()->_size);
            _text->SetText(GetSelectedText());
            _button->GetSlot()->Size({GetSlot()->_size.y, GetSlot()->_size.y});
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
            if (auto slot = GetLinearSlot(this))
                slot->SizePolicy(ESizePolicy::kAuto, ESizePolicy::kAuto);
        }
        CollapsibleView::CollapsibleView(String title) : UIElement("CollapsibleView")
        {
            if (auto slot = GetLinearSlot(this))
                slot->SizePolicy(ESizePolicy::kAuto, ESizePolicy::kAuto);
            _header = AddChild<HorizontalBox>();
            _header->GetSlot()->Size({_header->GetSlot()->_size.x, s_header_height});
            _title = _header->AddChild<Text>(title);
            _title->OnMouseClick() += [this](UIEvent &e)
            {
                SetCollapsed(!_is_collapsed, false);
                LOG_INFO("CollapsibleView: collapsed {}", _is_collapsed);
            };
            auto content = AddChild<Border>();
            content->_bg_color = Color(0.0f, 0.0f, 0.0f, 0.0f);
            content->_border_color = Color(0.0f, 0.0f, 0.0f, 0.0f);
            _content = content;
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
            if (!_is_collapsed && !_content->GetChildren().empty())
                sz = _content->MeasureDesiredSize();
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
                Vector2f sz = _content->GetChildren().empty() ? Vector2f::kZero : _content->MeasureDesiredSize();
                _content->Arrange(0.0f, s_header_height, _content_rect.z, sz.y);
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
                bool is_hover_bar = IsPointInside(e._mouse_position, CalculateSplitBarRect(true));
                if (is_hover_bar != _is_hover_bar)
                {
                    _is_hover_bar = is_hover_bar;
                }
                if (_is_hover_bar || _is_dragging_bar)
                    Application::Get().SetCursor(_is_horizontal ? ECursorType::kSizeEW : ECursorType::kSizeNS,
                                                 ECursorPriority::kHigh);
                else
                    Application::Get().SetCursor(ECursorType::kArrow);

                if (_is_dragging_bar)
                {
                    Vector2f local_pos = e._mouse_position - _abs_rect.xy;
                    if (_is_horizontal)
                    {
                        SetRatio(local_pos.x / _abs_rect.z);
                    }
                    else
                    {
                        SetRatio(local_pos.y / _abs_rect.w);
                    }
                    e._is_handled = true;
                }
            };
            OnMouseDown() += [this](UIEvent &e)
            {
                if (_is_hover_bar)
                {
                    _is_dragging_bar = true;
                    Application::Get().SetCursor(_is_horizontal ? ECursorType::kSizeEW : ECursorType::kSizeNS,
                                                 ECursorPriority::kHigh);
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
        void SplitView::SetRatio(f32 ratio)
        {
            ratio = std::clamp(ratio, 0.1f, 0.9f);
            if (NearbyEqual(_ratio, ratio))
                return;
            _ratio = ratio;
            InvalidateLayout();
        }
        UIElement *SplitView::HitTest(Vector2f pos)
        {
            Vector2f local_pos = TransformCoord(_inv_matrix, {pos, 0.0f}).xy;
            if (!IsPointInside(local_pos))
                return nullptr;
            if (IsPointInside(pos, CalculateSplitBarRect(true)))
                return this;
            return UIElement::HitTest(pos);
        }
        void SplitView::RenderImpl(UIRenderer &r)
        {
            r.DrawVisual(_arrange_rect, _matrix, _resolved_visual);
            for (auto &child: _children)
            {
                child->Render(r);
            }
            Vector4f bar_rect = CalculateSplitBarRect(false);
            UIBrush bar_brush;
            bar_brush._type = EUIBrushType::kColor;
            bar_brush._tint = _is_hover_bar ? _resolved_visual._content_color : _resolved_visual._border_color;
            r.DrawQuad(bar_rect, _matrix, bar_brush);
        }

        void SplitView::ResolveStyle(const UIStyleContext &context)
        {
            _resolved_visual._background = UIBrush{};
            _resolved_visual._background._type = EUIBrushType::kColor;
            _resolved_visual._background._tint = Colors::kTransparent;
            _resolved_visual._content_color = Colors::kWhite;
            _resolved_visual._border_color = Color(0.8f, 0.8f, 0.9f, 0.6f);
            _style_override.ApplyTo(_resolved_visual);
        }

        const UIControlVisual *SplitView::GetVisual(EUIVisualState state) const
        {
            return &_resolved_visual;
        }
        void SplitView::PostDeserialize()
        {
        }
        Vector4f SplitView::CalculateSplitBarRect(bool is_absolute) const
        {
            Vector4f bar_rect = is_absolute ? _abs_rect : _content_rect;
            if (_is_horizontal)
            {
                bar_rect.x += bar_rect.z * _ratio - kSplitBarThickness * 0.5f;
                bar_rect.z = kSplitBarThickness;
            }
            else
            {
                bar_rect.y += bar_rect.w * _ratio - kSplitBarThickness * 0.5f;
                bar_rect.w = kSplitBarThickness;
            }
            return bar_rect;
        }
        void SplitView::MeasureAndArrange(f32 dt)
        {
            if (_children.size() != 2)
            {
                LOG_WARNING("SplitView: splitview({}) must have exactly two children!", _name);
                return;
            }
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
