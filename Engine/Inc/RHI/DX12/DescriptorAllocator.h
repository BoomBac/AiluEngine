#pragma once
#ifndef __DESCRIPTOR_MGR__
#define __DESCRIPTOR_MGR__
#include "d3dx12.h"
#include "DescriptorAllocation.h"
#include "GlobalMarco.h"
#include <set>

namespace Ailu
{
	class DescriptorAllocatorPage;
	class DescriptorAllocator
	{
	public:
		DescriptorAllocator(D3D12_DESCRIPTOR_HEAP_TYPE type, u32 num_descriptor_per_heap = 256);
		virtual ~DescriptorAllocator();
		DescriptorAllocation Allocate(u32 num = 1u);
		void ReleaseStaleDescriptors(u64 frame_num);
	private:
		using DescriptorHeapPool = Vector<Ref<DescriptorAllocatorPage>>;
		Ref<DescriptorAllocatorPage> CreateAllocatorPage();
		D3D12_DESCRIPTOR_HEAP_TYPE _type;
		u32 _num_descriptor_per_heap;
		DescriptorHeapPool _heap_pool;
		std::set<u64> _available_heaps;
		std::mutex _allocation_mutex;
	};
}

#endif // !DESCRIPTOR_MGR__

