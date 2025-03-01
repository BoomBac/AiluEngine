#pragma once
#ifndef __CPUDESCRIPTOR_MGR__
#define __CPUDESCRIPTOR_MGR__
#include "GlobalMarco.h"
#include <d3dx12.h>

using Microsoft::WRL::ComPtr;
namespace Ailu
{
	struct CPUVisibleDescriptorPage
	{
	public:
		CPUVisibleDescriptorPage(const CPUVisibleDescriptorPage& other) = delete;
		CPUVisibleDescriptorPage& operator=(const CPUVisibleDescriptorPage& other) = delete;
		CPUVisibleDescriptorPage(CPUVisibleDescriptorPage&& other) noexcept;
		CPUVisibleDescriptorPage(u16 id, D3D12_DESCRIPTOR_HEAP_TYPE type, u16 num);
		i16 Allocate(u16 num);
		void Free(u16 offset, u16 num, u64 frame_count);
		u16 ReleaseAllStaleBlock(u64 frame_count);
		bool HasSpace(u16 num);
		u16 AvailableDescriptorNum() const { return _available_num; }
		u16 DescriptorSize() const { return _desc_size; }
		u16 PageID() const { return _id; }
		D3D12_DESCRIPTOR_HEAP_TYPE PageType() const { return _type; }
		D3D12_CPU_DESCRIPTOR_HANDLE BaseHandle() const { return _heap->GetCPUDescriptorHandleForHeapStart(); }
	private:
		u16 _id;
		ComPtr<ID3D12DescriptorHeap> _heap;
		D3D12_DESCRIPTOR_HEAP_TYPE _type;
		u16 _available_num;
		u16 _desc_size;
		using OffsetType = u16;
		using SizeType = u16;
		struct FreeBlockInfo;
		using FreeListByOffset = std::map<OffsetType, FreeBlockInfo>;
		using FreeListBySize = std::multimap<SizeType, FreeListByOffset::iterator>;
		struct FreeBlockInfo
		{
			FreeBlockInfo(SizeType size)
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
	struct CPUVisibleDescriptorAllocation
	{
	public:
		CPUVisibleDescriptorAllocation()
			:_page_id(0)
			, _inner_page_offset(0)
			, _descriptor_num(0)
			, _descriptor_size(0)
			, _page_base_handle()
		{}
		CPUVisibleDescriptorAllocation(u16 page_id, u16 inner_page_offset, u16 descriptor_num, u16 descriptor_size, D3D12_CPU_DESCRIPTOR_HANDLE page_base_handle)
			:_page_id(page_id)
			, _inner_page_offset(inner_page_offset)
			, _descriptor_num(descriptor_num)
			, _descriptor_size(descriptor_size)
			, _page_base_handle(page_base_handle)
		{}
		CPUVisibleDescriptorAllocation(const CPUVisibleDescriptorAllocation& other) = delete;
		CPUVisibleDescriptorAllocation& operator=(const CPUVisibleDescriptorAllocation& other) = delete;
		CPUVisibleDescriptorAllocation(CPUVisibleDescriptorAllocation&& other) noexcept
		{
			_page_id = other._page_id;
			_inner_page_offset = other._inner_page_offset;
			_descriptor_num = other._descriptor_num;
			_descriptor_size = other._descriptor_size;
			_page_base_handle = other._page_base_handle;
			other._page_id = 0;
			other._inner_page_offset = 0;
			other._descriptor_num = 0;
			other._descriptor_size = 0;
			other._page_base_handle = D3D12_CPU_DESCRIPTOR_HANDLE();
		}
		CPUVisibleDescriptorAllocation& operator=(CPUVisibleDescriptorAllocation&& other) noexcept
		{
			_page_id = other._page_id;
			_inner_page_offset = other._inner_page_offset;
			_descriptor_num = other._descriptor_num;
			_descriptor_size = other._descriptor_size;
			_page_base_handle = other._page_base_handle;
			other._page_id = 0;
			other._inner_page_offset = 0;
			other._descriptor_num = 0;
			other._descriptor_size = 0;
			other._page_base_handle = D3D12_CPU_DESCRIPTOR_HANDLE();
			return *this;
		}
		u16 DescriptorSize() const { return _descriptor_size; }
		u16 PageID() const { return _page_id; }
		u16 PageOffset() const { return _inner_page_offset; }
		u16 DescriptorNum() const { return _descriptor_num; }
		D3D12_CPU_DESCRIPTOR_HANDLE At(u16 index) const
		{
			D3D12_CPU_DESCRIPTOR_HANDLE handle{};
			handle.ptr = _page_base_handle.ptr + (_inner_page_offset + index) * _descriptor_size;
			return handle;
		}
	private:
		u16 _descriptor_size;
		u16 _page_id;
		u16 _inner_page_offset;
		u16 _descriptor_num;
		D3D12_CPU_DESCRIPTOR_HANDLE _page_base_handle;
	};
	class CPUVisibleDescriptorAllocator
	{
	public:
		inline static const u16 kMaxDescriptorNumPerPage = 1024;
		CPUVisibleDescriptorAllocation&& Allocate(D3D12_DESCRIPTOR_HEAP_TYPE type, u16 num = kMaxDescriptorNumPerPage);
		void Free(CPUVisibleDescriptorAllocation&& handle);
		void ReleaseSpace();
	private:
		std::multimap<u16, u16>::iterator AddNewPage(D3D12_DESCRIPTOR_HEAP_TYPE type, u16 num = kMaxDescriptorNumPerPage);
	private:
		Vector<CPUVisibleDescriptorPage> _pages;
		//page_available_desc_num,_page_id
		Map<D3D12_DESCRIPTOR_HEAP_TYPE, std::multimap<u16, u16>> _page_free_space_lut;
	};
	extern CPUVisibleDescriptorAllocator* g_pCPUDescriptorAllocator;
}


#endif // !__CPUDESCRIPTOR_MGR__
