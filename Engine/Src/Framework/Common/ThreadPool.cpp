#include "Framework/Common/ThreadPool.h"

#include "pch.h"

namespace Ailu::Core
{
    namespace
    {
        Scope<ThreadPool> s_thread_pool;
    }

    void ThreadPool::Init(u8 thread_num, std::string name)
    {
        AL_ASSERT_MSG(s_thread_pool == nullptr, "ThreadPool already init!");
        s_thread_pool = MakeScope<ThreadPool>(thread_num, std::move(name));
    }

    void ThreadPool::Shutdown()
    {
        s_thread_pool.reset();
    }

    ThreadPool &ThreadPool::Get()
    {
        AL_ASSERT_MSG(s_thread_pool != nullptr, "ThreadPool has not been initialized!");
        return *s_thread_pool;
    }
}