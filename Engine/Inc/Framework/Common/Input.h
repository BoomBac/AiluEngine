#pragma once
#pragma warning(push)
#pragma warning(disable : 4251)
#ifndef __INPUT_H__
#define __INPUT_H__
#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/String.h"
#include "Framework/Core/Containers/Map.h"
#include "Framework/Math/ALMath.hpp"
#include "KeyCode.h"
#include <bitset>
#include <mutex>
#include <tuple>

namespace Ailu
{
    enum class InputChannel : u8
    {
        kMouse = 1u << 0u,
        kKeyboard = 1u << 1u,
        kText = 1u << 2u,
        kGamepad = 1u << 3u
    };

    enum class EInputOwner : u8
    {
        kNone,
        kImGui,
        kEngine
    };

    class AILU_API InputRouteState
    {
    public:
        static InputRouteState &Get();
        void BeginFrame();
        void Consume(InputChannel channel);
        bool IsConsumed(InputChannel channel) const;
        void SetOwner(InputChannel channel, EInputOwner owner);
        void ClearOwner(InputChannel channel, EInputOwner owner = EInputOwner::kNone);
        EInputOwner GetOwner(InputChannel channel) const;
        bool IsOwnedBy(InputChannel channel, EInputOwner owner) const;
        void CaptureMouse(EInputOwner owner);
        void ReleaseMouseCapture(EInputOwner owner = EInputOwner::kNone);
        bool IsMouseCaptured() const;
        bool IsMouseCapturedBy(EInputOwner owner) const;

    private:
        EInputOwner &_GetOwner(InputChannel channel);
        const EInputOwner &_GetOwner(InputChannel channel) const;

        u8 _consumed_channels = 0u;
        EInputOwner _mouse_owner = EInputOwner::kNone;
        EInputOwner _keyboard_owner = EInputOwner::kNone;
        EInputOwner _text_owner = EInputOwner::kNone;
        EInputOwner _gamepad_owner = EInputOwner::kNone;
        EInputOwner _mouse_capture_owner = EInputOwner::kNone;
    };

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
        /// <summary>
        /// Snapshot state for the current frame. Use this for continuous input.
        /// </summary>
        inline static bool IsKeyDown(EKey keycode)
        {
            return s_cur_key_state[keycode];
        }
        /// <summary>
        /// Immediate platform query. Use only when frame snapshot precision is not enough.
        /// </summary>
        inline static bool IsKeyDownAccurate(EKey keycode)
        {
            return sp_instance != nullptr && sp_instance->IsKeyPressed(keycode);
        }
        /// <summary>
        /// Legacy alias. Prefer IsKeyDown for continuous state.
        /// </summary>
        inline static bool IsKeyPressed(EKey keycode) 
        {
            return IsKeyDown(keycode);
        }
        /// <summary>
        /// Determines if a key was pressed in the current frame.
        /// </summary>
        /// <param name="keycode">The key code representing the key to check.</param>
        /// <returns>True if the key was pressed in the current frame and was not pressed in the previous frame; otherwise, false.</returns>
        inline static bool IsKeyJustPressed(EKey keycode)
        {
            return s_pressed_key_state[keycode];
        }
        /// <summary>
        /// Determines if a key was released in the current frame.
        /// </summary>
        /// <param name="code">The key code representing the key to check.</param>
        /// <returns>True if the key was released in the current frame and was pressed in the previous frame; otherwise, false.</returns>
        inline static bool IsKeyJustReleased(EKey code)
        {
            return s_released_key_state[code];
        }
        /// <summary>
        /// 获取鼠标位置，起始点为客户区左上角,每帧开始时缓存的值
        /// </summary>
        /// <param name="window_handle">hwnd</param>
        /// <returns></returns>
        inline static Vector2f GetMousePos(Window *w = nullptr)
        {
            std::lock_guard lock(s_mouse_state_mutex);
            auto it = s_window_mouse_pos_map.find(w);
            if (it != s_window_mouse_pos_map.end())
                return it->second;
            if (w != nullptr)
            {
                auto default_it = s_window_mouse_pos_map.find(nullptr);
                if (default_it != s_window_mouse_pos_map.end())
                    return default_it->second;
            }
            return Vector2f::kZero;
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
        static void NotifyKeyPressed(EKey keycode);
        static void NotifyKeyReleased(EKey keycode);
        static void NotifyMouseMove(Window *w, const Vector2f &local_pos);
        static void NotifyFocusLost();
        static void NotifyWindowClosed(Window *w);
        inline static Scope<InputPlatform> sp_instance;
        inline static std::mutex s_input_state_mutex;
        inline static std::mutex s_mouse_state_mutex;
        inline static Vector2f s_mouse_pos_delta = Vector2f::kZero;
        inline static Vector2f s_cur_global_mouse_pos = Vector2f::kZero;
        inline static Vector2f s_pending_global_mouse_pos = Vector2f::kZero;
        inline static bool s_has_pending_mouse_state = false;
        inline static HashMap<Window *, Vector2f> s_window_mouse_pos_map;
        inline static HashMap<Window *, Vector2f> s_pending_window_mouse_pos_map;

        inline static std::bitset<kMaxKeyNum> s_cur_key_state;
        inline static std::bitset<kMaxKeyNum> s_pre_key_state;
        inline static std::bitset<kMaxKeyNum> s_pending_key_state;
        inline static std::bitset<kMaxKeyNum> s_pressed_key_state;
        inline static std::bitset<kMaxKeyNum> s_released_key_state;
        inline static std::bitset<kMaxKeyNum> s_pending_pressed_state;
        inline static std::bitset<kMaxKeyNum> s_pending_released_state;
    private:
        static void BeginFrame();
    };
}// namespace Ailu
#pragma warning(pop)
#endif // !INPUT_H__
