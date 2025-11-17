#include "pch.h"
#include "UI/Basic.h"
#include "UI/UIRenderer.h"
#include "UI/TextRenderer.h"
#include "Framework/Common/Application.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/Input.h"

namespace Ailu
{
    namespace UI
    {
#pragma region Button
        Button::Button() : Button::Button("button")
        {
        }
        Button::Button(const String &name) : UIElement(name)
        {
            _text = nullptr;
            _icon = nullptr;
        }

        Vector2f Button::MeasureDesiredSize()
        {
            if (_slot._size_policy_h == ESizePolicy::kFixed)
                return _slot._size;
            return Vector2f(80.0f,20.0f);
        }
        void Button::SetText(const String &text)
        {
            if (_text == nullptr)
                _text = AddChild<Text>(text);
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
                _icon = AddChild<Image>(tex);
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
            r.DrawQuad(_content_rect, _matrix, _state._is_hovered ? Colors::kRed : Colors::kGreen);
            for (auto& c: _children)
                c->Render(r);
        }
        void Button::PostArrange()
        {
            InvalidateTransform();
            if (_text != nullptr)
            {
                _text->Arrange(0.0f,0.0f,_content_rect.z,_content_rect.w);
            }
            if (_icon != nullptr)
            {
                _icon->Arrange(0.0f, 0.0f, _content_rect.z, _content_rect.w);
            }
        }
#pragma endregion

#pragma region Text
        Text::Text() : UIElement("Text")
        {
            _on_text_change += [this](const String &new_text)
            {
                Vector2f new_size = TextRenderer::CalculateTextSize(new_text,_font_size);
                new_size.x += _padding._l + _padding._r;
                new_size.y += _padding._t + _padding._b;
                Vector2f dv = Abs(new_size - _text_size);
                f32 tolerance = std::max(1.0f, (f32)_font_size * 0.1f);
                if (dv.x > tolerance || dv.y > tolerance)
                {
                    _text_size = new_size;
                    InvalidateLayout();
                }
            };
            SetText("text");
        }
        Text::Text(String text) : Text()
        {
            SetText(text);
        }
        void Text::SetText(const String &text)
        {
            if (_text == text)
                return;
            _text = text;
            _on_text_change_delegate.Invoke(_text);
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
                    aligned_pos.y = slot_pos.y;
                    break;
                case EAlignment::kCenter:
                    aligned_pos.y = slot_pos.y + (slot_size.y - _text_size.y) * 0.5f;
                    break;
                case EAlignment::kBottom:
                    aligned_pos.y = slot_pos.y + slot_size.y - _text_size.y;
                    break;
                case EAlignment::kFill:
                    // 暂时当成居中
                    aligned_pos.y = slot_pos.y + (slot_size.y - _text_size.y) * 0.5f;
                    break;
                default:
                    break;
            }

            // 绘制文字
            r.DrawText(_text, aligned_pos,_matrix, _font_size, _color);
        }

        void Text::PostDeserialize()
        {
            UIElement::PostDeserialize();
            _on_text_change_delegate.Invoke(_text);
        }

        Vector2f Text::MeasureDesiredSize()
        {
            if (_slot._size_policy_h == ESizePolicy::kFixed)
                return _slot._size;
            Vector2f text_size = TextRenderer::CalculateTextSize(_text);
            text_size.x += _padding._l + _padding._r;
            text_size.y += _padding._t + _padding._b;
            return text_size;
        }
        void Text::FontSize(f32 size)
        {
            _font_size = size;
            _on_text_change_delegate.Invoke(_text);
        }
#pragma endregion

#pragma region Slider
        Slider::Slider() : Slider("Slider")
        {
            OnMouseMove() += [this](UIEvent &e)
            {
                if (_state._is_pressed)
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
            Clamp(_value,_range.x,_range.y);
            f32 value01 = (_value - _range.x) / (_range.y - _range.x);
            f32 bar_height = _content_rect.w * 0.33f;
            _bar_rect = {_content_rect.x, _content_rect.y + (_content_rect.w * 0.5f - bar_height * 0.5f), _content_rect.z, bar_height};
            bar_height = bar_height * 1.5f;
            _dot_rect = {_content_rect.x, _content_rect.y + (_content_rect.w * 0.5f - bar_height * 0.5f), bar_height, bar_height};
            _dot_rect.x = Lerp(_dot_rect.x + _dot_rect.z * 0.5f, _dot_rect.x + _content_rect.z - bar_height * 0.5f, value01) - bar_height * 0.5f;
        }
        UIElement *Slider::HitTest(Vector2f pos) 
        {
            Vector2f lpos = TransformCoord(_inv_matrix,Vector3f{pos,0.0f}).xy;
            return IsPointInside(lpos, _dot_rect) ? this : nullptr;
        }
        void Slider::RenderImpl(UIRenderer &r)
        {
            r.DrawQuad(_bar_rect, _matrix, Colors::kGray);
            r.DrawQuad(_dot_rect, _matrix, _state._is_pressed ? Colors::kBlue : Colors::kWhite);
        }
        Vector2f Slider::MeasureDesiredSize()
        {
            if (_slot._size_policy_h == ESizePolicy::kFixed)
                return _slot._size;
            return Vector2f(100.0f,20.0f);
        }
        void Slider::SetValue(f32 v)
        {
            if (NearbyEqual(v,_value))
                return;
            _value = v;
            Clamp(_value,_range.x,_range.y);
            _on_value_change_delegate.Invoke(_value);
        }
        #pragma endregion

#pragma region CheckBox
        CheckBox::CheckBox() : UIElement("CheckBox")
        {
            OnMouseClick() += [this](UIEvent &e)
            {
                _is_checked = !_is_checked;
            };
        }

        void CheckBox::RenderImpl(UIRenderer &r)
        {
            r.DrawQuad(_content_rect,_matrix, Colors::kGray);
            if (_is_checked)
            {
                const f32 width = _content_rect.z * 0.2f;
                Vector4f fill_rect = {_content_rect.x + width, _content_rect.y + width, _content_rect.z - width * 2.0f, _content_rect.w - width * 2.0f};
                r.DrawQuad(fill_rect, _matrix, Colors::kWhite);
            }
        }
        Vector2f CheckBox::MeasureDesiredSize()
        {
            return Vector2f(20.0f,20.0f);
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
        void Border::RenderImpl(UIRenderer &r)
        {
            if (_thickness == Vector4f::kZero)
            {
                r.DrawQuad(_content_rect, _matrix, _bg_color);
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
                // top 边
                if (_thickness.y > 0)
                    r.DrawQuad({outerL, outerT, outerR - outerL, innerT - outerT}, _matrix, _border_color);
                // bottom 边
                if (_thickness.w > 0)
                    r.DrawQuad({outerL, innerB, outerR - outerL, outerB - innerB}, _matrix, _border_color);
                // left 边
                if (_thickness.x > 0)
                    r.DrawQuad({outerL, innerT, innerL - outerL, innerB - innerT}, _matrix, _border_color);
                // right 边
                if (_thickness.z > 0)
                    r.DrawQuad({innerR, innerT, outerR - innerR, innerB - innerT}, _matrix, _border_color);

                // 背景
                r.DrawQuad(_content_rect, _matrix, _bg_color);
            }

            if (!_children.empty())
                _children[0]->Render(r);
        }


        Vector2f Border::MeasureDesiredSize()
        {
            if (_slot._size_policy_h == ESizePolicy::kFixed)
                return _slot._size;
            return Vector2f(40.0f,20.0f);
        }

        void Border::MeasureAndArrange(f32 dt)
        {
            if (!_children.empty())
            {
                Vector2f desired_size = _children[0]->MeasureDesiredSize();
                if (_children[0]->SlotSizePolicy() == ESizePolicy::kFill)
                    desired_size = _content_rect.zw;
                _children[0]->Arrange(_content_rect.x, _content_rect.y, desired_size.x, desired_size.y);
                _children[0]->InvalidateLayout();
            }
        }
        void Border::PostDeserialize()
        {
            SlotPadding(_thickness);
        }
#pragma endregion

#pragma region InputBlock
        InputBlock::InputBlock(const String &content) : UIElement("InputBlock")
        {
            _on_content_changed += [this](String content) {
                FillCursorOffsetTable();
            };
            _on_focus_gained += [this]()
            {
                FillCursorOffsetTable();
            };
            SetContent(content);
            OnKeyDown() += [this](UIEvent &e)
            {
                if (!_state._is_focused)
                    return;
                auto has_selection = (_select_start != _select_end);
                auto sb = std::min(_select_start, _select_end);
                auto se = std::max<u32>(_select_start, _select_end);

                if (e._key_code == EKey::kBACK)
                {
                    if (has_selection)
                    {
                        // 删除选区
                        _content.erase(sb, se - sb);
                        _cursor_pos = sb;
                        ClearSelection();
                    }
                    else if (_cursor_pos > 0 && !_content.empty())
                    {
                        _content.erase(_cursor_pos - 1, 1);
                        _cursor_pos--;
                    }
                    CommitEdit(false);
                }
                else if (e._key_code == EKey::kDELETE)
                {
                    if (has_selection)
                    {
                        _content.erase(sb, se - sb);
                        _cursor_pos = sb;
                        ClearSelection();
                    }
                    else if (_cursor_pos < _content.size() && !_content.empty())
                    {
                        _content.erase(_cursor_pos, 1);
                    }
                    CommitEdit(false);
                }
                else if (e._key_code == EKey::kLEFT)
                {
                    if (!_is_editing)
                        return;
                    if (has_selection)
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
                    if (has_selection)
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
                    Application::Get().SetCursor(ECursorType::kArrow);
                }
                else
                {
                    char c = ToChar(Input::GetCharFromKeyCode((EKey)e._key_code))[0];
                    if (c != '\0')
                    {
                        if (has_selection)
                        {
                            // 覆盖选区
                            _content.erase(sb, se - sb);
                            _cursor_pos = sb;
                            ClearSelection();
                        }

                        _content.insert(_cursor_pos, 1, c);
                        _cursor_pos++;
                        CommitEdit(false);
                    }
                }

                _cursor_pos = std::clamp<u32>(_cursor_pos, 0, (u32) _content.size());
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
                }
                _is_editing = true;
            };
            OnMouseClick() += [this](UIEvent &e)
            {
                if (e._key_code == EKey::kLBUTTON)
                    _cursor_pos = _select_end;
            };
            OnMouseUp() += [this](UIEvent &e)
            {
                if (e._key_code == EKey::kLBUTTON)
                {
                    _is_selecting = false;
                    _is_drag_adjusting = false;
                    Application::Get().SetCursor(ECursorType::kArrow);
                }
            };
            OnMouseMove() += [this](UIEvent &e)
            {
                if (_is_selecting)// 鼠标左键按下中
                {
                    _select_end = IndexFromMouseX(e._mouse_position.x);
                    // 这里可以触发 UI 重绘，让选区高亮
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
            _on_focus_lost += [this]()
            {
                CommitEdit();
            };
        }

        InputBlock::InputBlock() : InputBlock("placeholder")
        {
            
        }
        void InputBlock::Update(f32 dt)
        {
            UIElement::Update(dt);
            if (_state._is_focused)// 只有获得焦点才需要闪烁
            {
                _cursor_timer += dt;
                if (_cursor_timer >= _blink_interval)// 比如 0.5 秒
                {
                    _cursor_visible = !_cursor_visible;// 翻转显示状态
                    _cursor_timer = 0.0f;
                }
            }
            else
            {
                _cursor_visible = false;// 失去焦点时隐藏光标
                _cursor_timer = 0.0f;
            }
            if (_is_drag_adjusting)
            {
                if (Input::IsKeyPressed(EKey::kLBUTTON))
                {
                    f32 delta = Input::GetMousePos().x - _drag_start_x;
                    f32 new_value = _drag_start_value + delta;
                    Application::Get().SetCursor(ECursorType::kSizeEW);
                    if (delta)
                    {
                        // 更新文本
                        _content = std::format("{:.3}", new_value);
                        SetContent(_content);
                        _cursor_pos = (u32) _content.size();
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

        void InputBlock::SetContent(String content)
        {
            _content = content;
            _on_content_changed_delegate.Invoke(content);
        }

        void InputBlock::RenderImpl(UIRenderer &r)
        {
            f32 font_height = _content_rect.w - 4.0f;
            r.DrawQuad(_content_rect, _matrix, Color{0.2f,0.2f,0.2f,0.2f});
            if (_state._is_focused && _select_start != _select_end)
            {
                f32 start_offset = _select_start == 0u? 0.0f :_cursor_offsets [_select_start];
                f32 end_offset = _select_end == 0u ? 0.0f : _cursor_offsets[_select_end];
                r.DrawQuad({_content_rect.x + start_offset, _content_rect.y,end_offset - start_offset, _content_rect.w}, _matrix, Color{1.0f,1.0f,0.0f,0.4f});
            }
            r.DrawText(_content, _content_rect.xy,_matrix, (u16) font_height,Colors::kBlack);
            auto text_size = r.CalculateTextSize(_content, (u16) font_height);
            //r.DrawBox(_content_rect.xy, text_size, 1.0f, Colors::kRed);
            if (_state._is_focused && !_is_selecting)
                r.DrawQuad({_content_rect.x + (_cursor_pos == 0u ? 1.0f : _cursor_offsets[_cursor_pos]), _content_rect.y,kCursorWidth, font_height}, _matrix, {0.0f, 0.0f, 0.0f, (f32) _cursor_visible});
        }
        void InputBlock::FillCursorOffsetTable()
        {
            _cursor_offsets.clear();
            _cursor_offsets.reserve(_content.size() + 1);
            _cursor_offsets.push_back(1.0f);
            f32 font_height = _content_rect.w - 4.0f;
            for (u64 i = 0; i < _content.size(); i++)
            {
                Vector2f size = TextRenderer::CalculateTextSize(_content.substr(0u, i + 1), (u16)font_height);
                _cursor_offsets.push_back(size.x - 1.0f);
            }
            if (_cursor_offsets.size() > 1)
                _cursor_offsets[0] = _cursor_offsets[1] * 0.5f;
            _text_rect_size = TextRenderer::CalculateTextSize(_content, (u16) font_height);
            _is_numeric = StringUtils::IsNumeric(_content);
            _select_start = _select_end = _cursor_pos;
        }
        u32 InputBlock::IndexFromMouseX(f32 x)
        {
            f32 localX = x - _abs_rect.x;

            if (_cursor_offsets.empty())
                return 0u;
            // 先处理边界，避免越界访问
            if (localX <= _cursor_offsets.front())
                return 0u;
            if (localX >= _cursor_offsets.back())
                return static_cast<u32>(_cursor_offsets.size() - 1u);
            // 找到第一个 >= localX 的位置（右候选）
            auto it = std::lower_bound(_cursor_offsets.begin(), _cursor_offsets.end(), localX);
            u64 hi = static_cast<u64>(std::distance(_cursor_offsets.begin(), it));

            // safety: 如果 it == begin 已处理，it == end 已处理
            u64 lo = hi - 1;

            f32 left = _cursor_offsets[lo];
            f32 right = _cursor_offsets[hi];

            // 选离鼠标最近的索引；相等时选择左边（<=），
            // 如果想偏向右边，把 <= 改成 <。
            return ((localX - left) <= (right - localX)) ? static_cast<u32>(lo) : static_cast<u32>(hi);
        }

        void InputBlock::CommitEdit(bool is_finish_edit)
        {
            _is_editing = !is_finish_edit;
            SetContent(_content);
            LOG_INFO("Committed edit({}): {}",_name, _content);
        }

        Vector2f InputBlock::MeasureDesiredSize()
        {
            if (_slot._size_policy_h == ESizePolicy::kFixed)
                return _slot._size;
            return Vector2f(100.0f,20.0f);
        }

#pragma endregion

#pragma region Image
        Image::Image() : UIElement("Image")
        {
        }
        Image::Image(Render::Texture *texture) : _texture(texture), UIElement("Image")
        {

        }
        void Image::RenderImpl(UIRenderer &r)
        {
            ImageDrawOptions opts;
            opts._transform = _matrix;
            opts._tint = _tint_color;
            opts._size_override = _tex_size;
            r.DrawImage(_texture, _content_rect, opts);
        }
        Vector2f Image::MeasureDesiredSize()
        {
            if (_slot._size_policy_h == ESizePolicy::kFixed)
                return _slot._size;
            return Max(_tex_size,{32.0f,32.0f});
        }
        void Image::SetTexture(Render::Texture *tex)
        {
            if (_texture == tex)
                return;
            _texture = tex;
            if (_texture)
            {
                _tex_size = {(f32) _texture->Width(), (f32) _texture->Height()};
            }
        }
        void Image::PostDeserialize()
        {
            if (!_texture_guid.empty())
            {
                SetTexture(g_pResourceMgr->Load<Texture2D>(Guid(_texture_guid)).get());
            }
        }
#pragma endregion
    }// namespace UI
}