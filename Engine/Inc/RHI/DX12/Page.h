//
// Created by 22292 on 2024/10/27.
//

#ifndef AILU_PAGE_H
#define AILU_PAGE_H

#include "GlobalMarco.h"

namespace Ailu
{
    class Page
    {
    public:
        DISALLOW_COPY_AND_ASSIGN(Page)
        Page(u16 id, u64 size);
        Page(Page&& other) noexcept;
        i64 Allocate(u64 size);
        void Free(u64 offset, u64 size, u64 frame_count);
        u16 ReleaseAllStaleBlock(u64 frame_count);
        bool HasSpace(u64 size);
        [[nodiscard]] u64 AvailableSize() const { return _available_num; }
        [[nodiscard]] u16 PageID() const { return _id; }

    protected:
        u16 _id;
        u64 _available_num;
        u64 _size;
        using OffsetType = u64;
        using SizeType = u64;
        struct FreeBlockInfo;
        using FreeListByOffset = std::map<OffsetType, FreeBlockInfo>;
        using FreeListBySize = std::multimap<SizeType, FreeListByOffset::iterator>;
        struct FreeBlockInfo
        {
            explicit FreeBlockInfo(SizeType size)
                : _size(size)
            {}
            SizeType _size;
            FreeListBySize::iterator _it_by_size;
        };
        FreeListByOffset _lut_free_block_by_offset;
        FreeListBySize _lut_free_block_by_size;
        struct StaleFreeBlock
        {
            OffsetType _offset;
            SizeType _size;
            u64 _frame_count;
            StaleFreeBlock(OffsetType offset, SizeType size, u64 frame_count)
                :_offset(offset)
                  , _size(size)
                  , _frame_count(frame_count)
            {}
        };
        Queue<StaleFreeBlock> _stale_blocks;
    private:
        void AddNewBlock(OffsetType offset, SizeType size);
        void FreeBlock(OffsetType offset, SizeType size);
        //List<FreeBlock> _free_blocks;
    };
}

#endif//AILU_PAGE_H
