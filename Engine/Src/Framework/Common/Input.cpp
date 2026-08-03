#include "Framework/Common/Input.h"
#include "Framework/Common/Application.h"

namespace Ailu
{
    InputRouteState &InputRouteState::Get()
    {
        static InputRouteState s_route_state;
        return s_route_state;
    }

    void InputRouteState::BeginFrame()
    {
        _consumed_channels = 0u;
    }

    void InputRouteState::Consume(InputChannel channel)
    {
        _consumed_channels |= static_cast<u8>(channel);
    }

    bool InputRouteState::IsConsumed(InputChannel channel) const
    {
        return (_consumed_channels & static_cast<u8>(channel)) != 0u;
    }

    void Input::NotifyKeyPressed(EKey keycode)
    {
        const auto key_index = static_cast<u32>(keycode);
        if (key_index >= kMaxKeyNum)
            return;

        std::lock_guard lock(s_input_state_mutex);
        if (!s_pending_key_state.test(key_index))
            s_pending_pressed_state.set(key_index);
        s_pending_key_state.set(key_index);
    }

    void Input::NotifyKeyReleased(EKey keycode)
    {
        const auto key_index = static_cast<u32>(keycode);
        if (key_index >= kMaxKeyNum)
            return;

        std::lock_guard lock(s_input_state_mutex);
        if (s_pending_key_state.test(key_index))
            s_pending_released_state.set(key_index);
        s_pending_key_state.reset(key_index);
    }

    void Input::NotifyMouseMove(Window *w, const Vector2f &local_pos)
    {
        std::lock_guard lock(s_mouse_state_mutex);
        s_pending_window_mouse_pos_map[w] = local_pos;
        if (w == &Application::Get().GetWindow())
            s_pending_window_mouse_pos_map[nullptr] = local_pos;
        s_pending_global_mouse_pos = sp_instance != nullptr ? sp_instance->GetGlobalMousePos() : Vector2f::kZero;
        s_has_pending_mouse_state = true;
    }

    void Input::NotifyFocusLost()
    {
        std::lock_guard lock(s_input_state_mutex);
        s_pending_key_state.reset();
        s_pending_pressed_state.reset();
        s_pending_released_state.reset();
    }

    void Input::NotifyWindowClosed(Window *w)
    {
        std::lock_guard lock(s_mouse_state_mutex);
        s_pending_window_mouse_pos_map.erase(w);
        s_window_mouse_pos_map.erase(w);
        if (w == &Application::Get().GetWindow())
        {
            s_pending_window_mouse_pos_map.erase(nullptr);
            s_window_mouse_pos_map.erase(nullptr);
        }
    }

    void Input::BeginFrame()
    {
        InputRouteState::Get().BeginFrame();
        {
            std::lock_guard lock(s_input_state_mutex);
            s_pre_key_state = s_cur_key_state;
            s_cur_key_state = s_pending_key_state;
            s_pressed_key_state = s_pending_pressed_state;
            s_released_key_state = s_pending_released_state;
            s_pending_pressed_state.reset();
            s_pending_released_state.reset();
        }

        {
            std::lock_guard lock(s_mouse_state_mutex);
            if (!s_has_pending_mouse_state && sp_instance != nullptr)
            {
                s_pending_global_mouse_pos = sp_instance->GetGlobalMousePos();
                s_pending_window_mouse_pos_map[nullptr] = sp_instance->GetMousePos(&Application::Get().GetWindow());
                s_has_pending_mouse_state = true;
            }

            const Vector2f pre_mouse_pos = s_cur_global_mouse_pos;
            s_cur_global_mouse_pos = s_pending_global_mouse_pos;
            s_mouse_pos_delta = s_cur_global_mouse_pos - pre_mouse_pos;
            s_window_mouse_pos_map = s_pending_window_mouse_pos_map;
        }
    }
}
