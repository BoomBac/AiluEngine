#pragma once
#pragma warning(push)
#pragma warning(disable : 4251)
#ifndef __INPUT_H__
#define __INPUT_H__
#include "GlobalMarco.h"
#include "Framework/Math/ALMath.hpp"
#include "KeyCode.h"
#include <tuple>

namespace Ailu
{
    class Window;
    class InputPlatform
    {
    public:
        friend class Input;
        virtual ~InputPlatform() = default;
    protected:
        virtual bool IsKeyPressed(EKey keycode) = 0;
        virtual Vector2f GetMousePos(Window *w) = 0;
        virtual Vector2f GetGlobalMousePos() = 0;
        virtual WString GetCharFromKeyCode(EKey key) = 0;
    };

    class AILU_API Input
    {
        friend class Application;
    public:
        inline const static u32 kMaxKeyNum = 512u;
        inline static bool IsKeyPressed(EKey keycode) 
        {
            bool is_press = sp_instance->IsKeyPressed(keycode);
            s_cur_key_state[keycode] = is_press;
            return is_press;
        }
        /// <summary>
        /// Determines if a key was just pressed in the current frame.
        /// </summary>
        /// <param name="keycode">The key code representing the key to check.</param>
        /// <returns>True if the key was pressed in the current frame and was not pressed in the previous frame; otherwise, false.</returns>
        inline static bool IsKeyJustPressed(EKey keycode)
        {
            return s_cur_key_state[keycode] && !s_pre_key_state[keycode];
        }
        /// <summary>
        /// Checks if a key was just released in the current frame.
        /// </summary>
        /// <param name="code">The key code to check.</param>
        /// <returns>True if the key was released in the current frame; otherwise, false.</returns>
        inline static bool JustReleased(EKey code)
        {
            return !s_cur_key_state[code] && s_pre_key_state[code];
        }
        /// <summary>
        /// 获取鼠标位置，起始点为客户区左上角,每帧开始时缓存的值
        /// </summary>
        /// <param name="window_handle">hwnd</param>
        /// <returns></returns>
        inline static Vector2f GetMousePos(Window *w = nullptr)
        {
            return s_window_mouse_pos_map[w];
        }
        /// <summary>
        /// Retrieves the current mouse position with high accuracy, optionally for a specific window.
        /// </summary>
        /// <param name="w">Pointer to the Window for which to get the mouse position. If nullptr, uses the default window.</param>
        /// <returns>A Vector2f representing the accurate mouse position.</returns>
        inline static Vector2f GetMousePosAccurate(Window *w = nullptr)
        {
            return sp_instance->GetMousePos(w);
        }
        /// <summary>
        /// 获取鼠标位置，起始点为屏幕左上角
        /// </summary>
        /// <param name="window_handle"></param>
        /// <returns></returns>
        inline static Vector2f GetGlobalMousePos()
        {
            return s_cur_global_mouse_pos;
        }
        inline static Vector2f GetGlobalMousePosAccurate()
        {
            return sp_instance->GetGlobalMousePos();
        }
        inline static bool IsInputBlock()
        {
            return  s_block_input;
        }
        inline static void BlockInput(bool block)
        {
            s_block_input = block;
        }
        inline static Vector2f GetMousePosDelta()
        {
            return s_mouse_pos_delta;
        }
        inline static WString GetCharFromKeyCode(EKey key)
        {
            return sp_instance->GetCharFromKeyCode(key);
        }
        inline static void SetupPlatformInput(Scope<InputPlatform> input)
        {
            sp_instance = std::move(input);
        }
    protected:
        inline static Scope<InputPlatform> sp_instance;
        inline static bool s_block_input = false;
        inline static Vector2f s_mouse_pos_delta = Vector2f::kZero;
        inline static Vector2f s_cur_global_mouse_pos = Vector2f::kZero;
        inline static HashMap<Window *, Vector2f> s_window_mouse_pos_map;

        inline static std::bitset<kMaxKeyNum> s_cur_key_state;
        inline static std::bitset<kMaxKeyNum> s_pre_key_state;
    private:
        static void BeginFrame()
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
        };
    };
}// namespace Ailu
#pragma warning(pop)
#endif // !INPUT_H__
