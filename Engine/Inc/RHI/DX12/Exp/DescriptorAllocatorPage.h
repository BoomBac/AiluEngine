//#pragma once
//#ifndef __DESCRIPTOR_ALLOCATOR_PAGE_H__
//#define __DESCRIPTOR_ALLOCATOR_PAGE_H__
//#include "DescriptorAllocation.h"
//#include "GlobalMarco.h"
////#include <d3d12.h>
////#include <wrl.h>
//#include <map>
////#include <memory>
////#include <mutex>
////#include <queue>
//namespace Ailu
//{
//	class DescriptorAllocatorPage : public std::enable_shared_from_this<DescriptorAllocatorPage>
//	{
//	public:
//		DescriptorAllocatorPage(D3D12_DESCRIPTOR_HEAP_TYPE type, u32 numDescriptors);
//		D3D12_DESCRIPTOR_HEAP_TYPE GetHeapType() const { return m_HeapType; };
//		/**
//		 * Check to see if this descriptor page has a contiguous block of descriptors
//		 * large enough to satisfy the request.
//		 */
//		bool HasSpace(u32 num_required);
//		/**
//		 * Get the number of available handles in the heap.
//		 */
//		u32 NumFreeHandles() const { return m_NumFreeHandles; };
//		/**
//		 * Allocate a number of descriptors from this descriptor heap.
//		 * If the allocation cannot be satisfied, then a NULL descriptor
//		 * is returned.
//		 */
//		DescriptorAllocation Allocate(u32 num);
//		/**
//
//		 * Return a descriptor back to the heap.
//		 * @param frameNumber Stale descriptors are not freed directly, but put
//		 * on a stale allocations queue. Stale allocations are returned to the heap
//		 * using the DescriptorAllocatorPage::ReleaseStaleAllocations method.
//		 */
//		void Free(DescriptorAllocation&& descriptor, u64 frame_num);
//		/**
//		 * Returned the stale descriptors back to the descriptor heap.
//		 */
//		void ReleaseStaleDescriptors(u64 frame_num);
//	protected:
//		// Compute the offset of the descriptor handle from the start of the heap.
//		u32 ComputeOffset(D3D12_CPU_DESCRIPTOR_HANDLE handle);
//		// Adds a new block to the free list.
//		void AddNewBlock(u32 offset, u32 num);
//		// Free a block of descriptors.
//		// This will also merge free blocks in the free list to form larger blocks
//		// that can be reused.
//		void FreeBlock(u32 offset, u32 num);
//	private:
//		// The offset (in descriptors) within the descriptor heap.
//		using OffsetType = u32;
//		// The number of descriptors that are available.
//		using SizeType = u32;
//		struct FreeBlockInfo;
//		// A map that lists the free blocks by the offset within the descriptor heap.
//		using FreeListByOffset = std::map<OffsetType, FreeBlockInfo>;
//		// A map that lists the free blocks by size.
//		// Needs to be a multimap since multiple blocks can have the same size.
//		using FreeListBySize = std::multimap<SizeType, FreeListByOffset::iterator>;
//		struct FreeBlockInfo
//		{
//			FreeBlockInfo(SizeType size)
//				: Size(size)
//			{}
//			SizeType Size;
//			FreeListBySize::iterator FreeListBySizeIt;
//		};
//		struct StaleDescriptorInfo
//		{
//			StaleDescriptorInfo(OffsetType offset, SizeType size, u64 frame)
//				: Offset(offset)
//				, Size(size)
//				, FrameNumber(frame)
//			{}
//
//			// The offset within the descriptor heap.
//			OffsetType Offset;
//			// The number of descriptors
//			SizeType Size;
//			// The frame number that the descriptor was freed.就是在多少帧之前一直被gpu引用，起着一个状态追踪的作用
//			u64 FrameNumber;
//		};
//		// Stale descriptors are queued for release until the frame that they were freed
//		// has completed.
//		using StaleDescriptorQueue = std::queue<StaleDescriptorInfo>;
//
//		FreeListByOffset m_FreeListByOffset;
//		FreeListBySize m_FreeListBySize;
//		StaleDescriptorQueue m_StaleDescriptors;
//
//		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_d3d12DescriptorHeap;
//		D3D12_DESCRIPTOR_HEAP_TYPE m_HeapType;
//		CD3DX12_CPU_DESCRIPTOR_HANDLE m_BaseDescriptor;
//		u32 m_DescriptorHandleIncrementSize;
//		u32 m_NumDescriptorsInHeap;
//		u32 m_NumFreeHandles;
//
//		std::mutex m_AllocationMutex;
//	};
//}
//
//#endif // !__DESCRIPTOR_ALLOCATOR_PAGE_H__
