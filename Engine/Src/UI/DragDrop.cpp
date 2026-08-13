#include "UI/DragDrop.h"
#include "UI/UIFramework.h"
#include "UI/UIElement.h"
#include "UI/UIRenderer.h"
#include "UI/Widget.h"
#include "Framework/Common/Input.h"
#include "Framework/Common/Application.h"

namespace Ailu
{
    namespace UI
    {
        static Scope<DragDropManager> s_DragDropMgr = nullptr;
        static Vector2f s_start_mouse_pos;
        namespace
        {
            UIElement *FindDragHoverTarget(Vector2f mouse_pos)
            {
                auto *ui_mgr = UI::UIManager::Get();
                auto *focused_window = Application::FocusedWindow();
                for (auto it = ui_mgr->_widgets.rbegin(); it != ui_mgr->_widgets.rend(); ++it)
                {
                    Widget *widget = it->get();
                    if (widget == nullptr || widget->_visibility != EVisibility::kVisible || !widget->_is_receive_event)
                        continue;
                    if (focused_window != nullptr && widget->Parent() != focused_window)
                        continue;
                    if (!widget->IsHover(mouse_pos) || widget->Root() == nullptr)
                        continue;
                    if (auto *target = widget->Root()->HitTest(mouse_pos); target != nullptr)
                        return target;
                }
                return ui_mgr->_hover_target;
            }
        }

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
            _display_name = std::move(display_name);
            _preview_tex = preview_tex;
            s_start_mouse_pos = Input::GetGlobalMousePos();
        }
        void DragDropManager::EndDrag()
        {
            if (_payload)
            {

            }
            _payload.reset();
            _display_name.clear();
            _preview_tex = nullptr;
            _is_drag_start = false;
        }
        void DragDropManager::Update()
        {
            if (!_payload)
                return;
            //Clean up if the left button was released before drag activated
            if (!Input::IsKeyDown(EKey::kLBUTTON) && !_is_drag_start)
            {
                EndDrag();
                return;
            }
            if (!_is_drag_start && Magnitude(Input::GetGlobalMousePos() - s_start_mouse_pos) > 5.0f)
                _is_drag_start = true;
            if (!_is_drag_start)
                return;
            auto mp = Input::GetMousePos(Application::FocusedWindow());
            UI::UIRenderer::Get()->DrawText(std::format("{} draging...",_display_name), mp, 9u);
            UIElement *hover = FindDragHoverTarget(mp);
            DropHandler *handle = nullptr;
            UIElement *drop_target = nullptr;
            // Walk up parent chain to find a DropHandler (child elements like Text
            // inside a row don't have one, but the row/Border does)
            for (UIElement *node = hover; node != nullptr; node = node->GetParent())
            {
                DropHandler *dh = node->GetDropHandler();
                if (dh && dh->_can_drop && dh->_can_drop(*_payload))
                {
                    handle = dh;
                    drop_target = node;
                    break;
                }
            }
            _hover_target = handle;
            if (_hover_target && _payload->_type != EDragType::kUIWidget)
            {
                UI::UIRenderer::Get()->DrawBox(drop_target->GetArrangeRect().xy, drop_target->GetArrangeRect().zw, 2.0f, Colors::kYellow, 0.0f);
            }
            if (Input::IsKeyJustReleased(EKey::kLBUTTON))
            {
                if (_hover_target)
                    _hover_target->_on_drop(*_payload, mp.x, mp.y);
                EndDrag();
            }
        }
    }// namespace UI
}

