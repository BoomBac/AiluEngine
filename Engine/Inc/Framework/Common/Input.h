#pragma once
#ifndef __INPUT_H__
#define __INPUT_H__
#include "GlobalMarco.h"

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
        inline static bool IsMouseButtonPressed(uint8_t button) { return sp_instance->IsMouseButtonPressedImpl(button); }
        inline static float GetMouseX() 
        { 
            return sp_instance->GetMouseYImpl();
        };
        inline static float GetMouseY() 
        { 
            return sp_instance->GetMouseYImpl();
        };
    protected:
        virtual bool IsKeyPressedImpl(int keycode) = 0;

        virtual bool IsMouseButtonPressedImpl(uint8_t button) = 0;
        virtual float GetMouseXImpl() = 0;
        virtual float GetMouseYImpl() = 0;
    protected:
        inline static Input* sp_instance;
    };
}

#endif // !INPUT_H__
