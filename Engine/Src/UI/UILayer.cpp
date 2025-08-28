#include "UI/UILayer.h"
#include "UI/Widget.h"
#include "Framework/Common/Input.h"
#include "pch.h"


namespace Ailu
{
    namespace UI
    {
        static UI::UIEvent::EType EventToUIEvent(const Ailu::Event& e)
        {
            switch (e.GetEventType())
            {
            case EEventType::kMouseMoved:
                return UI::UIEvent::EType::kMouseMove;
            case EEventType::kMouseButtonPressed:
                return UI::UIEvent::EType::kMouseDown;
            case EEventType::kMouseButtonReleased:
                return UI::UIEvent::EType::kMouseUp;
            case EEventType::kMouseScrolled:
                return UI::UIEvent::EType::kMouseScroll;
            case EEventType::kMouseExitWindow:
                return UI::UIEvent::EType::kMouseExitWindow;
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
            static Vector2f s_mouse_pos = Input::GetMousePos();
            for (auto w : _widgets)
            {
                UI::UIEvent ue;
                ue._type = EventToUIEvent(e);
                ue._mouse_position = Input::GetMousePos();
                ue._mouse_delta = ue._mouse_position - s_mouse_pos;
                w->OnEvent(ue);
                if (ue._is_handled)
                    break;
            }
            s_mouse_pos = Input::GetMousePos();
        }
        void UILayer::OnImguiRender() {}
    }// namespace UI

}// namespace Ailu