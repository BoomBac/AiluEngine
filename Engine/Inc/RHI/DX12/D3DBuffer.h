#pragma once
#ifndef __D3DBUFFER_H__
#define __D3DBUFFER_H__
#include "Render/Buffer.h"

#include <map>
#include <d3dx12.h>
using Microsoft::WRL::ComPtr;

namespace Ailu
{
	class D3DVertexBuffer : public VertexBuffer
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
		u8* GetStream(u8 index) final
		{
			return _mapped_data[index];
		};
		u32 GetVertexCount() const override;
	private:
		VertexBufferLayout _buffer_layout;
		u32 _vertices_count;
		u32 _buf_start;
		u8 _buf_num;
		//just for dynamic buffer
		Vector<u8*> _mapped_data;
		inline static std::vector<ComPtr<ID3D12Resource>> s_vertex_bufs{};
		inline static std::vector<ComPtr<ID3D12Resource>> s_vertex_upload_bufs{};
		inline static std::vector<D3D12_VERTEX_BUFFER_VIEW> s_vertex_buf_views{};
		std::map<String, u8> _buffer_layout_indexer;
		inline static u32 s_global_offset = 0u;
	};

	class D3DDynamicVertexBuffer : public DynamicVertexBuffer
	{
	public:
		D3DDynamicVertexBuffer(VertexBufferLayout layout = {
				{"POSITION",EShaderDateType::kFloat3,0},
				{"COLOR",EShaderDateType::kFloat4,1},
			});
		~D3DDynamicVertexBuffer();
		void Bind(CommandBuffer* cmd) const final;
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

	class D3DIndexBuffer : public IndexBuffer
	{
	public:
		D3DIndexBuffer(u32* indices, u32 count);
		~D3DIndexBuffer();
		void Bind(CommandBuffer* cmd) const final;
		u32 GetCount() const override;
	private:
		u32 _index_count;
		u32 _buf_view_index;
		ComPtr<ID3D12Resource> _index_buf;
		inline static std::vector<D3D12_INDEX_BUFFER_VIEW> s_index_buf_views{};
		inline static u32 s_cur_view_offset = 0u;
	};

	class D3DConstantBuffer : public ConstantBuffer
	{
	public:
		D3DConstantBuffer(u32 size);
		~D3DConstantBuffer();
		void Bind(CommandBuffer* cmd, u8 bind_slot) const final;
		u8* GetData() final {return s_p_data + _offset;};
	private:
		inline static ComPtr<ID3D12Resource> s_p_d3d_res;
		inline static ComPtr<ID3D12DescriptorHeap> s_p_d3d_heap;
		inline static std::vector<D3D12_CONSTANT_BUFFER_VIEW_DESC> s_cbuf_views;
		inline static u32 s_desc_size, s_total_size,s_global_offset,s_global_index;
		inline static u8* s_p_data;
		u32 _offset,_index;
	};
}
#endif // !D3DBUFFER_H__

