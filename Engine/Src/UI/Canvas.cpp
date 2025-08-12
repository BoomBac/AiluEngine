//
// Created by 22292 on 2024/10/29.
//

#include "Inc/UI/Canvas.h"
#include "Render/Texture.h"
#include "Framework/Common/Input.h"
#include "Framework/Events/MouseEvent.h"
#include "Render/CommandBuffer.h"
#include "UI/UIRenderer.h"
#include <Render/TextRenderer.h>

namespace Ailu
{
    using namespace Render;
    namespace UI
    {
        Canvas::Canvas() : Object("Canvas")
        {
            _name = "Canvas";
            _color = RenderTexture::Create(1920, 1080, _name, ERenderTargetFormat::kDefault);
            _depth = RenderTexture::Create(1920, 1080, _name, ERenderTargetFormat::kDepth);
            _size = Vector2f(1920, 1080);
            _scale = Vector2f(1.0f, 1.0f);
            _position = Vector2f::kZero;
        }
        void Canvas::BindOutput(RenderTexture *color, RenderTexture *depth)
        {
            _external_color = color;
            _external_depth = depth;
            _is_external_output = true;
        }
        void Canvas::AddChild(UIElement *child, Slot slot)
        {
            if (_slots.contains(child->ID()))
            {
                LOG_WARNING("child already exists");
                return;
            }
            _slots[child->ID()] = SlotItem{slot,child};
            _children.push_back(child);
            child->OnMouseEnter() += [](UIEvent &e)
            { LOG_INFO("mouse enter") };
            child->OnMouseExit().AddListener([](UIEvent &e)
                                             { LOG_INFO("mouse leave") });
            child->OnMouseDown().AddListener([](UIEvent &e)
                                             { LOG_INFO("mouse down") });
            child->OnMouseUp().AddListener([](UIEvent &e)
                                           { LOG_INFO("mouse up") });
            child->OnMouseClick().AddListener([](UIEvent &e)
                                              { LOG_INFO("mouse click") });
        }
        void Canvas::RemoveChild(UIElement *child)
        {
            std::erase_if(_children, [&](UIElement *c){ return *c == *child; });
            _slots.erase(child->ID());
            UIElement::Destroy(child);
        }
        Slot &Canvas::GetSlot(UIElement *child)
        {
            return _slots[child->ID()]._slot;
        }
        void Canvas::Update()
        {
            Vector2f loc_mouse_pos = Input::GetMousePos() - _position;
            //TextRenderer::DrawText(std::format("MousePos: ({}, {})", loc_mouse_pos.x, loc_mouse_pos.y), Vector2f(0, 0));
            f32 depth = 0.0f;
            for (auto child: _children)
            {
                auto &s = _slots[child->ID()]._slot;
                {
                    child->SetDesiredRect(s._position.x, s._position.y, s._size.x, s._size.y);
                    child->SetDepth(depth);
                    depth += 0.1f;
                }
            }
        }
        void Ailu::UI::Canvas::Render(UIRenderer& r)
        {
            if (!_is_external_output && (_size.x > _color->Width() || _size.y > _color->Height()))
            {
                _color.reset();
                _depth.reset();
                _color = RenderTexture::Create((u16)_size.x, (u16)_size.y, _name, ERenderTargetFormat::kDefault);
                _depth = RenderTexture::Create((u16)_size.x, (u16)_size.y, _name, ERenderTargetFormat::kDepth);
            }
            for (auto &c: _children)
                c->Render(r);
        }

        Vector2f Canvas::GetSize() const
        {
            return _size;
        }
        void Canvas::SetPosition(Vector2f position)
        {
            _position = position;
        }
        void Canvas::OnEvent(UIEvent &event)
        {

        }
    }// namespace UI
}// namespace Ailu