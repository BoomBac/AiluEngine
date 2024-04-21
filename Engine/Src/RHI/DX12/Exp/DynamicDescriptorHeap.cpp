//#include "pch.h"
//#include "RHI/DX12/DynamicDescriptorHeap.h"
//#include "RHI/DX12/D3DCommandBuffer.h"
//#include "RHI/DX12/D3DContext.h"
//
//namespace Ailu
//{
//    DynamicDescriptorHeap::DynamicDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType, u32 numDescriptorsPerHeap)
//        : m_DescriptorHeapType(heapType)
//        , m_NumDescriptorsPerHeap(numDescriptorsPerHeap)
//        , m_DescriptorTableBitMask(0)
//        , m_StaleDescriptorTableBitMask(0)
//        , m_CurrentCPUDescriptorHandle(D3D12_DEFAULT)
//        , m_CurrentGPUDescriptorHandle(D3D12_DEFAULT)
//        , m_NumFreeHandles(0)
//    {
//        
//        m_DescriptorHandleIncrementSize = D3DContext::GetInstance()->GetDevice()->GetDescriptorHandleIncrementSize(heapType);
//
//        // Allocate space for staging CPU visible descriptors.
//        m_DescriptorHandleCache = MakeScope<D3D12_CPU_DESCRIPTOR_HANDLE[]>(m_NumDescriptorsPerHeap);
//    }
//    void DynamicDescriptorHeap::ParseRootSignature(const RootSignature& rootSignature)
//    {
//        //// If the root signature changes, all descriptors must be (re)bound to the
//        //// command list.
//        //m_StaleDescriptorTableBitMask = 0;
//
//        //const auto& rootSignatureDesc = rootSignature.GetRootSignatureDesc();
//        //// Get a bit mask that represents the root parameter indices that match the 
//        //// descriptor heap type for this dynamic descriptor heap.
//        //m_DescriptorTableBitMask = rootSignature.GetDescriptorTableBitMask(m_DescriptorHeapType);
//        //uint32_t descriptorTableBitMask = m_DescriptorTableBitMask;
//    }
//
//	DynamicDescriptorHeap::~DynamicDescriptorHeap()
//	{
//	}
//	void DynamicDescriptorHeap::StageDescriptors(u32 rootParameterIndex, u32 offset, u32 numDescriptors, const D3D12_CPU_DESCRIPTOR_HANDLE srcDescriptors)
//	{
//	}
//}
