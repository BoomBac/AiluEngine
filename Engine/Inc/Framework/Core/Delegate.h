#pragma once
#include "CoreMinimal.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Common/NonCopyable.h"
#include <functional>
#include <vector>
#include <algorithm>
#include <mutex>

namespace Ailu
{
    template<typename... Args>
    class Delegate : public NonCopyable
    {
    public:
        using HandlerType = std::function<void(Args...)>;
        using Handle = u32;

        Delegate() = default;

        Delegate(Delegate&& other) noexcept
        {
            std::lock_guard<std::mutex> lock(other._mutex);
            _handlers = std::move(other._handlers);
            _next_id = other._next_id;
            other._next_id = 0;
        }

        Delegate& operator=(Delegate&& other) noexcept
        {
            if (this == &other)
                return *this;

            std::scoped_lock lock(_mutex, other._mutex);
            _handlers = std::move(other._handlers);
            _next_id = other._next_id;
            other._next_id = 0;
            return *this;
        }

        class EventView
        {
        public:
            Handle Subscribe(HandlerType&& handler) { return _delegate->Subscribe(std::move(handler)); }
            void Unsubscribe(Handle id) { _delegate->Unsubscribe(id); }
            void Unsubscribe(HandlerType handler) { _delegate->Unsubscribe(std::move(handler)); }
            Handle operator+=(HandlerType&& handler) { return Subscribe(std::move(handler)); }
            void operator-=(Handle id) { Unsubscribe(id); }
            void operator-=(HandlerType handler) { Unsubscribe(std::move(handler)); }

        private:
            friend class Delegate;
            explicit EventView(Delegate* d) : _delegate(d) {}
            Delegate* _delegate = nullptr;
        };

        EventView GetEventView() { return EventView(this); }

        void Invoke(Args... args)
        {
            Vector<std::pair<Handle, HandlerType>> handlers_copy;
            {
                std::lock_guard<std::mutex> lock(_mutex);
                handlers_copy = _handlers;
            }

            for (auto& [id, handler] : handlers_copy)
                handler(args...);
        }

    private:
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
                                        [id](const auto& pair) { return pair.first == id; }),
                            _handlers.end());
        }

        void Unsubscribe(HandlerType handler)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _handlers.erase(std::remove_if(_handlers.begin(), _handlers.end(),
                                        [&handler](const auto& pair)
                                        {
                                            return pair.second.template target<void(Args...)>() ==
                                                    handler.template target<void(Args...)>();
                                        }),
                            _handlers.end());
        }

    private:
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
}
