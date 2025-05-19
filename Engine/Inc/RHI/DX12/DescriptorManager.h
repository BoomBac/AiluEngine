#pragma once
#ifndef __DESCRIPTOR_MGR__
#define __DESCRIPTOR_MGR__
#include "GlobalMarco.h"
#include <d3dx12.h>
#include <mutex>
/*
高性能的gpu可见堆管理方式为将堆分为两个部分，一个cpu可见，一个gpu可见，前者用于存储实际资源，实际尺寸庞大，可容纳大量的描述符，
后者只用于提交至管线，每次dc/dp之前，根据跟签名的描述符表布局和资源，从前者拷贝描述符到后者，资源可能分散存储在前者（资源可能在不同的堆），按照根前面的布局将其组织成相邻的
描述符range，减少SetDescriptorTable的调用同时也减少heap的切换。
暂时为了简单起见，直接套用cpu的版本的设计，目前也没有使用描述符表的特性，每个表中只有一个描述符（见D3DShader中根签名生成部分）2024.4.20
*/
using Microsoft::WRL::ComPtr;
namespace Ailu::RHI::DX12
{
	#pragma region CPUDescriptorScope
	struct DescriptorPage
	{
	public:
		DescriptorPage(const DescriptorPage& other) = delete;
		DescriptorPage& operator=(const DescriptorPage& other) = delete;
		DescriptorPage(DescriptorPage&& other) noexcept;
		DescriptorPage(u16 id, D3D12_DESCRIPTOR_HEAP_TYPE type, u16 num,D3D12_DESCRIPTOR_HEAP_FLAGS flag);
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
		inline D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(u16 index = 0) const;
		D3D12_CPU_DESCRIPTOR_HANDLE BaseCPUHandle() const { return _heap->GetCPUDescriptorHandleForHeapStart(); }
		D3D12_GPU_DESCRIPTOR_HANDLE BaseGPUHandle() const { return _flag == D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE? _heap->GetGPUDescriptorHandleForHeapStart() : (D3D12_GPU_DESCRIPTOR_HANDLE)0u; }
		const ComPtr<ID3D12DescriptorHeap>& GetHeap() const { return _heap; }
	private:
		u16 _id;
		ComPtr<ID3D12DescriptorHeap> _heap;
		D3D12_DESCRIPTOR_HEAP_TYPE _type;
		D3D12_DESCRIPTOR_HEAP_FLAGS _flag;
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
		CPUVisibleDescriptorAllocation Allocate(D3D12_DESCRIPTOR_HEAP_TYPE type, u16 num = kMaxDescriptorNumPerPage);
		void Free(CPUVisibleDescriptorAllocation&& handle);
		void ReleaseSpace();
	private:
		std::multimap<u16, u16>::iterator AddNewPage(D3D12_DESCRIPTOR_HEAP_TYPE type, u16 num = kMaxDescriptorNumPerPage);
	private:
		Vector<DescriptorPage> _pages;
		//page_available_desc_num,_page_id
		Map<D3D12_DESCRIPTOR_HEAP_TYPE, std::multimap<u16, u16>> _page_free_space_lut;
		std::mutex _mutex;
	};
	#pragma endregion

	#pragma region GPUDescriptorScope
	class D3DCommandBuffer;
	struct GPUVisibleDescriptorAllocation
	{
	public:
		GPUVisibleDescriptorAllocation():
			  _inner_page_offset(0)
			,_descriptor_num(0)
			,_p_page(nullptr)
		{}
		GPUVisibleDescriptorAllocation(u16 inner_page_offset, u16 descriptor_num,DescriptorPage* p_page)
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
		DescriptorPage* Page() const { return _p_page; }
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
		void SetupDescriptorHeap(D3DCommandBuffer* cmd);
	private:
		DescriptorPage* _p_page;
		u16 _inner_page_offset;
		u16 _descriptor_num;
	};
	class GPUVisibleDescriptorAllocator
	{
		friend class D3DDescriptorMgr;
	public:
		inline static const u16 kMaxDescriptorNumPerPage = 1024;
		GPUVisibleDescriptorAllocator();
		~GPUVisibleDescriptorAllocator();
		GPUVisibleDescriptorAllocation Allocate(u16 num = 1u, D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		void Free(GPUVisibleDescriptorAllocation&& handle);
		u32 ReleaseSpace();
		void AllocationInfo(u32& total_num, u32& available_num) const;
		D3D12_GPU_DESCRIPTOR_HANDLE GetBindGpuHandle(const GPUVisibleDescriptorAllocation& alloc);
		std::tuple<D3D12_CPU_DESCRIPTOR_HANDLE,D3D12_GPU_DESCRIPTOR_HANDLE> GetBindHandle(const GPUVisibleDescriptorAllocation& alloc);
		ID3D12DescriptorHeap* GetBindHeap() {return _main_heap.Get();}
	private:
		void CommitDescriptorsForDraw(D3DCommandBuffer* cmd,u16 slot,const GPUVisibleDescriptorAllocation& alloc);
		void CommitDescriptorsForDispatch(D3DCommandBuffer* cmd,u16 slot,const GPUVisibleDescriptorAllocation& alloc);
	private:
		std::multimap<u16, u16>::iterator AddNewPage(D3D12_DESCRIPTOR_HEAP_TYPE type, u16 num = kMaxDescriptorNumPerPage);
		ComPtr<ID3D12DescriptorHeap> _main_heap;
		u16 _desc_size;
		Vector<DescriptorPage> _pages;
		//page_available_desc_num,_page_id
		Map<D3D12_DESCRIPTOR_HEAP_TYPE, std::multimap<u16, u16>> _page_free_space_lut;
		std::mutex _mutex;
		//hash to gpu page index
		HashMap<u32,u16> _desc_lut;
		//Reserved index 0 for imgui 
		u16 _main_page_index = 1u;
		ID3D12Device* _device;
	};

	class D3DDescriptorMgr
	{
	public:
		static void Init();
		static void Shutdown();
		static D3DDescriptorMgr& Get();
		D3DDescriptorMgr();
		~D3DDescriptorMgr();
		GPUVisibleDescriptorAllocation AllocGPU(u16 num = 1u,D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
		{
			return _gpu_alloc->Allocate(num,type);
		};
		CPUVisibleDescriptorAllocation AllocCPU(u16 num,D3D12_DESCRIPTOR_HEAP_TYPE type)
		{
			return _cpu_alloc->Allocate(type,num);
		}
		void Free(GPUVisibleDescriptorAllocation&& alloc) {_gpu_alloc->Free(std::move(alloc));};
		void Free(CPUVisibleDescriptorAllocation&& alloc) {_cpu_alloc->Free(std::move(alloc));};
		u32 ReleaseSpace()
		{
			_cpu_alloc->ReleaseSpace();
			return _gpu_alloc->ReleaseSpace();
		}
		/// @brief 将传入的描述符分配对象拷贝到绑定堆并返回其在绑定堆中的gpu句柄
		/// @param alloc 
		/// @return 
		D3D12_GPU_DESCRIPTOR_HANDLE GetBindGpuHandle(const GPUVisibleDescriptorAllocation& alloc) {return _gpu_alloc->GetBindGpuHandle(alloc);};
		/// @brief 将传入的描述符分配对象拷贝到绑定堆并返回其在绑定堆中的cpu和gpu句柄
		/// @param alloc 
		/// @return 
		std::tuple<D3D12_CPU_DESCRIPTOR_HANDLE,D3D12_GPU_DESCRIPTOR_HANDLE> GetBindHandle(const GPUVisibleDescriptorAllocation& alloc) {return _gpu_alloc->GetBindHandle(alloc);};
		ID3D12DescriptorHeap* GetBindHeap() {return _gpu_alloc->GetBindHeap();}
		void CommitDescriptorsForDraw(D3DCommandBuffer* cmd,u16 slot,const GPUVisibleDescriptorAllocation& alloc) {
			_gpu_alloc->CommitDescriptorsForDraw(cmd,slot,alloc);
		}
		void CommitDescriptorsForDispatch(D3DCommandBuffer* cmd,u16 slot,const GPUVisibleDescriptorAllocation& alloc) {
			_gpu_alloc->CommitDescriptorsForDispatch(cmd,slot,alloc);
		}
	private:
		GPUVisibleDescriptorAllocator* _gpu_alloc;
		CPUVisibleDescriptorAllocator* _cpu_alloc;
	};
	#pragma endregion
}


#endif // !__DESCRIPTOR_MGR__
