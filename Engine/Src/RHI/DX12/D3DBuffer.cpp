#include "pch.h"
#include "RHI/DX12/D3DBuffer.h"
#include "RHI/DX12/dxhelper.h"
#include "RHI/DX12/D3DContext.h"

namespace Ailu
{
	D3DVectexBuffer::D3DVectexBuffer(VertexBufferLayout layout)
	{
		_buffer_layout = std::move(layout);
		_buf_start = s_cur_offset;
		for (size_t i = 0; i < _buffer_layout.GetStreamCount(); i++)
		{
			s_vertex_bufs.emplace_back(ComPtr<ID3D12Resource>());
			s_vertex_buf_views.emplace_back(D3D12_VERTEX_BUFFER_VIEW{});
			++s_cur_offset;
		}
		_buf_num = _buffer_layout.GetStreamCount();
	}
	D3DVectexBuffer::D3DVectexBuffer(float* vertices, uint32_t size) : _buf_num(1)
	{
		s_vertex_bufs.emplace_back(ComPtr<ID3D12Resource>());
		s_vertex_buf_views.emplace_back(D3D12_VERTEX_BUFFER_VIEW{});
		_buf_start = s_cur_offset;
		++s_cur_offset;
		SetStream(vertices,size,0);
	}
	D3DVectexBuffer::~D3DVectexBuffer()
	{
		
	}
	void D3DVectexBuffer::Bind() const
	{
		if (_buf_start < 0 || _buf_start >= s_vertex_buf_views.size())
		{
			AL_ASSERT(false, "Try to bind a vertex buffer with an invalid index");
			return;
		}
		D3DContext::GetCmdList()->IASetVertexBuffers(0, _buf_num, &s_vertex_buf_views[_buf_start]);
	}

	void D3DVectexBuffer::SetLayout(VertexBufferLayout layout)
	{
		_buffer_layout = std::move(layout);
	}

	const VertexBufferLayout& D3DVectexBuffer::GetLayout() const
	{
		return _buffer_layout;
	}

	void D3DVectexBuffer::SetStream(float* vertices, uint32_t size,uint8_t stream_index)
	{
		if (_buffer_layout.GetStride(stream_index) == 0)
		{
			AL_ASSERT(true, "Try to set a null stream!");
			return;
		}
		ComPtr<ID3D12Resource> upload_heap;
		auto heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		auto res_desc = CD3DX12_RESOURCE_DESC::Buffer(size);
		ThrowIfFailed(D3DContext::GetDevice()->CreateCommittedResource(
			&heap_prop,
			D3D12_HEAP_FLAG_NONE,
			&res_desc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(upload_heap.GetAddressOf())));
		// Copy the triangle data to update heap
		uint8_t* pVertexDataBegin;
		CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
		ThrowIfFailed(upload_heap->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
		memcpy(pVertexDataBegin, vertices, size);
		upload_heap->Unmap(0, nullptr);

		// Create a Default Heap for the vertex buffer
		uint32_t cur_buffer_index = _buf_start + stream_index;
		s_vertex_bufs[cur_buffer_index].Reset();
		heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		ThrowIfFailed(D3DContext::GetDevice()->CreateCommittedResource(
			&heap_prop,
			D3D12_HEAP_FLAG_NONE,
			&res_desc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(s_vertex_bufs[cur_buffer_index].GetAddressOf())));
		D3DContext::GetCmdList()->CopyBufferRegion(s_vertex_bufs[cur_buffer_index].Get(), 0, upload_heap.Get(), 0, size);
		auto buf_state = CD3DX12_RESOURCE_BARRIER::Transition(s_vertex_bufs[cur_buffer_index].Get(),
			D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
		D3DContext::GetCmdList()->ResourceBarrier(1, &buf_state);
		// Initialize the vertex buffer view.
		s_vertex_buf_views[cur_buffer_index].BufferLocation = s_vertex_bufs[cur_buffer_index]->GetGPUVirtualAddress();
		s_vertex_buf_views[cur_buffer_index].StrideInBytes = _buffer_layout.GetStride(stream_index);
		s_vertex_buf_views[cur_buffer_index].SizeInBytes = size;

		s_vertex_upload_bufs.emplace_back(upload_heap);
	}

	//-----------------------------------------------------------------IndexBuffer----------------------------------------------------------
	D3DIndexBuffer::D3DIndexBuffer(uint32_t* indices, uint32_t count) : _count(count)
	{
		auto size = sizeof(uint32_t) * count;
		auto heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		auto res_desc = CD3DX12_RESOURCE_DESC::Buffer(size);
		ComPtr<ID3D12Resource> temp_buffer = nullptr;
		ThrowIfFailed(D3DContext::GetDevice()->CreateCommittedResource(
			&heap_prop,
			D3D12_HEAP_FLAG_NONE,
			&res_desc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&_index_buf)));

		// Copy the triangle data to the vertex buffer.
		uint8_t* pVertexDataBegin;
		CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
		ThrowIfFailed(_index_buf->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
		memcpy(pVertexDataBegin, indices, size);
		_index_buf->Unmap(0, nullptr);
		s_index_buf_views.emplace_back(D3D12_INDEX_BUFFER_VIEW{});
		// Initialize the vertex buffer view.
		s_index_buf_views[s_cur_view_offset].BufferLocation = _index_buf->GetGPUVirtualAddress();
		s_index_buf_views[s_cur_view_offset].Format = DXGI_FORMAT_R32_UINT;
		s_index_buf_views[s_cur_view_offset].SizeInBytes = static_cast<uint32_t>(size);
		_buf_view_index = s_cur_view_offset;
		++s_cur_view_offset;
	}
	D3DIndexBuffer::~D3DIndexBuffer()
	{
	}
	void D3DIndexBuffer::Bind() const
	{
		if (_buf_view_index < 0 || _buf_view_index >= s_index_buf_views.size())
		{
			AL_ASSERT(false, "Try to bind a index buffer with an invalid index");
			return;
		}
		D3DContext::GetCmdList()->IASetIndexBuffer(&s_index_buf_views[_buf_view_index]);
	}
	uint32_t D3DIndexBuffer::GetCount() const
	{
		return _count;
	}
}
