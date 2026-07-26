/*
 * @author    : BoomBac
 * @created   : 2026.7
 * @brief     : Runtime input value – unified representation for Button, Axis1D, Axis2D, Axis3D
 */

#pragma once
#ifndef __INPUT_VALUE_H__
#define __INPUT_VALUE_H__

#include "InputTypes.h"
#include "Framework/Math/ALMath.hpp"
#include "Framework/Math/Vector.hpp"
#include "Framework/Math/VectorMath.hpp"
#include <cmath>

namespace Ailu
{
    struct InputValue
    {
        EInputValueType _type = EInputValueType::kButton;
        Vector3f _value = Vector3f::kZero;

        /// @brief Interpret as a boolean button with a configurable threshold
        [[nodiscard]] bool AsButton(f32 threshold = InputConstants::kDefaultActuationThreshold) const
        {
            return _value.x >= threshold;
        }

        /// @brief Interpret as a 1D axis value
        [[nodiscard]] f32 AsAxis1D() const
        {
            return _value.x;
        }

        /// @brief Interpret as a 2D axis value
        [[nodiscard]] Vector2f AsAxis2D() const
        {
            return {_value.x, _value.y};
        }

        /// @brief Interpret as a 3D axis value
        [[nodiscard]] Vector3f AsAxis3D() const
        {
            return _value;
        }

        /// @brief Magnitude of the input value (for comparing binding strength)
        [[nodiscard]] f32 Magnitude() const
        {
            switch (_type)
            {
            case EInputValueType::kButton:
            case EInputValueType::kAxis1D:
                return std::fabs(_value.x);
            case EInputValueType::kAxis2D:
                return Math::Magnitude(Vector2f(_value.x, _value.y));
            case EInputValueType::kAxis3D:
                return Math::Magnitude(_value);
            default:
                return 0.0f;
            }
        }

        /// @brief True if this value exceeds the given threshold in any dimension
        [[nodiscard]] bool IsActuated(f32 threshold = InputConstants::kDefaultActuationThreshold) const
        {
            return Magnitude() >= threshold;
        }

        /// @brief Convenience: create a button value
        static InputValue MakeButton(bool pressed)
        {
            InputValue v;
            v._type = EInputValueType::kButton;
            v._value.x = pressed ? 1.0f : 0.0f;
            return v;
        }

        /// @brief Convenience: create a 1D axis value
        static InputValue MakeAxis1D(f32 value)
        {
            InputValue v;
            v._type = EInputValueType::kAxis1D;
            v._value.x = value;
            return v;
        }

        /// @brief Convenience: create a 2D axis value
        static InputValue MakeAxis2D(const Vector2f &value)
        {
            InputValue v;
            v._type = EInputValueType::kAxis2D;
            v._value = {value.x, value.y, 0.0f};
            return v;
        }

        /// @brief Convenience: create a 3D axis value
        static InputValue MakeAxis3D(const Vector3f &value)
        {
            InputValue v;
            v._type = EInputValueType::kAxis3D;
            v._value = value;
            return v;
        }
    };

} // namespace Ailu

#endif // !__INPUT_VALUE_H__
