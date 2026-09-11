#pragma once
#ifndef __D3DBUFFER_H__
#define __D3DBUFFER_H__
#include "Render/Buffer.h"

#include "D3DResource.h"
#include "DescriptorManager.h"
#include "RHI/DX12/GPUResourceManager.h"
#include <map>

using Microsoft::WRL::ComPtr;
using Ailu::Render::GPUBuffer;
using Ailu::Render::VertexBuffer;
using Ailu::Render::IndexBuffer;
using Ailu::Render::ConstantBuffer;
using Ailu::Render::BufferDesc;
using Ailu::Render::EResourceState;
using Ailu::Render::BindParams;
using Ailu::Render::UploadParams;
using Ailu::Render::GraphicsContext;
using Ailu::Render::RHICommandBuffer;
using Ailu::Render::VertexBufferLayout;

namespace Ailu::RHI::DX12
{
    class D3DGPUBuffer : public GPUBuffer
    {
    public:
		D3DGPUBuffer(BufferDesc desc);
		~D3DGPUBuffer();
		void Name(const String &name) final;
		ID3D12Resource *GetD3DResource() const { return _p_d3d_res.Get(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const
		{
			return _p_d3d_res ? _p_d3d_res->GetGPUVirtualAddress() : 0u;
		}
		void ReadBack(u8 *dst, u32 size) final;
		void ReadBackAsync(u8 *dst, u32 size, std::function<void()> on_complete);
		void GetCounter(std::function<void(u32)> callback) final;
		void SetCounter(u32 counter) final;
		ID3D12Resource* GetCounterBuffer() { return _counter_buffer.Get(); }
		const D3DResource &Resource() const { return _p_d3d_res; }
		const D3DResource &CounterResource() const { return _counter_buffer; }
		void RequireState(RHICommandBuffer *rhi_cmd, EResourceState state, u32 sub_res = Render::kTotalSubRes) final;
		void UavBarrier(RHICommandBuffer *rhi_cmd) final;
		Render::NativeHandle NativeResource() final { return {Render::RendererAPI::ERenderAPI::kDirectX12, _p_d3d_res.Get()}; }
	protected:
		void OnDataChanged() final;
	public:
		D3DResource _p_d3d_res;
		D3DResource _counter_buffer;
	private:
        void BindImpl(RHICommandBuffer* rhi_cmd, const BindParams& params) final;
        void UploadImpl(GraphicsContext* ctx,RHICommandBuffer* rhi_cmd,UploadParams* params) final;
    private:
        GPUVisibleDescriptorAllocation _uav_alloc,_srv_alloc,_counter_uav;
        D3D12_GPU_VIRTUAL_ADDRESS _gpu_ptr;
        void *_mapped_data = nullptr;
    };

	class D3DVertexBuffer : public VertexBuffer
	{
	public:
		D3DVertexBuffer(VertexBufferLayout layout);
		~D3DVertexBuffer();
		void Name(const String& name) final;
		void RequireState(RHICommandBuffer *rhi_cmd, EResourceState state, u32 sub_res = Render::kTotalSubRes) final;
		Render::NativeHandle NativeResource() final { return {Render::RendererAPI::ERenderAPI::kDirectX12, _vertex_buffers.empty() ? nullptr : _vertex_buffers[0].Get()}; }
		Render::NativeHandle NativeResource(u16 stream_idx) { return {Render::RendererAPI::ERenderAPI::kDirectX12, _vertex_buffers.empty() ? nullptr : _vertex_buffers[stream_idx].Get()}; }
	private:
        void BindImpl(RHICommandBuffer* rhi_cmd, const BindParams& params) final;
        void UploadImpl(GraphicsContext* ctx,RHICommandBuffer* rhi_cmd,UploadParams* params) final;
	private:
		Vector<D3DResource> _vertex_buffers;
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
		void RequireState(RHICommandBuffer *rhi_cmd, EResourceState state, u32 sub_res = Render::kTotalSubRes) final;
		Render::NativeHandle NativeResource() final { return {Render::RendererAPI::ERenderAPI::kDirectX12, _index_buf.Get()}; }
		const D3DResource &Resource() const { return _index_buf; }
	private:
        void BindImpl(RHICommandBuffer* rhi_cmd, const BindParams& params) final;
	private:
		D3DResource _index_buf;
		D3D12_INDEX_BUFFER_VIEW _index_buf_view;
	};

	class D3DConstantBuffer : public ConstantBuffer
	{
	public:
		D3DConstantBuffer(u32 size);
		~D3DConstantBuffer() override;
        void Reset() final;
		Render::NativeHandle NativeResource() final { return {Render::RendererAPI::ERenderAPI::kDirectX12, GpuResourceManager::Get()->NativeResource(_alloc)}; }
	private:
        void BindImpl(RHICommandBuffer* rhi_cmd, const BindParams& params) final;
	private:
        GpuResourceManager::Allocation _alloc;
	};
}
#endif // !D3DBUFFER_H__

