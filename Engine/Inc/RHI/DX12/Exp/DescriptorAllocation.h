//#pragma once
//#ifndef __DESCRIPTOR_ALLOCATION__
//#define __DESCRIPTOR_ALLOCATION__
//#include "GlobalMarco.h"
//#include "d3dx12.h"
//
//namespace Ailu
//{
//	class DescriptorAllocatorPage;
//	//CPU Handle包装器
//	class DescriptorAllocation
//	{
//	public:
//		DescriptorAllocation();
//		DescriptorAllocation(D3D12_CPU_DESCRIPTOR_HANDLE handle, u32 num, u32 desc_size, Ref<DescriptorAllocatorPage> page);
//		~DescriptorAllocation();
//
//		// Copies are not allowed.
//		DescriptorAllocation(const DescriptorAllocation&) = delete;
//		DescriptorAllocation& operator=(const DescriptorAllocation&) = delete;
//
//		// Move is allowed.
//		DescriptorAllocation(DescriptorAllocation&& allocation);
//		DescriptorAllocation& operator=(DescriptorAllocation&& other);
//		// Check if this a valid descriptor.
//		bool IsNull() const { return m_Descriptor.ptr == 0; };
//		// Get a descriptor at a particular offset in the allocation.
//		D3D12_CPU_DESCRIPTOR_HANDLE GetDescriptorHandle(u32 offset = 0) const;
//		// Get the number of (consecutive) handles for this allocation.
//		u32 GetNumHandles() const { return m_NumHandles; };
//		// Get the heap that this allocation came from.
//		// (For internal use only).
//		Ref<DescriptorAllocatorPage> GetDescriptorAllocatorPage() const { return m_Page; };
//	private:
//		// Free the descriptor back to the heap it came from.
//		void Free();
//		// The base descriptor.
//		D3D12_CPU_DESCRIPTOR_HANDLE m_Descriptor;
//		// The number of descriptors in this allocation.
//		u32 m_NumHandles;
//		// The offset to the next descriptor.
//		u32 m_DescriptorSize;
//		// A pointer back to the original page where this allocation came from.
//		Ref<DescriptorAllocatorPage> m_Page;
//	};
//}
//
//#endif // !__DESCRIPTOR_ALLOCATION__
//
