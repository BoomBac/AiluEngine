#include "pch.h"
#include "RHI/DX12/DescriptorAllocator.h"
#include "RHI/DX12/DescriptorAllocatorPage.h"

namespace Ailu
{
	DescriptorAllocator::DescriptorAllocator(D3D12_DESCRIPTOR_HEAP_TYPE type, u32 num_descriptor_per_heap) : _type(type),
		_num_descriptor_per_heap(num_descriptor_per_heap)
	{

	}
	DescriptorAllocator::~DescriptorAllocator()
	{
	}
	DescriptorAllocation DescriptorAllocator::Allocate(u32 num)
	{
		std::lock_guard<std::mutex> lock(_allocation_mutex);
		DescriptorAllocation allocation;
		for (auto iter = _available_heaps.begin(); iter != _available_heaps.end(); ++iter)
		{
			auto allocatorPage = _heap_pool[*iter];
			allocation = allocatorPage->Allocate(num);
			if (allocatorPage->NumFreeHandles() == 0)
			{
				iter = _available_heaps.erase(iter);
			}
			// A valid allocation has been found.
			if (!allocation.IsNull())
			{
				break;
			}
		}
		// No available heap could satisfy the requested number of descriptors.
		if (allocation.IsNull())
		{
			_num_descriptor_per_heap = max(_num_descriptor_per_heap, num);
			auto new_page = CreateAllocatorPage();
			allocation = new_page->Allocate(num);
		}
		return allocation;
	}
	void DescriptorAllocator::ReleaseStaleDescriptors(u64 frame_num)
	{
		std::lock_guard<std::mutex> lock(_allocation_mutex);
		for (size_t i = 0; i < _heap_pool.size(); ++i)
		{
			auto page = _heap_pool[i];
			page->ReleaseStaleDescriptors(frame_num);
			if (page->NumFreeHandles() > 0)
			{
				_available_heaps.insert(i);
			}
		}
	}
	Ref<DescriptorAllocatorPage> DescriptorAllocator::CreateAllocatorPage()
	{
		auto new_page = MakeRef<DescriptorAllocatorPage>(_type, _num_descriptor_per_heap);
		_heap_pool.emplace_back(new_page);
		_available_heaps.insert(_heap_pool.size() - 1);
		return new_page;
	}
}

