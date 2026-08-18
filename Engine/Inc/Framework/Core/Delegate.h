#pragma once
#include "CoreMinimal.h"
#include "Framework/Core/Containers/Map.h"
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
        static constexpr Handle kInvalidHandle = 0u;

        Delegate() = default;

        Delegate(Delegate&& other) noexcept
        {
            std::lock_guard<std::mutex> lock(other._mutex);
            _handlers = std::move(other._handlers);
            _next_id = other._next_id;
            other._next_id = 1u;
        }

        Delegate& operator=(Delegate&& other) noexcept
        {
            if (this == &other)
                return *this;

            std::scoped_lock lock(_mutex, other._mutex);
            _handlers = std::move(other._handlers);
            _next_id = other._next_id;
            other._next_id = 1u;
            return *this;
        }

        class EventView
        {
        public:
            EventView() = default;
            Handle Subscribe(HandlerType&& handler)
            {
                return _delegate != nullptr ? _delegate->Subscribe(std::move(handler)) : kInvalidHandle;
            }
            void Unsubscribe(Handle id) { if (_delegate != nullptr) _delegate->Unsubscribe(id); }
            Handle operator+=(HandlerType&& handler) { return Subscribe(std::move(handler)); }
            void operator-=(Handle id) { Unsubscribe(id); }

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

    private:
        Vector<std::pair<Handle, HandlerType>> _handlers;
        Handle _next_id = 1u;
        std::mutex _mutex;
    };

    template<typename Key, typename... Args>
    class EventRouter : public NonCopyable
    {
    public:
        using HandlerType = std::function<void(Args...)>;
        using Handle = u32;
        static constexpr Handle kInvalidHandle = 0u;

        struct HandlerEntry
        {
            Handle _handle = kInvalidHandle;
            HandlerType _handler;
        };

        class EventView
        {
        public:
            EventView() = default;

            Handle Subscribe(const Key &key, HandlerType &&handler)
            {
                return _router != nullptr ? _router->Subscribe(key, std::move(handler)) : kInvalidHandle;
            }

            void Unsubscribe(Handle handle)
            {
                if (_router != nullptr)
                    _router->Unsubscribe(handle);
            }

        private:
            friend class EventRouter;
            explicit EventView(EventRouter *router) : _router(router) {}
            EventRouter *_router = nullptr;
        };

        EventView GetEventView() { return EventView(this); }

        void Invoke(const Key &key, Args... args)
        {
            Vector<HandlerEntry> handlers_copy;
            {
                std::lock_guard<std::mutex> lock(_mutex);
                const auto bucket_iter = _handlers.find(key);
                if (bucket_iter == _handlers.end())
                    return;
                handlers_copy = bucket_iter->second;
            }

            for (auto &entry : handlers_copy)
                entry._handler(args...);
        }

    private:
        Handle Subscribe(const Key &key, HandlerType &&handler)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            const Handle handle = _next_id++;
            _handlers[key].emplace_back(HandlerEntry{handle, std::move(handler)});
            _handle_to_key.emplace(handle, key);
            return handle;
        }

        void Unsubscribe(Handle handle)
        {
            if (handle == kInvalidHandle)
                return;

            std::lock_guard<std::mutex> lock(_mutex);
            const auto key_iter = _handle_to_key.find(handle);
            if (key_iter == _handle_to_key.end())
                return;

            const auto bucket_iter = _handlers.find(key_iter->second);
            if (bucket_iter != _handlers.end())
            {
                auto &handlers = bucket_iter->second;
                handlers.erase(std::remove_if(handlers.begin(), handlers.end(),
                                              [handle](const HandlerEntry &entry) { return entry._handle == handle; }),
                               handlers.end());
                if (handlers.empty())
                    _handlers.erase(bucket_iter);
            }
            _handle_to_key.erase(key_iter);
        }

        HashMap<Key, Vector<HandlerEntry>> _handlers;
        HashMap<Handle, Key> _handle_to_key;
        Handle _next_id = 1u;
        std::mutex _mutex;
    };

#define DECLARE_DELEGATE(Name, ...)          \
protected:                                     \
    Delegate<__VA_ARGS__> _##Name##_delegate; \
                                             \
public:                                      \
    Delegate<__VA_ARGS__>::EventView _##Name = _##Name##_delegate.GetEventView()

#define DECLARE_DELEGATE_VIEW(Name, ...)      \
public:                                       \
    Delegate<__VA_ARGS__>::EventView _##Name

#define DECLARE_EVENT_ROUTER(Name, Key, ...)       \
protected:                                          \
    EventRouter<Key, __VA_ARGS__> _##Name##_router; \
                                                  \
public:                                             \
    EventRouter<Key, __VA_ARGS__>::EventView _##Name = _##Name##_router.GetEventView()
}
