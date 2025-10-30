#include "Framework/Common/Input.h"
#include "Framework/Common/Application.h"

namespace Ailu
{
    void Input::BeginFrame()
    {
        s_pre_key_state = s_cur_key_state;
        static Vector2f s_pre_mouse_pos = sp_instance->GetGlobalMousePos();
        s_cur_global_mouse_pos = sp_instance->GetGlobalMousePos();
        s_mouse_pos_delta = s_cur_global_mouse_pos - s_pre_mouse_pos;
        s_pre_mouse_pos = s_cur_global_mouse_pos;
        for (auto &it: s_window_mouse_pos_map)
        {
            it.second = sp_instance->GetMousePos(it.first);
        }
        s_window_mouse_pos_map[nullptr] = sp_instance->GetMousePos(&Application::Get().GetWindow());
    };
}