#include "pch.h"
#include "Framework/Common/Application.h"
#include "RHI/DX12/GPUDescriptorManager.h"
#include "RHI/DX12/D3DContext.h"
#include "RHI/DX12/dxhelper.h"
#include "RHI/DX12/D3DCommandBuffer.h"

namespace Ailu
{

	//-----------------------------------------------------------GPUVisibleDescriptorPage-----------------------------------------------------------------
	GPUVisibleDescriptorPage::GPUVisibleDescriptorPage(u16 id, D3D12_DESCRIPTOR_HEAP_TYPE type, u16 num)
		: _id(id), _type(type), _available_num(num)
	{
		auto p_device = D3DContext::Get()->GetDevice();
		_desc_size = p_device->GetDescriptorHandleIncrementSize(type);
		D3D12_DESCRIPTOR_HEAP_DESC desc{};
		desc.NumDescriptors = num;
		desc.Type = type;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		ThrowIfFailed(p_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&_heap)));
		AddNewBlock(0, num);
		_heap->SetName(std::format(L"GPUDescriptorHead_{}",id).c_str());
	}
	GPUVisibleDescriptorPage::GPUVisibleDescriptorPage(GPUVisibleDescriptorPage&& other) noexcept
	{
		_id = other._id;
		_type = other._type;
		_available_num = other._available_num;
		_desc_size = other._desc_size;
		_heap = other._heap;
		_lut_free_block_by_offset = std::move(other._lut_free_block_by_offset);
		_lut_free_block_by_size = std::move(other._lut_free_block_by_size);
		_stale_blocks = std::move(other._stale_blocks);
		other._heap = nullptr;
	}
	bool GPUVisibleDescriptorPage::HasSpace(u16 num)
	{
		return _lut_free_block_by_size.lower_bound(num) != _lut_free_block_by_size.end();
	}

	std::tuple<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> GPUVisibleDescriptorPage::GetDescriptorHandle(u16 index) const
	{
		D3D12_CPU_DESCRIPTOR_HANDLE ch{};
		D3D12_GPU_DESCRIPTOR_HANDLE gh{};
		ch.ptr = _heap->GetCPUDescriptorHandleForHeapStart().ptr + index * _desc_size;
		gh.ptr = _heap->GetGPUDescriptorHandleForHeapStart().ptr + index * _desc_size;
		return std::make_tuple(ch,gh);
	}

	D3D12_GPU_DESCRIPTOR_HANDLE GPUVisibleDescriptorPage::GetGPUDescriptorHandle(u16 index) const
	{
		D3D12_GPU_DESCRIPTOR_HANDLE gh{};
		gh.ptr = _heap->GetGPUDescriptorHandleForHeapStart().ptr + index * _desc_size;
		return gh;
	}

	i16 GPUVisibleDescriptorPage::Allocate(u16 num)
	{
		if (num > _available_num)
			return -1;
		auto size_it = _lut_free_block_by_size.lower_bound(num);
		if (size_it != _lut_free_block_by_size.end())
		{
			auto& offset_it = size_it->second;
			auto& free_block_info = offset_it->second;
			u16 new_block_offset = offset_it->first;
			u16 new_block_size = free_block_info._size;
			u16 ret_offset = new_block_offset;
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

	void GPUVisibleDescriptorPage::Free(u16 offset, u16 num, u64 frame_count)
	{
		_stale_blocks.push(StaleFreeBlock(offset, num, frame_count));
	}

	u16 GPUVisibleDescriptorPage::ReleaseAllStaleBlock(u64 frame_count)
	{
		u16 num_released = 0;
		while (!_stale_blocks.empty() && _stale_blocks.front()._frame_count <= frame_count)
		{
			auto& stale_block = _stale_blocks.front();
			auto offset = stale_block._offset;
			auto numDescriptors = stale_block._size;
			FreeBlock(offset, numDescriptors);
			num_released += numDescriptors;
			_stale_blocks.pop();
		}
		return num_released;
	}

	void GPUVisibleDescriptorPage::AddNewBlock(OffsetType offset, SizeType size)
	{
		auto new_offset_it = _lut_free_block_by_offset.emplace(offset, size);
		auto new_size_it = _lut_free_block_by_size.emplace(size, new_offset_it.first);
		new_offset_it.first->second._it_by_size = new_size_it;
	}

	void GPUVisibleDescriptorPage::FreeBlock(OffsetType offset, SizeType size)
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

	//-----------------------------------------------------------GPUVisibleDescriptorPage-----------------------------------------------------------------


	GPUVisibleDescriptorAllocation&& GPUVisibleDescriptorAllocator::Allocate(u16 num, D3D12_DESCRIPTOR_HEAP_TYPE type)
	{
		static bool s_first_alloc = true;
		if (s_first_alloc)
		{
			//防止vec扩容后，原有的allocation中存储的page指针失效
			_pages.reserve(10);
			s_first_alloc = false;
		}
		AL_ASSERT_MSG(num >= kMaxDescriptorNumPerPage, "GPUVisibleDescriptorAllocator bad alloc: num to large!");
		if (!_page_free_space_lut.contains(type))
		{
			_page_free_space_lut[type] = HashMap<u16, u16>{};
		}
		auto page_it = _page_free_space_lut[type].lower_bound(num);
		if (page_it == _page_free_space_lut[type].end())
		{
			page_it = AddNewPage(type);
		}
		auto& page = _pages[page_it->second];
		i16 offset = page.Allocate(num);
		if (offset != -1)
		{
			_page_free_space_lut[type].erase(page_it);
			_page_free_space_lut[type].emplace(std::make_pair(page.AvailableDescriptorNum(), page.PageID()));
			return GPUVisibleDescriptorAllocation(offset,num,&page);
		}
		return GPUVisibleDescriptorAllocation();
	}
	void GPUVisibleDescriptorAllocator::Free(GPUVisibleDescriptorAllocation&& handle)
	{
		if(handle.Page())
			_pages[handle.PageID()].Free(handle.PageOffset(), handle.DescriptorNum(), Application::s_frame_count);
	}

	void GPUVisibleDescriptorAllocator::ReleaseSpace()
	{
		_page_free_space_lut.clear();
		for (auto& page : _pages)
		{
			u16 release_num = page.ReleaseAllStaleBlock(Application::s_frame_count);
			_page_free_space_lut[page.PageType()].emplace(std::make_pair(page.AvailableDescriptorNum(), page.PageID()));
			g_pLogMgr->LogFormat("Release GPUVisibleDescripto at page{} with num {}.", page.PageID(), release_num);
		}
	}

	void GPUVisibleDescriptorAllocator::AllocationInfo(u32& total_num, u32& available_num) const
	{
		for (auto& page : _pages)
		{
			available_num += page.AvailableDescriptorNum();
		}
		total_num = static_cast<u32>(_pages.size()) * kMaxDescriptorNumPerPage;
	}

	HashMap<u16, u16>::iterator GPUVisibleDescriptorAllocator::AddNewPage(D3D12_DESCRIPTOR_HEAP_TYPE type, u16 num)
	{
		GPUVisibleDescriptorPage new_page(static_cast<u16>(_pages.size()), type, num);
		_pages.emplace_back(std::move(new_page));
		return _page_free_space_lut[type].emplace(std::make_pair(num, static_cast<u16>(_pages.size() - 1)));
	}

	void GPUVisibleDescriptorAllocation::CommitDescriptorsForDraw(D3DCommandBuffer* cmd, u16 slot, u16 start)
	{
		SetupDescriptorHeap(cmd);
		cmd->GetCmdList()->SetGraphicsRootDescriptorTable(slot,_p_page->GetGPUDescriptorHandle(_inner_page_offset + start));
	}

	void GPUVisibleDescriptorAllocation::CommitDescriptorsForDispatch(D3DCommandBuffer* cmd, u16 slot, u16 start)
	{
		SetupDescriptorHeap(cmd);
		cmd->GetCmdList()->SetComputeRootDescriptorTable(slot, _p_page->GetGPUDescriptorHandle(_inner_page_offset + start));
	}

	void GPUVisibleDescriptorAllocation::SetupDescriptorHeap(D3DCommandBuffer* cmd)
	{
		auto cmd_heap_id = cmd->GetDescriptorHeapId();
		if (cmd_heap_id != _p_page->PageID())
		{
			cmd->GetCmdList()->SetDescriptorHeaps(1, _p_page->GetHeap().GetAddressOf());
			cmd->SetDescriptorHeapId(_p_page->PageID());
		}
	}
}
