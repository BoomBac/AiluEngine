#pragma once
#ifndef __EVENT_H__
#define __EVENT_H__
#include <string>
#include <functional>
#include "GlobalMarco.h"

namespace Ailu
{
	enum class EEventType
	{
		kNone = 0,
		kWindowClose,kWindowResize,kWindowFocus,kWindowLostFocus,kWindowMoved,
		AppTick,kAppUpdate,kAppRender,
		kKeyPressed,kKeyReleased,
		kMouseButtonPressed,kMouseButtonReleased,kMouseMoved,kMouseScrolled
	};

	enum EEventCategory
	{
		kNone = 0,
		kEventCategoryApplication	= BIT(0),
		kEventCategoryInput			= BIT(1),
		kEventCategoryKeyboard		= BIT(2),
		kEventCategoryMouse			= BIT(3),
		kEventCategoryMouseButton	= BIT(4),
	};

#define EVENT_CLASS_TYPE(type) static EEventType GetStaticType() { return EEventType::##type; }\
								virtual EEventType GetEventType() const override { return GetStaticType(); }\
								virtual const char* GetName() const override { return #type; }
	
#define EVENT_CLASS_CATEGORY(category) virtual int GetCategoryFlags() const override { return category; }

	class AILU_API Event
	{
		friend class EventDispather;
	public:
		virtual EEventType GetEventType() const = 0;
		virtual const char* GetName() const = 0;
		virtual int GetCategoryFlags() const = 0;
		virtual std::string ToString() const { return GetName();  }

		inline bool IsInCategory(EEventCategory category) const
		{
			return GetCategoryFlags() & category;
		}
	protected:
		bool _handled = false;
	};

	class EventDispather
	{
		template<typename T>
		using EventHandle = std::function<bool(T&)>;
	public:
		EventDispather(Event& e) : _event(e) {}

		template<typename T>
		bool Dispatch(EventHandle<T> handle)
		{
			if (_event.GetEventType() == T::GetStaticType())
			{
				_event._handled = handle(*(T*)&_event);
				return true;
			}
			return false;
		}
	private:
		Event& _event;
	};
}


#endif // !EVENT_H__