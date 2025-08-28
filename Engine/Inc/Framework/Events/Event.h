#pragma once
#ifndef __EVENT_H__
#define __EVENT_H__
#include "GlobalMarco.h"
#include <functional>
#include <mutex>
#include <string>

namespace Ailu
{
    enum class EEventType
    {
        kNone = 0,
        kWindowClose,
        kWindowResize,
        kWindowFocus,
        kWindowLostFocus,
        kWindowMoved,
        kWindowMinimize,
        kWindowMaximize,
        kAppTick,
        kAppUpdate,
        kAppRender,
        kKeyPressed,
        kKeyReleased,
        kMouseButtonPressed,
        kMouseButtonReleased,
        kMouseMoved,
        kMouseScrolled,
        kMouseEnterWindow,
        kMouseExitWindow,
        kDragFile
    };

    enum EEventCategory
    {
        kNone = 0,
        kEventCategoryApplication = BIT(0),
        kEventCategoryInput = BIT(1),
        kEventCategoryKeyboard = BIT(2),
        kEventCategoryMouse = BIT(3),
        kEventCategoryMouseButton = BIT(4),
    };

#define EVENT_CLASS_TYPE(type)                                                   \
    static EEventType GetStaticType() { return EEventType::##type; }             \
    virtual EEventType GetEventType() const override { return GetStaticType(); } \
    virtual const char *GetName() const override { return #type; }

#define EVENT_CLASS_CATEGORY(category) \
    virtual int GetCategoryFlags() const override { return category; }

    class AILU_API Event
    {
        friend class EventDispather;

    public:
        virtual EEventType GetEventType() const = 0;
        virtual const char *GetName() const = 0;
        virtual int GetCategoryFlags() const = 0;
        virtual std::string ToString() const { return GetName(); }
        const bool Handled() const { return _handled; }
        inline bool IsInCategory(EEventCategory category) const
        {
            return GetCategoryFlags() & category;
        }
        bool operator==(const Event &other) const
        {
            return GetEventType() == other.GetEventType();
        }

    protected:
        bool _handled = false;
    };

    class EventDispather
    {
        template<typename T>
        using EventHandle = std::function<bool(T &)>;

    public:
        EventDispather(Event &e) : _event(e) {}
        void CommitEvent(Event &e)
        {
            _event = e;
        }
        template<typename T>
        bool Dispatch(EventHandle<T> handle)
        {
            if (_event.GetEventType() == T::GetStaticType())
            {
                _event._handled = handle(*(T *) &_event);
                return true;
            }
            return false;
        }

    private:
        Event &_event;
    };

    template<typename... Args>
    class Delegate
    {
    public:
        using HandlerType = std::function<void(Args...)>;
        using Handle = u32;

		Handle Subscribe(HandlerType&& handler) 
		{
			std::lock_guard<std::mutex> lock(_mutex);
			_handlers.emplace_back(_next_id, std::move(handler));
			return _next_id++;
		}

        void Unsubscribe(Handle id)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _handlers.erase(std::remove_if(_handlers.begin(), _handlers.end(),
                                           [id](const auto &pair)
                                           { return pair.first == id; }),
                            _handlers.end());
        }

        void Unsubscribe(HandlerType handler)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _handlers.erase(std::remove_if(_handlers.begin(), _handlers.end(),
                                           [&handler](const auto &pair)
                                           { return pair.second.target<void(Args...)>() == handler.target<void(Args...)>(); }),
                            _handlers.end());
        }

        void Invoke(Args... args)
        {
            Vector<std::pair<Handle, HandlerType>> handlers_copy;
            {
                std::lock_guard<std::mutex> lock(_mutex);
                handlers_copy = _handlers;
            }
            for (auto &[id, handler]: handlers_copy)
            {
                handler(args...);
            }
        }
        Handle operator+=(HandlerType&& handler) { return Subscribe(std::move(handler)); }
        void operator-=(Handle id) { Unsubscribe(id); }
        void operator-=(HandlerType handler) { Unsubscribe(handler); }

    private:
        Vector<std::pair<Handle, HandlerType>> _handlers;
        Handle _next_id = 0;
        std::mutex _mutex;
    };

}// namespace Ailu


#endif// !EVENT_H__