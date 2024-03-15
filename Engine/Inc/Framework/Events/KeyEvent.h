#pragma once
#ifndef __KEY_EVENT_H__
#define __KEY_EVENT_H__
#include <format>

#include "GlobalMarco.h"
#include "Event.h"

namespace Ailu
{
	class AILU_API KeyEvent : public Event
	{
	public:
		inline u8 GetKeyCode() const { return _key_code; }
		EVENT_CLASS_CATEGORY(kEventCategoryKeyboard | kEventCategoryInput)
	protected:
		KeyEvent(u8 keycode) : _key_code(keycode) {}
		u8 _key_code;
	};

	class AILU_API KeyPressedEvent : public KeyEvent
	{
	public:
		KeyPressedEvent(u8 keycode, int repeat_count) : KeyEvent(keycode), _repeat_count(repeat_count) {}
		inline u8 GetRepeatCount() const { return _repeat_count; }
		std::string ToString() const override
		{
			return std::format("KeyPressEvent: {} ({} repeats)",_key_code,_repeat_count);
		}
		EVENT_CLASS_TYPE(kKeyPressed)
	private:
		int8_t _repeat_count;
	};

	class AILU_API KeyReleasedEvent : public KeyEvent
	{
	public:
		KeyReleasedEvent(u8 keycode) : KeyEvent(keycode) {}

		std::string ToString() const override
		{
			return std::format("KeyReleasedEvent: {}",_key_code);
		}
		EVENT_CLASS_TYPE(kKeyReleased)
	};
}


#endif // !KEY_EVENT_H__

