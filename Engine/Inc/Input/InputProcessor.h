/*
 * @author    : BoomBac
 * @created   : 2026.7
 * @brief     : InputProcessor base and built-in processors
 *
 * Processors transform InputValue in a stateless (or light-state) manner.
 * They do NOT handle timing, phases, or context routing.
 *
 * Built-in: Scale, Invert, DeadZone, Normalize, Clamp, SensitivityCurve
 */

#pragma once
#ifndef __INPUT_PROCESSOR_H__
#define __INPUT_PROCESSOR_H__

#include "InputTypes.h"
#include "InputValue.h"
#include "Framework/Core/SmartPtr.h"
#include "Framework/Math/Vector.hpp"
#include "Framework/Math/VectorMath.hpp"
#include <algorithm>

namespace Ailu
{
    // --- Base ---
    class InputProcessor
    {
    public:
        virtual ~InputProcessor() = default;
        virtual InputValue Process(const InputValue &value) const = 0;
        virtual Scope<InputProcessor> Clone() const = 0;
    };

    // ========================================================================
    //  Built-in Processors
    // ========================================================================

    /// @brief Multiply each component by a scale vector
    class ScaleProcessor final : public InputProcessor
    {
    public:
        explicit ScaleProcessor(const Vector3f &scale = Vector3f::kOne)
            : _scale(scale) {}

        InputValue Process(const InputValue &value) const override
        {
            InputValue result = value;
            result._value.x *= _scale.x;
            result._value.y *= _scale.y;
            result._value.z *= _scale.z;
            return result;
        }

        Scope<InputProcessor> Clone() const override { return MakeScope<ScaleProcessor>(_scale); }

        void SetScale(const Vector3f &scale) { _scale = scale; }
        const Vector3f &GetScale() const { return _scale; }

    private:
        Vector3f _scale = Vector3f::kOne;
    };

    /// @brief Invert (negate) axes; each component can be independently inverted
    class InvertProcessor final : public InputProcessor
    {
    public:
        explicit InvertProcessor(bool invert_x = false, bool invert_y = false, bool invert_z = false)
            : _invert_x(invert_x), _invert_y(invert_y), _invert_z(invert_z) {}

        InputValue Process(const InputValue &value) const override
        {
            InputValue result = value;
            if (_invert_x) result._value.x = -result._value.x;
            if (_invert_y) result._value.y = -result._value.y;
            if (_invert_z) result._value.z = -result._value.z;
            return result;
        }

        Scope<InputProcessor> Clone() const override { return MakeScope<InvertProcessor>(_invert_x, _invert_y, _invert_z); }

        bool GetInvertX() const { return _invert_x; }
        bool GetInvertY() const { return _invert_y; }
        bool GetInvertZ() const { return _invert_z; }

    private:
        bool _invert_x = false;
        bool _invert_y = false;
        bool _invert_z = false;
    };

    /// @brief 2D stick dead-zone with optional normalisation
    class StickDeadZoneProcessor final : public InputProcessor
    {
    public:
        explicit StickDeadZoneProcessor(f32 min_dead_zone = InputConstants::kDefaultDeadZoneMin,
                                        f32 max_dead_zone = InputConstants::kDefaultDeadZoneMax)
            : _min_dead_zone(min_dead_zone), _max_dead_zone(max_dead_zone) {}

        InputValue Process(const InputValue &value) const override
        {
            const Vector2f axis = value.AsAxis2D();
            const f32 length = Math::Magnitude(axis);

            InputValue result = value;
            if (length <= _min_dead_zone)
            {
                result._value = Vector3f::kZero;
                return result;
            }

            f32 normalized_length = (length - _min_dead_zone) / (_max_dead_zone - _min_dead_zone);
            normalized_length = std::clamp(normalized_length, 0.0f, 1.0f);

            const Vector2f normalized_axis = Math::Normalize(axis) * normalized_length;
            result._value = {normalized_axis.x, normalized_axis.y, 0.0f};
            return result;
        }

        Scope<InputProcessor> Clone() const override { return MakeScope<StickDeadZoneProcessor>(_min_dead_zone, _max_dead_zone); }

        f32 GetMinDeadZone() const { return _min_dead_zone; }
        f32 GetMaxDeadZone() const { return _max_dead_zone; }

    private:
        f32 _min_dead_zone = InputConstants::kDefaultDeadZoneMin;
        f32 _max_dead_zone = InputConstants::kDefaultDeadZoneMax;
    };

    /// @brief 1D axis dead-zone
    class AxisDeadZoneProcessor final : public InputProcessor
    {
    public:
        explicit AxisDeadZoneProcessor(f32 dead_zone = 0.1f)
            : _dead_zone(dead_zone) {}

        InputValue Process(const InputValue &value) const override
        {
            InputValue result = value;
            if (std::fabs(result._value.x) <= _dead_zone)
                result._value.x = 0.0f;
            if (std::fabs(result._value.y) <= _dead_zone)
                result._value.y = 0.0f;
            if (std::fabs(result._value.z) <= _dead_zone)
                result._value.z = 0.0f;
            return result;
        }

        Scope<InputProcessor> Clone() const override { return MakeScope<AxisDeadZoneProcessor>(_dead_zone); }

        f32 GetDeadZone() const { return _dead_zone; }

    private:
        f32 _dead_zone = 0.1f;
    };

    /// @brief Normalize the input value to unit length (for Axis2D / Axis3D)
    class NormalizeProcessor final : public InputProcessor
    {
    public:
        InputValue Process(const InputValue &value) const override
        {
            InputValue result = value;
            const f32 mag = value.Magnitude();
            if (mag > Math::kFloatEpsilon)
            {
                result._value.x /= mag;
                result._value.y /= mag;
                result._value.z /= mag;
            }
            return result;
        }

        Scope<InputProcessor> Clone() const override { return MakeScope<NormalizeProcessor>(); }
    };

    /// @brief Clamp each component to the given range
    class ClampProcessor final : public InputProcessor
    {
    public:
        explicit ClampProcessor(f32 min_val = -1.0f, f32 max_val = 1.0f)
            : _min(min_val), _max(max_val) {}

        InputValue Process(const InputValue &value) const override
        {
            InputValue result = value;
            result._value.x = std::clamp(result._value.x, _min, _max);
            result._value.y = std::clamp(result._value.y, _min, _max);
            result._value.z = std::clamp(result._value.z, _min, _max);
            return result;
        }

        Scope<InputProcessor> Clone() const override { return MakeScope<ClampProcessor>(_min, _max); }

        f32 GetMin() const { return _min; }
        f32 GetMax() const { return _max; }

    private:
        f32 _min = -1.0f;
        f32 _max = 1.0f;
    };

    /// @brief Convert an axis value to a button using a threshold
    class AxisToButtonProcessor final : public InputProcessor
    {
    public:
        explicit AxisToButtonProcessor(f32 threshold = 0.5f)
            : _threshold(threshold) {}

        InputValue Process(const InputValue &value) const override
        {
            InputValue result = value;
            result._type = EInputValueType::kButton;
            result._value.x = (std::fabs(value._value.x) >= _threshold) ? 1.0f : 0.0f;
            result._value.y = 0.0f;
            result._value.z = 0.0f;
            return result;
        }

        Scope<InputProcessor> Clone() const override { return MakeScope<AxisToButtonProcessor>(_threshold); }

        f32 GetThreshold() const { return _threshold; }

    private:
        f32 _threshold = 0.5f;
    };

} // namespace Ailu

#endif // !__INPUT_PROCESSOR_H__
