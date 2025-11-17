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
            if (e.GetCategoryFlags() & EEventCategory::kEventCategoryKeyboard)
                ue._key_code = dynamic_cast<KeyEvent *>(&e)->GetKeyCode();
            else if (e.GetEventType() == EEventType::kMouseButtonPressed)
                ue._key_code = static_cast<MouseButtonPressedEvent *>(&e)->GetButton();
            else if (e.GetEventType() == EEventType::kMouseButtonReleased)
            {
                ue._key_code = static_cast<MouseButtonReleasedEvent *>(&e)->GetButton();
                if (ue._key_code == EKey::kRBUTTON)
                {

                }
                if (s_mgr->GetPopupWidget() != s_mgr->_pre_hover_widget)
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

                ue._drop_files = static_cast<DragFileEvent *>(&e)->GetDragedFilesPath();
            }
            else {}
            auto &widget = s_mgr->_widgets;
            for (i32 i = (i32)widget.size() - 1; i >= 0; i--)
            {
                auto w = widget[i].get();
                if (w->_visibility != EVisibility::kVisible || w->_is_receive_event == false || w->Parent() != e._window)
                    continue;
                if (w->IsHover(ue._mouse_position))//上层已经生成了事件，下次就不再响应
                {
                    w->OnEvent(ue);
                    cur_hover_widget = w;
                    break;
                }
                //else
                //{
                //    UI::UIEvent ue;
                //    ue._type = UIEvent::EType::kMouseExitWindow;
                //    ue._mouse_position = Input::GetMousePos(e._window);
                //    ue._mouse_delta = Input::GetMousePosDelta();
                //    w->OnEvent(ue);
                //}
            }
            if (s_mgr->_pre_hover_widget && cur_hover_widget && cur_hover_widget != s_mgr->_pre_hover_widget)
            {
                UI::UIEvent ue;
                ue._type = UIEvent::EType::kMouseExitWindow;
                ue._mouse_position = Input::GetMousePos(e._window);
                ue._mouse_delta = Input::GetMousePosDelta();
                s_mgr->_pre_hover_widget->OnEvent(ue);
            }
            //else
            //{
            //    if (s_mgr->_pre_hover_widget && cur_hover_widget == nullptr)
            //    {
            //        UI::UIEvent ue;
            //        ue._type = UIEvent::EType::kMouseExitWindow;
            //        ue._mouse_position = Input::GetMousePos(e._window);
            //        ue._mouse_delta = Input::GetMousePosDelta();
            //        s_mgr->_pre_hover_widget->OnEvent(ue);
            //    }
            //}
            s_mgr->_pre_hover_widget = cur_hover_widget;
        }
    }// namespace UI

}// namespace Ailu