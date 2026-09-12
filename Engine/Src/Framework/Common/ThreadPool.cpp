#include "Framework/Common/ThreadPool.h"

#include "pch.h"
#include "Framework/Common/Allocator.hpp"

namespace Ailu::Core
{
    void ThreadPool::Init(u8 thread_num, std::string name)
    {
        AL_ASSERT_MSG(s_instance == nullptr, "ThreadPool already init!");
        AL_NEW_TAG(EMemoryTag::kCore, ThreadPool, thread_num, std::move(name));
    }

    void ThreadPool::Shutdown()
    {
        if (s_instance != nullptr)
            AL_DELETE(s_instance);
    }

    ThreadPool &ThreadPool::Get()
    {
        AL_ASSERT_MSG(s_instance != nullptr, "ThreadPool has not been initialized!");
        return *s_instance;
    }
}
