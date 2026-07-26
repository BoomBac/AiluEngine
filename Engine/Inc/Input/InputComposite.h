/*
 * @author    : BoomBac
 * @created   : 2026.7
 * @brief     : Composite binding – synthesises a value from multiple child bindings
 *
 * Built-in: 2DVector (WASD / arrows), 1DAxis (two buttons)
 */

#pragma once
#ifndef __INPUT_COMPOSITE_H__
#define __INPUT_COMPOSITE_H__

#include "InputTypes.h"
#include "InputValue.h"
#include "InputBinding.h"
#include "Framework/Core/SmartPtr.h"
#include "Framework/Math/Vector.hpp"
#include "Framework/Math/VectorMath.hpp"

namespace Ailu
{
    // ========================================================================
    //  InputCompositeBinding – base
    // ========================================================================
    class InputCompositeBinding
    {
    public:
        virtual ~InputCompositeBinding() = default;

        /// @brief Evaluate the composite by reading all child bindings and combining
        [[nodiscard]] virtual InputValue Evaluate() const = 0;

        /// @brief Return child bindings for resolution & consumption tracking
        [[nodiscard]] virtual Vector<InputBinding *> GetChildBindings() = 0;
        [[nodiscard]] virtual Vector<const InputBinding *> GetChildBindings() const = 0;
        [[nodiscard]] virtual Scope<InputCompositeBinding> Clone() const = 0;
    };

    // ========================================================================
    //  Axis2DCompositeBinding – WASD / Arrow-key style
    // ========================================================================
    class Axis2DCompositeBinding final : public InputCompositeBinding
    {
    public:
        InputValue Evaluate() const override
        {
            Vector2f result = Vector2f::kZero;

            result.y += _up.Evaluate().AsButton() ? 1.0f : 0.0f;
            result.y -= _down.Evaluate().AsButton() ? 1.0f : 0.0f;
            result.x -= _left.Evaluate().AsButton() ? 1.0f : 0.0f;
            result.x += _right.Evaluate().AsButton() ? 1.0f : 0.0f;

            if (_normalize)
            {
                const f32 sqr_len = result.x * result.x + result.y * result.y;
                if (sqr_len > 1.0f)
                    result = Math::Normalize(result);
            }

            InputValue value;
            value._type = EInputValueType::kAxis2D;
            value._value = {result.x, result.y, 0.0f};
            return value;
        }

        [[nodiscard]] Vector<InputBinding *> GetChildBindings() override
        {
            Vector<InputBinding *> children;
            children.reserve(4);
            children.push_back(&_up);
            children.push_back(&_down);
            children.push_back(&_left);
            children.push_back(&_right);
            return children;
        }

        [[nodiscard]] Vector<const InputBinding *> GetChildBindings() const override
        {
            Vector<const InputBinding *> children;
            children.reserve(4);
            children.push_back(&_up);
            children.push_back(&_down);
            children.push_back(&_left);
            children.push_back(&_right);
            return children;
        }

        [[nodiscard]] Scope<InputCompositeBinding> Clone() const override
        {
            auto clone = MakeScope<Axis2DCompositeBinding>();
            clone->_up = _up;
            clone->_down = _down;
            clone->_left = _left;
            clone->_right = _right;
            clone->_normalize = _normalize;
            return clone;
        }

        // --- Public child bindings (assigned during resolution) ---
        InputBinding _up;
        InputBinding _down;
        InputBinding _left;
        InputBinding _right;
        bool _normalize = true;
    };

    // ========================================================================
    //  Axis1DCompositeBinding – two-button axis (e.g. W/S for forward/back)
    // ========================================================================
    class Axis1DCompositeBinding final : public InputCompositeBinding
    {
    public:
        InputValue Evaluate() const override
        {
            f32 result = 0.0f;
            result += _positive.Evaluate().AsButton() ? 1.0f : 0.0f;
            result -= _negative.Evaluate().AsButton() ? 1.0f : 0.0f;

            InputValue value;
            value._type = EInputValueType::kAxis1D;
            value._value.x = result;
            return value;
        }

        [[nodiscard]] Vector<InputBinding *> GetChildBindings() override
        {
            return {&_positive, &_negative};
        }

        [[nodiscard]] Vector<const InputBinding *> GetChildBindings() const override
        {
            return {&_positive, &_negative};
        }

        [[nodiscard]] Scope<InputCompositeBinding> Clone() const override
        {
            auto clone = MakeScope<Axis1DCompositeBinding>();
            clone->_positive = _positive;
            clone->_negative = _negative;
            return clone;
        }

        InputBinding _positive;  // e.g. W → +1
        InputBinding _negative;  // e.g. S → –1
    };

    // ========================================================================
    //  ButtonWithModifierCompositeBinding – requires a modifier to be held
    // ========================================================================
    class ButtonWithModifierCompositeBinding final : public InputCompositeBinding
    {
    public:
        InputValue Evaluate() const override
        {
            if (_modifier.Evaluate().AsButton())
            {
                return _button.Evaluate();
            }
            return InputValue{};
        }

        [[nodiscard]] Vector<InputBinding *> GetChildBindings() override
        {
            return {&_modifier, &_button};
        }

        [[nodiscard]] Vector<const InputBinding *> GetChildBindings() const override
        {
            return {&_modifier, &_button};
        }

        [[nodiscard]] Scope<InputCompositeBinding> Clone() const override
        {
            auto clone = MakeScope<ButtonWithModifierCompositeBinding>();
            clone->_modifier = _modifier;
            clone->_button = _button;
            return clone;
        }

        InputBinding _modifier;  // e.g. LeftShift
        InputBinding _button;    // e.g. F
    };

} // namespace Ailu

#endif // !__INPUT_COMPOSITE_H__
