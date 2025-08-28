#include "pch.h"
#include "UI/Basic.h"
#include "UI/UIRenderer.h"

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
            OnMouseExit() += [&](UIEvent& e) {
                LOG_INFO("OnMouseExit: {}",_name)
            };
        }
        void Button::Update(f32 dt)
        {
        }
        void Button::Render(UIRenderer &r)
        {
            r.DrawQuad(_content_rect.xy, _content_rect.zw, _depth, _state._is_hovered ? Colors::kRed : Colors::kGreen);
            //auto text_size = TextRenderer::CalculateTextSize("Button");
            //Vector2f text_pos = {_content_rect.x + _content_rect.z / 2 - text_size.x / 2, _content_rect.y - (_content_rect.w / 2 - text_size.y / 2)};
            //TextRenderer::DrawText("Button", text_pos, 14u, Vector2f::kOne, Colors::kRed);
        }
        #pragma endregion

        #pragma region Text
        Text::Text() : UIElement("Text")
        {
        }
        Text::Text(const String &name) : UIElement(name)
        {
            _text = "";
        }
        void Text::Update(f32 dt)
        {
        }
        void Text::Render(UIRenderer &r)
        {
            // if (_state == EElementState::kHover)
            //     r.DrawQuad(_content_rect.xy, _content_rect.zw, _depth, Colors::kRed);
            // else if (_state == EElementState::kNormal)
            //     r.DrawQuad(_content_rect.xy, _content_rect.zw, _depth, Colors::kGreen);
            // auto text_size = TextRenderer::CalculateTextSize("Button");
            // Vector2f text_pos = {_content_rect.x + _content_rect.z / 2 - text_size.x / 2, _content_rect.y - (_content_rect.w / 2 - text_size.y / 2)};
            // TextRenderer::DrawText("Button", text_pos, 14u, Vector2f::kOne, Colors::kRed);
            r.DrawText(_text, _content_rect.xy, _font_size, Vector2f::kOne, _color);
        }
        #pragma endregion

        #pragma region Slider
        Slider::Slider() : Slider("Slider")
        {
            OnMouseMove() += [this](UIEvent &e)
            {
                if (_state._is_pressed)
                {
                    f32 rel = (e._mouse_position.x - (_content_rect.x + _dot_rect.z * 0.5f)) / (_content_rect.z - _dot_rect.z);
                    _value = Lerp(_range.x, _range.y, rel);
                    Clamp(_value, _range.x, _range.y);
                }
            };
        }
        Slider::Slider(const String &name)
        {

        }
        f32 static PingPong(f32 v)
        {
            f32 t = v - (i32)v;
            return (i32(v) & 1) ? 1.0f - t : t;
        }
        void Slider::Update(f32 dt)
        {
            //_value = PingPong(_value + 0.001f);
            Clamp(_value,_range.x,_range.y);
            f32 bar_height = _content_rect.w * 0.33f;
            _bar_rect = {_content_rect.x, _content_rect.y + (_content_rect.w * 0.5f - bar_height * 0.5f), _content_rect.z, bar_height};
            bar_height = bar_height * 1.5f;
            _dot_rect = {_content_rect.x, _content_rect.y + (_content_rect.w * 0.5f - bar_height * 0.5f), bar_height, bar_height};
            _dot_rect.x = Lerp(_dot_rect.x + _dot_rect.z * 0.5f, _dot_rect.x + _content_rect.z - bar_height * 0.5f, _value) - bar_height * 0.5f;
        }
        UIElement *Slider::HitTest(Vector2f pos) 
        {
            return IsPointInside(pos,_dot_rect) ? this : nullptr;
        }
        void Slider::Render(UIRenderer &r)
        {
            r.DrawBox(_content_rect.xy, _content_rect.zw);
            r.DrawQuad(_bar_rect.xy, _bar_rect.zw, _depth, Colors::kGray);
            r.DrawQuad(_dot_rect.xy, _dot_rect.zw, _depth + 0.001f, _state._is_pressed ? Colors::kBlue : Colors::kWhite);
            r.DrawText(std::format("{:.2f}", _value), {_content_rect.x + _content_rect.z + 5.0f, _content_rect.y + _content_rect.w * 0.5f - _font_size * 0.5f}, _font_size, Vector2f::kOne, Colors::kWhite);
        }
        #pragma endregion

#pragma region CheckBox
        CheckBox::CheckBox() : UIElement("CheckBox")
        {
            OnMouseClick() += [this](UIEvent &e) {
                _is_checked = !_is_checked;
            };
        }

        void CheckBox::Render(UIRenderer &r)
        {
            r.DrawQuad(_content_rect,_depth, Colors::kGray);
            if (_is_checked)
            {
                const f32 width = _content_rect.z * 0.2f;
                Vector4f fill_rect = {_content_rect.x + width, _content_rect.y + width, _content_rect.z - width * 2.0f, _content_rect.w - width * 2.0f};
                r.DrawQuad(fill_rect, _depth + 0.01f, Colors::kWhite);
            }
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
        void Border::Update(f32 dt)
        {
            _padding._l = _thickness;
            _padding._r = _thickness;
            _padding._t = _thickness;
            _padding._b = _thickness;
            if (!_children.empty())
            {
                _children[0]->SetDesiredRect(_content_rect);
                _children[0]->Update(dt);
            }
        }
        void Border::Render(UIRenderer &r)
        {
            if (_thickness == 0.0f)
            {
                r.DrawQuad(_content_rect, _depth, _bg_color);
            }
            else
            {
                r.DrawQuad(_desired_rect, _depth, _border_color);
                r.DrawQuad(_content_rect, _depth + 0.001f, _bg_color);
            }
            if (!_children.empty())
                _children[0]->Render(r);
        }
#pragma endregion
    }// namespace UI
}