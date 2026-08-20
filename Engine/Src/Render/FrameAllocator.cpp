#include "Render/FrameAllocator.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/Allocator.hpp"
#include <algorithm>

namespace Ailu::Render
{
    static inline u64 AlignUp(u64 value, u64 align)
    {
        return (value + (align - 1)) & ~(align - 1);
    }

    FrameAllocator::FrameAllocator(u64 page_size)
        : _page_size(page_size)
    {
        AddPage(_page_size);
    }

    FrameAllocator::~FrameAllocator()
    {
        for (auto &page: _pages)
        {
            AL_FREE(page._data);
        }
        _pages.clear();
        _total_size = 0u;
        _used_size = 0u;
    }

    void FrameAllocator::AddPage(u64 size)
    {
        Page page;
        page._size = size;
        page._offset = 0u;
        page._data = AL_ALLOC_TAG(EMemoryTag::kTemporary, u8, size);
        _pages.emplace_back(page);
        _total_size += size;
    }

    u64 FrameAllocator::AlignOffset(const Page &page, u64 offset, u64 align) const
    {
        const uintptr_t base = reinterpret_cast<uintptr_t>(page._data);
        const uintptr_t ptr = base + offset;
        const uintptr_t aligned = AlignUp(ptr, align);
        return static_cast<u64>(aligned - base);
    }

    void *FrameAllocator::Allocate(u64 size, u64 align)
    {
        if (size == 0u)
            return nullptr;
        if (align == 0u)
            align = 1u;
        AL_ASSERT_MSG((align & (align - 1)) == 0u, "FrameAllocator: align must be power of two. align={} ", align);

        std::lock_guard<std::mutex> lock(_mutex);

        if (_pages.empty())
        {
            AddPage(_page_size);
            _current_page = 0u;
        }

        Page *page = &_pages[_current_page];
        u64 aligned_offset = AlignOffset(*page, page->_offset, align);
        if (aligned_offset + size > page->_size)
        {
            if (_current_page + 1 < _pages.size())
            {
                ++_current_page;
                page = &_pages[_current_page];
                page->_offset = 0u;
            }
            else
            {
                const u64 new_page_size = std::max(_page_size, AlignUp(size, align));
                AddPage(new_page_size);
                _current_page = static_cast<u64>(_pages.size() - 1);
                page = &_pages[_current_page];
            }
            aligned_offset = AlignOffset(*page, page->_offset, align);
            if (aligned_offset + size > page->_size)
            {
                AL_ASSERT_MSG(false, "FrameAllocator: allocation too large for page. size={}, page_size={}", size, page->_size);
                return nullptr;
            }
        }

        void *result = page->_data + aligned_offset;
        page->_offset = aligned_offset + size;
        _used_size += size;
        return result;
    }

    void FrameAllocator::Reset()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (auto &page: _pages)
            page._offset = 0u;
        _current_page = 0u;
        _used_size = 0u;
    }

    void FrameAllocator::NewFrame(u64 frame_count)
    {
        if (_last_frame == frame_count)
            return;
        _last_frame = frame_count;
        Reset();
    }
}
