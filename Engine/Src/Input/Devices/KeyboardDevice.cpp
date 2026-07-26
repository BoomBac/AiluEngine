/*
 * @author    : BoomBac
 * @created   : 2026.7
 */

#include "Input/Devices/KeyboardDevice.h"
#include "Framework/Common/Input.h"

namespace Ailu
{
    KeyboardDevice::KeyboardDevice()
    {
        _device_name = "Keyboard";
        _device_id = 0x4B00; // 'K' 'B' prefix

        BuildControlDescriptors();
    }

    void KeyboardDevice::BuildControlDescriptors()
    {
        _control_descs.clear();
        _control_descs.reserve(InputConstants::kMaxKeyboardKeys);

        // Initialize key-to-index mapping
        _key_to_control_index.fill(InputConstants::kInvalidControlIndex);

        // Build control descriptors for all valid EKey values
        // We map Windows VK codes (0x01–0xFF) to control indices
        for (u16 i = 0; i < InputConstants::kMaxKeyboardKeys; ++i)
        {
            InputControlDesc desc;
            desc._index = i;

            // Generate a human-readable name from the EKey value
            EKey key_code = static_cast<EKey>(i);

            // Only register common valid keys
            // Full VK range is 0x01–0xFF but we skip gaps
            if (i >= 0x01 && i <= 0xFE)
            {
                desc._value_type = EInputValueType::kButton;

                // Map well-known keys to readable names
                switch (key_code)
                {
                case EKey::kSPACE:    desc._name = "space"; break;
                case EKey::kRETURN:   desc._name = "enter"; break;
                case EKey::kESCAPE:   desc._name = "escape"; break;
                case EKey::kTAB:      desc._name = "tab"; break;
                case EKey::kBACK:     desc._name = "backspace"; break;
                case EKey::kDELETE:   desc._name = "delete"; break;
                case EKey::kINSERT:   desc._name = "insert"; break;
                case EKey::kHOME:     desc._name = "home"; break;
                case EKey::kEND:      desc._name = "end"; break;
                case EKey::kPRIOR:    desc._name = "page_up"; break;
                case EKey::kNEXT:     desc._name = "page_down"; break;
                case EKey::kUP:       desc._name = "up"; break;
                case EKey::kDOWN:     desc._name = "down"; break;
                case EKey::kLEFT:     desc._name = "left"; break;
                case EKey::kRIGHT:    desc._name = "right"; break;
                case EKey::kLSHIFT:   desc._name = "left_shift"; break;
                case EKey::kRSHIFT:   desc._name = "right_shift"; break;
                case EKey::kLCONTROL: desc._name = "left_control"; break;
                case EKey::kRCONTROL: desc._name = "right_control"; break;
                case EKey::kSHIFT:    desc._name = "shift"; break;
                case EKey::kCONTROL:  desc._name = "control"; break;
                case EKey::kALT:      desc._name = "alt"; break;
                case EKey::kCAPITAL:  desc._name = "caps_lock"; break;
                case EKey::kNUMLOCK:  desc._name = "num_lock"; break;
                case EKey::kSCROLL:   desc._name = "scroll_lock"; break;
                case EKey::kF1:       desc._name = "f1"; break;
                case EKey::kF2:       desc._name = "f2"; break;
                case EKey::kF3:       desc._name = "f3"; break;
                case EKey::kF4:       desc._name = "f4"; break;
                case EKey::kF5:       desc._name = "f5"; break;
                case EKey::kF6:       desc._name = "f6"; break;
                case EKey::kF7:       desc._name = "f7"; break;
                case EKey::kF8:       desc._name = "f8"; break;
                case EKey::kF9:       desc._name = "f9"; break;
                case EKey::kF10:      desc._name = "f10"; break;
                case EKey::kF11:      desc._name = "f11"; break;
                case EKey::kF12:      desc._name = "f12"; break;
                case EKey::kNUMPAD0:  desc._name = "numpad_0"; break;
                case EKey::kNUMPAD1:  desc._name = "numpad_1"; break;
                case EKey::kNUMPAD2:  desc._name = "numpad_2"; break;
                case EKey::kNUMPAD3:  desc._name = "numpad_3"; break;
                case EKey::kNUMPAD4:  desc._name = "numpad_4"; break;
                case EKey::kNUMPAD5:  desc._name = "numpad_5"; break;
                case EKey::kNUMPAD6:  desc._name = "numpad_6"; break;
                case EKey::kNUMPAD7:  desc._name = "numpad_7"; break;
                case EKey::kNUMPAD8:  desc._name = "numpad_8"; break;
                case EKey::kNUMPAD9:  desc._name = "numpad_9"; break;
                case EKey::kMULTIPLY:  desc._name = "numpad_multiply"; break;
                case EKey::kADD:       desc._name = "numpad_add"; break;
                case EKey::kSUBTRACT:  desc._name = "numpad_subtract"; break;
                case EKey::kDECIMAL:   desc._name = "numpad_decimal"; break;
                case EKey::kDIVIDE:    desc._name = "numpad_divide"; break;
                default:
                    // Alphabet letters (0x41–0x5A)
                    if (i >= 0x41 && i <= 0x5A)
                    {
                        desc._name = String(1, static_cast<char>('a' + (i - 0x41)));
                    }
                    // Digits (0x30–0x39)
                    else if (i >= 0x30 && i <= 0x39)
                    {
                        desc._name = String(1, static_cast<char>('0' + (i - 0x30)));
                    }
                    else
                    {
                        desc._name = "key_" + std::to_string(i);
                    }
                    break;
                }

                _key_to_control_index[i] = static_cast<u16>(_control_descs.size());
                _control_descs.push_back(std::move(desc));
            }
        }
    }

    void KeyboardDevice::Poll()
    {
        _previous_keys = _current_keys;

        // Read key states from the global Input system
        for (u16 i = 0; i < InputConstants::kMaxKeyboardKeys; ++i)
        {
            _current_keys[i] = Input::IsKeyDown(static_cast<EKey>(i));
        }
    }

    InputValue KeyboardDevice::ReadControl(u16 control_index) const
    {
        InputValue value;
        value._type = EInputValueType::kButton;

        if (control_index < _control_descs.size())
        {
            const auto &desc = _control_descs[control_index];
            if (desc._index < InputConstants::kMaxKeyboardKeys)
            {
                value._value.x = _current_keys[desc._index] ? 1.0f : 0.0f;
            }
        }

        return value;
    }

    u16 KeyboardDevice::KeyToControlIndex(EKey key) const
    {
        const auto key_val = static_cast<u8>(key);
        if (key_val >= _key_to_control_index.size())
            return InputConstants::kInvalidControlIndex;
        return _key_to_control_index[key_val];
    }

    StringView KeyboardDevice::ControlIndexToName(u16 control_index) const
    {
        if (control_index < _control_descs.size())
            return _control_descs[control_index]._name;
        return "unknown";
    }

} // namespace Ailu
