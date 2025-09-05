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
        kMouseScroll,
        kMouseEnterWindow,
        kMouseExitWindow,
        kMouseSetCursor,
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

    class Window;
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
        Window *_window = nullptr;
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

        class EventView
        {
        public:
            Handle Subscribe(HandlerType &&handler) { return _delegate->Subscribe(std::move(handler)); }
            void Unsubscribe(Handle id) { _delegate->Unsubscribe(id); }
            void Unsubscribe(HandlerType handler) { _delegate->Unsubscribe(std::move(handler)); }
            Handle operator+=(HandlerType &&handler) { return Subscribe(std::move(handler)); }
            void operator-=(Handle id) { Unsubscribe(id); }
            void operator-=(HandlerType handler) { Unsubscribe(std::move(handler)); }

        private:
            friend class Delegate;
            explicit EventView(Delegate *d) : _delegate(d) {}
            Delegate *_delegate;
        };

        EventView GetEventView() { return EventView(this); }

        void Invoke(Args... args)
        {
            Vector<std::pair<Handle, HandlerType>> handlers_copy;
            {
                std::lock_guard<std::mutex> lock(_mutex);
                handlers_copy = _handlers;
            }
            for (auto &[id, handler]: handlers_copy)
                handler(args...);
        }

    private:
        Handle Subscribe(HandlerType &&handler)
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

        Vector<std::pair<Handle, HandlerType>> _handlers;
        Handle _next_id = 0;
        std::mutex _mutex;
    };

#define DECLARE_DELEGATE(Name, ...)          \
protected:                                     \
    Delegate<__VA_ARGS__> _##Name##_delegate; \
                                             \
public:                                      \
    Delegate<__VA_ARGS__>::EventView _##Name = _##Name##_delegate.GetEventView()


}// namespace Ailu


#endif// !EVENT_H__