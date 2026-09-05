#pragma once
#ifndef __D3DBUFFER_H__
#define __D3DBUFFER_H__
#include "Render/Buffer.h"

#include "D3DResourceBase.h"
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
		void StateTranslation(RHICommandBuffer* rhi_cmd,EResourceState new_state,u32 sub_res) final;
		void ApplyResourceBarrier(RHICommandBuffer *rhi_cmd, EResourceState before_state, EResourceState after_state,
		                          u32 sub_res) final;
		void TrackResourceState(EResourceState new_state, u32 sub_res = Render::kTotalSubRes) final;
		EResourceState CurrentResourceState(u32 sub_res = Render::kTotalSubRes) const final;
        bool TryCurrentResourceState(EResourceState &out_state, u32 sub_res = Render::kTotalSubRes) const final;
		void InsertUAVBarrier(RHICommandBuffer* rhi_cmd) final;
		void Name(const String &name) final;
		ID3D12Resource *GetD3DResource() const { return _p_d3d_res.Get(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const
		{
			return _p_d3d_res != nullptr ? _p_d3d_res->GetGPUVirtualAddress() : 0u;
		}
		void ReadBack(u8 *dst, u32 size) final;
		void ReadBackAsync(u8 *dst, u32 size, std::function<void()> on_complete);
		void GetCounter(std::function<void(u32)> callback) final;
		void SetCounter(u32 counter) final;
		ID3D12Resource* GetCounterBuffer() {return _counter_buffer.Get();}
		Render::NativeHandle NativeResource() final { return {Render::RendererAPI::ERenderAPI::kDirectX12, _p_d3d_res.Get()}; }
	protected:
		void OnDataChanged() final;
	public:
		D3DResourceStateGuard _state_guard;
		D3DResourceStateGuard _counter_state_guard;
	private:
        void BindImpl(RHICommandBuffer* rhi_cmd, const BindParams& params) final;
        void UploadImpl(GraphicsContext* ctx,RHICommandBuffer* rhi_cmd,UploadParams* params) final;
    private:
        GPUVisibleDescriptorAllocation _uav_alloc,_srv_alloc,_counter_uav;
        ComPtr<ID3D12Resource> _p_d3d_res;
        ComPtr<ID3D12Resource> _counter_buffer;
        D3D12_GPU_VIRTUAL_ADDRESS _gpu_ptr;
        void *_mapped_data = nullptr;
    };

	class D3DVertexBuffer : public VertexBuffer
	{
	public:
		D3DVertexBuffer(VertexBufferLayout layout);
		~D3DVertexBuffer();
		void Name(const String& name) final;
		void StateTranslation(RHICommandBuffer* rhi_cmd, EResourceState new_state, u32 sub_res) final;
		void ApplyResourceBarrier(RHICommandBuffer *rhi_cmd, EResourceState before_state, EResourceState after_state,
		                          u32 sub_res) final;
		Render::NativeHandle NativeResource() final { return {Render::RendererAPI::ERenderAPI::kDirectX12, _vertex_buffers.empty() ? nullptr : _vertex_buffers[0].Get()}; }
		Render::NativeHandle NativeResource(u16 stream_idx) { return {Render::RendererAPI::ERenderAPI::kDirectX12, _vertex_buffers.empty() ? nullptr : _vertex_buffers[stream_idx].Get()}; }
	private:
        void BindImpl(RHICommandBuffer* rhi_cmd, const BindParams& params) final;
        void UploadImpl(GraphicsContext* ctx,RHICommandBuffer* rhi_cmd,UploadParams* params) final;
	private:
		Vector<ComPtr<ID3D12Resource>> _vertex_buffers;
		Vector<D3D12_VERTEX_BUFFER_VIEW> _buffer_views;
		Vector<D3DResourceStateGuard> _state_guards;
	};

	class D3DIndexBuffer : public IndexBuffer
	{
	public:
		D3DIndexBuffer(u32* indices, u32 count,bool is_dynamic);
		~D3DIndexBuffer() final;
        void UploadImpl(GraphicsContext* ctx,RHICommandBuffer* rhi_cmd,UploadParams* params) final;
		void Name(const String& name) final;
        void Resize(u32 new_size) final;
		Render::NativeHandle NativeResource() final { return {Render::RendererAPI::ERenderAPI::kDirectX12, _index_buf.Get()}; }
	private:
        void BindImpl(RHICommandBuffer* rhi_cmd, const BindParams& params) final;
	private:
		ComPtr<ID3D12Resource> _index_buf;
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

