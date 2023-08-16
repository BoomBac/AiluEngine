#pragma once
#ifndef __WINDOW_EVENT_H__
#define __WINDOW_EVENT_H__
#include <format>
#include "Framework/Events/Event.h"

namespace Ailu
{
	class AILU_API WindowCloseEvent : public Event
	{
	public:
		WindowCloseEvent() {};
		std::string ToString() const override
		{
			return "WindowCloseEvent";
		}
		EVENT_CLASS_TYPE(kWindowClose)
		EVENT_CLASS_CATEGORY(kEventCategoryApplication)
	};

	class AILU_API WindowFocusEvent : public Event
	{
	public:
		WindowFocusEvent() {};
		std::string ToString() const override
		{
			return "WindowFocusEvent";
		}
		EVENT_CLASS_TYPE(kWindowFocus)
		EVENT_CLASS_CATEGORY(kEventCategoryApplication)
	};

	class AILU_API WindowLostFocusEvent : public Event
	{
	public:
		WindowLostFocusEvent() {};
		std::string ToString() const override
		{
			return "WindowLostFocusEvent";
		}
		EVENT_CLASS_TYPE(kWindowLostFocus)
			EVENT_CLASS_CATEGORY(kEventCategoryApplication)
	};

	class AILU_API WindowResizeEvent : public Event
	{
	public:
		WindowResizeEvent(uint16_t w, uint16_t h) : _width(w), _height(h) {}
		inline float GetWidth() const { return _width; }
		inline float GetHeight() const { return _height; }
		std::string ToString() const override
		{
			return std::format("WindowResizeEvent : new window size {},{}", _width, _height);
		}
		EVENT_CLASS_TYPE(kWindowResize)
		EVENT_CLASS_CATEGORY(kEventCategoryApplication)
	private:
		uint16_t _width, _height;
	};

	class AILU_API WindowMovedEvent : public Event
	{
	public:
		WindowMovedEvent() {};
		std::string ToString() const override
		{
			return "WindowMovedEvent";
		}
		EVENT_CLASS_TYPE(kWindowMoved)
		EVENT_CLASS_CATEGORY(kEventCategoryApplication)
	};
}


#endif // !WINDOW_EVENT_H__

