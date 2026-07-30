#include "pch.h"
#include "UI/Basic.h"
#include "UI/UIFramework.h"
#include "UI/UIRenderer.h"
#include "UI/TextRenderer.h"
#include "Framework/Common/Application.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/Input.h"

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

            UIBrush ColorBrush(const Color &color)
            {
                UIBrush brush;
                brush._type = EUIBrushType::kColor;
                brush._tint = color;
                return brush;
            }

        }

#pragma region Button
        Button::Button() : Button::Button("button")
        {
        }
        Button::Button(const String &text) : UIElement()
        {
            _name = "button";
            _text = nullptr;
            _icon = nullptr;
            _on_child_add += [this](UIElement *child)
            {
                if (auto *text_child = dynamic_cast<Text *>(child); text_child != nullptr)
                    _text = text_child;
                else if (auto *image_child = dynamic_cast<Image *>(child); image_child != nullptr)
                    _icon = image_child;
            };
            _on_child_remove += [this](UIElement *child)
            {
                if (child == _text)
                    _text = nullptr;
                if (child == _icon)
                    _icon = nullptr;
            };
            if (!text.empty())
                SetText(text);
        }

        Vector2f Button::MeasureDesiredSize()
        {
            EnsureStyleResolved();
            Vector2f desired_size = _resolved_style._min_size;
            if (auto *slot = GetLinearSlot(this); slot != nullptr)
            {
                if (slot->_size_policy_h == ESizePolicy::kFixed)
                    desired_size.x = slot->_size.x;
                if (slot->_size_policy_v == ESizePolicy::kFixed)
                    desired_size.y = slot->_size.y;
            }
            return desired_size;
        }

        UIElement *Button::HitTest(Vector2f pos)
        {
            Vector2f local_pos = TransformCoord(_inv_matrix, Vector3f{pos, 0.0f}).xy;
            return IsPointInside(local_pos) ? this : nullptr;
        }

        void Button::SetStyleId(const UIStyleId &id)
        {
            if (_style_id == id)
                return;
            _style_id = id;
            InvalidateStyle();
        }

        void Button::ResolveStyle(const UIStyleContext &context)
        {
            if (context._theme)
            {
                const UIButtonStyle *theme_style = context._theme->FindButtonStyle(_style_id);
                if (theme_style)
                    _resolved_style = *theme_style;
                else
                    _resolved_style = context._theme->_button_style;
            }
            else
            {
                static UITheme s_default_theme = UITheme::DefaultDark();
                _resolved_style = s_default_theme._button_style;
            }

            _style_override.ApplyTo(_resolved_style);
            _padding = _resolved_style._padding;
            if (_text != nullptr)
            {
                _text->FontSize(_resolved_style._font_size, false);
                if (const UIControlVisual *visual = GetCurrentVisual())
                    _text->_color = visual->_content_color;
            }
        }

        const UIControlVisual *Button::GetVisual(EUIVisualState state) const
        {
            switch (state)
            {
                case EUIVisualState::kHovered:
                    return &_resolved_style._hovered;
                case EUIVisualState::kPressed:
                    return &_resolved_style._pressed;
                case EUIVisualState::kFocused:
                    return &_resolved_style._focused;
                case EUIVisualState::kDisabled:
                    return &_resolved_style._disabled;
                default:
                    return &_resolved_style._normal;
            }
        }

        void Button::SetText(const String &text, bool trigger_event)
        {
            if (_text == nullptr)
            {
                _text = AddChild<Text>(text);
                _text->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFill);
                _text->_horizontal_align = EAlignment::kCenter;
                _text->_vertical_align = EAlignment::kCenter;
            }
            _text->SetText(text, trigger_event);
        }
        String Button::GetText() const
        {
            if (_text != nullptr)
                return _text->GetText();
            return "button";
        }
        void Button::SetTexture(Render::Texture *tex)
        {
            if (_icon == nullptr)
            {
                _icon = AddChild<Image>(tex);
                _icon->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFill);
            }
            else
                _icon->SetTexture(tex);
        }
        Render::Texture *Button::GetTexture() const
        {
            if (_icon != nullptr)
                return _icon->GetTexture();
            return nullptr;
        }
        void Button::RenderImpl(UIRenderer &r)
        {
            if (const UIControlVisual *visual = GetCurrentVisual())
                r.DrawVisual(_arrange_rect, _matrix, *visual);
            for (auto& c: _children)
                c->Render(r);
        }
        void Button::PostArrange()
        {
            InvalidateTransform();
            if (_text != nullptr)
            {
                _text->Arrange(_padding._l, _padding._t, _content_rect.z, _content_rect.w);
            }
            if (_icon != nullptr)
            {
                _icon->Arrange(_padding._l, _padding._t, _content_rect.z, _content_rect.w);
            }
        }
        void Button::PostDeserialize()
        {
            UIElement::PostDeserialize();
            RebindContentChildren();
            if (_text == nullptr && _icon == nullptr)
                SetText(_name.empty() ? "button" : _name, false);
        }
        void Button::RebindContentChildren()
        {
            _text = nullptr;
            _icon = nullptr;
            for (auto &child: _children)
            {
                if (_text == nullptr)
                    _text = child->As<Text>();
                if (_icon == nullptr)
                    _icon = child->As<Image>();
            }
            if (_text != nullptr)
            {
                _text->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFill);
                _text->_horizontal_align = EAlignment::kCenter;
                _text->_vertical_align = EAlignment::kCenter;
            }
            if (_icon != nullptr)
                _icon->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFill);
        }
        Ref<UISlot> Button::CreateSlotForChild()
        {
            return MakeRef<LinearSlot>();
        }
#pragma endregion

#pragma region Text
        Text::Text() : UIElement("Text")
        {
            SetText("text");
        }
        Text::Text(String text) : Text()
        {
            SetText(text);
        }
        void Text::SetText(const String &text, bool trigger_event)
        {
            if (_text == text)
                return;
            _text = text;
            MarkTextLayoutDirty();
            UpdateTextLayout();
            InvalidatePaint();
            if (trigger_event)
                _on_text_change_delegate.Invoke(_text);
        }
        void Text::UpdateTextLayout(bool record_dirty_reason)
        {
            Render::Font *font = TextRenderer::GetDefaultFont();
            if (!_is_text_layout_dirty && _text_layout_font == font && NearbyEqual(_text_layout_font_size, _font_size))
                return;
            _text_layout_cache = TextRenderer::BuildLayout(_text, Vector2f::kZero, _font_size, font);
            _text_layout_font = font;
            _text_layout_font_size = _font_size;
            _is_text_layout_dirty = false;
            Vector2f new_size = _text_layout_cache._size;
            _text_visual_bounds = TextRenderer::CalculateTextVisualBounds(_text_layout_cache);
            new_size.x += _padding._l + _padding._r;
            new_size.y += _padding._t + _padding._b;
            Vector2f dv = Abs(new_size - _text_size);
            f32 tolerance = std::max(1.0f, (f32)_font_size * 0.1f);
            if (dv.x > tolerance || dv.y > tolerance)
            {
                _text_size = new_size;
                if (record_dirty_reason)
                    InvalidateLayout();
            }
        }
        void Text::MarkTextLayoutDirty(bool record_dirty_reason)
        {
            _is_text_layout_dirty = true;
            if (record_dirty_reason)
            {
                _dirty_reasons |= EUIInvalidationReason::kTextLayout | EUIInvalidationReason::kPaint;
                _debug_dirty_reasons |= EUIInvalidationReason::kTextLayout | EUIInvalidationReason::kPaint;
            }
        }
        void Text::ResolveStyle(const UIStyleContext &context)
        {
            f32 old_font_size = _font_size;
            Color old_color = _color;
            if (context._theme)
            {
                _font_size = context._theme->_typography._normal_font_size;
                _color = IsInteractiveEnabled() ? context._theme->_colors._text_primary : context._theme->_colors._text_disabled;
            }
            _resolved_visual._content_color = _color;
            _resolved_visual._background = UIBrush{};
            _resolved_visual._background._type = EUIBrushType::kColor;
            _resolved_visual._background._tint = Colors::kTransparent;
            _style_override.ApplyTo(_resolved_visual);
            _color = _resolved_visual._content_color;
            if (_style_override.HasOverride(EUIControlVisualOverride::kFontSize))
                _font_size = _style_override._font_size;
            if (!NearbyEqual(old_font_size, _font_size))
                MarkTextLayoutDirty(false);
            UpdateTextLayout(false);
        }

        const UIControlVisual *Text::GetVisual(EUIVisualState state) const
        {
            return &_resolved_visual;
        }
        void Text::RenderImpl(UIRenderer &r)
        {
            // slot 分配的矩形区域
            Vector2f slot_pos = _content_rect.xy;
            Vector2f slot_size = _content_rect.zw;// 假设 xy = pos, zw = size
            // 初始对齐位置 = slot 左上角
            Vector2f aligned_pos = slot_pos;
            // 水平对齐
            switch (_horizontal_align)
            {
                case EAlignment::kLeft:
                    aligned_pos.x = slot_pos.x;
                    break;
                case EAlignment::kCenter:
                    aligned_pos.x = slot_pos.x + (slot_size.x - _text_size.x) * 0.5f;
                    break;
                case EAlignment::kRight:
                    aligned_pos.x = slot_pos.x + slot_size.x - _text_size.x;
                    break;
                case EAlignment::kFill:
                    // 暂时当成居中（不缩放字体）
                    aligned_pos.x = slot_pos.x + (slot_size.x - _text_size.x) * 0.5f;
                    break;
                default:
                    break;
            }

            // 垂直对齐
            switch (_vertical_align)
            {
                case EAlignment::kTop:
                    aligned_pos.y = slot_pos.y - _text_visual_bounds.y;
                    break;
                case EAlignment::kCenter:
                    aligned_pos.y = slot_pos.y + (slot_size.y - _text_visual_bounds.w) * 0.5f - _text_visual_bounds.y;
                    break;
                case EAlignment::kBottom:
                    aligned_pos.y = slot_pos.y + slot_size.y - _text_visual_bounds.w - _text_visual_bounds.y;
                    break;
                case EAlignment::kFill:
                    // 暂时当成居中
                    aligned_pos.y = slot_pos.y + (slot_size.y - _text_visual_bounds.w) * 0.5f - _text_visual_bounds.y;
                    break;
                default:
                    break;
            }

            // 绘制文字
            UpdateTextLayout(false);
            r.DrawTextLayout(_text_layout_cache, aligned_pos, _matrix, _font_size, _color);
        }

        void Text::PostDeserialize()
        {
            UIElement::PostDeserialize();
            UpdateTextLayout();
        }
        void Text::OnPropertyChanged(const PropertyInfo &prop)
        {
            UIElement::OnPropertyChanged(prop);
            const String &name = prop.Name();
            if (name == "_text" || name == "_font_size")
            {
                MarkTextLayoutDirty();
                UpdateTextLayout();
                InvalidatePaint();
            }
            else if (name == "_color" || name == "_horizontal_align" || name == "_vertical_align")
            {
                InvalidatePaint();
            }
        }

        Vector2f Text::MeasureDesiredSize()
        {
            UpdateTextLayout();
            Vector2f desired_size = _text_layout_cache._size;
            desired_size.x += _padding._l + _padding._r;
            desired_size.y += _padding._t + _padding._b;
            if (GetSizePolicy(this, true) == ESizePolicy::kFixed)
                desired_size.x = GetSlot()->_size.x;
            if (GetSizePolicy(this, false) == ESizePolicy::kFixed)
                desired_size.y = GetSlot()->_size.y;
            return desired_size;
        }
        void Text::FontSize(f32 size, bool record_dirty_reason)
        {
            if (NearbyEqual(_font_size, size))
                return;
            _font_size = size;
            MarkTextLayoutDirty(record_dirty_reason);
            UpdateTextLayout(record_dirty_reason);
            if (record_dirty_reason)
            {
                InvalidatePaint();
                _on_text_change_delegate.Invoke(_text);
            }
        }
#pragma endregion

#pragma region Slider
        Slider::Slider() : Slider("Slider")
        {
            OnMouseMove() += [this](UIEvent &e)
            {
                if (IsPressed())
                {
                    auto lmpos = TransformCoord(_inv_matrix, {e._mouse_position, 0.0f});
                    f32 rel = (lmpos.x - (_content_rect.x + _dot_rect.z * 0.5f)) / (_content_rect.z - _dot_rect.z);
                    SetValue(Lerp(_range.x, _range.y, rel));
                }
            };
        }
        Slider::Slider(const String &name) : UIElement(name)
        {

        }

        Slider::Slider(f32 min, f32 max, f32 value) : Slider()
        {
            _range = Vector2f(min, max);
            SetValue(value);
        }

        f32 static PingPong(f32 v)
        {
            f32 t = v - (i32)v;
            return (i32(v) & 1) ? 1.0f - t : t;
        }
        void Slider::Update(f32 dt)
        {
            UIElement::Update(dt);
            //_value = PingPong(_value + 0.001f);
            EnsureStyleResolved();
            Clamp(_value,_range.x,_range.y);
            f32 value01 = (_value - _range.x) / (_range.y - _range.x);
            f32 bar_height = _resolved_style._track_thickness;
            _bar_rect = {_content_rect.x, _content_rect.y + (_content_rect.w * 0.5f - bar_height * 0.5f), _content_rect.z, bar_height};
            _dot_rect = {_content_rect.x, _content_rect.y + (_content_rect.w * 0.5f - _resolved_style._thumb_size.y * 0.5f), _resolved_style._thumb_size.x, _resolved_style._thumb_size.y};
            _dot_rect.x = Lerp(_content_rect.x, _content_rect.x + _content_rect.z - _dot_rect.z, value01);
        }
        UIElement *Slider::HitTest(Vector2f pos) 
        {
            Vector2f lpos = TransformCoord(_inv_matrix,Vector3f{pos,0.0f}).xy;
            return IsPointInside(lpos, _dot_rect) ? this : nullptr;
        }
        void Slider::RenderImpl(UIRenderer &r)
        {
            if (const UIControlVisual *visual = GetCurrentVisual())
                r.DrawVisual(_arrange_rect, _matrix, *visual);
            Vector4f track_corner = Vector4f(_resolved_style._track_corner_radius);
            r.DrawQuad(_bar_rect, _matrix, _resolved_style._track_background, track_corner);
            f32 value01 = (_value - _range.x) / (_range.y - _range.x);
            r.DrawQuad({_bar_rect.x, _bar_rect.y, _bar_rect.z * value01, _bar_rect.w}, _matrix, _resolved_style._track_fill, track_corner);
            const UIBrush *thumb = &_resolved_style._thumb;
            if (!IsInteractiveEnabled())
                thumb = &_resolved_style._thumb_disabled;
            else if (IsPressed())
                thumb = &_resolved_style._thumb_pressed;
            else if (IsHovered())
                thumb = &_resolved_style._thumb_hovered;
            Vector4f thumb_corner = Vector4f(_resolved_style._thumb_corner_radius);
            r.DrawQuad(_dot_rect, _matrix, *thumb, thumb_corner);
        }
        Vector2f Slider::MeasureDesiredSize()
        {
            EnsureStyleResolved();
            if (GetSizePolicy(this, true) == ESizePolicy::kFixed)
                return GetSlot()->_size;
            return _resolved_style._min_size;
        }
        void Slider::SetStyleId(const UIStyleId &id)
        {
            if (_style_id == id)
                return;
            _style_id = id;
            InvalidateStyle();
        }

        void Slider::ResolveStyle(const UIStyleContext &context)
        {
            if (context._theme)
            {
                const UISliderStyle *theme_style = context._theme->FindSliderStyle(_style_id);
                if (theme_style)
                    _resolved_style = *theme_style;
                else
                    _resolved_style = context._theme->_slider_style;
            }
            else
            {
                static UITheme s_default_theme = UITheme::DefaultDark();
                _resolved_style = s_default_theme._slider_style;
            }
            _style_override.ApplyTo(_resolved_style);
            _padding = _resolved_style._padding;
        }
        const UIControlVisual *Slider::GetVisual(EUIVisualState state) const
        {
            switch (state)
            {
                case EUIVisualState::kHovered:
                    return &_resolved_style._hovered;
                case EUIVisualState::kPressed:
                    return &_resolved_style._pressed;
                case EUIVisualState::kFocused:
                    return &_resolved_style._focused;
                case EUIVisualState::kDisabled:
                    return &_resolved_style._disabled;
                default:
                    return &_resolved_style._normal;
            }
        }
        void Slider::SetValue(f32 v, bool trigger_event)
        {
            if (NearbyEqual(v,_value))
                return;
            _value = v;
            Clamp(_value,_range.x,_range.y);
            InvalidatePaint();
            if (trigger_event)
                _on_value_change_delegate.Invoke(_value);
        }
        #pragma endregion

#pragma region CheckBox
        CheckBox::CheckBox() : UIElement("CheckBox")
        {
            OnMouseClick() += [this](UIEvent &e)
            {
                SetChecked(!_is_checked);
            };
        }

        void CheckBox::RenderImpl(UIRenderer &r)
        {
            if (const UIControlVisual *visual = GetCurrentVisual())
                r.DrawVisual(_arrange_rect, _matrix, *visual);
            const UIBrush *box_brush = &_resolved_style._unchecked;
            if (_is_checked)
                box_brush = &_resolved_style._checked;
            if (!IsInteractiveEnabled())
                box_brush = _is_checked ? &_resolved_style._disabled_mark : &_resolved_style._unchecked;
            else if (IsPressed())
                box_brush = _is_checked ? &_resolved_style._checked_pressed : &_resolved_style._unchecked_pressed;
            else if (IsHovered())
                box_brush = _is_checked ? &_resolved_style._checked_hovered : &_resolved_style._unchecked_hovered;
            Vector4f box_rect = {_content_rect.x, _content_rect.y, _resolved_style._box_size.x, _resolved_style._box_size.y};
            r.DrawQuad(box_rect,_matrix, *box_brush);
            if (_is_checked)
            {
                const f32 width = box_rect.z * 0.28f;
                Vector4f fill_rect = {box_rect.x + width, box_rect.y + width, box_rect.z - width * 2.0f, box_rect.w - width * 2.0f};
                UIBrush mark;
                mark._type = EUIBrushType::kColor;
                mark._tint = _resolved_style._mark_color;
                r.DrawQuad(fill_rect, _matrix, mark);
            }
        }
        Vector2f CheckBox::MeasureDesiredSize()
        {
            EnsureStyleResolved();
            return _resolved_style._box_size + Vector2f(_padding._l + _padding._r, _padding._t + _padding._b);
        }
        void CheckBox::SetStyleId(const UIStyleId &id)
        {
            if (_style_id == id)
                return;
            _style_id = id;
            InvalidateStyle();
        }

        void CheckBox::ResolveStyle(const UIStyleContext &context)
        {
            if (context._theme)
            {
                const UICheckBoxStyle *theme_style = context._theme->FindCheckBoxStyle(_style_id);
                if (theme_style)
                    _resolved_style = *theme_style;
                else
                    _resolved_style = context._theme->_check_box_style;
            }
            else
            {
                static UITheme s_default_theme = UITheme::DefaultDark();
                _resolved_style = s_default_theme._check_box_style;
            }
            _style_override.ApplyTo(_resolved_style);
            _padding = _resolved_style._padding;
        }
        const UIControlVisual *CheckBox::GetVisual(EUIVisualState state) const
        {
            switch (state)
            {
                case EUIVisualState::kHovered:
                    return &_resolved_style._hovered;
                case EUIVisualState::kPressed:
                    return &_resolved_style._pressed;
                case EUIVisualState::kFocused:
                    return &_resolved_style._focused;
                case EUIVisualState::kDisabled:
                    return &_resolved_style._disabled;
                default:
                    return &_resolved_style._normal;
            }
        }
        void CheckBox::SetChecked(bool is_checked)
        {
            if (_is_checked == is_checked)
                return;
            _is_checked = is_checked;
            InvalidatePaint();
            _on_click_delegate.Invoke(_is_checked);
        }
#pragma endregion

#pragma region Border
        Border::Border() : UIElement("Border")
        {
            _on_child_add += [this](UIElement* child) {
                if (_children.size() > 1)
                {
                    LOG_ERROR("Border({}) can only have one child",_name);
                    _children.erase(_children.end() - 1);
                }
            };
        }

        void Border::Thickness(f32 thickness)
        {
            Thickness(Vector4f{thickness});
        }

        void Border::Thickness(Vector4f ltrb)
        {
            if (_thickness == ltrb)
                return;
            _thickness = ltrb;
            SlotPadding() = Padding(_thickness);
            InvalidateStyle();
            InvalidateLayout();
        }

        void Border::CornerRadius(f32 radius)
        {
            CornerRadius(Vector4f{radius});
        }

        void Border::CornerRadius(Vector4f radius)
        {
            if (_corner_radius == radius)
                return;
            _corner_radius = radius;
            InvalidateStyle();
        }

        void Border::ResolveStyle(const UIStyleContext &context)
        {
            _resolved_visual._background = ColorBrush(_bg_color);
            _resolved_visual._border_color = _border_color;
            _resolved_visual._border_width = (_thickness.x + _thickness.y + _thickness.z + _thickness.w) * 0.25f;
            _resolved_visual._corner_radius = _corner_radius;
            _style_override.ApplyTo(_resolved_visual);
        }

        const UIControlVisual *Border::GetVisual(EUIVisualState state) const
        {
            return &_resolved_visual;
        }

        void Border::RenderImpl(UIRenderer &r)
        {
            const UIBrush &bg = _resolved_visual._background;
            Color border = _resolved_visual._border_color;
            Vector4f corner_radius = _resolved_visual._corner_radius;
            if (corner_radius != Vector4f::kZero)
            {
                if (_thickness == Vector4f::kZero)
                {
                    if (bg._type != EUIBrushType::kNone && bg._tint.a > 0.0f)
                        r.DrawQuad(_content_rect, _matrix, bg, corner_radius);
                }
                else
                {
                    if (border.a > 0.0f)
                        r.DrawQuad(_arrange_rect, _matrix, ColorBrush(border), corner_radius);
                    Vector4f inner_radius = Max(corner_radius - Vector4f{(_thickness.x + _thickness.y + _thickness.z + _thickness.w) * 0.25f}, Vector4f::kZero);
                    if (bg._type != EUIBrushType::kNone && bg._tint.a > 0.0f)
                        r.DrawQuad(_content_rect, _matrix, bg, inner_radius);
                }
            }
            else
            if (_thickness == Vector4f::kZero)
            {
                if (bg._type != EUIBrushType::kNone && bg._tint.a > 0.0f)
                    r.DrawQuad(_content_rect, _matrix, bg);
            }
            else
            {
                f32 outerL = _arrange_rect.x;
                f32 outerT = _arrange_rect.y;
                f32 outerR = _arrange_rect.x + _arrange_rect.z;
                f32 outerB = _arrange_rect.y + _arrange_rect.w;
                f32 innerL = _content_rect.x;
                f32 innerT = _content_rect.y;
                f32 innerR = _content_rect.x + _content_rect.z;
                f32 innerB = _content_rect.y + _content_rect.w;

                // 背景
                if (bg._type != EUIBrushType::kNone && bg._tint.a > 0.0f)
                    r.DrawQuad(_content_rect, _matrix, bg);

                if (border.a > 0.0f)
                {
                    // top 边
                    if (_thickness.y > 0)
                        r.DrawQuad({outerL, outerT, outerR - outerL, innerT - outerT}, _matrix, ColorBrush(border));
                    // bottom 边
                    if (_thickness.w > 0)
                        r.DrawQuad({outerL, innerB, outerR - outerL, outerB - innerB}, _matrix, ColorBrush(border));
                    // left 边
                    if (_thickness.x > 0)
                        r.DrawQuad({outerL, innerT, innerL - outerL, innerB - innerT}, _matrix, ColorBrush(border));
                    // right 边
                    if (_thickness.z > 0)
                        r.DrawQuad({innerR, innerT, outerR - innerR, innerB - innerT}, _matrix, ColorBrush(border));
                }
            }

            if (!_children.empty())
                _children[0]->Render(r);
        }


        Vector2f Border::MeasureDesiredSize()
        {
            Vector2f desired_size = GetSlot()->_size;
            if (!_children.empty())
            {
                const Vector2f child_desired_size = _children[0]->MeasureDesiredSize();
                if (GetSizePolicy(this, true) != ESizePolicy::kFixed)
                    desired_size.x = child_desired_size.x + _padding._l + _padding._r;
                if (GetSizePolicy(this, false) != ESizePolicy::kFixed)
                    desired_size.y = child_desired_size.y + _padding._t + _padding._b;
                return desired_size;
            }
            if (GetSizePolicy(this, true) != ESizePolicy::kFixed)
                desired_size.x = 40.0f;
            if (GetSizePolicy(this, false) != ESizePolicy::kFixed)
                desired_size.y = 20.0f;
            return desired_size;
        }

        void Border::MeasureAndArrange(f32 dt)
        {
            if (!_children.empty())
            {
                const auto &slot = _children[0]->GetSlotAs<LinearSlot>();
                Vector2f desired_size = _children[0]->MeasureDesiredSize();
                if (slot._size_policy_h == ESizePolicy::kFill)
                    desired_size.x = _content_rect.z;
                if (slot._size_policy_v == ESizePolicy::kFill)
                    desired_size.y = _content_rect.w;
                _children[0]->Arrange(_padding._l, _padding._t, desired_size.x, desired_size.y);
            }
        }
        void Border::PostDeserialize()
        {
            SlotPadding() = Padding(_thickness);
            InvalidateLayout();
        }
        void Border::OnPropertyChanged(const PropertyInfo &prop)
        {
            UIElement::OnPropertyChanged(prop);
            const String &name = prop.Name();
            if (name == "_thickness")
            {
                SlotPadding() = Padding(_thickness);
                InvalidateStyle();
                InvalidateLayout();
            }
            else if (name == "_bg_color" || name == "_border_color")
            {
                InvalidateStyle();
            }
            else if (name == "_corner_radius")
            {
                InvalidateStyle();
            }
        }
        Ref<UISlot> Border::CreateSlotForChild()
        {
            return MakeRef<LinearSlot>();
        }
#pragma endregion

#pragma region InputBlock
        InputBlock::InputBlock(const String &content) : UIElement("InputBlock")
        {
            _on_focus_gained += [this]()
            {
                FillCursorOffsetTable();
                KeepCursorVisible();
            };
            SetContent(content);
            OnKeyDown() += [this](UIEvent &e)
            {
                if (!IsFocused())
                    return;
                const bool is_ctrl_down = Input::IsKeyDown(EKey::kCONTROL) || Input::IsKeyDown(EKey::kLCONTROL) ||
                                          Input::IsKeyDown(EKey::kRCONTROL);
                const bool is_shift_down = Input::IsKeyDown(EKey::kSHIFT) || Input::IsKeyDown(EKey::kLSHIFT) ||
                                           Input::IsKeyDown(EKey::kRSHIFT);
                const bool is_alt_down = Input::IsKeyDown(EKey::kALT);
                auto has_selection = (_select_start != _select_end);
                auto sb = std::min(_select_start, _select_end);
                auto se = std::max<u32>(_select_start, _select_end);

                if (is_ctrl_down && e._key_code == EKey::kA)
                {
                    _select_start = 0u;
                    _select_end = (u32) _content.size();
                    _cursor_pos = _select_end;
                    KeepCursorVisible();
                    return;
                }

                if (e._key_code == EKey::kBACK)
                {
                    String new_content = _content;
                    bool content_changed = false;
                    if (has_selection)
                    {
                        // 删除选区
                        new_content.erase(sb, se - sb);
                        _cursor_pos = sb;
                        ClearSelection();
                        content_changed = true;
                    }
                    else if (_cursor_pos > 0 && !new_content.empty())
                    {
                        new_content.erase(_cursor_pos - 1, 1);
                        _cursor_pos--;
                        content_changed = true;
                    }
                    if (content_changed)
                        SetContent(new_content);
                    CommitEdit(false);
                }
                else if (e._key_code == EKey::kDELETE)
                {
                    String new_content = _content;
                    bool content_changed = false;
                    if (has_selection)
                    {
                        new_content.erase(sb, se - sb);
                        _cursor_pos = sb;
                        ClearSelection();
                        content_changed = true;
                    }
                    else if (_cursor_pos < new_content.size() && !new_content.empty())
                    {
                        new_content.erase(_cursor_pos, 1);
                        content_changed = true;
                    }
                    if (content_changed)
                        SetContent(new_content);
                    CommitEdit(false);
                }
                else if (e._key_code == EKey::kLEFT)
                {
                    if (!_is_editing)
                        return;
                    if (is_alt_down && is_shift_down)
                    {
                        if (!has_selection)
                            _select_start = _cursor_pos;
                        if (_cursor_pos > 0)
                            _cursor_pos--;
                        _select_end = _cursor_pos;
                    }
                    else if (has_selection)
                    {
                        // 光标跳到选区起点，并清除选区
                        _cursor_pos = sb;
                        ClearSelection();
                    }
                    else if (_cursor_pos > 0)
                    {
                        _cursor_pos--;
                    }
                }
                else if (e._key_code == EKey::kRIGHT)
                {
                    if (!_is_editing)
                        return;
                    if (is_alt_down && is_shift_down)
                    {
                        if (!has_selection)
                            _select_start = _cursor_pos;
                        if (_cursor_pos < _content.size())
                            _cursor_pos++;
                        _select_end = _cursor_pos;
                    }
                    else if (has_selection)
                    {
                        _cursor_pos = se;
                        ClearSelection();
                    }
                    else if (_cursor_pos < _content.size())
                    {
                        _cursor_pos++;
                    }
                }
                else if (e._key_code == EKey::kRETURN)
                {
                    CommitEdit();
                    _is_selecting = false;
                    _is_drag_adjusting = false;
                    _cursor_visible = false;
                    _cursor_timer = 0.0f;
                    UIManager::Get()->ClearFocus(this);
                    Application::Get().SetCursor(ECursorType::kArrow);
                    InvalidatePaint();
                }
                else
                {
                    char c = ToChar(Input::GetCharFromKeyCode((EKey)e._key_code))[0];
                    if (c != '\0')
                    {
                        String new_content = _content;
                        if (has_selection)
                        {
                            // 覆盖选区
                            new_content.erase(sb, se - sb);
                            _cursor_pos = sb;
                            ClearSelection();
                        }

                        new_content.insert(_cursor_pos, 1, c);
                        _cursor_pos++;
                        SetContent(new_content);
                        CommitEdit(false);
                    }
                }

                _cursor_pos = std::clamp<u32>(_cursor_pos, 0, (u32) _content.size());
                KeepCursorVisible();
            };

            OnMouseDown() += [this](UIEvent &e)
            {
                if (e._key_code == EKey::kLBUTTON)
                {
                    if (abs(e._mouse_position.x - _abs_rect.x - _abs_rect.z) < 6.0f && _is_numeric)
                    {
                        LOG_INFO("begin drag adjust...");
                        _is_drag_adjusting = true;
                        _drag_start_value = std::stof(_content);
                        _drag_start_x = e._mouse_position.x;
                        Application::Get().SetCursor(ECursorType::kSizeEW);
                        return;
                    }
                    _is_selecting = true;
                    _select_start = IndexFromMouseX(e._mouse_position.x);
                    _select_end = _select_start;
                    _cursor_pos = _select_end;
                    KeepCursorVisible();
                }
                _is_editing = true;
            };
            OnMouseClick() += [this](UIEvent &e)
            {
                if (e._key_code == EKey::kLBUTTON)
                {
                    _cursor_pos = _select_end;
                    KeepCursorVisible();
                }
            };
            OnMouseUp() += [this](UIEvent &e)
            {
                if (e._key_code == EKey::kLBUTTON)
                {
                    _is_selecting = false;
                    _is_drag_adjusting = false;
                    Application::Get().SetCursor(ECursorType::kArrow);
                    KeepCursorVisible();
                }
            };
            OnMouseMove() += [this](UIEvent &e)
            {
                if (_is_selecting)// 鼠标左键按下中
                {
                    const u32 select_end = IndexFromMouseX(e._mouse_position.x);
                    if (_select_end != select_end)
                    {
                        _select_end = select_end;
                        _cursor_pos = _select_end;
                        KeepCursorVisible();
                    }
                }
                if (abs(e._mouse_position.x - _abs_rect.x - _abs_rect.z) < 6.0f && _is_numeric)
                {
                    Application::Get().SetCursor(ECursorType::kSizeEW);
                }
            };
            OnMouseExit() += [this](UIEvent &e)
            {
                Application::Get().SetCursor(ECursorType::kArrow);
            };
            OnMouseDoubleClick() += [this](UIEvent &e)
            {
                if (e._key_code == EKey::kLBUTTON)
                {
                    _select_start = 0u;
                    _select_end = (u32) _content.size();
                    _cursor_pos = _select_end;
                    _is_selecting = false;
                    KeepCursorVisible();
                }
            };
            _on_focus_lost += [this]()
            {
                CommitEdit();
                _is_selecting = false;
                _cursor_visible = false;
                _cursor_timer = 0.0f;
                _cursor_hold_timer = 0.0f;
                InvalidatePaint();
            };
            _is_selecting = false;
        }

        InputBlock::InputBlock() : InputBlock("placeholder")
        {
            
        }
        void InputBlock::Update(f32 dt)
        {
            UIElement::Update(dt);
            if (_is_need_recalc_offset_table)
            {
                FillCursorOffsetTable();
            }
            if (IsFocused())// 只有获得焦点才需要闪烁
            {
                if (_cursor_hold_timer > 0.0f)
                {
                    _cursor_hold_timer = std::max(0.0f, _cursor_hold_timer - dt);
                    _cursor_timer = 0.0f;
                    if (!_cursor_visible)
                    {
                        _cursor_visible = true;
                        InvalidatePaint();
                    }
                }
                else
                {
                    _cursor_timer += dt;
                    if (_cursor_timer >= _blink_interval)// 比如 0.5 秒
                    {
                        _cursor_visible = !_cursor_visible;// 翻转显示状态
                        _cursor_timer = 0.0f;
                        InvalidatePaint();
                    }
                }
            }
            else
            {
                if (_cursor_visible)
                {
                    _cursor_visible = false;// 失去焦点时隐藏光标
                    InvalidatePaint();
                }
                _cursor_timer = 0.0f;
                _cursor_hold_timer = 0.0f;
            }
            if (_is_drag_adjusting)
            {
                if (Input::IsKeyDown(EKey::kLBUTTON))
                {
                    f32 delta = Input::GetMousePos().x - _drag_start_x;
                    f32 new_value = _drag_start_value + delta;
                    Application::Get().SetCursor(ECursorType::kSizeEW);
                    if (delta)
                    {
                        const String new_content = std::format("{:.3}", new_value);
                        SetContent(new_content);
                        _cursor_pos = (u32) new_content.size();
                    }
                }
                else
                {
                    _is_drag_adjusting = false;
                    LOG_INFO("Exit _is_drag_adjusting")
                    Application::Get().SetCursor(ECursorType::kArrow);
                }
            }
        }

        void InputBlock::SetContent(const String& content, bool trigger_event)
        {
            if (_content == content)
                return;
            _content = content;
            _is_need_recalc_offset_table = true;
            InvalidateLayout();
            InvalidatePaint();
            if (trigger_event)
                _on_content_changed_delegate.Invoke(content);
        }

        void InputBlock::RenderImpl(UIRenderer &r)
        {
            const UIControlVisual *visual = GetCurrentVisual();
            const f32 font_height = _resolved_style._font_size;
            if (visual)
                r.DrawVisual(_arrange_rect, _matrix, *visual);
            const auto text_layout = TextRenderer::BuildLayout(_content, Vector2f::kZero, font_height);
            const Vector4f text_visual_bounds = TextRenderer::CalculateTextVisualBounds(text_layout);
            const f32 visual_height = text_visual_bounds.w > 0.0f ? text_visual_bounds.w : font_height;
            const f32 line_y = _content_rect.y + (_content_rect.w - visual_height) * 0.5f;
            const f32 caret_height = std::min(_content_rect.w, std::max(font_height, visual_height));
            const f32 caret_y = _content_rect.y + (_content_rect.w - caret_height) * 0.5f;
            const Vector2f text_pos = {_content_rect.x, line_y - text_visual_bounds.y};
            if (IsFocused() && _select_start != _select_end)
            {
                const u32 select_start = std::min(_select_start, _select_end);
                const u32 select_end = std::max(_select_start, _select_end);
                f32 start_offset = select_start == 0u ? 0.0f : _cursor_offsets[select_start];
                f32 end_offset = select_end == 0u ? 0.0f : _cursor_offsets[select_end];
                UIBrush selection;
                selection._tint = _resolved_style._selection_color;
                r.DrawQuad({_content_rect.x + start_offset, caret_y, end_offset - start_offset, caret_height}, _matrix, selection);
            }
            r.DrawTextLayout(text_layout, text_pos, _matrix, font_height, visual ? visual->_content_color : Colors::kWhite);
            UIBrush caret;
            caret._tint = Color(_resolved_style._caret_color.x, _resolved_style._caret_color.y, _resolved_style._caret_color.z,
                                (f32) _cursor_visible);
            if (IsFocused() && !_is_selecting)
                r.DrawQuad({_content_rect.x + (_cursor_pos == 0u ? 1.0f : _cursor_offsets[_cursor_pos]), caret_y,
                            _resolved_style._caret_width, caret_height}, _matrix, caret);
        }
        void InputBlock::FillCursorOffsetTable()
        {
            if (_content.empty())
            {
                _cursor_offsets = {1.0f};
                _text_rect_size = {0.0f,0.0f};
                _is_numeric = true;
                _is_need_recalc_offset_table = false;
                return;
            }
            if (_content_rect.w <= 4.0)
            {
                LOG_ERROR("InputBlock({}) content rect is too small to calculate cursor offsets",_name);
                return;
            }
            _cursor_offsets.clear();
            _cursor_offsets.reserve(_content.size() + 1);
            _cursor_offsets.push_back(1.0f);
            EnsureStyleResolved();
            f32 font_height = _resolved_style._font_size;
            for (u64 i = 0; i < _content.size(); i++)
            {
                Vector2f size = TextRenderer::CalculateTextSize(_content.substr(0u, i + 1), (u16)font_height);
                _cursor_offsets.push_back(size.x + 1.0f);
            }
            _text_rect_size = TextRenderer::CalculateTextSize(_content, (u16) font_height);
            _is_numeric = StringUtils::IsNumeric(_content);
            _select_start = _select_end = _cursor_pos;
            _is_need_recalc_offset_table = false;
        }
        u32 InputBlock::IndexFromMouseX(f32 x)
        {
            const f32 scale_x = std::max(std::abs(_scale.x), 0.0001f);
            f32 local_x = (x - GetContentRect().x) / scale_x;

            if (_cursor_offsets.empty())
                return 0u;
            // 先处理边界，避免越界访问
            if (local_x <= _cursor_offsets.front())
                return 0u;
            if (local_x >= _cursor_offsets.back())
                return static_cast<u32>(_cursor_offsets.size() - 1u);
            // 找到第一个 >= localX 的位置（右候选）
            auto it = std::lower_bound(_cursor_offsets.begin(), _cursor_offsets.end(), local_x);
            u64 hi = static_cast<u64>(std::distance(_cursor_offsets.begin(), it));

            // safety: 如果 it == begin 已处理，it == end 已处理
            u64 lo = hi - 1;

            f32 left = _cursor_offsets[lo];
            f32 right = _cursor_offsets[hi];

            // 选离鼠标最近的索引；相等时选择左边（<=），
            // 如果想偏向右边，把 <= 改成 <。
            return ((local_x - left) <= (right - local_x)) ? static_cast<u32>(lo) : static_cast<u32>(hi);
        }

        void InputBlock::CommitEdit(bool is_finish_edit)
        {
            _is_editing = !is_finish_edit;
            LOG_INFO("Committed edit({}): {}",_name, _content);
        }

        void InputBlock::KeepCursorVisible()
        {
            _cursor_visible = true;
            _cursor_timer = 0.0f;
            _cursor_hold_timer = kCaretHoldDuration;
            InvalidatePaint();
        }

        Vector2f InputBlock::MeasureDesiredSize()
        {
            EnsureStyleResolved();
            if (GetSizePolicy(this, true) == ESizePolicy::kFixed)
                return GetSlot()->_size;
            return _resolved_style._min_size;
        }
        void InputBlock::SetStyleId(const UIStyleId &id)
        {
            if (_style_id == id)
                return;
            _style_id = id;
            InvalidateStyle();
        }

        void InputBlock::ResolveStyle(const UIStyleContext &context)
        {
            if (context._theme)
            {
                const UIInputStyle *theme_style = context._theme->FindInputStyle(_style_id);
                if (theme_style)
                    _resolved_style = *theme_style;
                else
                    _resolved_style = context._theme->_input_style;
            }
            else
            {
                static UITheme s_default_theme = UITheme::DefaultDark();
                _resolved_style = s_default_theme._input_style;
            }
            _style_override.ApplyTo(_resolved_style);
            _padding = _resolved_style._padding;
            _is_need_recalc_offset_table = true;
        }
        const UIControlVisual *InputBlock::GetVisual(EUIVisualState state) const
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
        void InputBlock::OnPropertyChanged(const PropertyInfo &prop)
        {
            UIElement::OnPropertyChanged(prop);
            if (prop.Name() == "_content")
            {
                _is_need_recalc_offset_table = true;
                InvalidateLayout();
            }
        }

#pragma endregion

#pragma region Image
        Image::Image() : UIElement("Image")
        {
        }
        Image::Image(Render::Texture *texture) : _texture(texture), UIElement("Image")
        {

        }
        void Image::ResolveStyle(const UIStyleContext &context)
        {
            _resolved_visual._background = UIBrush{};
            _resolved_visual._background._type = EUIBrushType::kColor;
            _resolved_visual._background._tint = Colors::kTransparent;
            _resolved_visual._content_color = _tint_color;
            _style_override.ApplyTo(_resolved_visual);
        }

        const UIControlVisual *Image::GetVisual(EUIVisualState state) const
        {
            return &_resolved_visual;
        }

        void Image::RenderImpl(UIRenderer &r)
        {
            // Draw background / border from resolved style
            r.DrawVisual(_arrange_rect, _matrix, _resolved_visual);
            // Draw the texture on top
            ImageDrawOptions opts;
            opts._transform = _matrix;
            opts._tint = _resolved_visual._content_color;
            opts._size_override = _tex_size;
            r.DrawImage(_texture, _content_rect, opts);
        }
        Vector2f Image::MeasureDesiredSize()
        {
            if (GetSizePolicy(this, true) == ESizePolicy::kFixed)
                return GetSlot()->_size;
            return Max(_tex_size,{32.0f,32.0f});
        }
        void Image::SetTexture(Render::Texture *tex)
        {
            if (_texture == tex)
                return;
            _texture = tex;
            Vector2f old_tex_size = _tex_size;
            if (_texture)
            {
                _tex_size = {(f32) _texture->Width(), (f32) _texture->Height()};
            }
            else
            {
                _tex_size = Vector2f::kZero;
            }
            if (!NearbyEqual(old_tex_size, _tex_size))
                InvalidateLayout();
            InvalidatePaint();
        }
        void Image::PostDeserialize()
        {
            if (!_texture_guid.empty())
            {
                SetTexture(ResourceMgr::Get().Load<Texture2D>(Guid(_texture_guid)).get());
            }
        }
        // void Image::OnPropertyChanged(const PropertyInfo &prop)
        // {
        //     UIElement::OnPropertyChanged(prop);
        //     if (prop.Name() == "_texture_guid")
        //         PostDeserialize();
        // }
#pragma endregion
    }// namespace UI
}
