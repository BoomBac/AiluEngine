#pragma once
#pragma warning(disable : 4251)
#ifndef __ALLOCATOR__
#define __ALLOCATOR__

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/String.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/Containers/Map.h"
#include "Framework/Common/Assert.h"
#include "Framework/Common/MemoryDebugTypes.h"

#include <atomic>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>

namespace Ailu
{
    enum class EPageState : u8
    {
        kEmpty,
        kPartial,
        kFull,
    };

    struct PageDebugInfo
    {
        u64 _page_address = 0u;
        u32 _block_count = 0u;
        Vector<u64> _allocated_bits;
        Vector<u64> _guard_bits;
    };

    struct PageHeader;
    class Bin;
    class Allocator;

    class AILU_API PageMgr
    {
    public:
        ~PageMgr();

        PageHeader *AllocatePage(u32 block_size, Bin *owner_bin);
        void DeallocatePage(PageHeader *page);
        String Dump();
        void InitializePageDebugInfo(PageHeader *page);
        void MarkBlockAllocated(PageHeader *page, void *ptr);
        void MarkBlockFreed(PageHeader *page, void *ptr);
        void CaptureGlobalStats(u64 &page_count, u64 &cached_empty_page_count, u64 &reserved_bytes) const;
        void CaptureBinSnapshots(Vector<AllocatorBinSnapshot> &snapshots) const;
        void AccumulateArenaPageStats(Vector<AllocatorArenaSnapshot> &snapshots) const;
        bool CapturePageSnapshot(u64 page_address, AllocatorPageSnapshot &snapshot) const;

    private:
        Vector<PageHeader *> _pages;
        HashMap<PageHeader *, PageDebugInfo> _page_debug_infos;
        mutable std::mutex _mutex;
    };
    class Arena;
    class Bin
    {
    public:
        Bin(PageMgr *page_mgr, u32 block_size, Arena *arena);
        ~Bin();

        void *Allocate();
        void Deallocate(PageHeader *page, void *ptr);

        u32 BlockSize() const { return _block_size; }
        PageMgr *GetPageMgr() const { return _page_mgr; }

    private:
        static void PushPage(PageHeader *&head, PageHeader *page);
        static void RemovePage(PageHeader *&head, PageHeader *page);
        static void MovePage(PageHeader *page, PageHeader *&from, PageHeader *&to);

        PageHeader *CreatePage();
        void TrimEmptyPages();
        void ReleaseEmptyPage(PageHeader *page);
        void ReleasePageList(PageHeader *&head);

    private:
        static constexpr u32 kMaxCachedEmptyPages = 2u;

        PageMgr *_page_mgr = nullptr;
        PageHeader *_partial_pages = nullptr;
        PageHeader *_full_pages = nullptr;
        PageHeader *_empty_pages = nullptr;
        u32 _empty_page_count = 0u;
        u32 _block_size = 0u;
        Arena *_arena;
    };

    class Arena
    {
    public:
        explicit Arena(Allocator *allocator);
        ~Arena();

        void *Allocate(u64 size, u64 align);
        u32 ArenaId() const { return _arena_id; }
        u64 ThreadId() const { return _thread_id; }
        void CaptureSnapshot(AllocatorArenaSnapshot &snapshot) const;

    private:
        Allocator *_allocator = nullptr;
        Vector<Bin *> _bins;
        u32 _arena_id = 0u;
        u64 _thread_id = 0u;
        u64 _allocation_count = 0u;
        u64 _free_count = 0u;
        u64 _page_count = 0u;
        u64 _requested_bytes = 0u;
        u64 _reserved_bytes = 0u;
        u64 _peak_requested_bytes = 0u;
        friend class Allocator;
    };

    enum class EMemoryTag : u16
    {
        kDefault,
        kCore,
        kEcs,
        kScene,
        kResource,
        kAsset,
        kRenderer,
        kRenderGraph,
        kSpriteBatch,
        kUi,
        kPhysics,
        kScript,
        kAudio,
        kJobSystem,
        kEditor,
        kTemporary,
        kCount,
    };

    constexpr std::string_view MemoryTagToString(EMemoryTag tag)
    {
        switch (tag)
        {
        case EMemoryTag::kDefault:
            return "Default";
        case EMemoryTag::kCore:
            return "Core";
        case EMemoryTag::kEcs:
            return "ECS";
        case EMemoryTag::kScene:
            return "Scene";
        case EMemoryTag::kResource:
            return "Resource";
        case EMemoryTag::kAsset:
            return "Asset";
        case EMemoryTag::kRenderer:
            return "Renderer";
        case EMemoryTag::kRenderGraph:
            return "RenderGraph";
        case EMemoryTag::kSpriteBatch:
            return "SpriteBatch";
        case EMemoryTag::kUi:
            return "UI";
        case EMemoryTag::kPhysics:
            return "Physics";
        case EMemoryTag::kScript:
            return "Script";
        case EMemoryTag::kAudio:
            return "Audio";
        case EMemoryTag::kJobSystem:
            return "JobSystem";
        case EMemoryTag::kEditor:
            return "Editor";
        case EMemoryTag::kTemporary:
            return "Temporary";
        case EMemoryTag::kCount:
        default:
            return "Invalid";
        }
    }


    class AILU_API Allocator
    {
    public:
        static void Init();
        static void Shutdown();
        static Allocator &Get();

        inline static constexpr u64 kDefaultAlign = 8u;
        static constexpr u32 kMaxSmallAllocationSize = 4095u;

        Arena &ThreadArena();
        void *Allocate(u64 size, const char *file, const char *function, u16 line, u64 align, EMemoryTag tag);
        void Deallocate(void *ptr);
        void PrintLeaks();
        void PrintAllocatedInfo();
        void CaptureGlobalSnapshot(AllocatorGlobalSnapshot &snapshot) const;
        void CaptureBinSnapshots(Vector<AllocatorBinSnapshot> &snapshots) const;
        void CaptureArenaSnapshots(Vector<AllocatorArenaSnapshot> &snapshots) const;
        bool CapturePageSnapshot(u64 page_address, AllocatorPageSnapshot &snapshot) const;
        void CaptureActiveAllocations(Vector<AllocatorAllocationSnapshot> &snapshots) const;
        void CaptureRecentEvents(Vector<MemoryDebugEvent> &events, u32 max_count) const;
        void ClearMemoryDebugEvents();

        u64 TotalAllocated() const { return _total_allocated.load(); }
        PageMgr &GetPageMgr() { return _page_mgr; }

    private:
        Allocator();

        struct AllocHeader
        {
            void *_raw_ptr = nullptr;
            u64 _requested_size = 0u;
            u64 _actual_size = 0u;
            u32 _arena_id = 0u;
            u32 _bin_index = kInvalidAllocatorIndex;
            u64 _thread_id = 0u;
            u32 _magic = 0u;
            EMemoryTag _tag = EMemoryTag::kDefault;
            u16 _alignment = 0u;
            u16 _flags = 0u;

            static constexpr u32 kMagicAllocated = 0xDEADC0DEu;
            static constexpr u32 kMagicFreed = 0xFEEEFEEEu;
            static constexpr u16 kFlagSystem = 1u << 0u;

            bool IsSystemAllocation() const { return (_flags & kFlagSystem) != 0u; }
        };

        struct AllocationInfo
        {
            u64 _size = 0u;
            u64 _actual_size = 0u;
            u32 _arena_id = 0u;
            u32 _bin_index = kInvalidAllocatorIndex;
            u64 _thread_id = 0u;
            u32 _alignment = 0u;
            const char *_file = nullptr;
            const char *_function = nullptr;
            u16 _line = 0u;
            bool _is_sys_alloc = false;
            EMemoryTag _tag = EMemoryTag::kDefault;
        };

        void RecordMemoryEvent(const MemoryDebugEvent &event);

        HashMap<void *, AllocationInfo> _allocations;
        mutable std::mutex _mutex;
        std::atomic<u64> _total_allocated = 0u;
        std::atomic<u64> _peak_allocated = 0u;
        u64 _total_allocation_count = 0u;
        u64 _total_free_count = 0u;
        u64 _active_system_allocation_count = 0u;
        u64 _active_system_reserved_bytes = 0u;
        mutable u64 _peak_reserved_bytes = 0u;
        u64 _dropped_event_count = 0u;
        u64 _next_event_sequence = 1u;
        Vector<MemoryDebugEvent> _events;
        u32 _event_head = 0u;
        u32 _event_count = 0u;
        PageMgr _page_mgr;
        std::mutex _arena_mutex;
        Vector<std::unique_ptr<Arena>> _arenas;
        u32 _next_arena_id = 1u;
    };

    namespace Memory
    {
        template<typename T, typename... Args>
        T *DebugNew(EMemoryTag tag, const char *file, const char *function, u32 line, Args &&...args)
        {
            void *memory = Allocator::Get().Allocate(sizeof(T), file, function, line, alignof(T), tag);
            if (memory == nullptr)
                return nullptr;

    #if defined(__cpp_exceptions)
            try
            {
                return new (memory) T(std::forward<Args>(args)...);
            }
            catch (...)
            {
                Allocator::Get().Deallocate(memory);
                throw;
            }
    #else
            return new (memory) T(std::forward<Args>(args)...);
    #endif
        }

        template<typename T>
        T *DebugAlloc(EMemoryTag tag, const char *file, const char *function, u32 line, u64 count,
                            u64 alignment = Allocator::kDefaultAlign)
        {
            static_assert(!std::is_void_v<T>, "DebugAlloc<void> is not supported.");

            if (count == 0u)
                return nullptr;

            AL_ASSERT(count <= (std::numeric_limits<u64>::max)() / sizeof(T));
            AL_ASSERT(alignment != 0u);
            AL_ASSERT((alignment & (alignment - 1u)) == 0u);
            AL_ASSERT(alignment >= alignof(T));

            const u64 size = count * sizeof(T);
            void *memory = Allocator::Get().Allocate(size, file, function, line, alignment, tag);

            return static_cast<T *>(memory);
        }

        template<typename T>
        void DebugDelete(T *&ptr)
        {
            if (ptr == nullptr)
                return;

            std::destroy_at(ptr);
            Allocator::Get().Deallocate(ptr);
            ptr = nullptr;
        }

        template<typename T>
        void DebugFree(T *&ptr)
        {
            if (ptr == nullptr)
                return;

            Allocator::Get().Deallocate(ptr);
            ptr = nullptr;
        }
    }

#define AL_NEW(type, ...)                                                                                          \
    ::Ailu::Memory::DebugNew<type>(                                                                                 \
        ::Ailu::EMemoryTag::kDefault, __FILE__, __FUNCTION__, __LINE__, ##__VA_ARGS__)

#define AL_NEW_TAG(tag, type, ...)                                                                                 \
    ::Ailu::Memory::DebugNew<type>((tag), __FILE__, __FUNCTION__, __LINE__, ##__VA_ARGS__)

#define AL_DELETE(ptr)                                                                                             \
    ::Ailu::Memory::DebugDelete(ptr)

#define AL_ALLOC(type, count)                                                                                      \
    ::Ailu::Memory::DebugAlloc<type>(                                                                               \
        ::Ailu::EMemoryTag::kDefault, __FILE__, __FUNCTION__, __LINE__, (count), alignof(type))

#define AL_ALLOC_TAG(tag, type, count)                                                                             \
    ::Ailu::Memory::DebugAlloc<type>((tag), __FILE__, __FUNCTION__, __LINE__, (count), alignof(type))

#define AL_ALIGN_ALLOC(type, count, alignment)                                                                     \
    ::Ailu::Memory::DebugAlloc<type>(                                                                               \
        ::Ailu::EMemoryTag::kDefault, __FILE__, __FUNCTION__, __LINE__, (count), (alignment))

#define AL_ALIGN_ALLOC_TAG(tag, type, count, alignment)                                                            \
    ::Ailu::Memory::DebugAlloc<type>((tag), __FILE__, __FUNCTION__, __LINE__, (count), (alignment))

#define AL_FREE(ptr)                                                                                               \
    ::Ailu::Memory::DebugFree(ptr)

}// namespace Ailu
#endif// __ALLOCATOR__
