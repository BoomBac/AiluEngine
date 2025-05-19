//
// Created by 22292 on 2024/10/27.
//
#include "RHI/DX12//Page.h"
namespace Ailu::RHI::DX12
{
    Page::Page(u16 id, u64 size) : _id(id), _size(size),_available_num(size)
    {
        AddNewBlock(0, size);
    }
    Page::Page(Page &&other) noexcept
    {
        _id = other._id;
        _available_num = other._available_num;
        _size = other._size;
        _lut_free_block_by_offset = std::move(other._lut_free_block_by_offset);
        _lut_free_block_by_size = std::move(other._lut_free_block_by_size);
        _stale_blocks = std::move(other._stale_blocks);
        other._id = (u16) -1;
        other._available_num = 0;
        other._size = 0;
    }
    bool Page::HasSpace(u64 num)
    {
        return _lut_free_block_by_size.lower_bound(num) != _lut_free_block_by_size.end();
    }

    i64 Page::Allocate(u64 num)
    {
        if (num > _available_num)
            return -1;
        auto size_it = _lut_free_block_by_size.lower_bound(num);
        if (size_it != _lut_free_block_by_size.end())
        {
            auto &offset_it = size_it->second;
            auto &free_block_info = offset_it->second;
            u64 new_block_offset = offset_it->first;
            u64 new_block_size = free_block_info._size;
            u64 ret_offset = new_block_offset;
            _lut_free_block_by_offset.erase(offset_it);
            _lut_free_block_by_size.erase(size_it);
            new_block_offset += num;
            new_block_size -= num;
            auto new_offset_it = _lut_free_block_by_offset.emplace(new_block_offset, new_block_size);
            auto new_size_it = _lut_free_block_by_size.emplace(new_block_size, new_offset_it.first);
            new_offset_it.first->second._it_by_size = new_size_it;
            _available_num -= num;
            return ret_offset;
        }
        return -1;
    }

    void Page::Free(u64 offset, u64 num, u64 frame_count)
    {
        _stale_blocks.emplace(offset, num, frame_count);
    }

    u16 Page::ReleaseAllStaleBlock(u64 frame_count)
    {
        u16 num_released = 0;
        while (!_stale_blocks.empty() && _stale_blocks.front()._frame_count <= frame_count)
        {
            auto &stale_block = _stale_blocks.front();
            auto offset = stale_block._offset;
            auto numDescriptors = stale_block._size;
            FreeBlock(offset, numDescriptors);
            num_released += numDescriptors;
            _stale_blocks.pop();
        }
        return num_released;
    }

    void Page::AddNewBlock(OffsetType offset, SizeType size)
    {
        auto new_offset_it = _lut_free_block_by_offset.emplace(offset, size);
        auto new_size_it = _lut_free_block_by_size.emplace(size, new_offset_it.first);
        new_offset_it.first->second._it_by_size = new_size_it;
    }

    void Page::FreeBlock(OffsetType offset, SizeType size)
    {
        //当释放一个块时，一共有四种情况：代码显式处理了一二两种情况，三四同时也会被处理
        //情况 1： 在释放列表中，有一个区块紧跟在被释放的区块之前。
        //情况 2：空闲列表中有一个程序块紧跟在被释放程序块之后。
        //情况 3：空闲列表中既有紧接在被释放数据块之前的数据块，也有紧接在被释放数据块之后的数据块。
        //情况 4：空闲列表中既没有紧接在被释放数据块之前的数据块，也没有紧接在被释放数据块之后的数据块。
        // Find the first element whose offset is greater than the specified offset.
        // This is the block that should appear after the block that is being freed.
        auto nextBlockIt = _lut_free_block_by_offset.upper_bound(offset);

        // Find the block that appears before the block being freed.
        auto prevBlockIt = nextBlockIt;
        // If it's not the first block in the list.
        if (prevBlockIt != _lut_free_block_by_offset.begin())
        {
            // Go to the previous block in the list.
            --prevBlockIt;
        }
        else
        {
            // Otherwise, just set it to the end of the list to indicate that no
            // block comes before the one being freed.
            prevBlockIt = _lut_free_block_by_offset.end();
        }

        // Add the number of free handles back to the heap.
        // This needs to be done before merging any blocks since merging
        // blocks modifies the numDescriptors variable.
        _available_num += size;
        //前一个块存在，并且前一个空闲块和当前需要释放的块相邻
        if (prevBlockIt != _lut_free_block_by_offset.end() && offset == prevBlockIt->first + prevBlockIt->second._size)
        {
            // The previous block is exactly behind the block that is to be freed.
            //
            // PrevBlock.Offset           Offset
            // |                          |
            // |<-----PrevBlock.Size----->|<------Size-------->|
            //

            // Increase the block size by the size of merging with the previous block.
            offset = prevBlockIt->first;
            size += prevBlockIt->second._size;

            // Remove the previous block from the free list.
            _lut_free_block_by_size.erase(prevBlockIt->second._it_by_size);
            _lut_free_block_by_offset.erase(prevBlockIt);
        }
        //后一个块存在，并且当时释放块与其相邻
        if (nextBlockIt != _lut_free_block_by_offset.end() && offset + size == nextBlockIt->first)
        {
            // The next block is exactly in front of the block that is to be freed.
            //
            // Offset               NextBlock.Offset
            // |                    |
            // |<------Size-------->|<-----NextBlock.Size----->|

            // Increase the block size by the size of merging with the next block.
            size += nextBlockIt->second._size;

            // Remove the next block from the free list.
            _lut_free_block_by_size.erase(nextBlockIt->second._it_by_size);
            _lut_free_block_by_offset.erase(nextBlockIt);
        }
        // Add the freed block to the free list.
        AddNewBlock(offset, size);
    }

}// namespace Ailu
