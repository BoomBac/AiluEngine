//
// Created by 22292 on 2024/10/28.
//

#include "UI/UIElement.h"
#include "UI/UIRenderer.h"
#include "UI/TextRenderer.h"
#include "UI/UIFramework.h"

namespace Ailu
{
    using namespace Render;
    namespace UI
    {
        String UIEvent::ToString() const
        {
            return "UIEvent{ type=" + TypeToString(_type) +
                   ", pos=" + _mouse_position.ToString() +
                   ", delta=" + _mouse_delta.ToString() +
                   ", target=" + (_target ? _target->Name() : String("null")) +
                   ", current=" + (_current_target ? _current_target->Name() : String("null")) +
                   ", handled=" + String(_is_handled ? "true" : "false") +
                   " }";
        }

        UIElement::UIElement() : SerializeObject()
        {
            _name = std::format("ui_element_{}", _id);
            _depth = 0.0f;
            _on_child_add += [this](UIElement* e) {
                InvalidateLayout();
                e->_transform._p_parent = &_transform;
            };
            _on_child_remove += [this](UIElement *e){ 
                InvalidateLayout(); 
                e->_transform._p_parent = nullptr;
            };
        }
        UIElement::UIElement(const String &name) : UIElement()
        {
            _name = name;
        }
        UIElement::~UIElement()
        {
            UIManager::Get()->OnElementDestroying(this);
            _children.clear();
        }
        UIElement *UIElement::AddChild(Ref<UIElement> child)
        {
            child->_parent = this;
            ++child->_hierarchy_depth;
            _children.emplace_back(child);
            _on_child_add_delegate.Invoke(child.get());
            return child.get();
        }
        void UIElement::RemoveChild(Ref<UIElement> child)
        {
            if (child == nullptr)
                return;
            auto it = std::find(_children.begin(), _children.end(), child);
            if (it != _children.end())
            {
                _on_child_remove_delegate.Invoke(it->get());
                child->_parent = nullptr;
                --child->_hierarchy_depth;
                UI::UIManager::Get()->Destroy(*it);
                _children.erase(it);
            }
        }
        void UIElement::RemoveChild(UIElement *child)
        {
            if (child == nullptr)
                return;
            auto it = std::find_if(_children.begin(), _children.end(), [&](Ref<UIElement> c)
                                   { return c.get() == child; });
            if (it != _children.end())
            {
                _on_child_remove_delegate.Invoke(it->get());
                (*it)->_parent = nullptr;
                --(*it)->_hierarchy_depth;
                UI::UIManager::Get()->Destroy(*it);
                _children.erase(it);
            }
        }

        void UIElement::ClearChildren()
        {
            if (_children.empty())
                return;
            for (auto &child: _children)
            {
                child->_parent = nullptr;
                --child->_hierarchy_depth;
                _on_child_remove_delegate.Invoke(child.get());
                UI::UIManager::Get()->Destroy(child);
            }
            _children.clear();
        }
        i32 UIElement::IndexOf(UIElement *child)
        {
            auto it = std::find_if(_children.begin(), _children.end(), [&](Ref<UIElement> c)
                                   { return c.get() == child; });
            if (it != _children.end())
            {
                return static_cast<i32>(std::distance(_children.begin(), it));
            }
            return -1;
        }
        UIElement *UIElement::ChildAt(u32 index)
        {
            if (index < static_cast<u32>(_children.size()))
                return _children[index].get();
            return nullptr;
        }
        void UIElement::Update(f32 dt)
        {
            if (!_is_visible)
                return;
            if (_is_transf_dirty)
            {
                _transform._position = _transition;
                _transform._scale = _scale;
                _transform._rotation = _rotation * k2Radius;
            }
            if (_is_layout_dirty)
            {
                MeasureAndArrange(dt);
                _is_layout_dirty = false;
                _matrix = CalculateWorldMatrix();
                _inv_matrix = MatrixInverse(_matrix);
                _is_transf_dirty = false;
            }
            if (_is_transf_dirty)
            {
                _matrix = CalculateWorldMatrix();
                _inv_matrix = MatrixInverse(_matrix);
                _is_transf_dirty = false;
                Vector3f corners[4] = {
                        {_arrange_rect.x, _arrange_rect.y, 1.0f},
                        {_arrange_rect.x + _arrange_rect.z, _arrange_rect.y, 1.0f},
                        {_arrange_rect.x + _arrange_rect.z, _arrange_rect.y + _arrange_rect.w, 1.0f},
                        {_arrange_rect.x, _arrange_rect.y + _arrange_rect.w, 1.0f}};
                TransformCoord(corners[0], _matrix);
                TransformCoord(corners[1], _matrix);
                TransformCoord(corners[2], _matrix);
                TransformCoord(corners[3], _matrix);
                _abs_rect = {corners[0].x,
                             corners[0].y,
                             corners[1].x - corners[0].x,
                             corners[3].y - corners[0].y};
            }
            for (auto &child: _children)
                child->Update(dt);
        }

        void UIElement::PostUpdate(f32 dt)
        {
            if (!_is_visible)
                return;
            for (auto &child: _children)
                child->PostUpdate(dt);
        }

        void UIElement::Render(UIRenderer &r)
        {
            if (!_is_visible)
                return;
            RenderImpl(r);
        }
        void UIElement::PreUpdate(f32 dt)
        {
            if (!_is_visible)
                return;
            for (auto &child: _children)
                child->PreUpdate(dt);
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
                case UIEvent::EType::kKeyDown:
                    _eventmap[UIEvent::EType::kKeyDown].Invoke(e);
                    break;
                case UIEvent::EType::kKeyUp:
                    _eventmap[UIEvent::EType::kKeyUp].Invoke(e);
                    break;
                case UIEvent::EType::kMouseScroll:
                    _eventmap[UIEvent::EType::kMouseScroll].Invoke(e);
                    break;
                case UIEvent::EType::kDropFiles:
                        _eventmap[UIEvent::EType::kDropFiles].Invoke(e);
                    break;
                default:
                {
                    LOG_ERROR("UIElement::OnEvent: unhandled event type {}", static_cast<u32>(e._type))
                }
                    break;
            }
        }


        ElementEvent::EventView UIElement::OnMouseEnter()
        {
            return _eventmap[UIEvent::EType::kMouseEnter].GetEventView();
        }
        ElementEvent::EventView UIElement::OnMouseExit()
        {
            return _eventmap[UIEvent::EType::kMouseExit].GetEventView();
        }
        ElementEvent::EventView UIElement::OnMouseDown()
        {
            return _eventmap[UIEvent::EType::kMouseDown].GetEventView();
        }
        ElementEvent::EventView UIElement::OnMouseUp()
        {
            return _eventmap[UIEvent::EType::kMouseUp].GetEventView();
        }
        ElementEvent::EventView UIElement::OnMouseDoubleClick()
        {
            return _eventmap[UIEvent::EType::kMouseDoubleClick].GetEventView();
        }
        ElementEvent::EventView UIElement::OnMouseMove()
        {
            return _eventmap[UIEvent::EType::kMouseMove].GetEventView();
        }
        ElementEvent::EventView UIElement::OnKeyDown()
        {
            return _eventmap[UIEvent::EType::kKeyDown].GetEventView();
        }
        ElementEvent::EventView UIElement::OnKeyUp()
        {
            return _eventmap[UIEvent::EType::kKeyUp].GetEventView();
        }
        ElementEvent::EventView UIElement::OnMouseScroll()
        {
            return _eventmap[UIEvent::EType::kMouseScroll].GetEventView();
        }
        ElementEvent::EventView UIElement::OnFileDrop()
        {
            return _eventmap[UIEvent::EType::kDropFiles].GetEventView();
        }
        ElementEvent::EventView UIElement::OnMouseClick()
        {
            return _eventmap[UIEvent::EType::kMouseClick].GetEventView();
        }
        void UIElement::Arrange(f32 x, f32 y, f32 width, f32 height)
        {
            _arrange_rect = {x, y, width, height};
            _content_rect = _arrange_rect;
            _content_rect.x += _padding._l;
            _content_rect.y += _padding._t;
            _content_rect.z -= (_padding._l + _padding._r);
            _content_rect.w -= (_padding._t + _padding._b);
            auto mat = CalculateWorldMatrix();
            Vector3f corners[4] = {
                    {_arrange_rect.x, _arrange_rect.y, 1.0f},
                    {_arrange_rect.x + _arrange_rect.z, _arrange_rect.y, 1.0f},
                    {_arrange_rect.x + _arrange_rect.z, _arrange_rect.y + _arrange_rect.w, 1.0f},
                    {_arrange_rect.x, _arrange_rect.y + _arrange_rect.w, 1.0f}};
            TransformCoord(corners[0],mat);
            TransformCoord(corners[1], mat);
            TransformCoord(corners[2], mat);
            TransformCoord(corners[3], mat);
            _abs_rect = {corners[0].x,
                         corners[0].y,
                         corners[1].x - corners[0].x,
                         corners[3].y - corners[0].y};
            InvalidateLayout(true);
            PostArrange();
        }
        void UIElement::Arrange(Vector4f rect)
        {
            Arrange(rect.x, rect.y, rect.z, rect.w);
        }

        void UIElement::SetVisible(bool visible)
        {
            _is_visible = visible;
        }
        bool UIElement::IsVisible() const
        {
            return _is_visible;
        }
        Vector4f UIElement::GetContentRect() const
        {
            Vector4f rect = _abs_rect;
            rect.x += _padding._l * _scale.x;
            rect.y += _padding._t * _scale.y;
            rect.z -= (_padding._l + _padding._r) * _scale.x;
            rect.w -= (_padding._t + _padding._b) * _scale.y;
            return rect;
        }
        void UIElement::SetDepth(f32 depth)
        {
            _depth = depth;
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
                    _on_child_add_delegate.Invoke(_children[i].get());
                    SerializerWrapper<UIElement>::Deserialize(_children[i].get(), ar, &item_name);
                    _children[i]->SetParent(this);
                }
                sar->EndArray();
                sar->EndObject();
            }
            PostDeserialize();
        }
        
        Vector4f UIElement::GetArrangeRect() const
        {
            return _abs_rect;
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
            Vector2f lpos = TransformCoord(_inv_matrix, {pos, 0.0f}).xy;
            if (!IsPointInside(lpos))
                return nullptr;

            for (auto &child: _children)
                if (auto *hit = child->HitTest(pos))
                    return hit;

            return this;
        }
        void UIElement::RequestFocus()
        {
            if (!_state._is_enabled || !_state._is_visible)
                return;
            UIManager::Get()->SetFocus(this);
        }
        void UIElement::InvalidateLayout(bool propagate_down)
        {
            //这里可以添加停止策略，例如如果父的 size policy 是 Fixed，只需要局部更新，不冒泡。
            if (!_is_layout_dirty)
            {
                _is_layout_dirty = true;
                if (propagate_down)
                {
                    for (auto &c: _children)
                        c->InvalidateLayout(true);
                }
                else
                {
                    if (_parent)
                        _parent->InvalidateLayout();// 向上传递
                }
            }
        }
        void UIElement::InvalidateTransform()
        {
            if (!_is_transf_dirty)
            {
                _is_transf_dirty = true;
                for (auto &c: _children)
                    c->InvalidateTransform();
            }
        }
        void UIElement::SetFocusedInternal(bool v)
        {
            if (_state._is_focused == v)
                return;
            _state._is_focused = v;
            if (v)
                _on_focus_gained_delegate.Invoke();
            else
                _on_focus_lost_delegate.Invoke();
        }
        void UIElement::ApplyTransform()
        {
            
        }
        Vector2f UIElement::MeasureDesiredSize()
        {
            return _slot._size;
        }
        Matrix4x4f UIElement::CalculateWorldMatrix(bool is_exclude_self_offset) const
        {
            Math::Transform2D tmp = _transform;
            if (!is_exclude_self_offset)
                tmp._position.xy += _arrange_rect.xy;
            Matrix4x4f mat = Math::Transform2D::ToMatrix(tmp);
            if (_parent)
            {
                mat = _parent->CalculateWorldMatrix(false) * mat;
            }
            return mat;
        }
        void UIElement::PostDeserialize()
        {
            InvalidateLayout();
        }
    }// namespace UI
}// namespace Ailu