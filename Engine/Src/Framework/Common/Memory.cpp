// #include "Framework/Common/Memory.h"
// #include "Memory.h"
// #include <iostream>
// #include <shared_mutex>
// #include <thread>
// #include <unordered_map>

// std::atomic<uint64_t> g_memory_allocated(0);


// std::unordered_map<void *, size_t> &GetMap()
// {
//     static std::unordered_map<void *, size_t> t_map{};
//     return t_map;
// }

// void *operator new(size_t size)
// {
//     void *ptr = std::malloc(size);
//     if (!ptr)
//     {
//         throw std::bad_alloc();
//     }
//     g_memory_allocated.fetch_add(size, std::memory_order_relaxed);
//     GetMap()[ptr] = size;
//     return ptr;
// }
// void operator delete(void *ptr) noexcept
// {
//     if (!ptr)
//         return;
//     size_t alloc_size = *static_cast<size_t *>(static_cast<size_t *>(ptr) - 1);
//     g_memory_allocated.fetch_sub(alloc_size, std::memory_order_relaxed);
//     std::free(ptr);
// }
// namespace Ailu
// {
//     void Memory::CheckMemoryLeaks()
//     {
//         std::cerr << "Memory leaks detected with " << g_memory_allocated.load() << "byte" << std::endl;
//     }
// }// namespace Ailu
