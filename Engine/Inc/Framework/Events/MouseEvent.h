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
			return std::format("MouseMovedEvent : {},{}\r\n",_pos_x,_pos_y);
		}
		EVENT_CLASS_TYPE(kMouseMoved)
		EVENT_CLASS_CATEGORY(kEventCategoryMouse | kEventCategoryInput)
	private:
		float _pos_x, _pos_y;
	};
}


#endif // !MOUSE_EVENT_H__

