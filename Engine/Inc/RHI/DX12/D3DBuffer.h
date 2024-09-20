#pragma once
#ifndef __D3DBUFFER_H__
#define __D3DBUFFER_H__
#include "Render/Buffer.h"

#include <map>
#include "D3DUtils.h"
#include "GPUDescriptorManager.h"
using Microsoft::WRL::ComPtr;

namespace Ailu
{
    class D3DGPUBuffer : public IGPUBuffer
    {
    public:
        D3DGPUBuffer(GPUBufferDesc desc);
        ~D3DGPUBuffer();
        void Bind(CommandBuffer *cmd, u8 bind_slot, bool is_compute_pipeline = false) const final;
        void SetName(const String &name) final;
        u8 *ReadBack() final;
        void ReadBack(u8 *dst, u32 size) final;
        void ReadBackAsync(u8 *dst, u32 size, std::function<void()> on_complete);

    private:
        String _name;
        GPUBufferDesc _desc;
        GPUVisibleDescriptorAllocation _alloc;
        ComPtr<ID3D12Resource> _p_d3d_res;
        ComPtr<ID3D12Resource> _p_d3d_res_readback;
        D3D12_GPU_VIRTUAL_ADDRESS _gpu_ptr;
        bool _is_compute_buffer;
        D3DResourceStateGuard _state_guard;
        D3DResourceStateGuard _state_guard_readback;
    };

	class D3DVertexBuffer : public IVertexBuffer
	{
	public:
		D3DVertexBuffer(VertexBufferLayout layout);
		D3DVertexBuffer(float* vertices, u32 size);
		~D3DVertexBuffer();
		void Bind(CommandBuffer* cmd, const VertexBufferLayout& pipeline_input_layout) const final;
		void SetLayout(VertexBufferLayout layout) override;
		const VertexBufferLayout& GetLayout() const override;
		void SetStream(float* vertices, u32 size,u8 stream_index) final;
		void SetStream(u8* data, u32 size, u8 stream_index, bool dynamic = false) final;
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
		std::map<String, u8> _buffer_layout_indexer;
		Vector<ComPtr<ID3D12Resource>> _vertex_buffers;
		Vector<D3D12_VERTEX_BUFFER_VIEW> _buffer_views;
	};

	class D3DDynamicVertexBuffer : public IDynamicVertexBuffer
	{
	public:
		D3DDynamicVertexBuffer(VertexBufferLayout layout = {
				{"POSITION",EShaderDateType::kFloat3,0},
				{"COLOR",EShaderDateType::kFloat4,1},
			});
		~D3DDynamicVertexBuffer();
		void Bind(CommandBuffer* cmd) const final;
		void SetName(const String& name) final {};
		void UploadData() final;
		void AppendData(float* data0, u32 num0, float* data1, u32 num1) final;
	private:
		u32 _vertex_num = 0u;
		u32 _size_pos_buf, _size_color_buf;
		u32 _ime_vertex_data_offset, _ime_color_data_offset;
		u8* _p_vertex_data = nullptr; 
		u8* _p_color_data = nullptr;
		u8* _p_ime_vertex_data = nullptr;
		u8* _p_ime_color_data = nullptr;
		ComPtr<ID3D12Resource> _p_vertex_buf;
		ComPtr<ID3D12Resource> _p_color_buf;
		D3D12_VERTEX_BUFFER_VIEW _buf_views[2];
	};

	class D3DIndexBuffer : public IIndexBuffer
	{
	public:
		D3DIndexBuffer(u32* indices, u32 count);
		~D3DIndexBuffer();
		void Bind(CommandBuffer* cmd) const final;
		u32 GetCount() const override;
		void SetName(const String& name) final;
	private:
		u32 _index_count;
		ComPtr<ID3D12Resource> _index_buf;
		D3D12_INDEX_BUFFER_VIEW _index_buf_view;
	};

	class D3DConstantBuffer : public IConstantBuffer
	{
	public:
		D3DConstantBuffer(u32 size,bool compute_buffer);
		~D3DConstantBuffer();
		void Bind(CommandBuffer* cmd, u8 bind_slot, bool is_compute_pipeline) const final;
		u8* GetData() final {return s_p_data + _offset;};
		u32 GetBufferSize() const final { return _size; };
		void SetName(const String& name) final {};
	private:
		inline static ComPtr<ID3D12Resource> s_p_d3d_res;
		inline static u32 s_desc_size, s_total_size,s_global_offset,s_global_index;
		inline static u8* s_p_data;
		u32 _offset,_index,_size;
		D3D12_GPU_VIRTUAL_ADDRESS _gpu_ptr;
		bool _is_compute_buffer;
	};
}
#endif // !D3DBUFFER_H__

