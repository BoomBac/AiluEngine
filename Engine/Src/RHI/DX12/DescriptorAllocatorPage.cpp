#include "pch.h"
#include "d3dx12.h"
#include "RHI/DX12/DescriptorAllocatorPage.h"
#include "RHI/DX12/dxhelper.h"
#include "RHI/DX12/D3DContext.h"

namespace Ailu
{
    DescriptorAllocatorPage::DescriptorAllocatorPage(D3D12_DESCRIPTOR_HEAP_TYPE type, u32 numDescriptors)
        : m_HeapType(type)
        , m_NumDescriptorsInHeap(numDescriptors)
    {
        auto device = D3DContext::GetInstance()->GetDevice();
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.Type = m_HeapType;
        heapDesc.NumDescriptors = m_NumDescriptorsInHeap;
        ThrowIfFailed(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_d3d12DescriptorHeap)));
        m_BaseDescriptor = m_d3d12DescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        m_DescriptorHandleIncrementSize = device->GetDescriptorHandleIncrementSize(m_HeapType);
        m_NumFreeHandles = m_NumDescriptorsInHeap;
        // Initialize the free lists
        AddNewBlock(0, m_NumFreeHandles);
    }

    bool DescriptorAllocatorPage::HasSpace(u32 num_required)
    {
        return m_FreeListBySize.lower_bound(num_required) != m_FreeListBySize.end();
    }
    DescriptorAllocation DescriptorAllocatorPage::Allocate(u32 num)
    {
        std::lock_guard<std::mutex> lock(m_AllocationMutex);
        // There are less than the requested number of descriptors left in the heap.
        // Return a NULL descriptor and try another heap.
        if (num > m_NumFreeHandles)
        {
            return DescriptorAllocation();
        }
        // Get the first block that is large enough to satisfy the request.
        auto smallestBlockIt = m_FreeListBySize.lower_bound(num);
        if (smallestBlockIt == m_FreeListBySize.end())
        {
            // There was no free block that could satisfy the request.
            return DescriptorAllocation();
        }
        //满足分配大小最小块中的描述符个数
        auto blockSize = smallestBlockIt->first;
        // The pointer to the same entry in the FreeListByOffset map.
        auto offsetIt = smallestBlockIt->second;
        // The offset in the descriptor heap.
        int offset = offsetIt->first;
        // 从两个map中移除对应的迭代器，也就是分配
        m_FreeListBySize.erase(smallestBlockIt);
        m_FreeListByOffset.erase(offsetIt);
        //当该块被分配之后，计算其剩余的块偏移和大小，并将其加到两个map中
        auto newOffset = offset + num;
        auto newSize = blockSize - num;
        if (newSize > 0)
        {
            // If the allocation didn't exactly match the requested size,
            // return the left-over to the free list.
            AddNewBlock(newOffset, newSize);
        }
        // Decrement free handles.
        m_NumFreeHandles -= num;
        return DescriptorAllocation(CD3DX12_CPU_DESCRIPTOR_HANDLE{ m_BaseDescriptor, offset, m_DescriptorHandleIncrementSize },num, m_DescriptorHandleIncrementSize, shared_from_this());
    }
    
    void DescriptorAllocatorPage::Free(DescriptorAllocation&& descriptor, u64 frame_num)
    {
        // Compute the offset of the descriptor within the descriptor heap.
        auto offset = ComputeOffset(descriptor.GetDescriptorHandle());

        std::lock_guard<std::mutex> lock(m_AllocationMutex);

        // Don't add the block directly to the free list until the frame has completed.
        m_StaleDescriptors.emplace(offset, descriptor.GetNumHandles(), frame_num);
    }

    void DescriptorAllocatorPage::ReleaseStaleDescriptors(u64 frame_num)
    {
        std::lock_guard<std::mutex> lock(m_AllocationMutex);

        while (!m_StaleDescriptors.empty() && m_StaleDescriptors.front().FrameNumber <= frame_num)
        {
            auto& staleDescriptor = m_StaleDescriptors.front();

            // The offset of the descriptor in the heap.
            auto offset = staleDescriptor.Offset;
            // The number of descriptors that were allocated.
            auto numDescriptors = staleDescriptor.Size;

            FreeBlock(offset, numDescriptors);

            m_StaleDescriptors.pop();
        }
    }

    u32 DescriptorAllocatorPage::ComputeOffset(D3D12_CPU_DESCRIPTOR_HANDLE handle)
    {
        return static_cast<u32>(handle.ptr - m_BaseDescriptor.ptr) / m_DescriptorHandleIncrementSize;
    }

    void DescriptorAllocatorPage::AddNewBlock(u32 offset, u32 num)
    {
        auto offsetIt = m_FreeListByOffset.emplace(offset, num);
        auto sizeIt = m_FreeListBySize.emplace(num, offsetIt.first);
        offsetIt.first->second.FreeListBySizeIt = sizeIt;
    }
    void DescriptorAllocatorPage::FreeBlock(u32 offset, u32 num)
    {
        //当释放一个块时，一共有四种情况：代码显式处理了一二两种情况，三四同时也会被处理
        //情况 1： 在释放列表中，有一个区块紧跟在被释放的区块之前。
        //情况 2：空闲列表中有一个程序块紧跟在被释放程序块之后。
        //情况 3：空闲列表中既有紧接在被释放数据块之前的数据块，也有紧接在被释放数据块之后的数据块。
        //情况 4：空闲列表中既没有紧接在被释放数据块之前的数据块，也没有紧接在被释放数据块之后的数据块。
        // Find the first element whose offset is greater than the specified offset.
        // This is the block that should appear after the block that is being freed.
        auto nextBlockIt = m_FreeListByOffset.upper_bound(offset);

        // Find the block that appears before the block being freed.
        auto prevBlockIt = nextBlockIt;
        // If it's not the first block in the list.
        if (prevBlockIt != m_FreeListByOffset.begin())
        {
            // Go to the previous block in the list.
            --prevBlockIt;
        }
        else
        {
            // Otherwise, just set it to the end of the list to indicate that no
            // block comes before the one being freed.
            prevBlockIt = m_FreeListByOffset.end();
        }

        // Add the number of free handles back to the heap.
        // This needs to be done before merging any blocks since merging
        // blocks modifies the numDescriptors variable.
        m_NumFreeHandles += num;
        //前一个块存在，并且前一个空闲块和当前需要释放的块相邻
        if (prevBlockIt != m_FreeListByOffset.end() && offset == prevBlockIt->first + prevBlockIt->second.Size)
        {
            // The previous block is exactly behind the block that is to be freed.
            //
            // PrevBlock.Offset           Offset
            // |                          |
            // |<-----PrevBlock.Size----->|<------Size-------->|
            //

            // Increase the block size by the size of merging with the previous block.
            offset = prevBlockIt->first;
            num += prevBlockIt->second.Size;

            // Remove the previous block from the free list.
            m_FreeListBySize.erase(prevBlockIt->second.FreeListBySizeIt);
            m_FreeListByOffset.erase(prevBlockIt);
        }
        //后一个块存在，并且当时释放块与其相邻
        if (nextBlockIt != m_FreeListByOffset.end() && offset + num == nextBlockIt->first)
        {
            // The next block is exactly in front of the block that is to be freed.
            //
            // Offset               NextBlock.Offset 
            // |                    |
            // |<------Size-------->|<-----NextBlock.Size----->|

            // Increase the block size by the size of merging with the next block.
            num += nextBlockIt->second.Size;

            // Remove the next block from the free list.
            m_FreeListBySize.erase(nextBlockIt->second.FreeListBySizeIt);
            m_FreeListByOffset.erase(nextBlockIt);
        }
        // Add the freed block to the free list.
        AddNewBlock(offset, num);
    }
}
