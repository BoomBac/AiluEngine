#pragma once
#ifndef __D3DBUFFER_H__
#define __D3DBUFFER_H__
#include "Render/Buffer.h"

#include "D3DUtils.h"
#include "GPUDescriptorManager.h"
#include "GPUResourceManager.h"
#include <Render/GpuResource.h>
#include <map>

using Microsoft::WRL::ComPtr;

namespace Ailu
{
    class D3DGPUBuffer : public GPUBuffer
    {
    public:
        D3DGPUBuffer(GPUBufferDesc desc);
        ~D3DGPUBuffer();
        void Bind(CommandBuffer *cmd, u8 bind_slot, bool is_compute_pipeline = false) final;
        void Name(const String &name) final;
        u8 *ReadBack() final;
        void ReadBack(u8 *dst, u32 size) final;
        void ReadBackAsync(u8 *dst, u32 size, std::function<void()> on_complete);
    private:
        GPUVisibleDescriptorAllocation _alloc;
        ComPtr<ID3D12Resource> _p_d3d_res;
        ComPtr<ID3D12Resource> _p_d3d_res_readback;
        D3D12_GPU_VIRTUAL_ADDRESS _gpu_ptr;
        bool _is_compute_buffer;
        D3DResourceStateGuard _state_guard;
        D3DResourceStateGuard _state_guard_readback;
    };

	class D3DVertexBuffer : public IVertexBuffer, public GpuResource
	{
	public:
		D3DVertexBuffer(VertexBufferLayout layout);
		~D3DVertexBuffer();
		void Bind(CommandBuffer * cmd, const VertexBufferLayout& pipeline_input_layout) final;
		void SetLayout(VertexBufferLayout layout) override;
		const VertexBufferLayout& GetLayout() const override;
		void SetStream(float* vertices, u32 size,u8 stream_index) final;
		void SetStream(u8* data, u32 size, u8 stream_index, bool dynamic = false) final;
        void SetData(u8* data,u32 size,u8 stream_index,u32 offset = 0u) final;
		void SetName(const String& name) final;
		u8* GetStream(u8 index) final
		{
			return _mapped_data[index];
		};
		u32 GetVertexCount() const override;
	private:
		VertexBufferLayout _buffer_layout;
		u32 _vertices_count;
		//just for dynamic buffer
		Vector<u8*> _mapped_data;
        Vector<u32> _stream_size;
		std::map<String, u8> _buffer_layout_indexer;
		Vector<ComPtr<ID3D12Resource>> _vertex_buffers;
		Vector<D3D12_VERTEX_BUFFER_VIEW> _buffer_views;
        u64 _size_all_stream;
	};

	class D3DIndexBuffer : public IIndexBuffer, public GpuResource
	{
	public:
		D3DIndexBuffer(u32* indices, u32 count,bool is_dynamic);
		~D3DIndexBuffer() final;
		void Bind(CommandBuffer * cmd) final;
		u32 GetCount() const override;
		void SetName(const String& name) final;
        u8 *GetData() final;
        void SetData(u8 *data, u32 size) final;
        void Resize(u32 new_size) final;
	private:
		u32 _size;
        u32 _capacity;
		u32 _count;
		ComPtr<ID3D12Resource> _index_buf;
		D3D12_INDEX_BUFFER_VIEW _index_buf_view;
        bool _is_dynamic;
        u8* _p_data = nullptr;
	};

	class D3DConstantBuffer : public ConstantBuffer
	{
	public:
		D3DConstantBuffer(u32 size,bool compute_buffer);
		~D3DConstantBuffer() override;
		void Bind(CommandBuffer * cmd, u8 bind_slot, bool is_compute_pipeline) final;
		u8* GetData() final {return (u8*)_alloc._cpu_ptr;};
		[[nodiscard]] u64 GetBufferSize() const final { return _alloc._size; };
        void Reset() final;
	private:
        GpuResourceManager::Allocation _alloc;
		bool _is_compute_buffer;
	};
}
#endif // !D3DBUFFER_H__

