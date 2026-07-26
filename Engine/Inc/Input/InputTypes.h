/*
 * @author    : BoomBac
 * @created   : 2026.7
 * @brief     : Core type definitions for the Input Action System
 */

#pragma once
#ifndef __INPUT_TYPES_H__
#define __INPUT_TYPES_H__

#include "Framework/Core/Types.h"

namespace Ailu
{
    // --- Device types ---
    enum class EInputDeviceType : u8
    {
        kKeyboard,
        kMouse,
        kGamepad,
        kTouch,
        kVirtual
    };

    // --- Input value dimensionality ---
    enum class EInputValueType : u8
    {
        kButton,
        kAxis1D,
        kAxis2D,
        kAxis3D
    };

    // --- Action type (semantic level) ---
    enum class EInputActionType : u8
    {
        kButton,       // Discrete input: Jump, Fire, Submit
        kValue,        // Continuous value: Move, Look, Throttle
        kPassThrough   // No binding competition, passthrough
    };

    // --- Action phase (state machine) ---
    enum class EInputActionPhase : u8
    {
        kDisabled,
        kWaiting,
        kStarted,
        kPerformed,
        kCanceled
    };

    // --- Binding merge strategy ---
    enum class EBindingMergeStrategy : u8
    {
        kMaxMagnitude,       // Default: pick the binding with the largest magnitude
        kAccumulate,         // Sum all binding values, then clamp
        kLastActiveDevice,   // Prefer the most recently used device
        kPassThrough         // Fire individually for each binding
    };

    // --- Control path constants ---
    namespace InputConstants
    {
        inline constexpr u16 kMaxKeyboardKeys = 256u;
        inline constexpr u16 kMaxMouseButtons = 8u;
        inline constexpr u16 kMaxGamepadButtons = 32u;
        inline constexpr u16 kMaxGamepadAxes = 8u;
        inline constexpr u16 kInvalidControlIndex = 0xFFFFu;

        inline constexpr f32 kDefaultActuationThreshold = 0.5f;
        inline constexpr f32 kDefaultDeadZoneMin = 0.125f;
        inline constexpr f32 kDefaultDeadZoneMax = 1.0f;
        inline constexpr f32 kDefaultHoldDuration = 0.5f;
        inline constexpr f32 kDefaultTapMaxDuration = 0.3f;
    } // namespace InputConstants

} // namespace Ailu

#endif // !__INPUT_TYPES_H__
