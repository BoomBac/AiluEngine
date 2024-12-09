// #ifndef __MEMORY_H__
// #define __MEMORY_H__

// // 重载全局 new 运算符
// void *operator new(size_t size);

// // 重载全局 delete 运算符
// void operator delete(void *ptr) noexcept;

// // 重载数组形式的 new 和 delete
// void *operator new[](size_t size)
// {
//     return ::operator new(size);
// }

// void operator delete[](void *ptr) noexcept
// {
//     ::operator delete(ptr);
// }

// namespace Ailu
// {
//     namespace Memory
//     {

//         // 检查是否存在内存泄漏
//         __declspec(dllexport) void CheckMemoryLeaks();
//     }// namespace Memory
// }// namespace Ailu


// #endif//__MEMORY_H__