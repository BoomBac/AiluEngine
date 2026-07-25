#pragma once
#ifndef AILU_FRAME_ALLOCATOR_H
#define AILU_FRAME_ALLOCATOR_H

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Common/NonCopyable.h"
#include <mutex>

namespace Ailu::Render
{
    class FrameAllocator : public NonCopyable
    {
    public:
        static constexpr u64 kDefaultPageSize = 4 * 1024 * 1024; // 4MB
    public:
        explicit FrameAllocator(u64 page_size = kDefaultPageSize);
        ~FrameAllocator();
        void *Allocate(u64 size, u64 align = 16u);
        template<typename T>
        T *Allocate(u64 count = 1u, u64 align = alignof(T))
        {
            return reinterpret_cast<T *>(Allocate(sizeof(T) * count, align));
        }
        void Reset();
        void NewFrame(u64 frame_count);

        u64 PageSize() const { return _page_size; }
        u64 TotalSize() const { return _total_size; }
        u64 UsedSize() const { return _used_size; }

    private:
        struct Page
        {
            u8 *_data = nullptr;
            u64 _size = 0u;
            u64 _offset = 0u;
        };

        void AddPage(u64 size);
        u64 AlignOffset(const Page &page, u64 offset, u64 align) const;

    private:
        std::mutex _mutex;
        Vector<Page> _pages;
        u64 _page_size = kDefaultPageSize;
        u64 _current_page = 0u;
        u64 _total_size = 0u;
        u64 _used_size = 0u;
        u64 _last_frame = static_cast<u64>(-1);
    };
}

#endif// AILU_FRAME_ALLOCATOR_H
