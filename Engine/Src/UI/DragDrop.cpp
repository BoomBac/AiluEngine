#include "UI/DragDrop.h"
#include "UI/UIFramework.h"
#include "UI/UIElement.h"
#include "UI/UIRenderer.h"
#include "Framework/Common/Input.h"
#include "Framework/Common/Application.h"

namespace Ailu
{
	namespace UI
    {
        static Scope<DragDropManager> s_DragDropMgr = nullptr;
        DragDropManager &DragDropManager::Get()
        {
            if (!s_DragDropMgr)
                s_DragDropMgr = MakeScope<DragDropManager>();
            return *s_DragDropMgr;
        }
        DragDropManager::DragDropManager()
        {

        }
        void DragDropManager::BeginDrag(const DragPayload &payload, String display_name, Render::Texture *preview_tex)
        {
            _payload = payload;
            _display_name = std::move(_display_name);
            _preview_tex = preview_tex;
        }
        void DragDropManager::EndDrag()
        {
            if (_payload)
            {

            }
            _payload.reset();
            _display_name.clear();
            _preview_tex = nullptr;
        }
        void DragDropManager::Update()
        {
            if (!_payload)
                return;
            Input::IsKeyPressed(EKey::kLBUTTON);
            Input::BlockInput(true);
            auto mp = Input::GetMousePos(Application::FocusedWindow());
            UI::UIRenderer::Get()->DrawText(std::format("{} draging...",_display_name), mp, 9u);
            auto ui_mgr = UI::UIManager::Get();
            UIElement *hover = ui_mgr->_hover_target;
            DropHandler *handle = hover ? hover->GetDropHandler() : nullptr;
            if (handle && handle->_can_drop && handle->_can_drop(*_payload))
                _hover_target = handle;
            else
                _hover_target = nullptr;
            if (_hover_target)
            {
                UI::UIRenderer::Get()->DrawBox(hover->GetArrangeRect().xy, hover->GetArrangeRect().zw, 2.0f, Colors::kYellow, 0.0f);
            }
            if (Input::JustReleased(EKey::kLBUTTON))
            {
                if (_hover_target)
                    _hover_target->_on_drop(*_payload, mp.x, mp.y);
                EndDrag();
                Input::BlockInput(false);
            }
        }
    }// namespace UI
}

