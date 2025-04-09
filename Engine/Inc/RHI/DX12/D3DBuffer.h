#pragma once
#ifndef __D3DBUFFER_H__
#define __D3DBUFFER_H__
#include "Render/Buffer.h"

#include "D3DResourceBase.h"
#include "DescriptorManager.h"
#include "RHI/DX12/GPUResourceManager.h"
#include <map>

using Microsoft::WRL::ComPtr;

namespace Ailu
{
    class D3DGPUBuffer : public GPUBuffer
    {
    public:
        D3DGPUBuffer(GPUBufferDesc desc);
        ~D3DGPUBuffer();
		void StateTranslation(RHICommandBuffer* rhi_cmd,EResourceState new_state,u32 sub_res) final;
        void Name(const String &name) final;
        void ReadBack(u8 *dst, u32 size) final;
        void ReadBackAsync(u8 *dst, u32 size, std::function<void()> on_complete);
		D3DResourceStateGuard _state_guard;
		private:
        void BindImpl(RHICommandBuffer* rhi_cmd,BindParams* params) final;
        void UploadImpl(GraphicsContext* ctx,RHICommandBuffer* rhi_cmd,UploadParams* params) final;
		void* NativeResource() {return reinterpret_cast<void*>(_p_d3d_res.Get());};
    private:
        GPUVisibleDescriptorAllocation _alloc;
        ComPtr<ID3D12Resource> _p_d3d_res;
        D3D12_GPU_VIRTUAL_ADDRESS _gpu_ptr;
    };

	class D3DVertexBuffer : public VertexBuffer
	{
	public:
		D3DVertexBuffer(VertexBufferLayout layout);
		~D3DVertexBuffer();
		void Name(const String& name) final;
		private:
        void BindImpl(RHICommandBuffer* rhi_cmd,BindParams* params) final;
        void UploadImpl(GraphicsContext* ctx,RHICommandBuffer* rhi_cmd,UploadParams* params) final;
	private:
		Vector<ComPtr<ID3D12Resource>> _vertex_buffers;
		Vector<D3D12_VERTEX_BUFFER_VIEW> _buffer_views;
	};

	class D3DIndexBuffer : public IndexBuffer
	{
	public:
		D3DIndexBuffer(u32* indices, u32 count,bool is_dynamic);
		~D3DIndexBuffer() final;
        void UploadImpl(GraphicsContext* ctx,RHICommandBuffer* rhi_cmd,UploadParams* params) final;
		void Name(const String& name) final;
        void Resize(u32 new_size) final;
	private:
        void BindImpl(RHICommandBuffer* rhi_cmd,BindParams* params) final;
	private:
		ComPtr<ID3D12Resource> _index_buf;
		D3D12_INDEX_BUFFER_VIEW _index_buf_view;
	};

	class D3DConstantBuffer : public ConstantBuffer
	{
	public:
		D3DConstantBuffer(u32 size,bool compute_buffer);
		~D3DConstantBuffer() override;
        void Reset() final;
	private:
        void BindImpl(RHICommandBuffer* rhi_cmd,BindParams* params) final;
	private:
        GpuResourceManager::Allocation _alloc;
		bool _is_compute_buffer;
	};
}
#endif // !D3DBUFFER_H__

