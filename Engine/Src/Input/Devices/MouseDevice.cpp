/*
 * @author    : BoomBac
 * @created   : 2026.7
 */

#include "Input/Devices/MouseDevice.h"
#include "Framework/Common/Input.h"
#include "Framework/Math/ALMath.hpp"

namespace Ailu
{
    MouseDevice::MouseDevice()
    {
        _device_name = "Mouse";
        _device_id = 0x4D00; // 'M' prefix

        BuildControlDescriptors();
    }

    void MouseDevice::BuildControlDescriptors()
    {
        _control_descs.clear();
        _control_descs.reserve(static_cast<size_t>(EMouseControl::kControlCount));

        // Buttons
        {
            InputControlDesc desc;
            desc._name = "left_button";
            desc._value_type = EInputValueType::kButton;
            desc._index = static_cast<u16>(EMouseControl::kLeftButton);
            _control_descs.push_back(std::move(desc));
        }
        {
            InputControlDesc desc;
            desc._name = "right_button";
            desc._value_type = EInputValueType::kButton;
            desc._index = static_cast<u16>(EMouseControl::kRightButton);
            _control_descs.push_back(std::move(desc));
        }
        {
            InputControlDesc desc;
            desc._name = "middle_button";
            desc._value_type = EInputValueType::kButton;
            desc._index = static_cast<u16>(EMouseControl::kMiddleButton);
            _control_descs.push_back(std::move(desc));
        }
        {
            InputControlDesc desc;
            desc._name = "xbutton_1";
            desc._value_type = EInputValueType::kButton;
            desc._index = static_cast<u16>(EMouseControl::kXButton1);
            _control_descs.push_back(std::move(desc));
        }
        {
            InputControlDesc desc;
            desc._name = "xbutton_2";
            desc._value_type = EInputValueType::kButton;
            desc._index = static_cast<u16>(EMouseControl::kXButton2);
            _control_descs.push_back(std::move(desc));
        }

        // Position (X, Y, 2D)
        {
            InputControlDesc desc;
            desc._name = "position_x";
            desc._value_type = EInputValueType::kAxis1D;
            desc._index = static_cast<u16>(EMouseControl::kPositionX);
            _control_descs.push_back(std::move(desc));
        }
        {
            InputControlDesc desc;
            desc._name = "position_y";
            desc._value_type = EInputValueType::kAxis1D;
            desc._index = static_cast<u16>(EMouseControl::kPositionY);
            _control_descs.push_back(std::move(desc));
        }
        {
            InputControlDesc desc;
            desc._name = "position";
            desc._value_type = EInputValueType::kAxis2D;
            desc._index = static_cast<u16>(EMouseControl::kPosition2D);
            _control_descs.push_back(std::move(desc));
        }

        // Delta (X, Y, 2D)
        {
            InputControlDesc desc;
            desc._name = "delta_x";
            desc._value_type = EInputValueType::kAxis1D;
            desc._index = static_cast<u16>(EMouseControl::kDeltaX);
            _control_descs.push_back(std::move(desc));
        }
        {
            InputControlDesc desc;
            desc._name = "delta_y";
            desc._value_type = EInputValueType::kAxis1D;
            desc._index = static_cast<u16>(EMouseControl::kDeltaY);
            _control_descs.push_back(std::move(desc));
        }
        {
            InputControlDesc desc;
            desc._name = "delta";
            desc._value_type = EInputValueType::kAxis2D;
            desc._index = static_cast<u16>(EMouseControl::kDelta2D);
            _control_descs.push_back(std::move(desc));
        }

        // Wheel
        {
            InputControlDesc desc;
            desc._name = "wheel";
            desc._value_type = EInputValueType::kAxis1D;
            desc._index = static_cast<u16>(EMouseControl::kWheel);
            _control_descs.push_back(std::move(desc));
        }
    }

    void MouseDevice::Poll()
    {
        _previous_buttons = _current_buttons;
        _previous_position = _current_position;

        // Read mouse buttons via the global Input system (which uses EKey for mouse buttons)
        _current_buttons[static_cast<u16>(EMouseControl::kLeftButton)] =
            Input::IsKeyDown(EKey::kLBUTTON);
        _current_buttons[static_cast<u16>(EMouseControl::kRightButton)] =
            Input::IsKeyDown(EKey::kRBUTTON);
        _current_buttons[static_cast<u16>(EMouseControl::kMiddleButton)] =
            Input::IsKeyDown(EKey::kMBUTTON);
        _current_buttons[static_cast<u16>(EMouseControl::kXButton1)] =
            Input::IsKeyDown(EKey::kXBUTTON1);
        _current_buttons[static_cast<u16>(EMouseControl::kXButton2)] =
            Input::IsKeyDown(EKey::kXBUTTON2);

        // Read position and delta from the global Input system
        _current_position = Input::GetMousePos(_window);
        _delta = Input::GetMousePosDelta();
        _wheel = 0.0f; // Wheel is typically event-driven; TBD with event integration
    }

    InputValue MouseDevice::ReadControl(u16 control_index) const
    {
        InputValue value;

        switch (static_cast<EMouseControl>(control_index))
        {
        case EMouseControl::kLeftButton:
        case EMouseControl::kRightButton:
        case EMouseControl::kMiddleButton:
        case EMouseControl::kXButton1:
        case EMouseControl::kXButton2:
            value._type = EInputValueType::kButton;
            value._value.x = _current_buttons[control_index] ? 1.0f : 0.0f;
            break;

        case EMouseControl::kPositionX:
            value._type = EInputValueType::kAxis1D;
            value._value.x = _current_position.x;
            break;

        case EMouseControl::kPositionY:
            value._type = EInputValueType::kAxis1D;
            value._value.x = _current_position.y;
            break;

        case EMouseControl::kPosition2D:
            value._type = EInputValueType::kAxis2D;
            value._value = {_current_position.x, _current_position.y, 0.0f};
            break;

        case EMouseControl::kDeltaX:
            value._type = EInputValueType::kAxis1D;
            value._value.x = _delta.x;
            break;

        case EMouseControl::kDeltaY:
            value._type = EInputValueType::kAxis1D;
            value._value.x = _delta.y;
            break;

        case EMouseControl::kDelta2D:
            value._type = EInputValueType::kAxis2D;
            value._value = {_delta.x, _delta.y, 0.0f};
            break;

        case EMouseControl::kWheel:
            value._type = EInputValueType::kAxis1D;
            value._value.x = _wheel;
            break;

        default:
            break;
        }

        return value;
    }

} // namespace Ailu
