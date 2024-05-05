#pragma once
#ifndef __GPUDESCRIPTOR_MGR__
#define __GPUDESCRIPTOR_MGR__
#include "GlobalMarco.h"
#include <d3dx12.h>
/*
高性能的gpu可见堆管理方式为将堆分为两个部分，一个cpu可见，一个gpu可见，前者用于存储实际资源，实际尺寸庞大，可容纳大量的描述符，
后者只用于提交至管线，每次dc/dp之前，根据跟签名的描述符表布局和资源，从前者拷贝描述符到后者，资源可能分散存储在前者（资源可能在不同的堆），按照根前面的布局将其组织成相邻的
描述符range，减少SetDescriptorTable的调用同时也减少heap的切换。
暂时为了简单起见，直接套用cpu的版本的设计，目前也没有使用描述符表的特性，每个表中只有一个描述符（见D3DShader中根签名生成部分）2024.4.20
*/
using Microsoft::WRL::ComPtr;
namespace Ailu
{
	struct GPUVisibleDescriptorPage
	{
	public:
		GPUVisibleDescriptorPage(const GPUVisibleDescriptorPage& other) = delete;
		GPUVisibleDescriptorPage& operator=(const GPUVisibleDescriptorPage& other) = delete;
		GPUVisibleDescriptorPage(GPUVisibleDescriptorPage&& other) noexcept;
		GPUVisibleDescriptorPage(u16 id, D3D12_DESCRIPTOR_HEAP_TYPE type, u16 num);
		i16 Allocate(u16 num);
		void Free(u16 offset, u16 num, u64 frame_count);
		u16 ReleaseAllStaleBlock(u64 frame_count);
		bool HasSpace(u16 num);
		u16 AvailableDescriptorNum() const { return _available_num; }
		u16 DescriptorSize() const { return _desc_size; }
		u16 PageID() const { return _id; }
		D3D12_DESCRIPTOR_HEAP_TYPE PageType() const { return _type; }
		std::tuple<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> GetDescriptorHandle(u16 index = 0) const;
		inline D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(u16 index = 0) const;
		const ComPtr<ID3D12DescriptorHeap>& GetHeap() const { return _heap; }
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
	class D3DCommandBuffer;
	struct GPUVisibleDescriptorAllocation
	{
	public:
		GPUVisibleDescriptorAllocation():
			  _inner_page_offset(0)
			,_descriptor_num(0)
			,_p_page(nullptr)
		{}
		GPUVisibleDescriptorAllocation(u16 inner_page_offset, u16 descriptor_num,GPUVisibleDescriptorPage* p_page)
			: _inner_page_offset(inner_page_offset)
			, _descriptor_num(descriptor_num)
			, _p_page(p_page)
		{}
		GPUVisibleDescriptorAllocation(const GPUVisibleDescriptorAllocation& other) = delete;
		GPUVisibleDescriptorAllocation& operator=(const GPUVisibleDescriptorAllocation& other) = delete;
		GPUVisibleDescriptorAllocation(GPUVisibleDescriptorAllocation&& other) noexcept
		{
			_inner_page_offset = other._inner_page_offset;
			_descriptor_num = other._descriptor_num;
			_p_page = other._p_page;
			other._inner_page_offset = 0;
			other._descriptor_num = 0;
			other._p_page = nullptr;
		}
		GPUVisibleDescriptorAllocation& operator=(GPUVisibleDescriptorAllocation&& other) noexcept
		{
			_inner_page_offset = other._inner_page_offset;
			_descriptor_num = other._descriptor_num;
			_p_page = other._p_page;
			other._inner_page_offset = 0;
			other._descriptor_num = 0;
			other._p_page = nullptr;
			return *this;
		}
		u16 DescriptorSize() const { return _p_page->DescriptorSize(); }
		u16 PageID() const { return _p_page->PageID(); }
		GPUVisibleDescriptorPage* Page() const { return _p_page; }
		u16 PageOffset() const { return _inner_page_offset; }
		u16 DescriptorNum() const { return _descriptor_num; }
		std::tuple<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> At(u16 index) const
		{
			return _p_page->GetDescriptorHandle(_inner_page_offset + index);
		}
		void CommitDescriptorsForDraw(D3DCommandBuffer* cmd,u16 slot,u16 start = 0u);
		void CommitDescriptorsForDispatch(D3DCommandBuffer* cmd, u16 slot,u16 start = 0u);
		/// <summary>
		/// make sure heap setup correctly when use gpu handle directly,such as ImGui::Texture()
		/// </summary>
		/// <param name="cmd"></param>
		inline void SetupDescriptorHeap(D3DCommandBuffer* cmd);
	private:
		GPUVisibleDescriptorPage* _p_page;
		u16 _inner_page_offset;
		u16 _descriptor_num;
	};
	class GPUVisibleDescriptorAllocator
	{
	public:
		inline static const u16 kMaxDescriptorNumPerPage = 1024;
		GPUVisibleDescriptorAllocation&& Allocate(u16 num = 1u, D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		void Free(GPUVisibleDescriptorAllocation&& handle);
		void ReleaseSpace();
		void AllocationInfo(u32& total_num, u32& available_num) const;
	private:
		HashMap<u16, u16>::iterator AddNewPage(D3D12_DESCRIPTOR_HEAP_TYPE type, u16 num = kMaxDescriptorNumPerPage);
	private:
		Vector<GPUVisibleDescriptorPage> _pages;
		//page_available_desc_num,page_id
		Map<D3D12_DESCRIPTOR_HEAP_TYPE, HashMap<u16, u16>> _page_free_space_lut;
	};
	extern GPUVisibleDescriptorAllocator* g_pGPUDescriptorAllocator;
}


#endif // !__GPUDESCRIPTOR_MGR__
