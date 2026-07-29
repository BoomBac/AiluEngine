#include "Framework/Core/Containers/Array.h"
#include "Framework/Common/Allocator.hpp"
#include "Framework/Common/Log.h"
#include "Framework/Math/ALMath.hpp"
#include "pch.h"

#include <bit>
#include <chrono>

namespace
{
    void *AlignedAlloc(size_t alignment, size_t size)
    {
#if defined(_MSC_VER)
        return _aligned_malloc(size, alignment);
#else
        const size_t aligned_size = (size + alignment - 1u) & ~(alignment - 1u);
        return std::aligned_alloc(alignment, aligned_size);
#endif
    }

    void AlignedFree(void *ptr)
    {
#if defined(_MSC_VER)
        _aligned_free(ptr);
#else
        std::free(ptr);
#endif
    }
}// namespace

namespace Ailu
{
    namespace
    {
        class BinMapper
        {
        public:
            BinMapper()
            {
                u32 bin_index = 0u;
                u32 bin_size = 8u;

                _bin_size_list.push_back(bin_size);
                for (u32 size = 0u; size < _size_to_bin.size(); ++size)
                {
                    while (size > bin_size)
                    {
                        if (bin_size < 128u) bin_size *= 2u;
                        else if (bin_size < 512u)
                            bin_size += 32u;
                        else
                            bin_size += 64u;

                        ++bin_index;
                        _bin_size_list.push_back(bin_size);
                    }
                    _size_to_bin[size] = static_cast<u8>(bin_index);
                }
            }
            u32 GetBinIndex(u64 size) const
            {
                if (size >= _size_to_bin.size()) return std::numeric_limits<u32>::max();
                return static_cast<u32>(_size_to_bin[static_cast<size_t>(size)]);
            }

            u32 GetBinSizeByIndex(u32 bin_index) const
            {
                AL_ASSERT(bin_index < _bin_size_list.size());
                return _bin_size_list[bin_index];
            }

            u32 GetBinCount() const { return static_cast<u32>(_bin_size_list.size()); }

        private:
            Array<u8, Allocator::kMaxSmallAllocationSize + 1u> _size_to_bin{};
            Vector<u32> _bin_size_list;
        };

        constexpr bool IsPowerOfTwo(u64 value) { return value != 0u && (value & (value - 1u)) == 0u; }

        uintptr_t AlignAddress(uintptr_t value, u64 alignment) { return (value + alignment - 1u) & ~(alignment - 1u); }

        constexpr u32 kDebugBitsPerWord = static_cast<u32>(sizeof(u64) * 8u);

        u32 GetDebugWordCount(u32 bit_count) { return (bit_count + kDebugBitsPerWord - 1u) / kDebugBitsPerWord; }

        void SetBit(Vector<u64> &bits, u32 bit_index)
        {
            const u32 word_index = bit_index / kDebugBitsPerWord;
            const u32 intra_word_index = bit_index % kDebugBitsPerWord;
            AL_ASSERT(word_index < bits.size());
            bits[word_index] |= 1ull << intra_word_index;
        }

        void ClearBit(Vector<u64> &bits, u32 bit_index)
        {
            const u32 word_index = bit_index / kDebugBitsPerWord;
            const u32 intra_word_index = bit_index % kDebugBitsPerWord;
            AL_ASSERT(word_index < bits.size());
            bits[word_index] &= ~(1ull << intra_word_index);
        }

        bool TestBit(const Vector<u64> &bits, u32 bit_index)
        {
            const u32 word_index = bit_index / kDebugBitsPerWord;
            const u32 intra_word_index = bit_index % kDebugBitsPerWord;
            AL_ASSERT(word_index < bits.size());
            return (bits[word_index] & (1ull << intra_word_index)) != 0u;
        }

        template<typename T>
        std::string PtrToAddress(T *ptr)
        {
            std::ostringstream oss;
            oss << "0x" << std::hex << std::setw(sizeof(void *) * 2u) << std::setfill('0') << reinterpret_cast<uintptr_t>(ptr);
            return oss.str();
        }

        Allocator *s_allocator = nullptr;
        BinMapper s_bin_mapper;

        u64 GetCurrentThreadIdValue()
        {
            return static_cast<u64>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
        }

        f64 GetMemoryDebugTimeSec()
        {
            using Clock = std::chrono::steady_clock;
            static const Clock::time_point s_start_time = Clock::now();
            return std::chrono::duration<f64>(Clock::now() - s_start_time).count();
        }

        EAllocatorPageState ToAllocatorPageState(EPageState state)
        {
            switch (state)
            {
                case EPageState::kPartial: return EAllocatorPageState::kPartial;
                case EPageState::kFull: return EAllocatorPageState::kFull;
                case EPageState::kEmpty:
                default: return EAllocatorPageState::kEmpty;
            }
        }

        const char *MemoryTagToCString(EMemoryTag tag)
        {
            return MemoryTagToString(tag).data();
        }

        u64 CountSetBits(const Vector<u64> &bits)
        {
            u64 count = 0u;
            for (u64 word: bits)
                count += static_cast<u64>(std::popcount(word));
            return count;
        }
    }// namespace

    thread_local Arena *g_thread_arena = nullptr;

    struct FreeBlock
    {
        FreeBlock *_next = nullptr;
    };

    struct PageHeader
    {
        PageMgr *_owner_page_mgr = nullptr;
        Bin *_owner_bin = nullptr;
        Arena *_owner_arena = nullptr;
        FreeBlock *_free_list = nullptr;
        PageHeader *_prev = nullptr;
        PageHeader *_next = nullptr;
        uintptr_t _block_begin = 0u;
        uintptr_t _block_end = 0u;
        u32 _block_size = 0u;
        u32 _block_count = 0u;
        u32 _free_count = 0u;
        EPageState _state = EPageState::kEmpty;
    };

    namespace
    {
        u32 GetBlockIndex(const PageHeader *page, const void *ptr)
        {
            AL_ASSERT(page != nullptr);
            const uintptr_t address = reinterpret_cast<uintptr_t>(ptr);
            AL_ASSERT(address >= page->_block_begin && address < page->_block_end);
            AL_ASSERT((address - page->_block_begin) % page->_block_size == 0u);
            return static_cast<u32>((address - page->_block_begin) / page->_block_size);
        }
    }// namespace

    struct Page
    {
        static constexpr u64 kPageSize = 65536u;

        static void Initialize(PageHeader *page, u32 block_size)
        {
            AL_ASSERT(page != nullptr);
            AL_ASSERT(block_size >= sizeof(FreeBlock));

            page->_block_size = block_size;
            const uintptr_t page_begin = reinterpret_cast<uintptr_t>(page);
            const uintptr_t data_begin = AlignAddress(page_begin + sizeof(PageHeader), block_size);
            const uintptr_t page_end = page_begin + kPageSize;

            AL_ASSERT(data_begin < page_end);
            page->_block_count = static_cast<u32>((page_end - data_begin) / block_size);
            AL_ASSERT(page->_block_count > 0u);

            page->_free_count = page->_block_count;
            page->_free_list = nullptr;
            page->_block_begin = data_begin;
            page->_block_end = data_begin + static_cast<uintptr_t>(page->_block_count) * block_size;
            page->_state = EPageState::kEmpty;

            for (u32 i = 0u; i < page->_block_count; ++i)
            {
                auto *block = reinterpret_cast<FreeBlock *>(data_begin + static_cast<uintptr_t>(i) * block_size);
                block->_next = page->_free_list;
                page->_free_list = block;
            }

            AL_ASSERT(page->_owner_page_mgr != nullptr);
            page->_owner_page_mgr->InitializePageDebugInfo(page);
        }

        static bool Owns(const PageHeader *page, const void *ptr)
        {
            const uintptr_t address = reinterpret_cast<uintptr_t>(ptr);
            if (address < page->_block_begin || address >= page->_block_end) return false;
            return (address - page->_block_begin) % page->_block_size == 0u;
        }

        static void *Allocate(PageHeader *page)
        {
            AL_ASSERT(page != nullptr && page->_free_list != nullptr && page->_free_count > 0u);
            FreeBlock *block = page->_free_list;
            page->_free_list = block->_next;
            --page->_free_count;
            page->_owner_page_mgr->MarkBlockAllocated(page, block);
            return block;
        }

        static void Deallocate(PageHeader *page, void *ptr)
        {
            AL_ASSERT(page != nullptr && ptr != nullptr);
            AL_ASSERT(page->_free_count < page->_block_count);
            AL_ASSERT(Owns(page, ptr));

            auto *block = static_cast<FreeBlock *>(ptr);
            block->_next = page->_free_list;
            page->_free_list = block;
            ++page->_free_count;
            page->_owner_page_mgr->MarkBlockFreed(page, ptr);
        }

        static PageHeader *GetPageHeaderFromPointer(void *ptr)
        {
            static_assert(IsPowerOfTwo(kPageSize));
            const uintptr_t page_start = reinterpret_cast<uintptr_t>(ptr) & ~(kPageSize - 1u);
            return reinterpret_cast<PageHeader *>(page_start);
        }

        static void Validate(const PageHeader *page)
        {
            AL_ASSERT(page != nullptr && page->_free_count <= page->_block_count);
            if (page->_state == EPageState::kEmpty) AL_ASSERT(page->_free_count == page->_block_count);
            else if (page->_state == EPageState::kPartial)
                AL_ASSERT(page->_free_count > 0u && page->_free_count < page->_block_count);
            else
                AL_ASSERT(page->_free_count == 0u);
        }
    };

    PageMgr::~PageMgr()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (PageHeader *page: _pages) AlignedFree(page);
        _pages.clear();
    }

    PageHeader *PageMgr::AllocatePage(u32 block_size, Bin *owner_bin)
    {
        void *memory = AlignedAlloc(Page::kPageSize, Page::kPageSize);
        AL_ASSERT(memory != nullptr);

        auto *page = new (memory) PageHeader();
        page->_owner_page_mgr = this;
        page->_owner_bin = owner_bin;
        Page::Initialize(page, block_size);

        std::lock_guard<std::mutex> lock(_mutex);
        _pages.push_back(page);
        return page;
    }

    void PageMgr::DeallocatePage(PageHeader *page)
    {
        if (page == nullptr) return;

        std::lock_guard<std::mutex> lock(_mutex);
        auto it = std::find(_pages.begin(), _pages.end(), page);
        AL_ASSERT(it != _pages.end());
        _pages.erase(it);
        _page_debug_infos.erase(page);
        page->~PageHeader();
        AlignedFree(page);
    }

    String PageMgr::Dump()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        std::ostringstream oss;
        oss << "Page count: " << _pages.size() << '\n';
        for (const PageHeader *page: _pages)
        {
            const auto debug_it = _page_debug_infos.find(const_cast<PageHeader *>(page));
            oss << PtrToAddress(page) << " block_size=" << page->_block_size << " block_count=" << page->_block_count
                << " free_count=" << page->_free_count;
            if (debug_it != _page_debug_infos.end())
            {
                oss << " allocated_bits=[";
                for (u32 i = 0u; i < debug_it->second._allocated_bits.size(); ++i)
                {
                    if (i != 0u) oss << ',';
                    oss << "0x" << std::hex << debug_it->second._allocated_bits[i] << std::dec;
                }
                oss << "] guard_bits=[";
                for (u32 i = 0u; i < debug_it->second._guard_bits.size(); ++i)
                {
                    if (i != 0u) oss << ',';
                    oss << "0x" << std::hex << debug_it->second._guard_bits[i] << std::dec;
                }
                oss << ']';
            }
            oss << '\n';
        }
        return oss.str();
    }

    void PageMgr::InitializePageDebugInfo(PageHeader *page)
    {
        AL_ASSERT(page != nullptr);
        const u32 word_count = GetDebugWordCount(page->_block_count);

        std::lock_guard<std::mutex> lock(_mutex);
        PageDebugInfo &debug_info = _page_debug_infos[page];
        debug_info._page_address = reinterpret_cast<u64>(page);
        debug_info._block_count = page->_block_count;
        debug_info._allocated_bits.assign(word_count, 0u);
        debug_info._guard_bits.assign(word_count, ~0ull);
        if (!debug_info._guard_bits.empty() && page->_block_count % kDebugBitsPerWord != 0u)
        {
            const u32 valid_bit_count = page->_block_count % kDebugBitsPerWord;
            debug_info._guard_bits.back() = (1ull << valid_bit_count) - 1ull;
        }
    }

    void PageMgr::MarkBlockAllocated(PageHeader *page, void *ptr)
    {
        AL_ASSERT(page != nullptr && ptr != nullptr);
        const u32 block_index = GetBlockIndex(page, ptr);

        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _page_debug_infos.find(page);
        AL_ASSERT(it != _page_debug_infos.end());
        AL_ASSERT(!TestBit(it->second._allocated_bits, block_index));
        AL_ASSERT(TestBit(it->second._guard_bits, block_index));
        SetBit(it->second._allocated_bits, block_index);
        ClearBit(it->second._guard_bits, block_index);
    }

    void PageMgr::MarkBlockFreed(PageHeader *page, void *ptr)
    {
        AL_ASSERT(page != nullptr && ptr != nullptr);
        const u32 block_index = GetBlockIndex(page, ptr);

        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _page_debug_infos.find(page);
        AL_ASSERT(it != _page_debug_infos.end());
        AL_ASSERT(TestBit(it->second._allocated_bits, block_index));
        AL_ASSERT(!TestBit(it->second._guard_bits, block_index));
        ClearBit(it->second._allocated_bits, block_index);
        SetBit(it->second._guard_bits, block_index);
    }

    void PageMgr::CaptureGlobalStats(u64 &page_count, u64 &cached_empty_page_count, u64 &reserved_bytes) const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        page_count = static_cast<u64>(_pages.size());
        cached_empty_page_count = 0u;
        for (const PageHeader *page: _pages)
        {
            if (page != nullptr && page->_state == EPageState::kEmpty)
                ++cached_empty_page_count;
        }
        reserved_bytes = page_count * Page::kPageSize;
    }

    void PageMgr::CaptureBinSnapshots(Vector<AllocatorBinSnapshot> &snapshots) const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        snapshots.clear();
        snapshots.resize(s_bin_mapper.GetBinCount());
        for (u32 i = 0u; i < snapshots.size(); ++i)
        {
            snapshots[i]._bin_index = i;
            snapshots[i]._block_size = s_bin_mapper.GetBinSizeByIndex(i);
        }

        for (const PageHeader *page: _pages)
        {
            if (page == nullptr)
                continue;
            const u32 bin_index = s_bin_mapper.GetBinIndex(page->_block_size);
            if (bin_index >= snapshots.size())
                continue;

            AllocatorBinSnapshot &snapshot = snapshots[bin_index];
            if (page->_state == EPageState::kEmpty)
                ++snapshot._empty_page_count;
            else if (page->_state == EPageState::kFull)
                ++snapshot._full_page_count;
            else
                ++snapshot._partial_page_count;

            const u64 used_count = page->_block_count - page->_free_count;
            snapshot._active_block_count += used_count;
            snapshot._free_block_count += page->_free_count;
            snapshot._capacity_bytes += used_count * page->_block_size;
            snapshot._reserved_bytes += Page::kPageSize;
            snapshot._page_addresses.push_back(reinterpret_cast<u64>(page));
        }
    }

    void PageMgr::AccumulateArenaPageStats(Vector<AllocatorArenaSnapshot> &snapshots) const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (const PageHeader *page: _pages)
        {
            if (page == nullptr || page->_owner_arena == nullptr)
                continue;
            const u32 arena_id = page->_owner_arena->ArenaId();
            auto it = std::find_if(snapshots.begin(), snapshots.end(), [arena_id](const AllocatorArenaSnapshot &snapshot)
            {
                return snapshot._arena_id == arena_id;
            });
            if (it == snapshots.end())
                continue;
            ++it->_page_count;
            it->_reserved_bytes += Page::kPageSize;
        }
    }

    bool PageMgr::CapturePageSnapshot(u64 page_address, AllocatorPageSnapshot &snapshot) const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        const auto it = std::find_if(_pages.begin(), _pages.end(), [page_address](const PageHeader *page)
        {
            return reinterpret_cast<u64>(page) == page_address;
        });
        if (it == _pages.end())
            return false;

        const PageHeader *page = *it;
        snapshot = AllocatorPageSnapshot{};
        snapshot._page_address = reinterpret_cast<u64>(page);
        snapshot._block_begin = static_cast<u64>(page->_block_begin);
        snapshot._block_end = static_cast<u64>(page->_block_end);
        snapshot._arena_id = page->_owner_arena != nullptr ? page->_owner_arena->ArenaId() : 0u;
        snapshot._thread_id = page->_owner_arena != nullptr ? page->_owner_arena->ThreadId() : 0u;
        snapshot._bin_index = s_bin_mapper.GetBinIndex(page->_block_size);
        snapshot._block_size = page->_block_size;
        snapshot._block_count = page->_block_count;
        snapshot._free_count = page->_free_count;
        snapshot._used_count = page->_block_count - page->_free_count;
        snapshot._state = ToAllocatorPageState(page->_state);
        snapshot._block_states.assign(page->_block_count, EAllocatorBlockState::kFree);

        const auto debug_it = _page_debug_infos.find(const_cast<PageHeader *>(page));
        if (debug_it != _page_debug_infos.end())
        {
            for (u32 i = 0u; i < page->_block_count; ++i)
            {
                if (TestBit(debug_it->second._allocated_bits, i))
                    snapshot._block_states[i] = EAllocatorBlockState::kUsed;
            }
        }
        return true;
    }

    Bin::Bin(PageMgr *page_mgr, u32 block_size, Arena *arena) : _page_mgr(page_mgr), _block_size(block_size), _arena(arena)
    { AL_ASSERT(_page_mgr != nullptr && _block_size >= sizeof(FreeBlock)); }

    Bin::~Bin()
    {
        ReleasePageList(_partial_pages);
        ReleasePageList(_full_pages);
        ReleasePageList(_empty_pages);
        _empty_page_count = 0u;
    }

    void *Bin::Allocate()
    {
        PageHeader *page = _partial_pages;
        if (page == nullptr && _empty_pages != nullptr)
        {
            page = _empty_pages;
            MovePage(page, _empty_pages, _partial_pages);
            page->_state = EPageState::kPartial;
            --_empty_page_count;
        }
        else if (page == nullptr)
        {
            page = CreatePage();
            PushPage(_partial_pages, page);
            page->_state = EPageState::kPartial;
        }

        void *ptr = Page::Allocate(page);
        if (page->_free_count == 0u)
        {
            MovePage(page, _partial_pages, _full_pages);
            page->_state = EPageState::kFull;
        }
        Page::Validate(page);
        return ptr;
    }

    void Bin::Deallocate(PageHeader *page, void *ptr)
    {
        AL_ASSERT(page != nullptr && page->_owner_bin == this);
        const bool was_full = page->_state == EPageState::kFull;
        Page::Deallocate(page, ptr);

        if (was_full)
        {
            MovePage(page, _full_pages, _partial_pages);
            page->_state = EPageState::kPartial;
        }

        if (page->_free_count == page->_block_count)
        {
            MovePage(page, _partial_pages, _empty_pages);
            page->_state = EPageState::kEmpty;
            ++_empty_page_count;
            TrimEmptyPages();
        }
        else
        {
            Page::Validate(page);
        }
    }

    void Bin::PushPage(PageHeader *&head, PageHeader *page)
    {
        page->_prev = nullptr;
        page->_next = head;
        if (head != nullptr) head->_prev = page;
        head = page;
    }

    void Bin::RemovePage(PageHeader *&head, PageHeader *page)
    {
        if (page->_prev != nullptr) page->_prev->_next = page->_next;
        else
            head = page->_next;
        if (page->_next != nullptr) page->_next->_prev = page->_prev;
        page->_prev = nullptr;
        page->_next = nullptr;
    }

    void Bin::MovePage(PageHeader *page, PageHeader *&from, PageHeader *&to)
    {
        RemovePage(from, page);
        PushPage(to, page);
    }

    PageHeader *Bin::CreatePage()
    {
        auto header = _page_mgr->AllocatePage(_block_size, this);
        header->_owner_arena = _arena;
        return header;
    }

    void Bin::TrimEmptyPages()
    {
        while (_empty_page_count > kMaxCachedEmptyPages) ReleaseEmptyPage(_empty_pages);
    }

    void Bin::ReleaseEmptyPage(PageHeader *page)
    {
        AL_ASSERT(page != nullptr && page->_state == EPageState::kEmpty);
        AL_ASSERT(page->_free_count == page->_block_count);
        RemovePage(_empty_pages, page);
        --_empty_page_count;
        _page_mgr->DeallocatePage(page);
    }

    void Bin::ReleasePageList(PageHeader *&head)
    {
        while (head != nullptr)
        {
            PageHeader *page = head;
            RemovePage(head, page);
            _page_mgr->DeallocatePage(page);
        }
    }

    Arena::Arena(Allocator *allocator) : _allocator(allocator)
    {
        AL_ASSERT(_allocator != nullptr);
        _thread_id = GetCurrentThreadIdValue();
        _bins.reserve(s_bin_mapper.GetBinCount());
        for (u32 i = 0u; i < s_bin_mapper.GetBinCount(); ++i)
            _bins.push_back(new Bin(&_allocator->GetPageMgr(), s_bin_mapper.GetBinSizeByIndex(i), this));
    }

    Arena::~Arena()
    {
        for (Bin *bin: _bins) delete bin;
        _bins.clear();
    }

    void *Arena::Allocate(u64 size, u64 align)
    {
        AL_ASSERT(size <= Allocator::kMaxSmallAllocationSize);
        const u32 bin_index = s_bin_mapper.GetBinIndex(size);
        AL_ASSERT(bin_index != std::numeric_limits<u32>::max());
        const u32 block_size = s_bin_mapper.GetBinSizeByIndex(bin_index);
        AL_ASSERT(block_size >= size);
        return _bins[bin_index]->Allocate();
    }

    void Arena::CaptureSnapshot(AllocatorArenaSnapshot &snapshot) const
    {
        snapshot._arena_id = _arena_id;
        snapshot._thread_id = _thread_id;
        snapshot._thread_name = std::format("Thread {}", _thread_id);
        snapshot._allocation_count = _allocation_count;
        snapshot._free_count = _free_count;
        snapshot._local_free_count = _free_count;
        snapshot._remote_free_count = 0u;
        snapshot._pending_remote_free_count = 0u;
        snapshot._page_count = _page_count;
        snapshot._requested_bytes = _requested_bytes;
        snapshot._reserved_bytes = _reserved_bytes;
        snapshot._peak_requested_bytes = _peak_requested_bytes;
    }

    Allocator::Allocator()
    {
        _events.resize(kMemoryDebugEventCapacity);
    }

    void Allocator::Init()
    {
        if (s_allocator == nullptr) s_allocator = new Allocator();
    }

    void Allocator::Shutdown()
    {
        if (s_allocator == nullptr) return;
        s_allocator->PrintLeaks();
        delete s_allocator;
        s_allocator = nullptr;
        g_thread_arena = nullptr;
    }

    Allocator &Allocator::Get()
    {
        if (s_allocator == nullptr) Init();
        return *s_allocator;
    }

    Arena &Allocator::ThreadArena()
    {
        if (g_thread_arena != nullptr) return *g_thread_arena;

        std::lock_guard<std::mutex> lock(_arena_mutex);
        auto arena = std::make_unique<Arena>(this);
        arena->_arena_id = _next_arena_id++;
        g_thread_arena = arena.get();
        _arenas.push_back(std::move(arena));
        return *g_thread_arena;
    }

    void *Allocator::Allocate(u64 size, const char *file, const char *function, u16 line, u64 align, EMemoryTag tag)
    {
        AL_ASSERT(size > 0u);
        AL_ASSERT(IsPowerOfTwo(align));
        AL_ASSERT(align <= std::numeric_limits<u16>::max());
        AL_ASSERT(size <= std::numeric_limits<u64>::max() - sizeof(AllocHeader) - (align - 1u));

        std::lock_guard<std::mutex> lock(_mutex);
        const u64 allocation_size = size + sizeof(AllocHeader) + align - 1u;
        const bool is_system_allocation = allocation_size > kMaxSmallAllocationSize;
        Arena *arena = is_system_allocation ? nullptr : &ThreadArena();
        const u32 bin_index = is_system_allocation ? kInvalidAllocatorIndex : s_bin_mapper.GetBinIndex(allocation_size);
        void *raw_ptr = is_system_allocation ? AlignedAlloc(std::max<u64>(align, alignof(void *)), allocation_size)
                                             : arena->Allocate(allocation_size, align);
        AL_ASSERT(raw_ptr != nullptr);

        const uintptr_t user_address = AlignAddress(reinterpret_cast<uintptr_t>(raw_ptr) + sizeof(AllocHeader), align);
        auto *header = reinterpret_cast<AllocHeader *>(user_address - sizeof(AllocHeader));
        header->_raw_ptr = raw_ptr;
        header->_requested_size = size;
        header->_actual_size = allocation_size;
        header->_arena_id = arena != nullptr ? arena->ArenaId() : 0u;
        header->_bin_index = bin_index;
        header->_thread_id = GetCurrentThreadIdValue();
        header->_magic = AllocHeader::kMagicAllocated;
        header->_alignment = static_cast<u16>(align);
        header->_flags = is_system_allocation ? AllocHeader::kFlagSystem : 0u;
        header->_tag = tag;

        void *user_ptr = reinterpret_cast<void *>(user_address);
        AL_ASSERT(reinterpret_cast<uintptr_t>(user_ptr) % align == 0u);
        const u64 new_total = _total_allocated.fetch_add(size) + size;
        u64 old_peak = _peak_allocated.load();
        while (new_total > old_peak && !_peak_allocated.compare_exchange_weak(old_peak, new_total)) {}
        ++_total_allocation_count;
        if (is_system_allocation)
        {
            ++_active_system_allocation_count;
            _active_system_reserved_bytes += allocation_size;
        }
        if (arena != nullptr)
        {
            ++arena->_allocation_count;
            arena->_requested_bytes += size;
            arena->_reserved_bytes += allocation_size;
            arena->_peak_requested_bytes = std::max(arena->_peak_requested_bytes, arena->_requested_bytes);
        }
#ifdef _DEBUG
        _allocations[user_ptr] = AllocationInfo{size, allocation_size, header->_arena_id, bin_index, header->_thread_id,
                                                static_cast<u32>(align), file, function, line, is_system_allocation, tag};
#endif
        MemoryDebugEvent event;
        event._sequence = _next_event_sequence++;
        event._time_sec = GetMemoryDebugTimeSec();
        event._type = EMemoryEventType::kAllocate;
        event._thread_id = header->_thread_id;
        event._arena_id = header->_arena_id;
        event._bin_index = bin_index;
        event._address = reinterpret_cast<u64>(user_ptr);
        event._raw_address = reinterpret_cast<u64>(raw_ptr);
        event._requested_size = size;
        event._actual_size = allocation_size;
        event._alignment = static_cast<u32>(align);
        event._file = file;
        event._function = function;
        event._tag = MemoryTagToCString(tag);
        event._line = line;
        RecordMemoryEvent(event);
        return user_ptr;
    }

    void Allocator::Deallocate(void *ptr)
    {
        if (ptr == nullptr) return;

        std::lock_guard<std::mutex> lock(_mutex);
        auto *header = reinterpret_cast<AllocHeader *>(reinterpret_cast<uintptr_t>(ptr) - sizeof(AllocHeader));
        AL_ASSERT(header->_magic == AllocHeader::kMagicAllocated);
        AL_ASSERT(header->_raw_ptr != nullptr);

        void *raw_ptr = header->_raw_ptr;
        const u64 requested_size = header->_requested_size;
        const u64 actual_size = header->_actual_size;
        const bool is_system_allocation = header->IsSystemAllocation();
        const u32 arena_id = header->_arena_id;
        const u32 bin_index = header->_bin_index;
        const u64 thread_id = GetCurrentThreadIdValue();
        const u32 alignment = header->_alignment;
        const EMemoryTag tag = header->_tag;
        header->_magic = AllocHeader::kMagicFreed;

        const char *file = nullptr;
        const char *function = nullptr;
        u32 line = 0u;
#ifdef _DEBUG
        const auto it = _allocations.find(ptr);
        AL_ASSERT(it != _allocations.end());
        file = it->second._file;
        function = it->second._function;
        line = it->second._line;
        _allocations.erase(it);
#endif
        _total_allocated.fetch_sub(requested_size);
        ++_total_free_count;
        if (is_system_allocation)
        {
            --_active_system_allocation_count;
            _active_system_reserved_bytes -= std::min(_active_system_reserved_bytes, actual_size);
        }
        else
        {
            for (auto &arena: _arenas)
            {
                if (arena && arena->ArenaId() == arena_id)
                {
                    ++arena->_free_count;
                    arena->_requested_bytes -= std::min(arena->_requested_bytes, requested_size);
                    arena->_reserved_bytes -= std::min(arena->_reserved_bytes, actual_size);
                    break;
                }
            }
        }

        MemoryDebugEvent event;
        event._sequence = _next_event_sequence++;
        event._time_sec = GetMemoryDebugTimeSec();
        event._type = EMemoryEventType::kFree;
        event._thread_id = thread_id;
        event._arena_id = arena_id;
        event._bin_index = bin_index;
        event._address = reinterpret_cast<u64>(ptr);
        event._raw_address = reinterpret_cast<u64>(raw_ptr);
        event._requested_size = requested_size;
        event._actual_size = actual_size;
        event._alignment = alignment;
        event._file = file;
        event._function = function;
        event._tag = MemoryTagToCString(tag);
        event._line = line;
        RecordMemoryEvent(event);

        if (is_system_allocation)
        {
            AlignedFree(raw_ptr);
            return;
        }

        PageHeader *page = Page::GetPageHeaderFromPointer(raw_ptr);
        AL_ASSERT(Page::Owns(page, raw_ptr));
        AL_ASSERT(page->_owner_bin != nullptr);
        page->_owner_bin->Deallocate(page, raw_ptr);
    }

    void Allocator::PrintLeaks()
    {
#ifdef _DEBUG
        std::lock_guard<std::mutex> lock(_mutex);
        if (_allocations.empty())
        {
            LOG_INFO("No memory leaks detected.");
            return;
        }

        LOG_WARNING("Detected {} memory leak(s).", _allocations.size());
        for (const auto &[ptr, info]: _allocations)
        {
            LOG_WARNING("Leak {} bytes at {}, {}:{} ({})", info._size, PtrToAddress(ptr), info._file, info._line, info._function);
        }
#else
        LOG_INFO("Memory leak tracking is disabled in this build.");
#endif
    }

    void Allocator::PrintAllocatedInfo()
    {
        LOG_INFO("Active allocation bytes: {}", TotalAllocated());
        LOG_INFO("{}", _page_mgr.Dump());
    }

    void Allocator::CaptureGlobalSnapshot(AllocatorGlobalSnapshot &snapshot) const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        snapshot = AllocatorGlobalSnapshot{};
        snapshot._requested_bytes = _total_allocated.load();
        snapshot._peak_requested_bytes = _peak_allocated.load();
        snapshot._active_allocation_count = static_cast<u64>(_allocations.size());
        snapshot._total_allocation_count = _total_allocation_count;
        snapshot._total_free_count = _total_free_count;
        snapshot._system_allocation_count = _active_system_allocation_count;
        snapshot._dropped_event_count = _dropped_event_count;
        _page_mgr.CaptureGlobalStats(snapshot._page_count, snapshot._cached_empty_page_count, snapshot._reserved_bytes);
        snapshot._reserved_bytes += _active_system_reserved_bytes;
        _peak_reserved_bytes = std::max(_peak_reserved_bytes, snapshot._reserved_bytes);
        snapshot._peak_reserved_bytes = _peak_reserved_bytes;
    }

    void Allocator::CaptureBinSnapshots(Vector<AllocatorBinSnapshot> &snapshots) const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _page_mgr.CaptureBinSnapshots(snapshots);
#ifdef _DEBUG
        for (const auto &[ptr, info]: _allocations)
        {
            (void) ptr;
            if (info._bin_index < snapshots.size())
                snapshots[info._bin_index]._requested_bytes += info._size;
        }
#endif
        for (AllocatorBinSnapshot &snapshot: snapshots)
        {
            snapshot._peak_block_count = snapshot._active_block_count;
            snapshot._alloc_count = snapshot._active_block_count;
        }
    }

    void Allocator::CaptureArenaSnapshots(Vector<AllocatorArenaSnapshot> &snapshots) const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        snapshots.clear();
        snapshots.reserve(_arenas.size());
        for (const auto &arena: _arenas)
        {
            if (arena == nullptr)
                continue;
            AllocatorArenaSnapshot snapshot;
            arena->CaptureSnapshot(snapshot);
            snapshots.push_back(snapshot);
        }
        _page_mgr.AccumulateArenaPageStats(snapshots);
    }

    bool Allocator::CapturePageSnapshot(u64 page_address, AllocatorPageSnapshot &snapshot) const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _page_mgr.CapturePageSnapshot(page_address, snapshot);
    }

    void Allocator::CaptureActiveAllocations(Vector<AllocatorAllocationSnapshot> &snapshots) const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        snapshots.clear();
#ifdef _DEBUG
        snapshots.reserve(_allocations.size());
        for (const auto &[ptr, info]: _allocations)
        {
            AllocatorAllocationSnapshot snapshot;
            snapshot._address = reinterpret_cast<u64>(ptr);
            snapshot._requested_size = info._size;
            snapshot._actual_size = info._actual_size;
            snapshot._alignment = info._alignment;
            snapshot._thread_id = info._thread_id;
            snapshot._arena_id = info._arena_id;
            snapshot._bin_index = info._bin_index;
            snapshot._file = info._file;
            snapshot._function = info._function;
            snapshot._tag = MemoryTagToCString(info._tag);
            snapshot._line = info._line;
            snapshots.push_back(snapshot);
        }
#endif
    }

    void Allocator::CaptureRecentEvents(Vector<MemoryDebugEvent> &events, u32 max_count) const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        events.clear();
        const u32 copy_count = std::min(max_count, _event_count);
        events.reserve(copy_count);
        const u32 first_index = (_event_head + kMemoryDebugEventCapacity - copy_count) % kMemoryDebugEventCapacity;
        for (u32 i = 0u; i < copy_count; ++i)
        {
            const u32 event_index = (first_index + i) % kMemoryDebugEventCapacity;
            events.push_back(_events[event_index]);
        }
    }

    void Allocator::ClearMemoryDebugEvents()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _event_head = 0u;
        _event_count = 0u;
        _dropped_event_count = 0u;
    }

    void Allocator::RecordMemoryEvent(const MemoryDebugEvent &event)
    {
        if (_events.empty())
            return;
        if (_event_count == kMemoryDebugEventCapacity)
            ++_dropped_event_count;
        else
            ++_event_count;
        _events[_event_head] = event;
        _event_head = (_event_head + 1u) % kMemoryDebugEventCapacity;
    }
}// namespace Ailu
