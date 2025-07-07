#pragma once
#pragma warning(disable : 4251)//disable dll export warning
#ifndef __ALLOCATOR__
#define __ALLOCATOR__
#include "GlobalMarco.h"
#include <mutex>
namespace Ailu
{
    namespace Core
    {
        struct Bin;
        struct PageHeader
        {
            u32 _page_index;
            Bin *_bin;
            u32 _block_size;
            u32 _block_count;
            std::atomic<u32> _used_count;
            PageHeader *_next;
            PageHeader *_prev;
            const char* _thread_name;
            u32 _aligned_head_size;
        };
        constexpr static u64 kPageHeaderSize = sizeof(PageHeader);
        constexpr static u64 kPageSize = 65536u;//64KB
        constexpr static u64 kMaxBinSize = 4096;//4KB

        struct Page
        {
            friend class PageMgr;

        public:
            static PageHeader *GetPageHeaderFromPointer(void *ptr)
            {
                uintptr_t page_start = reinterpret_cast<uintptr_t>(ptr) & ~(kPageSize - 1);
                return reinterpret_cast<PageHeader *>(page_start);
            }

        public:
            Page(u32 block_size, Bin *bin);
            ~Page();
            /// @brief 返回对齐后可用于分配block的地址
            /// @return
            u8 *Data() const { return reinterpret_cast<u8 *>(_head) + _head->_aligned_head_size; };
            String Dump() const;
            PageHeader *_head;
            inline static std::atomic<u32> s_page_index = 0u;
        };

        class PageMgr
        {
        public:
            ~PageMgr();
            Page *AlloctatePage(u64 block_size, Bin *bin);
            void DeallocatePage(PageHeader *header);
            String Dump();

        private:
            List<Page> _pages;
            std::mutex _mutex;
        };

        struct Bin
        {
            explicit Bin(u32 block_size) : _block_size(block_size),_used_bytes_ratio(0.0f) {};
            void *Allocate(u64 size);
            void Deallocate(void *ptr);
            struct Block
            {
                Block *_next;
            };
            Block *_free_list = nullptr;
            u32 _block_size;
            f32 _used_bytes_ratio;
            Vector<PageHeader*> _pages;
            u32 _empty_page_count = 0u;
        };
        class Allocator;
        class Arena
        {
        public:
            explicit Arena(Allocator *allocator);
            ~Arena();
            void *Allocate(u64 size,u64 align);
            void Deallocate(void *ptr);

        private:
            Allocator *_allocator;
            Vector<Bin *> _bins;
        };

        class AILU_API Allocator
        {
        public:
            static void Init();
            static void Shutdown();
            static Allocator &Get();

        public:
            inline static u64 kDefaultAlign = 8u;
            Arena &ThreadArena();
            void *Allocate(u64 size, const char *file, const char *function, u16 line, u64 align = kDefaultAlign);
            void Deallocate(void *ptr);
            void PrintLeaks();
            void PrintAllocatedInfo();
            /// @brief 所有分配的内存总量(字节)
            /// @return
            u64 TotalAllocated() const { return _total_allocated; }
            PageMgr &GetPageMgr() { return _page_mgr; }

        private:
        private:
            struct AllocHeader
            {
                u32 _size_and_flags;// 高位 flags，低位 size
                u32 _magic;         // 仍保留 debug 用
                static constexpr u32 kAllocFlagSystem = 1u << 31;
                static constexpr u32 kAllocSizeMask = ~kAllocFlagSystem;

                static u32 PackAllocSize(u32 size, bool is_sys_alloc)
                {
                    return size | (is_sys_alloc ? kAllocFlagSystem : 0);
                }
                static u32 UnpackAllocSize(u32 size_and_flags)
                {
                    return size_and_flags & kAllocSizeMask;
                }
                static bool IsSysAlloc(u32 size_and_flags)
                {
                    return (size_and_flags & kAllocFlagSystem) != 0;
                }
            };
            static constexpr u32 kAllocHeaderSize = sizeof(AllocHeader);

            struct AllocationInfo
            {
                u64 _size;
                const char *_file;
                const char *_function;
                u16 _line;
                bool _is_sys_alloc;
            };
            HashMap<void *, AllocationInfo> _allocations;
            std::mutex _mutex;
            std::atomic<u64> _total_allocated = 0u;
            PageMgr _page_mgr;
            std::mutex _arena_mutex;
            Vector<std::unique_ptr<Arena>> _arenas;// 管理所有arena
        };


    }// namespace Core

    template<typename T, typename... Args>
    static T *DebugNew(const char *file, const char *function, int line, Args &&...args)
    {
        void *mem = Core::Allocator::Get().Allocate(sizeof(T), file, function, line,std::alignment_of<T>::value);
        return new (mem) T(std::forward<Args>(args)...);
    }

    template<typename T>
    static T *DebugAlloc(const char *file, const char *function, int line, u64 size, u64 align = Core::Allocator::kDefaultAlign)
    {
        return reinterpret_cast<T *>(Core::Allocator::Get().Allocate(size * sizeof(T), file, function, line, align));
    }

    template<typename T>
    static void DebugDelete(T *ptr)
    {
        if (ptr)
        {
            ptr->~T();
            Core::Allocator::Get().Deallocate(ptr);
            ptr = nullptr;
        }
    }
    template<typename T>
    static void DebugFree(T *ptr)
    {
        if (ptr)
        {
            Core::Allocator::Get().Deallocate(ptr);
            ptr = nullptr;
        }
    }

#define AL_NEW(T, ...)               DebugNew<T>(__FILE__, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define AL_DELETE(ptr)               DebugDelete(ptr)

#define AL_ALLOC(T, size)            DebugAlloc<T>(__FILE__, __FUNCTION__, __LINE__, size)
#define AL_ALIGN_ALLOC(T,size,align) DebugAlloc<T>(__FILE__, __FUNCTION__, __LINE__, size, align)
#define AL_FREE(ptr)                 DebugFree(ptr)

};// namespace Ailu
#endif//__ALLOCATOR__