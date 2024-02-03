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
		D3DVertexBuffer(VertexBufferLayout layout,bool is_static = true);
		D3DVertexBuffer(float* vertices, uint32_t size, bool is_static = true);
		~D3DVertexBuffer();
		void Bind() const override;
		void Bind(const VertexBufferLayout& pipeline_input_layout) const final;
		void SetLayout(VertexBufferLayout layout) override;
		const VertexBufferLayout& GetLayout() const override;
		void SetStream(float* vertices, uint32_t size,uint8_t stream_index) final;
		void SetStream(u8* data, uint32_t size, u8 stream_index) final;
		uint32_t GetVertexCount() const override;
		bool IsStatic() const final {return _is_static;};
	private:
		VertexBufferLayout _buffer_layout;
		uint32_t _vertices_count;
		uint32_t _buf_start;
		uint8_t _buf_num;
		bool _is_static;
		//just for dynamic buffer
		Vector<u8*> _mapped_data;
		inline static std::vector<ComPtr<ID3D12Resource>> s_vertex_bufs{};
		inline static std::vector<ComPtr<ID3D12Resource>> s_vertex_upload_bufs{};
		inline static std::vector<D3D12_VERTEX_BUFFER_VIEW> s_vertex_buf_views{};
		std::map<String, u8> _buffer_layout_indexer;
		inline static uint32_t s_global_offset = 0u;
	};

	class D3DDynamicVertexBuffer : public DynamicVertexBuffer
	{
	public:
		D3DDynamicVertexBuffer(VertexBufferLayout layout = {
				{"POSITION",EShaderDateType::kFloat3,0},
				{"COLOR",EShaderDateType::kFloat4,1},
			});
		~D3DDynamicVertexBuffer();
		void Bind() const final;
		void UploadData() final;
		void AppendData(float* data0, uint32_t num0, float* data1, uint32_t num1) final;
	private:
		uint32_t _vertex_num = 0u;
		uint32_t _size_pos_buf, _size_color_buf;
		uint32_t _ime_vertex_data_offset, _ime_color_data_offset;
		uint8_t* _p_vertex_data = nullptr; 
		uint8_t* _p_color_data = nullptr;
		uint8_t* _p_ime_vertex_data = nullptr;
		uint8_t* _p_ime_color_data = nullptr;
		ComPtr<ID3D12Resource> _p_vertex_buf;
		ComPtr<ID3D12Resource> _p_color_buf;
		D3D12_VERTEX_BUFFER_VIEW _buf_views[2];
	};

	class D3DIndexBuffer : public IndexBuffer
	{
	public:
		D3DIndexBuffer(uint32_t* indices, uint32_t count);
		~D3DIndexBuffer();
		void Bind() const override;
		uint32_t GetCount() const override;
	private:
		uint32_t _index_count;
		uint32_t _buf_view_index;
		ComPtr<ID3D12Resource> _index_buf;
		inline static std::vector<D3D12_INDEX_BUFFER_VIEW> s_index_buf_views{};
		inline static uint32_t s_cur_view_offset = 0u;
	};

	class D3DConstantBuffer : public ConstantBuffer
	{
	public:
		D3DConstantBuffer(u32 size);
		~D3DConstantBuffer();
		void Bind(u8 bind_slot) const final;
		u8* GetData() final {return _p_data + _offset;};
	private:
		inline static ComPtr<ID3D12Resource> s_p_d3d_res;
		inline static ComPtr<ID3D12DescriptorHeap> s_p_d3d_heap;
		inline static std::vector<D3D12_CONSTANT_BUFFER_VIEW_DESC> s_cbuf_views;
		inline static u32 s_desc_size, s_total_size,s_global_offset,s_global_index;
		u32 _offset,_index;
		u8* _p_data;
	};
}
#endif // !D3DBUFFER_H__

