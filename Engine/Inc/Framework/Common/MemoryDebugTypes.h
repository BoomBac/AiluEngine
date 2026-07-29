#pragma once

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/String.h"
#include "Framework/Core/Containers/Vector.h"

namespace Ailu
{
    inline constexpr u32 kInvalidAllocatorIndex = 0xffffffffu;
    inline constexpr u32 kMemoryDebugEventCapacity = 50000u;
    inline constexpr u32 kMemoryDebugTimelineCapacity = 600u;

    enum class EAllocatorPageState : u8
    {
        kEmpty,
        kPartial,
        kFull,
    };

    enum class EAllocatorBlockState : u8
    {
        kFree,
        kUsed,
        kPendingRemoteFree,
        kCorrupted,
    };

    enum class EMemoryEventType : u8
    {
        kAllocate,
        kFree,
        kRemoteFree,
        kPageCreate,
        kPageRelease,
        kInvalidFree,
        kDoubleFree,
        kGuardCorruption,
        kLeak,
    };

    struct AllocatorGlobalSnapshot
    {
        u64 _requested_bytes = 0u;
        u64 _reserved_bytes = 0u;
        u64 _peak_requested_bytes = 0u;
        u64 _peak_reserved_bytes = 0u;
        u64 _active_allocation_count = 0u;
        u64 _total_allocation_count = 0u;
        u64 _total_free_count = 0u;
        u64 _page_count = 0u;
        u64 _cached_empty_page_count = 0u;
        u64 _system_allocation_count = 0u;
        u64 _double_free_count = 0u;
        u64 _invalid_free_count = 0u;
        u64 _guard_corruption_count = 0u;
        u64 _dropped_event_count = 0u;
    };

    struct AllocatorBinSnapshot
    {
        u32 _bin_index = 0u;
        u32 _block_size = 0u;
        u32 _partial_page_count = 0u;
        u32 _full_page_count = 0u;
        u32 _empty_page_count = 0u;
        u64 _active_block_count = 0u;
        u64 _free_block_count = 0u;
        u64 _peak_block_count = 0u;
        u64 _requested_bytes = 0u;
        u64 _capacity_bytes = 0u;
        u64 _reserved_bytes = 0u;
        u64 _alloc_count = 0u;
        u64 _free_count = 0u;
        u64 _remote_free_count = 0u;
        u64 _pending_remote_free_count = 0u;
        Vector<u64> _page_addresses;
    };

    struct AllocatorPageSnapshot
    {
        u64 _page_address = 0u;
        u64 _block_begin = 0u;
        u64 _block_end = 0u;
        u32 _arena_id = 0u;
        u64 _thread_id = 0u;
        u32 _bin_index = 0u;
        u32 _block_size = 0u;
        u32 _block_count = 0u;
        u32 _used_count = 0u;
        u32 _free_count = 0u;
        u32 _pending_remote_free_count = 0u;
        EAllocatorPageState _state = EAllocatorPageState::kEmpty;
        Vector<EAllocatorBlockState> _block_states;
    };

    struct AllocatorArenaSnapshot
    {
        u32 _arena_id = 0u;
        u64 _thread_id = 0u;
        String _thread_name;
        u64 _allocation_count = 0u;
        u64 _free_count = 0u;
        u64 _local_free_count = 0u;
        u64 _remote_free_count = 0u;
        u64 _pending_remote_free_count = 0u;
        u64 _page_count = 0u;
        u64 _requested_bytes = 0u;
        u64 _reserved_bytes = 0u;
        u64 _peak_requested_bytes = 0u;
    };

    struct AllocatorAllocationSnapshot
    {
        u64 _address = 0u;
        u64 _requested_size = 0u;
        u64 _actual_size = 0u;
        u32 _alignment = 0u;
        u64 _thread_id = 0u;
        u32 _arena_id = 0u;
        u32 _bin_index = 0u;
        const char *_file = nullptr;
        const char *_function = nullptr;
        const char *_tag = nullptr;
        u32 _line = 0u;
    };

    struct MemoryDebugEvent
    {
        u64 _sequence = 0u;
        f64 _time_sec = 0.0;
        EMemoryEventType _type = EMemoryEventType::kAllocate;
        u64 _thread_id = 0u;
        u32 _arena_id = 0u;
        u32 _bin_index = 0u;
        u64 _address = 0u;
        u64 _raw_address = 0u;
        u64 _requested_size = 0u;
        u64 _actual_size = 0u;
        u32 _alignment = 0u;
        const char *_file = nullptr;
        const char *_function = nullptr;
        const char *_tag = nullptr;
        u32 _line = 0u;
    };

    struct MemoryTimelineSample
    {
        f64 _time_sec = 0.0;
        u64 _requested_bytes = 0u;
        u64 _reserved_bytes = 0u;
        u64 _active_allocations = 0u;
        u64 _allocation_rate = 0u;
        u64 _free_rate = 0u;
    };

    struct AllocatorSnapshot
    {
        u64 _snapshot_id = 0u;
        f64 _time_sec = 0.0;
        AllocatorGlobalSnapshot _global;
        Vector<AllocatorBinSnapshot> _bins;
        Vector<AllocatorArenaSnapshot> _arenas;
        Vector<AllocatorAllocationSnapshot> _allocations;
    };
}
