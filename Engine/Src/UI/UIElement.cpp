//
// Created by 22292 on 2024/10/28.
//

#include "UI/UIElement.h"
#include "UI/UIRenderer.h"
#include "UI/TextRenderer.h"
#include "UI/UIFramework.h"
#include "UI/Widget.h"
#include "UI/Style/UIStyles.h"
#include "UI/Style/UITheme.h"
#include <memory>

namespace Ailu
{
    using namespace Render;
    namespace UI
    {
        namespace
        {
            constexpr u32 kPropertyVisualOverrideBackground = 1u << 0u;
            constexpr u32 kPropertyVisualOverrideBorderColor = 1u << 1u;
            constexpr u32 kPropertyVisualOverrideBorderWidth = 1u << 2u;
            constexpr u32 kPropertyVisualOverrideCornerRadius = 1u << 3u;
            constexpr u32 kPropertyVisualOverrideContentColor = 1u << 4u;
            constexpr u32 kPropertyVisualOverrideFontSize = 1u << 5u;

            u32 GetPropertyVisualOverrideFlag(const String &name)
            {
                if (name == "_bg_color") return kPropertyVisualOverrideBackground;
                if (name == "_border_color") return kPropertyVisualOverrideBorderColor;
                if (name == "_thickness") return kPropertyVisualOverrideBorderWidth;
                if (name == "_corner_radius") return kPropertyVisualOverrideCornerRadius;
                if (name == "_color" || name == "_tint_color") return kPropertyVisualOverrideContentColor;
                return name == "_font_size" ? kPropertyVisualOverrideFontSize : 0u;
            }
        }

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
            RegenerateGuid();
            _depth = 0.0f;
            _on_child_add += [this](UIElement* e) {
                InvalidateLayout();
                e->_transform._p_parent = &_transform;
            };
            _on_child_remove += [this](UIElement *e){ 
                InvalidateLayout(); 
                e->_transform._p_parent = nullptr;
            };
            _is_visible = true;
        }
        UIElement::UIElement(const String &name) : UIElement()
        {
            _name = name;
        }
        UIElement::~UIElement()
        {
            if (_slot_obj != nullptr)
                _slot_obj->SetOwner(nullptr);
            if (UIManager *ui_manager = UIManager::Get(); ui_manager != nullptr)
                ui_manager->OnElementDestroying(this);
            _property_observers.clear();
            _children.clear();
        }
        UIElement *UIElement::AddChild(Ref<UIElement> child)
        {
            if (child == nullptr)
                return nullptr;
            if (child->_parent == this)
                return child.get();

            Ref<UISlot> old_slot = child->_slot_obj;
            if (child->_parent != nullptr)
            {
                UIElement *old_parent = child->_parent;
                auto it = std::find_if(old_parent->_children.begin(), old_parent->_children.end(), [&](const Ref<UIElement> &c)
                                       { return c.get() == child.get(); });
                if (it != old_parent->_children.end())
                {
                    old_parent->_on_child_remove_delegate.Invoke(child.get());
                    old_parent->_children.erase(it);
                    old_parent->InvalidateHierarchy();
                }
            }

            child->_parent = this;
            child->SetOwningWidgetRecursive(_owning_widget);
            child->_hierarchy_depth = _hierarchy_depth + 1u;
            Ref<UISlot> new_slot = CreateSlotForChild();
            if (old_slot != nullptr && new_slot != nullptr)
            {
                new_slot->_margin = old_slot->_margin;
                new_slot->_size = old_slot->_size;
            }
            child->SetSlot(new_slot);
            _children.emplace_back(child);
            _on_child_add_delegate.Invoke(child.get());
            InvalidateHierarchy();
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
                child->SetOwningWidgetRecursive(nullptr);
                child->_hierarchy_depth = 0u;
                child->SetSlot(nullptr);
                UI::UIManager::Get()->Destroy(*it);
                _children.erase(it);
                InvalidateHierarchy();
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
                (*it)->SetOwningWidgetRecursive(nullptr);
                (*it)->_hierarchy_depth = 0u;
                (*it)->SetSlot(nullptr);
                UI::UIManager::Get()->Destroy(*it);
                _children.erase(it);
                InvalidateHierarchy();
            }
        }

        bool UIElement::MoveChild(UIElement *child, u32 new_index)
        {
            if (child == nullptr || _children.empty())
                return false;

            auto it = std::find_if(_children.begin(), _children.end(), [child](const Ref<UIElement> &item)
                                   { return item.get() == child; });
            if (it == _children.end())
                return false;

            const u32 old_index = static_cast<u32>(std::distance(_children.begin(), it));
            new_index = std::min(new_index, static_cast<u32>(_children.size() - 1u));
            if (old_index == new_index)
                return false;

            Ref<UIElement> moved_child = std::move(*it);
            _children.erase(it);
            _children.insert(_children.begin() + new_index, std::move(moved_child));
            InvalidateHierarchy();
            return true;
        }

        void UIElement::RegenerateGuid()
        {
            _guid = Guid::Generate();
        }

        void UIElement::RegenerateGuidRecursive()
        {
            RegenerateGuid();
            for (const Ref<UIElement> &child : _children)
            {
                if (child != nullptr)
                    child->RegenerateGuidRecursive();
            }
        }

        UIElement *UIElement::FindChildByGuid(const Guid &guid, bool recursive)
        {
            if (_guid == guid)
                return this;
            for (const Ref<UIElement> &child : _children)
            {
                if (child == nullptr)
                    continue;
                if (child->_guid == guid)
                    return child.get();
                if (recursive)
                {
                    if (UIElement *result = child->FindChildByGuid(guid, true); result != nullptr)
                        return result;
                }
            }
            return nullptr;
        }

        const UIElement *UIElement::FindChildByGuid(const Guid &guid, bool recursive) const
        {
            return const_cast<UIElement *>(this)->FindChildByGuid(guid, recursive);
        }

        void UIElement::ClearChildren()
        {
            if (_children.empty())
                return;
            for (auto &child: _children)
            {
                child->_parent = nullptr;
                child->SetOwningWidgetRecursive(nullptr);
                child->_hierarchy_depth = 0u;
                child->SetSlot(nullptr);
                _on_child_remove_delegate.Invoke(child.get());
                UI::UIManager::Get()->Destroy(child);
            }
            _children.clear();
            InvalidateHierarchy();
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
            if (_parent == nullptr)
                EnsureStyleResolvedRecursive();
            else
                EnsureStyleResolved();
            auto refresh_abs_rect = [this]()
            {
                Vector3f corners[4] = {
                        {_arrange_rect.x, _arrange_rect.y, 1.0f},
                        {_arrange_rect.x + _arrange_rect.z, _arrange_rect.y, 1.0f},
                        {_arrange_rect.x + _arrange_rect.z, _arrange_rect.y + _arrange_rect.w, 1.0f},
                        {_arrange_rect.x, _arrange_rect.y + _arrange_rect.w, 1.0f}};
                TransformCoord(corners[0], _matrix);
                TransformCoord(corners[1], _matrix);
                TransformCoord(corners[2], _matrix);
                TransformCoord(corners[3], _matrix);
                _abs_rect = {
                        corners[0].x,
                        corners[0].y,
                        corners[1].x - corners[0].x,
                        corners[3].y - corners[0].y};
            };
            if (_is_transf_dirty)
            {
                _transform._position = _transition;
                _transform._scale = _scale;
                _transform._rotation = _rotation * k2Radius;
            }
            if (_is_layout_dirty)
            {
                if (auto renderer = UIRenderer::Get(); renderer != nullptr)
                    ++renderer->MutableStats()._ui_layout_count;
                MeasureAndArrange(dt);
                _is_layout_dirty = false;
                _matrix = CalculateWorldMatrix(true);
                _inv_matrix = MatrixInverse(_matrix);
                _is_transf_dirty = false;
                refresh_abs_rect();
            }
            if (_is_transf_dirty)
            {
                _matrix = CalculateWorldMatrix(true);
                _inv_matrix = MatrixInverse(_matrix);
                _is_transf_dirty = false;
                refresh_abs_rect();
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
            ++r.MutableStats()._ui_element_visit_count;
            EnsureStyleResolved();
            ++r.MutableStats()._ui_render_impl_count;
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
            if (!IsInteractiveEnabled() || !IsStateVisible() || !WantsMouseEvents())
                return;// 不可交互控件直接忽略
            if (e._is_handled)
                return;
            switch (e._type)
            {
                case UIEvent::EType::kMouseEnter:
                    SetHovered(true);
                    _eventmap[UIEvent::EType::kMouseEnter].Invoke(e);
                    break;

                case UIEvent::EType::kMouseExit:
                    SetHovered(false);
                    _eventmap[UIEvent::EType::kMouseExit].Invoke(e);
                    break;

                case UIEvent::EType::kMouseDown:
                    if (e._current_target == this)// 确保事件是作用在当前元素
                        SetPressed(true);
                    _eventmap[UIEvent::EType::kMouseDown].Invoke(e);
                    break;

                case UIEvent::EType::kMouseUp:
                    if (IsPressed())
                        SetPressed(false);// 松开鼠标恢复 pressed 状态
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
                    if (!IsHovered())
                        SetHovered(true);
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
            const Vector4f new_arrange_rect = {x, y, width, height};
            Vector4f new_content_rect = new_arrange_rect;
            new_content_rect.x += _padding._l;
            new_content_rect.y += _padding._t;
            new_content_rect.z -= (_padding._l + _padding._r);
            new_content_rect.w -= (_padding._t + _padding._b);
            const bool is_layout_changed = !NearbyEqual(_arrange_rect, new_arrange_rect) || !NearbyEqual(_content_rect, new_content_rect);
            const bool needs_post_arrange = _is_layout_dirty || is_layout_changed;
            _arrange_rect = new_arrange_rect;
            _content_rect = new_content_rect;
            // 这里算出的 mat 同时用于 _abs_rect；必须一并写回 _matrix/_inv_matrix。
            // 否则当父容器在本帧 Update 之后才重新 Arrange 时，子元素缓存的 _matrix
            // 会停留在上一次布局，绘制仍然按旧位置烘焙（旧图标残留），而 _abs_rect
            // 已是新值，且元素不再 dirty，问题会一直保持到整棵子树被重建。
            if (_is_transf_dirty)
            {
                _transform._position = _transition;
                _transform._scale = _scale;
                _transform._rotation = _rotation * k2Radius;
            }
            auto mat = CalculateWorldMatrix(true);
            _matrix = mat;
            _inv_matrix = MatrixInverse(_matrix);
            _is_transf_dirty = false;
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
            if (is_layout_changed)
            {
                _paint_dirty = true;
                _dirty_reasons |= EUIInvalidationReason::kLayout | EUIInvalidationReason::kPaint;
                _debug_paint_dirty = true;
                _debug_dirty_reasons |= EUIInvalidationReason::kLayout | EUIInvalidationReason::kPaint;
                _is_layout_dirty = true;
                _is_transf_dirty = true;
                if (_owning_widget != nullptr)
                    _owning_widget->InvalidatePaint(EUIInvalidationReason::kLayout | EUIInvalidationReason::kPaint, this);
                for (auto &child: _children)
                    child->InvalidateLayout(true);
            }
            if (needs_post_arrange)
                PostArrange();
        }
        void UIElement::Arrange(Vector4f rect)
        {
            Arrange(rect.x, rect.y, rect.z, rect.w);
        }

        void UIElement::SetVisible(bool visible)
        {
            if (_is_visible == visible)
                return;
            _is_visible = visible;
            InvalidateHierarchy();
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
        void UIElement::SetSlot(Ref<UISlot> slot)
        {
            if (_slot_obj != nullptr)
                _slot_obj->SetOwner(nullptr);
            _slot_obj = slot;
            if (_slot_obj != nullptr)
                _slot_obj->SetOwner(this);
            InvalidateLayout();
        }
        void UIElement::OnPropertyChanged(const PropertyInfo &prop)
        {
            Object::OnPropertyChanged(prop);
            const String &name = prop.Name();
            UIControlVisualOverride *style_override = GetPropertyVisualOverride();
            const u32 visual_override_flag = GetPropertyVisualOverrideFlag(name);
            if (style_override != nullptr && visual_override_flag != 0u)
            {
                if (name == "_bg_color")
                {
                    UIBrush brush;
                    brush._type = EUIBrushType::kColor;
                    brush._tint = prop.Get<Color>(this);
                    style_override->SetBackground(brush);
                }
                else if (name == "_border_color")
                {
                    style_override->SetBorderColor(prop.Get<Color>(this));
                }
                else if (name == "_thickness")
                {
                    style_override->SetBorderWidth(prop.Get<Vector4f>(this));
                }
                else if (name == "_corner_radius")
                {
                    style_override->SetCornerRadius(prop.Get<Vector4f>(this));
                }
                else if (name == "_color" || name == "_tint_color")
                {
                    style_override->SetContentColor(prop.Get<Color>(this));
                }
                else if (name == "_font_size")
                {
                    style_override->SetFontSize(prop.Get<f32>(this));
                }
                _property_visual_override_flags |= visual_override_flag;
                InvalidateStyle(name == "_font_size" ? EStyleInvalidation::kLayoutAndPaint : EStyleInvalidation::kPaintOnly);
            }
            if (name == "_slot_obj")
            {
                InvalidateLayout();
            }
            else if (name == "_padding")
            {
                InvalidateLayout();
            }
            else if (name == "_transition" || name == "_rotation" || name == "_scale")
            {
                InvalidateTransform();
            }
            else if (name == "_visibility")
            {
                _is_visible = (_visibility == EVisibility::kVisible);
                InvalidateHierarchy();
            }
        }
        void UIElement::Serialize(FArchive &ar)
        {
            EnsureSlotObject();
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
            if (_slot_obj != nullptr)
                _slot_obj->SetOwner(this);
            else
                EnsureSlotObject();
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
                    const Type *child_type = nullptr;
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
                    _children[i]->SetParent(this);
                    _children[i]->_hierarchy_depth = _hierarchy_depth + 1u;
                    _on_child_add_delegate.Invoke(_children[i].get());
                    SerializerWrapper<UIElement>::Deserialize(_children[i].get(), ar, &item_name);
                }
                sar->EndArray();
                sar->EndObject();
            }
            RestorePropertyVisualOverrides();
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
            if (!IsInteractiveEnabled() || !IsStateVisible())
                return;
            UIManager::Get()->SetFocus(this);
        }
        void UIElement::InvalidateLayout(bool propagate_down)
        {
            _paint_dirty = true;
            _dirty_reasons |= EUIInvalidationReason::kLayout | EUIInvalidationReason::kPaint;
            _debug_paint_dirty = true;
            _debug_dirty_reasons |= EUIInvalidationReason::kLayout | EUIInvalidationReason::kPaint;
            if (_owning_widget != nullptr)
                _owning_widget->InvalidatePaint(EUIInvalidationReason::kLayout | EUIInvalidationReason::kPaint, this);
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
            _paint_dirty = true;
            _dirty_reasons |= EUIInvalidationReason::kTransform | EUIInvalidationReason::kPaint;
            _debug_paint_dirty = true;
            _debug_dirty_reasons |= EUIInvalidationReason::kTransform | EUIInvalidationReason::kPaint;
            if (_owning_widget != nullptr)
                _owning_widget->InvalidatePaint(EUIInvalidationReason::kTransform | EUIInvalidationReason::kPaint, this);
            _is_transf_dirty = true;
            for (auto &c: _children)
                c->InvalidateTransform();
        }
        void UIElement::InvalidatePaint()
        {
            _paint_dirty = true;
            _dirty_reasons |= EUIInvalidationReason::kPaint;
            _debug_paint_dirty = true;
            _debug_dirty_reasons |= EUIInvalidationReason::kPaint;
            if (_owning_widget != nullptr)
                _owning_widget->InvalidatePaint(EUIInvalidationReason::kPaint, this);
        }
        void UIElement::InvalidateHierarchy()
        {
            _paint_dirty = true;
            _dirty_reasons |= EUIInvalidationReason::kHierarchy | EUIInvalidationReason::kLayout | EUIInvalidationReason::kPaint;
            _debug_paint_dirty = true;
            _debug_dirty_reasons |= EUIInvalidationReason::kHierarchy | EUIInvalidationReason::kLayout | EUIInvalidationReason::kPaint;
            if (_owning_widget != nullptr)
            {
                _owning_widget->InvalidatePaint(EUIInvalidationReason::kHierarchy | EUIInvalidationReason::kLayout |
                                                        EUIInvalidationReason::kPaint,
                                                this);
            }
            InvalidateLayout();
        }
        void UIElement::ClearPaintDirtyRecursive()
        {
            _paint_dirty = false;
            _dirty_reasons = EUIInvalidationReason::kNone;
            for (auto &child: _children)
                child->ClearPaintDirtyRecursive();
        }
        void UIElement::ClearDebugPaintDirtyRecursive()
        {
            _debug_paint_dirty = false;
            _debug_dirty_reasons = EUIInvalidationReason::kNone;
            for (auto &child: _children)
                child->ClearDebugPaintDirtyRecursive();
        }
        void UIElement::SnapshotPaintDirtyToDebugRecursive()
        {
            if (_paint_dirty)
            {
                _debug_paint_dirty = true;
                _debug_dirty_reasons |= _dirty_reasons;
            }
            for (auto &child: _children)
                child->SnapshotPaintDirtyToDebugRecursive();
        }
        void UIElement::SetOwningWidgetRecursive(Widget *widget)
        {
            _owning_widget = widget;
            for (auto &child: _children)
                child->SetOwningWidgetRecursive(widget);
        }
        // ── 交互状态位域访问器实现 ─────────────────────────────────────
        bool UIElement::IsHovered() const { return (_state_flags & (u32)EUIElementState::kHovered) != 0u; }
        bool UIElement::IsPressed() const { return (_state_flags & (u32)EUIElementState::kPressed) != 0u; }
        bool UIElement::IsFocused() const { return (_state_flags & (u32)EUIElementState::kFocused) != 0u; }
        bool UIElement::IsInteractiveEnabled() const { return (_state_flags & (u32)EUIElementState::kEnabled) != 0u; }
        bool UIElement::IsStateVisible() const { return (_state_flags & (u32)EUIElementState::kVisible) != 0u; }
        bool UIElement::WantsMouseEvents() const { return (_state_flags & (u32)EUIElementState::kMouseEvents) != 0u; }

        void UIElement::SetHovered(bool v)
        {
            if (IsHovered() == v)
                return;
            v ? (_state_flags |= (u32)EUIElementState::kHovered) : (_state_flags &= ~(u32)EUIElementState::kHovered);
            InvalidatePaint();
        }
        void UIElement::SetPressed(bool v)
        {
            if (IsPressed() == v)
                return;
            v ? (_state_flags |= (u32)EUIElementState::kPressed) : (_state_flags &= ~(u32)EUIElementState::kPressed);
            InvalidatePaint();
        }
        void UIElement::SetInteractiveEnabled(bool v)
        {
            if (IsInteractiveEnabled() == v)
                return;
            v ? (_state_flags |= (u32)EUIElementState::kEnabled) : (_state_flags &= ~(u32)EUIElementState::kEnabled);
            InvalidatePaint();
        }
        void UIElement::SetStateVisible(bool v)
        {
            if (IsStateVisible() == v)
                return;
            v ? (_state_flags |= (u32)EUIElementState::kVisible) : (_state_flags &= ~(u32)EUIElementState::kVisible);
            InvalidatePaint();
        }
        void UIElement::SetWantsMouseEvents(bool v)
        {
            if (WantsMouseEvents() == v)
                return;
            v ? (_state_flags |= (u32)EUIElementState::kMouseEvents) : (_state_flags &= ~(u32)EUIElementState::kMouseEvents);
            InvalidatePaint();
        }
        void UIElement::SetFocused(bool v)
        {
            if (IsFocused() == v)
                return;
            v ? (_state_flags |= (u32)EUIElementState::kFocused) : (_state_flags &= ~(u32)EUIElementState::kFocused);
            InvalidatePaint();
        }

        EUIVisualState UIElement::GetVisualState() const
        {
            if (!IsInteractiveEnabled())  return EUIVisualState::kDisabled;
            if (IsPressed())              return EUIVisualState::kPressed;
            if (IsHovered())              return EUIVisualState::kHovered;
            if (IsFocused())              return EUIVisualState::kFocused;
            return EUIVisualState::kNormal;
        }

        // ── Style 解析 ──────────────────────────────────────────────────
        const UITheme *UIElement::GetTheme() const
        {
            return UIManager::Get()->GetTheme();
        }

        UIStyleContext UIElement::BuildStyleContext() const
        {
            UIStyleContext ctx;
            ctx._theme = GetTheme();
            ctx._parent = _parent;
            ctx._theme_revision = ctx._theme ? ctx._theme->Revision() : 0u;
            return ctx;
        }

        void UIElement::EnsureStyleResolved()
        {
            const UITheme *theme = GetTheme();
            if (!theme)
                return;

            const u64 current_revision = theme->Revision();

            if (!_is_style_dirty && _resolved_theme_revision == current_revision)
                return;

            const UIStyleContext context = BuildStyleContext();
            ResolveStyle(context);

            _resolved_theme_revision = context._theme_revision;
            _is_style_dirty = false;
        }

        void UIElement::EnsureStyleResolvedRecursive()
        {
            EnsureStyleResolved();
            for (auto &child: _children)
                child->EnsureStyleResolvedRecursive();
        }

        void UIElement::InvalidateStyle(EStyleInvalidation invalidation)
        {
            _is_style_dirty = true;
            _paint_dirty = true;
            _dirty_reasons |= EUIInvalidationReason::kPaint;
            _debug_paint_dirty = true;
            _debug_dirty_reasons |= EUIInvalidationReason::kPaint;
            if (_owning_widget != nullptr)
                _owning_widget->InvalidatePaint(EUIInvalidationReason::kPaint, this);

            if (invalidation == EStyleInvalidation::kLayoutAndPaint)
                InvalidateLayout();
        }

        void UIElement::SetFocusedInternal(bool v)
        {
            if (IsFocused() == v)
                return;
            SetFocused(v);
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
            EnsureStyleResolved();
        return EnsureSlotObject()->_size;
        }
        Ref<UISlot> &UIElement::EnsureSlotObject() const
        {
            if (_slot_obj == nullptr)
            {
                _slot_obj = MakeRef<UISlot>();
                _slot_obj->SetOwner(const_cast<UIElement *>(this));
            }
            return _slot_obj;
        }
        Ref<UISlot> UIElement::CreateSlotForChild()
        {
            return MakeRef<UISlot>();
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

        void UIElement::RestorePropertyVisualOverrides()
        {
            if (_property_visual_override_flags == 0u || GetPropertyVisualOverride() == nullptr)
                return;
            for (const Type *type = GetType(); type != nullptr; type = type->BaseType())
            {
                for (const PropertyInfo &property : type->GetProperties())
                {
                    const u32 flag = GetPropertyVisualOverrideFlag(property.Name());
                    if (flag != 0u && (_property_visual_override_flags & flag) != 0u)
                        OnPropertyChanged(property);
                }
            }
        }
    }// namespace UI
}// namespace Ailu
