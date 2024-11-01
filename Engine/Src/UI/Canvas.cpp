//
// Created by 22292 on 2024/10/29.
//

#include "Inc/UI/Canvas.h"
#include "Framework/Common/Input.h"
#include "Framework/Events/MouseEvent.h"
#include "Render/CommandBuffer.h"
#include "UI/UIRenderer.h"
#include <Render/TextRenderer.h>

namespace Ailu
{
    namespace UI
    {
        Canvas::Canvas()
        {
            _name = "Canvas";
            _color = RenderTexture::Create(1920, 1080, _name, ERenderTargetFormat::kDefault);
            _depth = RenderTexture::Create(1920, 1080, _name, ERenderTargetFormat::kDepth);
            _size = Vector2f(1920, 1080);
        }
        void Canvas::AddChild(UIElement *child, Canvas::Slot slot)
        {
            UIElement::AddChild(child);
            if (!_slots.contains(child->ID()))
                _slots[child->ID()] = slot;

            child->OnMouseEnter().AddListener([](const Ailu::Event &e)
                                              { LOG_INFO("mouse enter") });
            child->OnMouseExit().AddListener([](const Ailu::Event &e)
                                             { LOG_INFO("mouse leave") });
            child->OnMouseDown().AddListener([](const Ailu::Event &e)
                                             { LOG_INFO("mouse down") });
            child->OnMouseUp().AddListener([](const Ailu::Event &e)
                                           { LOG_INFO("mouse up") });
            child->OnMouseClick().AddListener([](const Ailu::Event &e)
                                              { LOG_INFO("mouse click") });
        }
        void Canvas::RemoveChild(UIElement *child)
        {
            UIElement::RemoveChild(child);
            _slots.erase(child->ID());
        }
        Canvas::Slot &Canvas::GetSlot(UIElement *child)
        {
            return _slots[child->ID()];
        }
        void Canvas::Update(f32 dt)
        {
            Vector2f loc_mouse_pos = Input::GetMousePos() - _position;
            TextRenderer::DrawText(std::format("MousePos: ({}, {})", loc_mouse_pos.x, loc_mouse_pos.y), Vector2f(0, 0));
            f32 depth = 0.0f;
            for (auto child: _children)
            {
                auto &s = _slots[child->ID()];
                {
                    child->SetDeservedRect(s._position.x, s._position.y, s._size.x, s._size.y);
                    child->SetDepth(depth);
                    depth += 0.1f;
                }
            }
        }
        void Canvas::Render()
        {
            //auto cmd = CommandBufferPool::Get(_name);
            if (_size.x > _color->Width() || _size.y > _color->Height())
            {
                _color.reset();
                _depth.reset();
                _color = RenderTexture::Create(_size.x, _size.y, _name, ERenderTargetFormat::kDefault);
                _depth = RenderTexture::Create(_size.x, _size.y, _name, ERenderTargetFormat::kDepth);
            }
            for (auto &c: _children)
                c->Render();
            UI::UIRenderer::Get()->Render(_color.get(), _depth.get());
            TextRenderer::Get()->Render(_color.get());
        }
        RenderTexture *Canvas::GetRenderTexture() const
        {
            return _color.get();
        }
        Vector2f Canvas::GetSize() const
        {
            return _size;
        }
        void Canvas::SetPosition(Vector2f position)
        {
            _position = position;
        }
        void Canvas::OnEvent(Ailu::Event &event)
        {
            if (event.GetEventType() == EEventType::kMouseMoved)
            {
                //相对于窗口的坐标
                MouseMovedEvent e = static_cast<MouseMovedEvent &>(event);
                //实际返回的是相对于屏幕的坐标
                Vector2f loc_mouse_pos = Input::GetMousePos() - _position;
                for (auto child: _children)
                {
                    auto &s = _slots[child->ID()];
                    {
                        if (child->IsMouseOver(loc_mouse_pos))
                        {
                            if (child->GetState() == EElementState::kNormal)
                                child->OnMouseEnter().Invoke(event);
                            child->SetElementState(EElementState::kHover);
                        }
                        else
                        {
                            if (child->GetState() == EElementState::kHover)
                            {
                                child->SetElementState(EElementState::kNormal);
                                child->OnMouseExit().Invoke(event);
                            }
                        }
                    }
                }
            }
            else if (event.GetEventType() == EEventType::kMouseButtonPressed)
            {
                MouseButtonPressedEvent e = static_cast<MouseButtonPressedEvent &>(event);
                for (auto child: _children)
                {
                    auto &s = _slots[child->ID()];
                    if (child->GetState() == EElementState::kHover)
                    {
                        child->SetElementState(EElementState::kPressed);
                        child->OnMouseDown().Invoke(event);
                    }
                }
            }
            else if (event.GetEventType() == EEventType::kMouseButtonReleased)
            {
                MouseButtonReleasedEvent e = static_cast<MouseButtonReleasedEvent &>(event);
                for (auto child: _children)
                {
                    auto &s = _slots[child->ID()];
                    {
                        if (child->GetState() == EElementState::kPressed)
                        {
                            child->OnMouseUp().Invoke(event);
                            child->OnMouseClick().Invoke(event);
                            child->SetElementState(EElementState::kHover);
                        }
                    }
                }
            }
        }
    }// namespace UI
}// namespace Ailu