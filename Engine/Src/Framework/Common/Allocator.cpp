
#include "Framework/Common/Allocator.hpp"
#include "Framework/Common/Log.h"
#include "Framework/Math/ALMath.hpp"
#include "pch.h"

#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>
#include <ranges>

namespace DebugAlloc
{
    constexpr uint32_t kGuardPattern = 0xDEADBEEF;
    constexpr size_t kGuardSize = sizeof(kGuardPattern);

    struct Header
    {
        size_t original_size;
        size_t aligned_size;
        size_t alignment;
        void* raw_ptr;
    };

    void* DebugAlignedAlloc(size_t size, size_t alignment)
    {
        size_t total_size = sizeof(Header) + kGuardSize + size + kGuardSize + alignment;
        void* raw = std::malloc(total_size);
        if (!raw) return nullptr;

        uintptr_t raw_addr = reinterpret_cast<uintptr_t>(raw);
        uintptr_t aligned_start = (raw_addr + sizeof(Header) + kGuardSize + alignment - 1) & ~(alignment - 1);
        void* user_ptr = reinterpret_cast<void*>(aligned_start);

        Header* header = reinterpret_cast<Header*>(aligned_start - kGuardSize - sizeof(Header));
        header->original_size = size;
        header->aligned_size = total_size;
        header->alignment = alignment;
        header->raw_ptr = raw;

        // Write guard patterns
        std::memcpy(reinterpret_cast<void*>(aligned_start - kGuardSize), &kGuardPattern, kGuardSize);
        std::memcpy(reinterpret_cast<void*>(aligned_start + size), &kGuardPattern, kGuardSize);

        return user_ptr;
    }

    void CheckMemoryCorruption(void* ptr)
    {
        if (!ptr) return;

        uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
        Header* header = reinterpret_cast<Header*>(addr - kGuardSize - sizeof(Header));

        uint32_t front_guard = *reinterpret_cast<uint32_t*>(addr - kGuardSize);
        uint32_t back_guard  = *reinterpret_cast<uint32_t*>(addr + header->original_size);

        if (front_guard != kGuardPattern)
        {
            std::fprintf(stderr, "[DebugAlloc] Memory corruption before block at %p!\n", ptr);
            assert(false);
        }

        if (back_guard != kGuardPattern)
        {
            std::fprintf(stderr, "[DebugAlloc] Memory corruption after block at %p!\n", ptr);
            assert(false);
        }
    }

    void DebugAlignedFree(void* ptr)
    {
        if (!ptr) return;
        CheckMemoryCorruption(ptr);

        uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
        Header* header = reinterpret_cast<Header*>(addr - kGuardSize - sizeof(Header));
        std::free(header->raw_ptr);
    }
}

static void *AlignedAlloc(size_t alignment, size_t size)
{
#if defined(_MSC_VER)
    return _aligned_malloc(size, alignment);
    //return DebugAlloc::DebugAlignedAlloc(size, alignment);
#else
    return std::aligned_alloc(alignment, size);// C++17
#endif
}

static void AlignedFree(void *ptr)
{
#if defined(_MSC_VER)
    //DebugAlloc::DebugAlignedFree(ptr);
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}

namespace Ailu
{
    namespace
    {
        class BinMapper
        {
        public:
            BinMapper()
            {
                u32 bin_index = 0;
                u32 bin_size = 8;// 最小块大小
                u32 next_threshold = 128;

                for (u32 size = 0; size < _size_to_bin.size(); ++size)
                {
                    // 进入新 bin 时记录实际 bin_size
                    while (size > bin_size)
                    {
                        if (bin_size < next_threshold)
                            bin_size *= 2;// 8, 16, 32, 64, 128
                        else if (bin_size < 512)
                            bin_size += 32;// 160, 192, ...
                        else
                            bin_size += 64;// 576, 640, ...

                        ++bin_index;
                        _bin_size_list.push_back(bin_size);
                    }

                    // 保证第一个 bin_size 被加入
                    if (_bin_size_list.empty())
                        _bin_size_list.push_back(bin_size);

                    _size_to_bin[size] = static_cast<u8>(bin_index);
                }
            }

            // 从大小查 bin 索引
            u32 GetBinIndex(u64 size) const
            {
                if (size >= _size_to_bin.size())
                    return 0xFF;
                return static_cast<u32>(_size_to_bin[size]);
            }

            // 从 bin 索引查实际大小
            u32 GetBinSizeByIndex(u32 bin_index) const
            {
                if (bin_index >= _bin_size_list.size())
                    return 0;
                return _bin_size_list[bin_index];
            }

            // bin 总数
            u32 GetBinCount() const
            {
                return static_cast<u32>(_bin_size_list.size());
            }

        private:
            Array<u8, 4096> _size_to_bin;
            Vector<u32> _bin_size_list;
        };

        bool IsPowerOfTwo(u64 value)
        {
            return (value & (value - 1)) == 0;
        }

        template<typename T>
        std::string PtrToAddress(T *ptr)
        {
            std::ostringstream oss;
            oss << "0x"
                << std::hex << std::setw(sizeof(void *) * 2) << std::setfill('0')
                << reinterpret_cast<uintptr_t>(ptr);
            return oss.str();
        }
    }// namespace
    namespace Core
    {
        static Arena *s_thread_arena;
        static BinMapper s_bin_mapper;
#pragma region Page
        Page::Page(u32 block_size, Bin *bin)
        {
            void *page_mem = AlignedAlloc(kPageSize, kPageSize);
            AL_ASSERT(page_mem != nullptr);
            _head = new (page_mem) PageHeader;
            //AL_ASSERT(reinterpret_cast<uintptr_t>(_head) % block_size == 0);
            _head->_page_index = s_page_index++;
            _head->_bin = bin;
            _head->_block_size = block_size;
            _head->_aligned_head_size = static_cast<u32>(AlignTo(kPageHeaderSize, block_size));
            _head->_block_count = (kPageSize - _head->_aligned_head_size) / block_size;
            _head->_used_count.store(0, std::memory_order_relaxed);
            _head->_next = nullptr;
            _head->_prev = nullptr;
            _head->_thread_name = GetThreadName().c_str();
        }
        Page::~Page()
        {
            LOG_INFO("Destory page with data: {}", PtrToAddress(_head))
            AlignedFree(reinterpret_cast<void *>(_head));
        }
        String Page::Dump() const
        {
            std::stringstream ss;
            ss << "Page:"
               << "  Page Index:" << _head->_page_index
               << "  Block Size:" << _head->_block_size
               << "  Aligned Head Size:" << _head->_aligned_head_size
               << "  Block Count:" << _head->_block_count
               << "  Used Count:" << _head->_used_count.load(std::memory_order_relaxed)
               << "  Next:" << (_head->_next != nullptr ? PtrToAddress(_head->_next) : "nullptr")
               << "  Prev:" << (_head->_prev != nullptr ? PtrToAddress(_head->_prev) : "nullptr") << std::endl;
            return ss.str();
        }

#pragma endregion

#pragma region Arena
        Arena::Arena(Allocator *allocator) : _allocator(_allocator)
        {
            for (u16 i = 0; i < s_bin_mapper.GetBinCount(); i++)
            {
                _bins.push_back(new Bin(s_bin_mapper.GetBinSizeByIndex(i)));
            }
        }
        Arena::~Arena()
        {
            for (auto &bin: _bins)
                DESTORY_PTR(bin);
        }
        void *Arena::Allocate(u64 size, u64 align)
        {
            u32 bin_index = s_bin_mapper.GetBinIndex(size);
            u32 bin_size = s_bin_mapper.GetBinSizeByIndex(bin_index);
            AL_ASSERT(bin_size % (u32) align == 0);
            return _bins[bin_index]->Allocate(size);
        }
        void Arena::Deallocate(void *ptr)
        {
            auto head = Page::GetPageHeaderFromPointer(ptr);
            head->_bin->Deallocate(ptr);
        }

#pragma endregion

#pragma region PageMgr
        PageMgr::~PageMgr()
        {
            _pages.clear();
        }
        Page *PageMgr::AlloctatePage(u64 block_size, Bin *bin)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_pages.empty())
            {
                _pages.emplace_back((u32) block_size, bin);
            }
            else
            {
                Page &cur_back = _pages.back();
                Page &new_page = _pages.emplace_back((u32) block_size, bin);
                cur_back._head->_next = new_page._head;
                new_page._head->_prev = cur_back._head;
            }
            auto *page_header = _pages.back()._head;
            LOG_INFO("[PageMgr::AlloctatePage]: Allocated new page(index: {},addr: {}) with block_size: {},current page num {}", page_header->_page_index, PtrToAddress(page_header), block_size, _pages.size());

            return &_pages.back();
        }
        void PageMgr::DeallocatePage(PageHeader *header)
        {
            if (!header)
                return;
            std::lock_guard<std::mutex> lock(_mutex);
            if (_pages.empty())
                return;
            LOG_INFO("[PageMgr::DeallocatePage]: Deallocated page(index: {}),current page num {}", header->_page_index, _pages.size());
            auto it = std::find_if(_pages.begin(), _pages.end(), [header](const Page &p)
                                        { return p._head == header; 
                                        });
            if (it != _pages.end())
                _pages.erase(it);
        }
        String PageMgr::Dump()
        {
            std::stringstream ss;
            ss << "Page Manager Dump:" << std::endl;
            for (auto &p: _pages)
            {
                ss << "Page: " << p.Dump() << std::endl;
            }
            return ss.str();
        }
#pragma endregion

#pragma region Bin
        void *Bin::Allocate(u64 size)
        {
            if (size > _block_size)
                return nullptr;
            if (_free_list != nullptr)
            {
                void *result = _free_list;
                auto *head = Page::GetPageHeaderFromPointer(result);
                ++head->_used_count;
                _free_list = _free_list->_next;
                _used_bytes_ratio += static_cast<f32>(size) / _block_size;
                _used_bytes_ratio *= 0.5f;
                //AL_ASSERT(reinterpret_cast<uintptr_t>(_free_list) != 0xdeadc0de00000048);
                //LOG_INFO("[Bin::Allocate]: Allocated block(size: {}),current used count {},next is {}",size,head->_used_count.load(),PtrToAddress(_free_list));
                return result;
            }
            Page *page = Allocator::Get().GetPageMgr().AlloctatePage(_block_size, this);
            u8 *block_data = page->Data();
            for (size_t i = 1; i < page->_head->_block_count; ++i)
            {
                auto *block = reinterpret_cast<Block *>(block_data + i * _block_size);
                block->_next = _free_list;
                _free_list = block;
            }
            _used_bytes_ratio = static_cast<f32>(size) / _block_size;
            ++page->_head->_used_count;
            _pages.emplace_back(page->_head);
            return block_data;
        }
        void Bin::Deallocate(void *ptr)
        {
            if (ptr == nullptr)
                return;
            Block *free_block = reinterpret_cast<Block *>(ptr);
            free_block->_next = _free_list;
            _free_list = free_block;
            auto *head = Page::GetPageHeaderFromPointer(ptr);
            --head->_used_count;
            if (head->_used_count == 0)
            {
                if (++_empty_page_count >= 4)
                {
                    auto it = std::ranges::remove_if(_pages.begin(), _pages.end(), [head](PageHeader *page) { return page->_used_count == 0u && page != head; });
                    for(auto itt = it.begin(); itt != it.end();itt++)
                    {
                        Allocator::Get().GetPageMgr().DeallocatePage(*itt);
                        LOG_INFO("Bin::Deallocate: ptr({})", PtrToAddress(*itt));
                    }
                    _pages.erase(it.begin(),it.end());
                    _empty_page_count = 1u;
                }
            }
        }
#pragma endregion

#pragma region Allocator
        static Allocator *g_Allocator;
        void Allocator::Init()
        {
            if (!g_Allocator)
            {
                g_Allocator = new Allocator();
            }
        }
        void Allocator::Shutdown()
        {
            g_Allocator->PrintLeaks();
            g_Allocator->_allocations.clear();
            g_Allocator->_arenas.clear();
            DESTORY_PTR(g_Allocator);
        }
        Allocator &Allocator::Get()
        {
            if (!g_Allocator)
                Init();
            return *g_Allocator;
        }
        Arena &Allocator::ThreadArena()
        {
            if (!s_thread_arena)
            {
                std::lock_guard lock(_arena_mutex);
                s_thread_arena = new Arena(this);
                _arenas.push_back(std::unique_ptr<Arena>(s_thread_arena));
            }
            return *s_thread_arena;
        }
        void *Allocator::Allocate(u64 size, const char *file, const char *function, u16 line, u64 align)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            void *data;
            bool is_sys_alloc = false;
            size += kAllocHeaderSize;
            if (size <= kMaxBinSize)
            {
                data = ThreadArena().Allocate(size, align);
            }
            else
            {
                data = AlignedAlloc(align,size);
                is_sys_alloc = true;
            }
            if (data)
            {
                //makesure data is aligned
                AL_ASSERT_MSG(reinterpret_cast<uintptr_t>(data) % align == 0, "Allocator::Allocate: data is not aligned, size: {}, align: {}, ptr: {}", size, align, PtrToAddress(data));
                AllocHeader *header = reinterpret_cast<AllocHeader *>(data);
                header->_size_and_flags = AllocHeader::PackAllocSize((u32) size, is_sys_alloc);
                header->_magic = 0xDEADC0DE;
                _total_allocated.fetch_add(size);
#ifdef _DEBUG
                if (!_allocations.contains(data))
                {
                    _allocations[data] = AllocationInfo{size, file, function, line};
                }
                else
                    AL_ASSERT(false);
#endif//_DEBUG
                return reinterpret_cast<void *>(header + 1);
            }
            else
                AL_ASSERT(false);
            return nullptr;
        }

        void Allocator::Deallocate(void *ptr)
        {
            if (!ptr) return;
            std::lock_guard<std::mutex> lock(_mutex);
            auto *header = reinterpret_cast<AllocHeader *>(ptr) - 1;
            AL_ASSERT(header->_magic == 0xDEADC0DE);
#ifdef _DEBUG
            void* key = reinterpret_cast<void *>(header);
            //auto alloc = _allocations[key];
            if (!_allocations.contains(key))
            {
                AL_ASSERT(false);
            }
            else
                _allocations.erase(key);
#endif//_DEBUG
            _total_allocated.fetch_sub(AllocHeader::UnpackAllocSize(header->_size_and_flags));
            if (AllocHeader::IsSysAlloc(header->_size_and_flags))
                AlignedFree(header);
            else
                ThreadArena().Deallocate(header);
        }


        void Allocator::PrintAllocatedInfo()
        {
            LOG_INFO(_page_mgr.Dump());
        }

        void Allocator::PrintLeaks()
        {
            std::lock_guard lock(_mutex);
            if (_allocations.empty())
            {
                LOG_INFO("Allocator: No memory leaks detected");
            }
            else
            {
                LOG_WARNING("Allocator Detected memory leaks:")
                for (const auto &[ptr, info]: _allocations)
                {
                    LOG_INFO("Leak at {},size: {},allocated at {}:{} ({})", ptr, info._size, info._file, info._line, info._function)
                }
            }
        }
    }// namespace Core

#pragma endregion
};// namespace Ailu
