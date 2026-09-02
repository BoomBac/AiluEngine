#include "UI/UILayer.h"
#include "Framework/Common/Input.h"
#include "Framework/Events/KeyEvent.h"
#include "Framework/Events/MouseEvent.h"
#include "Framework/Events/WindowEvent.h"
#include "UI/Widget.h"
#include "UI/UIFramework.h"
#include "pch.h"


namespace Ailu
{
    namespace UI
    {
        static UI::UIEvent::EType EventToUIEvent(const Ailu::Event &e)
        {
            switch (e.GetEventType())
            {
                case EEventType::kMouseMoved:
                    return UI::UIEvent::EType::kMouseMove;
                case EEventType::kMouseButtonPressed:
                    return UI::UIEvent::EType::kMouseDown;
                case EEventType::kMouseButtonReleased:
                    return UI::UIEvent::EType::kMouseUp;
                case EEventType::kMouseScroll:
                    return UI::UIEvent::EType::kMouseScroll;
                case EEventType::kMouseExitWindow:
                    return UI::UIEvent::EType::kMouseExitWindow;
                case EEventType::kKeyPressed:
                    return UI::UIEvent::EType::kKeyDown;
                case EEventType::kKeyReleased:
                    return UI::UIEvent::EType::kKeyUp;
                case EEventType::kDragFile:
                    return UI::UIEvent::EType::kDropFiles;
                default:
                    return UI::UIEvent::EType::kMouseMove;
            }
            return UI::UIEvent::EType::kMouseMove;
        }

        static Vector2f GetEventMousePosition(const Ailu::Event &event)
        {
            if (event.GetEventType() == EEventType::kMouseMoved)
            {
                const auto &mouse_event = static_cast<const MouseMovedEvent &>(event);
                return {mouse_event.GetX(), mouse_event.GetY()};
            }
            return Input::GetMousePosAccurate(event._window);
        }
        UILayer::UILayer()
        {
        }
        UILayer::UILayer(const String &name)
        {
        }
        UILayer::~UILayer()
        {
        }
        void UILayer::OnAttach() {}
        void UILayer::OnDetach() {}
        void UILayer::OnEvent(Ailu::Event &e)
        {
            const bool is_mouse_event = (e.GetCategoryFlags() & EEventCategory::kEventCategoryMouse) != 0;
            if (e.Handled() || (is_mouse_event && InputRouteState::Get().IsMouseCaptured()))
                return;
            static UIManager *s_mgr = UIManager::Get();
            Widget *cur_hover_widget = nullptr;
            UI::UIEvent ue;
            ue._type = EventToUIEvent(e);
            ue._mouse_position = GetEventMousePosition(e);
            ue._mouse_delta = Input::GetMousePosDelta();
            const bool is_keyboard_event = (e.GetCategoryFlags() & EEventCategory::kEventCategoryKeyboard) != 0;
            const InputChannel route_channel = is_keyboard_event ? InputChannel::kKeyboard : InputChannel::kMouse;
            if (e.GetEventType() == EEventType::kKeyPressed)
                ue._key_code = dynamic_cast<KeyEvent *>(&e)->GetKeyCode();
            else if (e.GetEventType() == EEventType::kKeyReleased)
                ue._key_code = dynamic_cast<KeyReleasedEvent *>(&e)->GetKeyCode();
            UIElement *capture_target = s_mgr->_capture_target;
            Widget *modal_widget = s_mgr->GetModalPopupWidget();
            const bool has_capture = s_mgr->_capture_target != nullptr;
            bool is_capture_sensitive_mouse_event = has_capture &&
                                                    (ue._type == UI::UIEvent::EType::kMouseMove ||
                                                     ue._type == UI::UIEvent::EType::kMouseUp);
            const auto is_capture_owner_widget = [capture_target](Widget *w)
            {
                if (!capture_target || !w || !w->Root())
                    return false;
                UIElement *node = capture_target;
                while (node)
                {
                    if (node == w->Root())
                        return true;
                    node = node->GetParent();
                }
                return false;
            };
            auto dispatch_widget = [&](Widget *widget) -> bool
            {
                if (widget == nullptr)
                    return false;
                widget->OnEvent(ue);
                if (ue._is_handled)
                {
                    e.SetHandled();
                    InputRouteState::Get().Consume(route_channel);
                }
                return ue._is_handled;
            };
            if (is_keyboard_event)
            {
                UIElement *focused = s_mgr->_focus_target;
                if (focused != nullptr)
                {
                    for (i32 i = (i32) s_mgr->_widgets.size() - 1; i >= 0; --i)
                    {
                        Widget *widget = s_mgr->_widgets[i].get();
                        if (widget->_visibility != EVisibility::kVisible || widget->Parent() != e._window ||
                            (modal_widget != nullptr && widget != modal_widget))
                            continue;
                        UIElement *node = focused;
                        while (node != nullptr && node != widget->Root())
                            node = node->GetParent();
                        if (node == widget->Root())
                        {
                            dispatch_widget(widget);
                            break;
                        }
                    }
                }
                return;
            }
            bool is_in_zone = false;
            for (const auto &zone: s_mgr->GetInteractionZones())
            {
                if (zone._rect.z <= 0.0f || zone._rect.w <= 0.0f)
                    continue;
                if (zone._owner != nullptr && zone._owner->Parent() != e._window)
                    continue;
                if (UIElement::IsPointInside(ue._mouse_position, zone._rect))
                {
                    // A registered interaction zone is an explicit input layer. Its rectangle is
                    // authoritative, including shared boundaries at resize corners.
                    is_in_zone = true;
                    break;
                }
            }
            if (!is_in_zone)
            {
                if (e.GetCategoryFlags() & EEventCategory::kEventCategoryKeyboard)
                    ue._key_code = dynamic_cast<KeyEvent *>(&e)->GetKeyCode();
                else if (e.GetEventType() == EEventType::kMouseButtonPressed)
                    ue._key_code = static_cast<MouseButtonPressedEvent *>(&e)->GetButton();
                else if (e.GetEventType() == EEventType::kMouseButtonReleased)
                {
                    ue._key_code = static_cast<MouseButtonReleasedEvent *>(&e)->GetButton();
                    Widget *top_popup = s_mgr->GetPopupWidget();
                    const u64 popup_group_id = s_mgr->GetPopupGroupId(top_popup);
                    const bool is_popup_group_click = popup_group_id != 0u &&
                                                       s_mgr->IsPointInsidePopupGroup(popup_group_id, ue._mouse_position);
                    if (ue._key_code != EKey::kRBUTTON && top_popup != nullptr && !s_mgr->IsPopupModal(top_popup) &&
                        !is_popup_group_click &&
                        (popup_group_id != 0u || !top_popup->IsHover(ue._mouse_position)))
                    {
                        if (popup_group_id != 0u)
                            s_mgr->HidePopupGroup(popup_group_id);
                        else
                            s_mgr->HidePopup();
                        // HidePopup clears the manager's capture target. Do not use the stale
                        // snapshot below, otherwise is_capture_owner_widget() may call GetParent()
                        // on an element that is already pending destruction.
                        capture_target = nullptr;
                        is_capture_sensitive_mouse_event = false;
                        modal_widget = s_mgr->GetModalPopupWidget();
                    }
                }
                else if (e.GetEventType() == EEventType::kMouseScroll)
                {
                    ue._scroll_delta = static_cast<MouseScrollEvent *>(&e)->GetOffsetY();
                }
                else if (e.GetEventType() == EEventType::kDragFile)
                {
                    auto *drag_file_event = static_cast<DragFileEvent *>(&e);
                    ue._drop_files = drag_file_event->GetDragedFilesPath();
                    if (drag_file_event->HasPosition())
                        ue._mouse_position = Vector2f(drag_file_event->GetX(), drag_file_event->GetY());
                    LOG_INFO("UILayer: drop files at {}", ue._mouse_position.ToString());
                }
                else {}
                auto &widget = s_mgr->_widgets;
                for (i32 i = (i32) widget.size() - 1; i >= 0; i--)
                {
                    auto w = widget[i].get();
                    if (w->_visibility != EVisibility::kVisible || w->_is_receive_event == false || w->Parent() != e._window)
                        continue;
                    if (modal_widget != nullptr && w != modal_widget && !s_mgr->IsPopupAboveModal(w))
                        continue;
                    if (is_capture_sensitive_mouse_event && is_capture_owner_widget(w))
                    {
                        dispatch_widget(w);
                        break;
                    }
                    if (w->IsHover(ue._mouse_position))//上层已经生成了事件，下次就不再响应
                    {
                        dispatch_widget(w);
                        cur_hover_widget = w;
                        break;
                    }
                }
            }
            else
            {
                // An interaction zone is an exclusive input layer.  It may be outside its
                // owner's widget, so a widget behind the zone can still pass IsHover().  That
                // widget must not remain the current hover target while the zone is active.
                cur_hover_widget = nullptr;
                auto &widget = s_mgr->_widgets;
                if (is_capture_sensitive_mouse_event)
                {
                    for (i32 i = (i32) widget.size() - 1; i >= 0; i--)
                    {
                        auto w = widget[i].get();
                        if (w->_visibility != EVisibility::kVisible || w->_is_receive_event == false || w->Parent() != e._window)
                            continue;
                        if (modal_widget != nullptr && w != modal_widget && !s_mgr->IsPopupAboveModal(w))
                            continue;
                        if (is_capture_owner_widget(w))
                        {
                            dispatch_widget(w);
                            break;
                        }
                    }
                }
            }
            if (capture_target != nullptr && is_capture_sensitive_mouse_event)
            {
                e.SetHandled();
                InputRouteState::Get().Consume(InputChannel::kMouse);
            }
            else if (modal_widget != nullptr && !e.Handled())
            {
                e.SetHandled();
                InputRouteState::Get().Consume(InputChannel::kMouse);
            }
            if (s_mgr->_pre_hover_widget)
            {
                if (cur_hover_widget && cur_hover_widget != s_mgr->_pre_hover_widget || cur_hover_widget == nullptr)
                {
                    UI::UIEvent ue;
                    ue._type = UIEvent::EType::kMouseExit;
                    ue._mouse_position = GetEventMousePosition(e);
                    ue._mouse_delta = Input::GetMousePosDelta();
                    s_mgr->_pre_hover_widget->OnEvent(ue);
                }
            }
            s_mgr->_pre_hover_widget = cur_hover_widget;
        }
    }// namespace UI

}// namespace Ailu
