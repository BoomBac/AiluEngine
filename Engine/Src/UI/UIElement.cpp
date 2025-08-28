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
        UIElement::UIElement() : SerializeObject()
        {
            _name = std::format("ui_element_{}", _id);
            _depth = 0.0f;
        }
        UIElement::UIElement(const String &name) : UIElement()
        {
            _name = name;
        }
        UIElement *UIElement::AddChild(Ref<UIElement> child)
        {
            child->_parent = this;
            ++child->_hierarchy_depth;
            _children.emplace_back(child);
            _on_child_add.Invoke(child.get());
            return child.get();
        }
        void UIElement::RemoveChild(Ref<UIElement> child)
        {
            auto it = std::find(_children.begin(), _children.end(), child);
            if (it != _children.end())
            {
                _on_child_remove.Invoke(it->get());
                child->_parent = nullptr;
                --child->_hierarchy_depth;
                _children.erase(it);
            }
        }
        void UIElement::Update(f32 dt)
        {
            if (!_is_visible)
                return;
            for (auto &child: _children)
            {
                child->Update(dt);
            }
        }

        void UIElement::Render(UIRenderer &r)
        {
            if (!_is_visible)
                return;
            for (auto &child: _children)
            {
                child->Render(r);
            }
        }
        void UIElement::OnEvent(UIEvent &e)
        {
            if (!_state._is_enabled || !_state._is_visible || !_state._wants_mouse_events)
                return;// 不可交互控件直接忽略
            if (e._is_handled)
                return;
            switch (e._type)
            {
                case UIEvent::EType::kMouseEnter:
                    _state._is_hovered = true;
                    _eventmap[UIEvent::EType::kMouseEnter].Invoke(e);
                    break;

                case UIEvent::EType::kMouseExit:
                    _state._is_hovered = false;
                    _eventmap[UIEvent::EType::kMouseExit].Invoke(e);
                    break;

                case UIEvent::EType::kMouseDown:
                    if (e._current_target == this)// 确保事件是作用在当前元素
                        _state._is_pressed = true;
                    _eventmap[UIEvent::EType::kMouseDown].Invoke(e);
                    break;

                case UIEvent::EType::kMouseUp:
                    if (_state._is_pressed)
                        _state._is_pressed = false;// 松开鼠标恢复 pressed 状态
                    _eventmap[UIEvent::EType::kMouseUp].Invoke(e);
                    break;

                case UIEvent::EType::kMouseClick:
                    _eventmap[UIEvent::EType::kMouseClick].Invoke(e);
                    break;

                case UIEvent::EType::kMouseDoubleClick:
                    _eventmap[UIEvent::EType::kMouseDoubleClick].Invoke(e);
                    break;

                case UIEvent::EType::kMouseMove:
                    // 如果鼠标在控件内部且不是 hover，则更新 hover 状态
                    if (!_state._is_hovered)
                        _state._is_hovered = true;
                    _eventmap[UIEvent::EType::kMouseMove].Invoke(e);
                    break;

                default:
                    AL_ASSERT(false);
                    break;
            }
        }


        ElementEvent &UIElement::OnMouseEnter()
        {
            return _eventmap[UIEvent::EType::kMouseEnter];
        }
        ElementEvent &UIElement::OnMouseExit()
        {
            return _eventmap[UIEvent::EType::kMouseExit];
        }
        ElementEvent &UIElement::OnMouseDown()
        {
            return _eventmap[UIEvent::EType::kMouseDown];
        }
        ElementEvent &UIElement::OnMouseUp()
        {
            return _eventmap[UIEvent::EType::kMouseUp];
        }
        ElementEvent &UIElement::OnMouseDoubleClick()
        {
            return _eventmap[UIEvent::EType::kMouseDoubleClick];
        }
        ElementEvent &UIElement::OnMouseMove()
        {
            return _eventmap[UIEvent::EType::kMouseMove];
        }
        ElementEvent &UIElement::OnMouseClick()
        {
            return _eventmap[UIEvent::EType::kMouseClick];
        }
        void UIElement::SetDesiredRect(f32 x, f32 y, f32 width, f32 height)
        {
            _desired_rect = {x, y, width, height};
            _content_rect = _desired_rect;
            _content_rect.x += _padding._l;
            _content_rect.y += _padding._t;
            _content_rect.z -= (_padding._l + _padding._r);
            _content_rect.w -= (_padding._t + _padding._b);
        }
        void UIElement::SetDesiredRect(Vector4f rect)
        {
            SetDesiredRect(rect.x, rect.y, rect.z, rect.w);
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
            _children.clear();
        }
        void UIElement::Serialize(FArchive &ar)
        {
            SerializeObject::Serialize(ar);
            if (auto sar = dynamic_cast<FStructedArchive *>(&ar); sar != nullptr)
            {
                sar->BeginObject("_children");
                sar->BeginArray(_children.size(), FStructedArchive::EStructedDataType::kStruct);
                String item_name;
                u32 count = 0u;
                for (auto &obj: _children)
                {
                    item_name = std::format("arr_item_{}", count++);
                    SerializerWrapper<UIElement>::Serialize(obj.get(), ar, &item_name);
                }
                sar->EndArray();
                sar->EndObject();
            }
        }
        void UIElement::Deserialize(FArchive &ar)
        {
            SerializeObject::Deserialize(ar);
            if (auto sar = dynamic_cast<FStructedArchive *>(&ar); sar != nullptr)
            {
                sar->BeginObject("_children");
                FStructedArchive::EStructedDataType type;
                u32 arr_size = sar->BeginArray(type);
                _children.resize(arr_size);
                for (u32 i = 0; i < arr_size; i++)
                {
                    String item_name{};
                    item_name = std::to_string(i);
                    Type *child_type = nullptr;
                    {
                        String type_name;
                        sar->BeginObject(item_name);
                        sar->BeginObject("_type_name");
                        *sar >> type_name;
                        child_type = Type::Find(type_name);
                        sar->EndObject();
                        sar->EndObject();
                        if (child_type == nullptr)
                        {
                            LOG_ERROR("UIElement::Deserialize: get type {} failed!", i)
                            continue;
                        }
                    }
                    _children[i].reset(child_type->CreateInstance<UIElement>());
                    _on_child_add.Invoke(_children[i].get());
                    SerializerWrapper<UIElement>::Deserialize(_children[i].get(), ar, &item_name);
                    _children[i]->SetParent(this);
                }
                sar->EndArray();
                sar->EndObject();
            }
        }
        Vector4f UIElement::GetDesiredRect() const
        {
            return _desired_rect;
        }
        bool UIElement::IsPointInside(Vector2f mouse_pos) const
        {
            return IsPointInside(mouse_pos, _content_rect);
        }
        Vector2f UIElement::GetWorldPosition() const
        {
            return Vector2f();
        }

        UIElement *UIElement::HitTest(Vector2f pos)
        {
            if (!IsPointInside(pos))
                return nullptr;

            for (auto &child: _children)
                if (auto *hit = child->HitTest(pos))
                    return hit;

            return this;
        }
    }// namespace UI
}// namespace Ailu