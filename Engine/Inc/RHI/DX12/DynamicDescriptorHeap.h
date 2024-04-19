#pragma once
#ifndef __DYNAMIC_DESCRIPTORHEAP_H__
#define __DYNAMIC_DESCRIPTORHEAP_H__
#include "GlobalMarco.h"
#include <d3dx12.h>
#include <wrl.h>

class D3DCommandBuffer;
namespace Ailu
{
	/*动态描述符堆（DynamicDescriptorHeap）类提供了在执行绘制（Draw）或派发（Dispatch）
	命令前暂存 CPU 可见描述符并将这些描述符提交到 GPU 可见描述符堆（GPU visible descriptor heap）的方法。
	DynamicDescriptorHeap 类还提供了一种将单个 CPU 可见描述符复制到 GPU 可见描述符堆的方法。*/
	class DynamicDescriptorHeap
	{
	public:
		DynamicDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType, u32 numDescriptorsPerHeap = 1024);
		virtual ~DynamicDescriptorHeap();
		/**
		 * Stages a contiguous range of CPU visible descriptors.
		 * Descriptors are not copied to the GPU visible descriptor heap until
		 * the CommitStagedDescriptors function is called.
		 */
		void StageDescriptors(u32 rootParameterIndex, u32 offset, u32 numDescriptors, const D3D12_CPU_DESCRIPTOR_HANDLE srcDescriptors);
		/**
		 * Copy all of the staged descriptors to the GPU visible descriptor heap and
		 * bind the descriptor heap and the descriptor tables to the command list.
		 * The passed-in function object is used to set the GPU visible descriptors
		 * on the command list. Two possible functions are:
		 *   * Before a draw    : ID3D12GraphicsCommandList::SetGraphicsRootDescriptorTable
		 *   * Before a dispatch: ID3D12GraphicsCommandList::SetComputeRootDescriptorTable
		 *
		 * Since the DynamicDescriptorHeap can't know which function will be used, it must
		 * be passed as an argument to the function.
		 */
		void CommitStagedDescriptors(D3DCommandBuffer& cmd, std::function<void(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_DESCRIPTOR_HANDLE)> setFunc);
		void CommitStagedDescriptorsForDraw(D3DCommandBuffer& cmd);
		void CommitStagedDescriptorsForDispatch(D3DCommandBuffer& cmd);
		/**
		 * Copies a single CPU visible descriptor to a GPU visible descriptor heap.
		 * This is useful for the
		 *   * ID3D12GraphicsCommandList::ClearUnorderedAccessViewFloat
		 *   * ID3D12GraphicsCommandList::ClearUnorderedAccessViewUint
		 * methods which require both a CPU and GPU visible descriptors for a UAV
		 * resource.
		 *
		 * @param commandList The command list is required in case the GPU visible
		 * descriptor heap needs to be updated on the command list.
		 * @param cpuDescriptor The CPU descriptor to copy into a GPU visible
		 * descriptor heap.
		 *
		 * @return The GPU visible descriptor.
		 */
		D3D12_GPU_DESCRIPTOR_HANDLE CopyDescriptor(D3DCommandBuffer& cmd, D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptor);
		/**
		* Parse the root signature to determine which root parameters contain
		* descriptor tables and determine the number of descriptors needed for
		* each table.
		*/
		void ParseRootSignature(const class RootSignature& rootSignature);
		/**
		* Reset used descriptors. This should only be done if any descriptors
		* that are being referenced by a command list has finished executing on the
		* command queue.
		*/
		void Reset();
	private:
		// Request a descriptor heap if one is available.
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> RequestDescriptorHeap();
		// Create a new descriptor heap of no descriptor heap is available.
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap();
		// Compute the number of stale descriptors that need to be copied
		// to GPU visible descriptor heap.
		u32 ComputeStaleDescriptorCount() const;
		/**
		* The maximum number of descriptor tables per root signature.
		* A 32-bit mask is used to keep track of the root parameter indices that
		* are descriptor tables.
		*/
		inline static const u32 kMaxDescriptorTables = 32;
		/**
		 * A structure that represents a descriptor table entry in the root signature.
		 * 如果为空，表示当前绑定的根签名没有使用描述符表
		 */
		struct DescriptorTableCache
		{
			DescriptorTableCache()
				: NumDescriptors(0)
				, BaseDescriptor(nullptr)
			{}

			// Reset the table cache.
			void Reset()
			{
				NumDescriptors = 0;
				BaseDescriptor = nullptr;
			}

			// The number of descriptors in this descriptor table.
			u32 NumDescriptors;
			// The pointer to the descriptor in the descriptor handle cache.
			D3D12_CPU_DESCRIPTOR_HANDLE* BaseDescriptor;
		};
		// Describes the type of descriptors that can be staged using this 
		// dynamic descriptor heap.
		// Valid values are:
		//   * D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
		//   * D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
		// This parameter also determines the type of GPU visible descriptor heap to 
		// create.
		D3D12_DESCRIPTOR_HEAP_TYPE m_DescriptorHeapType;

		// The number of descriptors to allocate in new GPU visible descriptor heaps.
		u32 m_NumDescriptorsPerHeap;

		// The increment size of a descriptor.
		u32 m_DescriptorHandleIncrementSize;
		// The descriptor handle cache.
		Scope<D3D12_CPU_DESCRIPTOR_HANDLE[]> m_DescriptorHandleCache;

		// Descriptor handle cache per descriptor table.
		DescriptorTableCache m_DescriptorTableCache[kMaxDescriptorTables];
		// Each bit in the bit mask represents the index in the root signature
		// that contains a descriptor table.
		u32 m_DescriptorTableBitMask;
		// Each bit set in the bit mask represents a descriptor table
		// in the root signature that has changed since the last time the 
		// descriptors were copied.
		u32 m_StaleDescriptorTableBitMask;
		using DescriptorHeapPool = std::queue< Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>>;

		DescriptorHeapPool m_DescriptorHeapPool;
		DescriptorHeapPool m_AvailableDescriptorHeaps;
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_CurrentDescriptorHeap;
		CD3DX12_GPU_DESCRIPTOR_HANDLE m_CurrentGPUDescriptorHandle;
		CD3DX12_CPU_DESCRIPTOR_HANDLE m_CurrentCPUDescriptorHandle;
		u32 m_NumFreeHandles;
	};
}
#endif // !DYNAMIC_DESCRIPTORHEAP_H__

