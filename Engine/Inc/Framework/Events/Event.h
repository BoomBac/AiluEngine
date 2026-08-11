#pragma once
#ifndef __EVENT_H__
#define __EVENT_H__
#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/Containers/Vector.h"
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
        kEventCategoryApplication = 1 << 1,
        kEventCategoryInput =       1 << 2,
        kEventCategoryKeyboard =    1 << 3,
        kEventCategoryMouse =       1 << 4,
        kEventCategoryMouseButton = 1 << 5,
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
        void SetHandled(bool handled = true) { _handled = handled; }
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
}// namespace Ailu


#endif// !EVENT_H__
