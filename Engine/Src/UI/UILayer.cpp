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
            static UIManager *s_mgr = UIManager::Get();
            Widget *cur_hover_widget = nullptr;
            UI::UIEvent ue;
            ue._type = EventToUIEvent(e);
            ue._mouse_position = Input::GetMousePos(e._window);
            ue._mouse_delta = Input::GetMousePosDelta();
            UIElement *capture_target = s_mgr->_capture_target;
            const bool has_capture = s_mgr->_capture_target != nullptr;
            const bool is_capture_sensitive_mouse_event = has_capture &&
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
            auto find_hover_widget = [&]() -> Widget *
            {
                auto &widget = s_mgr->_widgets;
                for (i32 i = (i32) widget.size() - 1; i >= 0; i--)
                {
                    auto w = widget[i].get();
                    if (w->_visibility != EVisibility::kVisible || w->_is_receive_event == false ||
                        w->Parent() != e._window)
                        continue;
                    if (w->IsHover(ue._mouse_position))
                        return w;
                }
                return nullptr;
            };
            Widget *top_hover_widget = find_hover_widget();
            bool is_in_zone = false;
            for (const auto &zone: s_mgr->GetInteractionZones())
            {
                if (zone._rect.z <= 0.0f || zone._rect.w <= 0.0f)
                    continue;
                if (UIElement::IsPointInside(ue._mouse_position, zone._rect))
                {
                    // Resize zones are intentionally outside their owner widget. Only reject an
                    // overlapping zone when the pointer is still inside another widget's content.
                    if (zone._owner != nullptr && top_hover_widget != nullptr && zone._owner != top_hover_widget &&
                        zone._owner->IsHover(ue._mouse_position))
                        continue;
                    is_in_zone = true;
                    break;
                }
            }
            Input::BlockInput(false);
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
                    if (ue._key_code != EKey::kRBUTTON && top_popup != nullptr && !top_popup->IsHover(ue._mouse_position))
                    {
                        s_mgr->HidePopup();
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
                    if (is_capture_sensitive_mouse_event && is_capture_owner_widget(w))
                    {
                        w->OnEvent(ue);
                        break;
                    }
                    if (w->IsHover(ue._mouse_position))//上层已经生成了事件，下次就不再响应
                    {
                        w->OnEvent(ue);
                        cur_hover_widget = w;
                        break;
                    }
                }
            }
            else
            {
                auto &widget = s_mgr->_widgets;
                if (is_capture_sensitive_mouse_event)
                {
                    for (i32 i = (i32) widget.size() - 1; i >= 0; i--)
                    {
                        auto w = widget[i].get();
                        if (w->_visibility != EVisibility::kVisible || w->_is_receive_event == false || w->Parent() != e._window)
                            continue;
                        if (is_capture_owner_widget(w))
                        {
                            w->OnEvent(ue);
                            break;
                        }
                    }
                }
                for (i32 i = (i32) widget.size() - 1; i >= 0; i--)
                {
                    auto w = widget[i].get();
                    if (w->_visibility != EVisibility::kVisible || w->_is_receive_event == false || w->Parent() != e._window)
                        continue;
                    if (w->IsHover(ue._mouse_position))//上层已经生成了事件，下次就不再响应
                    {
                        cur_hover_widget = w;
                        break;
                    }
                }
                Input::BlockInput(cur_hover_widget != nullptr);
            }
            if (s_mgr->_pre_hover_widget)
            {
                if (cur_hover_widget && cur_hover_widget != s_mgr->_pre_hover_widget || cur_hover_widget == nullptr)
                {
                    UI::UIEvent ue;
                    ue._type = UIEvent::EType::kMouseExit;
                    ue._mouse_position = Input::GetMousePos(e._window);
                    ue._mouse_delta = Input::GetMousePosDelta();
                    s_mgr->_pre_hover_widget->OnEvent(ue);
                }
            }
            s_mgr->_pre_hover_widget = cur_hover_widget;
        }
    }// namespace UI

}// namespace Ailu
