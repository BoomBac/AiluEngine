//
// Created by 22292 on 2024/10/28.
//

#include "Inc/UI/UIElement.h"
#include "UI/UIRenderer.h"
#include <Render/TextRenderer.h>

namespace Ailu
{
    using namespace Render;
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
        void Ailu::UI::UIElement::Render(UIRenderer& r)
        {
            if (!_is_visible)
                return;
            for (auto child: _children)
            {
                child->Render(r);
            }
        }
        void UIElement::OnEvent(UIEvent &e)
        {
            if (e._is_handled)
                return;
            switch (e._type)
            {
                case UIEvent::EType::kMouseEnter:
                    _eventmap[UIEvent::EType::kMouseEnter].Invoke(e);
                    SetElementState(EElementState::kHover);
                    break;
                case UIEvent::EType::kMouseExit:
                    _eventmap[UIEvent::EType::kMouseExit].Invoke(e);
                    SetElementState(EElementState::kNormal);
                    break;
                case UIEvent::EType::kMouseDown:
                    _eventmap[UIEvent::EType::kMouseDown].Invoke(e);
                    SetElementState(EElementState::kPressed);
                    break;
                case UIEvent::EType::kMouseUp:
                    _eventmap[UIEvent::EType::kMouseUp].Invoke(e);
                    SetElementState(EElementState::kNormal);
                    break;
                case UIEvent::EType::kMouseDoubleClick:
                    _eventmap[UIEvent::EType::kMouseDoubleClick].Invoke(e);
                    SetElementState(EElementState::kNormal);
                    break;
                case UIEvent::EType::kMouseClick:
                    _eventmap[UIEvent::EType::kMouseClick].Invoke(e);
                    SetElementState(EElementState::kNormal);
                    break;
                case UIEvent::EType::kMouseMove:
                    _eventmap[UIEvent::EType::kMouseMove].Invoke(e);
                    SetElementState(IsPointInside(e._mouse_position) ? EElementState::kHover : EElementState::kNormal);
                    break;
                default:
                    AL_ASSERT(false);
                    break;
            }
        }

        Event &UIElement::OnMouseEnter()
        {
            return _eventmap[UIEvent::EType::kMouseEnter];
        }
        Event &UIElement::OnMouseExit()
        {
            return _eventmap[UIEvent::EType::kMouseExit];
        }
        Event &UIElement::OnMouseDown()
        {
            return _eventmap[UIEvent::EType::kMouseDown];
        }
        Event &UIElement::OnMouseUp()
        {
            return _eventmap[UIEvent::EType::kMouseUp];
        }
        Event &UIElement::OnMouseDoubleClick()
        {
            return _eventmap[UIEvent::EType::kMouseDoubleClick];
        }
        Event &UIElement::OnMouseMove()
        {
            return _eventmap[UIEvent::EType::kMouseMove];
        }
        Event &UIElement::OnMouseClick()
        {
            return _eventmap[UIEvent::EType::kMouseClick];
        }
        void UIElement::SetDesiredRect(f32 x, f32 y, f32 width, f32 height)
        {
            _desired_rect = {x, y, width, height};
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
        Vector4f UIElement::GetDesiredRect() const
        {
            return _desired_rect;
        }
        bool UIElement::IsPointInside(Vector2f mouse_pos) const
        {
            return mouse_pos.x >= _desired_rect.x && mouse_pos.y >= _desired_rect.y && mouse_pos.x <= _desired_rect.x + _desired_rect.z && mouse_pos.y <= _desired_rect.y + _desired_rect.w;
        }
        Vector2f UIElement::GetWorldPosition() const
        {
            return Vector2f();
        }
        RectTransform UIElement::GetWorldRect() const
        {
            return RectTransform();
        }
        void UIElement::SetElementState(EElementState::EElementState state)
        {
            _state = state;
        }
        EElementState::EElementState UIElement::GetState() const
        {
            return _state;
        }


        //-------------------------------------------------------------------------------------Event---------------------------------------------------------------------------
        void Event::AddListener(const Event::Callback &callback)
        {
            _callbacks.emplace_back(callback);
        }
        void Event::RemoveListener(const Event::Callback &callback)
        {
        }
        void Event::Invoke(UIEvent &e)
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
        void Ailu::UI::Button::Render(UIRenderer &r)
        {
            if (_state == EElementState::kHover)
                r.DrawQuad(_desired_rect.xy, _desired_rect.zw, _depth, Colors::kRed);
            else if (_state == EElementState::kNormal)
                r.DrawQuad(_desired_rect.xy, _desired_rect.zw, _depth, Colors::kGreen);
            //auto text_size = TextRenderer::CalculateTextSize("Button");
            //Vector2f text_pos = {_desired_rect.x + _desired_rect.z / 2 - text_size.x / 2, _desired_rect.y - (_desired_rect.w / 2 - text_size.y / 2)};
            //TextRenderer::DrawText("Button", text_pos, 14u, Vector2f::kOne, Colors::kRed);
        }
    }// namespace UI
}// namespace Ailu