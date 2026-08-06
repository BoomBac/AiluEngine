#pragma once
#ifndef __AUTOMATION_CONCURRENT_QUEUE_H__
#define __AUTOMATION_CONCURRENT_QUEUE_H__

#include <deque>
#include <mutex>

namespace Ailu
{
    namespace Editor
    {
        // Minimal thread-safe FIFO. Transport threads push; the main thread pops
        // during Tick(). Deliberately non-blocking: request completion is signaled
        // through a promise/future, so there is no consumer-side wait queue.
        template<typename T>
        class ConcurrentQueue
        {
        public:
            void Push(T value)
            {
                std::lock_guard<std::mutex> lock(_mutex);
                _queue.push_back(std::move(value));
            }

            bool TryPop(T &out)
            {
                std::lock_guard<std::mutex> lock(_mutex);
                if (_queue.empty())
                    return false;
                out = std::move(_queue.front());
                _queue.pop_front();
                return true;
            }

            bool Empty() const
            {
                std::lock_guard<std::mutex> lock(_mutex);
                return _queue.empty();
            }

            size_t Size() const
            {
                std::lock_guard<std::mutex> lock(_mutex);
                return _queue.size();
            }

        private:
            mutable std::mutex _mutex;
            std::deque<T> _queue;
        };
    }// namespace Editor
}// namespace Ailu

#endif// !__AUTOMATION_CONCURRENT_QUEUE_H__
