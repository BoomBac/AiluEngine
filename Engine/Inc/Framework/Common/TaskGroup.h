#pragma once

#include "Framework/Common/ThreadPool.h"

#include <future>
#include <mutex>
#include <vector>

namespace Ailu
{
    class AILU_API TaskGroup
    {
    public:
        void Close();
        void Wait();

        template<typename callable_t>
        bool Dispatch(callable_t &&task)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_closed)
                return false;

            try
            {
                _tasks.emplace_back(Core::ThreadPool::Get().Enqueue(std::forward<callable_t>(task)));
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

    private:
        std::mutex _mutex;
        std::vector<std::future<void>> _tasks;
        bool _closed = false;
    };
}
