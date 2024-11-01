//
// Created by 22292 on 2024/10/28.
//

#include "Inc/UI/UIElement.h"
#include "UI/UIRenderer.h"
#include <Render/TextRenderer.h>

namespace Ailu
{
    namespace UI
    {
        UIElement::UIElement() : Object()
        {
            _name = std::format("ui_element_{}", _id);
        }
        UIElement::UIElement(const String &name) : UIElement()
        {
            _name = name;
        }
        void UIElement::AddChild(UIElement *child)
        {
            child->_parent = this;
            ++child->_hierarchy_depth;
            _children.emplace_back(child);
        }
        void UIElement::RemoveChild(UIElement *child)
        {
            auto it = std::find(_children.begin(), _children.end(), child);
            if (it != _children.end())
            {
                child->_parent = nullptr;
                --child->_hierarchy_depth;
                _children.erase(it);
            }
        }
        void UIElement::Update(f32 dt)
        {
            if (!_is_visible)
                return;
            for (auto child: _children)
            {
                child->Update(dt);
            }
        }
        RectTransform &UIElement::GetRect()
        {
            return _rect;
        }
        void UIElement::Render()
        {
            if (!_is_visible)
                return;
            for (auto child: _children)
            {
                child->Render();
            }
        }
        Event &UIElement::OnMouseEnter()
        {
            return _on_mouse_enter;
        }
        Event &UIElement::OnMouseExit()
        {
            return _on_mouse_exit;
        }
        Event &UIElement::OnMouseDown()
        {
            return _on_mouse_move;
        }
        Event &UIElement::OnMouseUp()
        {
            return _on_mouse_up;
        }
        Event &UIElement::OnMouseDoubleClick()
        {
            return _on_mouse_double_click;
        }
        Event &UIElement::OnMouseMove()
        {
            return _on_mouse_move;
        }
        void UIElement::SetDeservedRect(f32 x, f32 y, f32 width, f32 height)
        {
            _deserved_rect = {x,y,width,height};
        }
        void UIElement::SetVisible(bool visible)
        {
            _is_visible = visible;
        }
        bool UIElement::IsVisible() const
        {
            return _is_visible;
        }
        void UIElement::SetDepth(f32 depth)
        {
            _depth = depth;
        }
        UIElement::~UIElement()
        {
            for (auto child: _children)
                DESTORY_PTR(child);
        }
        Vector4f UIElement::GetDeservedRect() const
        {
            return _deserved_rect;
        }
        bool UIElement::IsMouseOver(Vector2f mouse_pos) const
        {
            return mouse_pos.x >= _deserved_rect.x && mouse_pos.y >= _deserved_rect.y && mouse_pos.x <= _deserved_rect.x + _deserved_rect.z && mouse_pos.y <= _deserved_rect.y + _deserved_rect.w;
        }
        void UIElement::SetElementState(EElementState::EElementState state)
        {
            _state = state;
        }
        EElementState::EElementState UIElement::GetState() const
        {
            return _state;
        }
        Event &UIElement::OnMouseClick()
        {
            return _on_mouse_click;
        }

        //-------------------------------------------------------------------------------------Event---------------------------------------------------------------------------
        void Event::AddListener(const Event::Callback &callback)
        {
            _callbacks.emplace_back(callback);
        }
        void Event::RemoveListener(const Event::Callback &callback)
        {

        }
        void Event::Invoke(const Ailu::Event &e)
        {
            for (auto &callback: _callbacks)
            {
                callback(e);
            }
        }
        void Event::Clear()
        {
            _callbacks.clear();
        }
        void Event::operator+=(const Event::Callback &callback)
        {
            AddListener(callback);
        }
        void Event::operator-=(const Event::Callback &callback)
        {
            RemoveListener(callback);
        }
        //-------------------------------------------------------------------------------------Button---------------------------------------------------------------------------
        Button::Button()
        {
        }
        Button::Button(const String &name) : UIElement(name)
        {
        }
        void Button::Update(f32 dt)
        {
        }
        void Button::Render()
        {
            if (_state == EElementState::kHover)
                UIRenderer::DrawQuad(_deserved_rect.xy, _deserved_rect.zw,_depth,Colors::kRed);
            else if (_state == EElementState::kNormal)
                UIRenderer::DrawQuad(_deserved_rect.xy, _deserved_rect.zw, _depth, Colors::kGreen);
            auto text_size = TextRenderer::CalculateTextSize("Button");
            Vector2f text_pos = {_deserved_rect.x + _deserved_rect.z / 2 - text_size.x / 2, _deserved_rect.y - (_deserved_rect.w / 2 - text_size.y / 2)};
            TextRenderer::DrawText("Button", text_pos, 14u,1,Colors::kRed);
        }
    }// namespace UI
}// namespace Ailu