#pragma once
#ifndef __D3DBUFFER_H__
#define __D3DBUFFER_H__
#include "Render/Buffer.h"

#include <d3dx12.h>
using Microsoft::WRL::ComPtr;

namespace Ailu
{
	class D3DVectexBuffer : public VertexBuffer
	{
	public:
		D3DVectexBuffer(VertexBufferLayout layout);
		D3DVectexBuffer(float* vertices, uint32_t size);
		~D3DVectexBuffer();
		void Bind() const override;
		void SetLayout(VertexBufferLayout layout) override;
		const VertexBufferLayout& GetLayout() const override;
		void SetStream(float* vertices, uint32_t size,uint8_t stream_index) override;
		uint32_t GetVertexCount() const override;
	private:
		VertexBufferLayout _buffer_layout;
		uint32_t _vertices_count;
		uint32_t _buf_start;
		uint8_t _buf_num;
		inline static std::vector<ComPtr<ID3D12Resource>> s_vertex_bufs{};
		inline static std::vector<ComPtr<ID3D12Resource>> s_vertex_upload_bufs{};
		inline static std::vector<D3D12_VERTEX_BUFFER_VIEW> s_vertex_buf_views{};
		inline static uint32_t s_cur_offset = 0u;
	};

	class D3DIndexBuffer : public IndexBuffer
	{
	public:
		D3DIndexBuffer(uint32_t* indices, uint32_t count);
		~D3DIndexBuffer();
		void Bind() const override;
		uint32_t GetCount() const override;
	private:
		uint32_t _count;
		uint32_t _buf_view_index;
		ComPtr<ID3D12Resource> _index_buf;
		inline static std::vector<D3D12_INDEX_BUFFER_VIEW> s_index_buf_views{};
		inline static uint32_t s_cur_view_offset = 0u;
	};
}
#endif // !D3DBUFFER_H__

