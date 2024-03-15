#pragma once
#ifndef __MOUSE_EVENT_H__
#define __MOUSE_EVENT_H__

#include <format>

#include "GlobalMarco.h"
#include "Event.h"

namespace Ailu
{
	class AILU_API MouseMovedEvent : public Event
	{
	public:
		MouseMovedEvent(float x, float y) : _pos_x(x), _pos_y(y) {}
		inline float GetX() const { return _pos_x;  }
		inline float GetY() const { return _pos_y;  }
		std::string ToString() const override
		{
			return std::format("MouseMovedEvent : {},{}",_pos_x,_pos_y);
		}
		EVENT_CLASS_TYPE(kMouseMoved)
		EVENT_CLASS_CATEGORY(kEventCategoryMouse | kEventCategoryInput)
	private:
		float _pos_x, _pos_y;
	};

	class AILU_API MouseScrollEvent : public Event
	{
	public:
		MouseScrollEvent(float offset_y) : _offset_y(offset_y) {}
		inline float GetOffsetY() const { return _offset_y;  }
		std::string ToString() const override
		{
			return std::format("MouseScrollEvent : offsetY: {}", _offset_y);
		}
		EVENT_CLASS_TYPE(kMouseScrolled)
		EVENT_CLASS_CATEGORY(kEventCategoryMouse | kEventCategoryInput)
	private:
		float _offset_y;
	};

	class AILU_API MouseButtonPressedEvent : public Event
	{
	public:
		MouseButtonPressedEvent(u8 button) : _mouse_btn(button) {}
		inline u8 GetButton() const { return _mouse_btn;  }
		std::string ToString() const override
		{
			return std::format("MouseButtonPressedEvent : {} be pressed", _mouse_btn);
		}
		EVENT_CLASS_TYPE(kMouseButtonPressed)
		EVENT_CLASS_CATEGORY(kEventCategoryMouse | kEventCategoryInput)
	private:
		u8 _mouse_btn;
	};

	class AILU_API MouseButtonReleasedEvent : public Event
	{
	public:
		MouseButtonReleasedEvent(u8 button) : _mouse_btn(button) {}
		inline u8 GetButton() const { return _mouse_btn; }
		std::string ToString() const override
		{
			return std::format("MouseButtonReleasedEvent : {} be released", _mouse_btn);
		}
		EVENT_CLASS_TYPE(kMouseButtonReleased)
		EVENT_CLASS_CATEGORY(kEventCategoryMouse | kEventCategoryInput)
	private:
		u8 _mouse_btn;
	};
}


#endif // !MOUSE_EVENT_H__

