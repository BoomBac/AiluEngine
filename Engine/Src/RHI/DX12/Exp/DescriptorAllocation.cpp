//#include "pch.h"
//#include "RHI/DX12/DescriptorAllocation.h"
//#include "RHI/DX12/DescriptorAllocatorPage.h"
//#include "Framework/Common/Application.h"
//
//namespace Ailu
//{
//	DescriptorAllocation::DescriptorAllocation()
//		: m_Descriptor{ 0 }
//		, m_NumHandles(0)
//		, m_DescriptorSize(0)
//		, m_Page(nullptr)
//	{}
//	DescriptorAllocation::DescriptorAllocation(D3D12_CPU_DESCRIPTOR_HANDLE handle, u32 num, u32 desc_size, Ref<DescriptorAllocatorPage> page)
//		: m_Descriptor(handle)
//		, m_NumHandles(num)
//		, m_DescriptorSize(desc_size)
//		, m_Page(page)
//	{
//	}
//	DescriptorAllocation::~DescriptorAllocation()
//	{
//		Free();
//	}
//	DescriptorAllocation::DescriptorAllocation(DescriptorAllocation&& allocation)
//		: m_Descriptor(allocation.m_Descriptor)
//		, m_NumHandles(allocation.m_NumHandles)
//		, m_DescriptorSize(allocation.m_DescriptorSize)
//		, m_Page(std::move(allocation.m_Page))
//	{
//		allocation.m_Descriptor.ptr = 0;
//		allocation.m_NumHandles = 0;
//		allocation.m_DescriptorSize = 0;
//	}
//	DescriptorAllocation& DescriptorAllocation::operator=(DescriptorAllocation&& other)
//	{
//		// Free this descriptor if it points to anything.
//		Free();
//
//		m_Descriptor = other.m_Descriptor;
//		m_NumHandles = other.m_NumHandles;
//		m_DescriptorSize = other.m_DescriptorSize;
//		m_Page = std::move(other.m_Page);
//
//		other.m_Descriptor.ptr = 0;
//		other.m_NumHandles = 0;
//		other.m_DescriptorSize = 0;
//
//		return *this;
//	}
//	D3D12_CPU_DESCRIPTOR_HANDLE DescriptorAllocation::GetDescriptorHandle(u32 offset) const
//	{
//		AL_ASSERT(offset < m_NumHandles,"Bad alloc");
//		return { m_Descriptor.ptr + (m_DescriptorSize * offset) };
//	}
//	void DescriptorAllocation::Free()
//	{
//		if (!IsNull() && m_Page)
//		{
//			//param2: Application::GetFrameCount()
//			m_Page->Free(std::move(*this), Application::s_frame_count);
//
//			m_Descriptor.ptr = 0;
//			m_NumHandles = 0;
//			m_DescriptorSize = 0;
//			m_Page.reset();
//		}
//	}
//}
