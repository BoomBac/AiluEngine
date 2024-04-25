#pragma once
#ifndef __INPUT_H__
#define __INPUT_H__
#include "GlobalMarco.h"
#include "Framework/Math/ALMath.hpp"
#include "KeyCode.h"
#include <tuple>

namespace Ailu
{
    class AILU_API Input
    {
    public:
        inline static bool IsKeyPressed(int keycode) { return sp_instance->IsKeyPressedImpl(keycode); }
        /// <summary>
        /// 
        /// </summary>
        /// <param name="button">: 0 mid btn;1 left btn;2 right btn</param>
        /// <returns></returns>
        inline static bool IsMouseButtonPressed(u8 button) { return sp_instance->IsMouseButtonPressedImpl(button); }
        inline static float GetMouseX() 
        { 
            return sp_instance->GetMouseYImpl();
        };
        inline static float GetMouseY() 
        { 
            return sp_instance->GetMouseYImpl();
        };
        inline static Vector2f GetMousePos()
        {
            return sp_instance->GetMousePosImpl();
        }
        inline static bool s_block_input = false;
    protected:
        virtual bool IsKeyPressedImpl(int keycode) = 0;

        virtual bool IsMouseButtonPressedImpl(u8 button) = 0;
        virtual float GetMouseXImpl() = 0;
        virtual float GetMouseYImpl() = 0;
        virtual Vector2f GetMousePosImpl() = 0;
    protected:
        inline static Input* sp_instance;
    };
}

#endif // !INPUT_H__
