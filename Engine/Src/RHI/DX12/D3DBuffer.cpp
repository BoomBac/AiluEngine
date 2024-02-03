#include "pch.h"
#include "Render/RenderingData.h"
#include "Render/RenderConstants.h"
#include "RHI/DX12/D3DBuffer.h"
#include "RHI/DX12/dxhelper.h"
#include "RHI/DX12/D3DContext.h"

namespace Ailu
{
	D3DVertexBuffer::D3DVertexBuffer(VertexBufferLayout layout, bool is_static) : _is_static(is_static)
	{
		_buffer_layout = std::move(layout);
		_buf_start = s_global_offset;
		for (size_t i = 0; i < _buffer_layout.GetStreamCount(); i++)
		{
			s_vertex_bufs.emplace_back(ComPtr<ID3D12Resource>());
			s_vertex_buf_views.emplace_back(D3D12_VERTEX_BUFFER_VIEW{});
			++s_global_offset;
		}
		_buf_num = _buffer_layout.GetStreamCount();
		if (!is_static)
		{
			_mapped_data.resize(_buf_num);
		}
	}
	D3DVertexBuffer::D3DVertexBuffer(float* vertices, uint32_t size, bool is_static) : _buf_num(1), _is_static(is_static)
	{
		s_vertex_bufs.emplace_back(ComPtr<ID3D12Resource>());
		s_vertex_buf_views.emplace_back(D3D12_VERTEX_BUFFER_VIEW{});
		_buf_start = s_global_offset;
		++s_global_offset;
		SetStream(vertices, size, 0);
		if (!is_static)
		{
			_mapped_data.resize(_buf_num);
		}
	}
	D3DVertexBuffer::~D3DVertexBuffer()
	{

	}
	void D3DVertexBuffer::Bind() const
	{
		if (_buf_start < 0 || _buf_start >= s_vertex_buf_views.size())
		{
			AL_ASSERT(false, "Try to bind a vertex buffer with an invalid index");
			return;
		}
		RenderingStates::s_vertex_num += _vertices_count;
		D3DContext::GetInstance()->GetCmdList()->IASetVertexBuffers(0, _buf_num, &s_vertex_buf_views[_buf_start]);
	}

	void D3DVertexBuffer::Bind(const VertexBufferLayout& pipeline_input_layout) const
	{
		static auto cmdlist = D3DContext::GetInstance()->GetCmdList();
		RenderingStates::s_vertex_num += _vertices_count;
		for (auto& layout_ele : pipeline_input_layout)
		{
			auto it = _buffer_layout_indexer.find(layout_ele.Name);
			if (it != _buffer_layout_indexer.end())
			{
				const u8 stream_index = it->second;
				cmdlist->IASetVertexBuffers(layout_ele.Stream, 1, &s_vertex_buf_views[_buf_start + stream_index]);
			}
			else
			{
				LOG_WARNING("Try to bind a vertex buffer with an invalid layout element name {}",layout_ele.Name);
			}
		}
	}

	void D3DVertexBuffer::SetLayout(VertexBufferLayout layout)
	{
		_buffer_layout = std::move(layout);
	}

	const VertexBufferLayout& D3DVertexBuffer::GetLayout() const
	{
		return _buffer_layout;
	}

	void D3DVertexBuffer::SetStream(float* vertices, uint32_t size, uint8_t stream_index)
	{
		auto d3d_conetxt = D3DContext::GetInstance();
		if (_buffer_layout.GetStride(stream_index) == 0)
		{
			AL_ASSERT(true, "Try to set a null stream!");
			return;
		}
		_vertices_count = size / _buffer_layout.GetStride(stream_index);
		ComPtr<ID3D12Resource> upload_heap;
		auto heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		auto res_desc = CD3DX12_RESOURCE_DESC::Buffer(size);
		ThrowIfFailed(d3d_conetxt->GetDevice()->CreateCommittedResource(
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
		ThrowIfFailed(d3d_conetxt->GetDevice()->CreateCommittedResource(
			&heap_prop,
			D3D12_HEAP_FLAG_NONE,
			&res_desc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(s_vertex_bufs[cur_buffer_index].GetAddressOf())));
		auto cmd = d3d_conetxt->GetTaskCmdList();
		cmd->CopyBufferRegion(s_vertex_bufs[cur_buffer_index].Get(), 0, upload_heap.Get(), 0, size);
		auto buf_state = CD3DX12_RESOURCE_BARRIER::Transition(s_vertex_bufs[cur_buffer_index].Get(),
			D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
		cmd->ResourceBarrier(1, &buf_state);
		// Initialize the vertex buffer view.
		s_vertex_buf_views[cur_buffer_index].BufferLocation = s_vertex_bufs[cur_buffer_index]->GetGPUVirtualAddress();
		s_vertex_buf_views[cur_buffer_index].StrideInBytes = _buffer_layout.GetStride(stream_index);
		s_vertex_buf_views[cur_buffer_index].SizeInBytes = size;
		_buffer_layout_indexer.emplace(std::make_pair(_buffer_layout[stream_index].Name, stream_index));
		s_vertex_upload_bufs.emplace_back(upload_heap);
	}

	void D3DVertexBuffer::SetStream(u8* data, uint32_t size, u8 stream_index)
	{
		auto d3d_conetxt = D3DContext::GetInstance();
		if (_buffer_layout.GetStride(stream_index) == 0)
		{
			AL_ASSERT(true, "Try to set a null stream!");
			return;
		}
		_vertices_count = size / _buffer_layout.GetStride(stream_index);
		auto heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		auto res_desc = CD3DX12_RESOURCE_DESC::Buffer(size);
		uint32_t cur_buffer_index = _buf_start + stream_index;
		if (_is_static)
		{
			ComPtr<ID3D12Resource> upload_heap;
			ThrowIfFailed(d3d_conetxt->GetDevice()->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &res_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(upload_heap.GetAddressOf())));
			// Copy the triangle data to update heap
			uint8_t* pVertexDataBegin;
			CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
			ThrowIfFailed(upload_heap->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
			memcpy(pVertexDataBegin, data, size);
			upload_heap->Unmap(0, nullptr);
			// Create a Default Heap for the vertex buffer
			s_vertex_bufs[cur_buffer_index].Reset();
			heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
			ThrowIfFailed(d3d_conetxt->GetDevice()->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &res_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(s_vertex_bufs[cur_buffer_index].GetAddressOf())));
			auto cmd = d3d_conetxt->GetTaskCmdList();
			cmd->CopyBufferRegion(s_vertex_bufs[cur_buffer_index].Get(), 0, upload_heap.Get(), 0, size);
			auto buf_state = CD3DX12_RESOURCE_BARRIER::Transition(s_vertex_bufs[cur_buffer_index].Get(),
				D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
			cmd->ResourceBarrier(1, &buf_state);
			s_vertex_upload_bufs.emplace_back(upload_heap);
		}
		else
		{
			_mapped_data[stream_index] = data;
			uint32_t cur_buffer_index = _buf_start + stream_index;
			s_vertex_bufs[cur_buffer_index].Reset();
			ThrowIfFailed(d3d_conetxt->GetDevice()->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &res_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(s_vertex_bufs[cur_buffer_index].GetAddressOf())));
			auto f = reinterpret_cast<Vector3f*>(_mapped_data[0]);
			LOG_WARNING("{}", f->ToString())
			s_vertex_bufs[cur_buffer_index]->Map(0, nullptr, reinterpret_cast<void**>(&_mapped_data[stream_index]));
			memcpy(_mapped_data[stream_index], data, size);
		}

		// Initialize the vertex buffer view.
		s_vertex_buf_views[cur_buffer_index].BufferLocation = s_vertex_bufs[cur_buffer_index]->GetGPUVirtualAddress();
		s_vertex_buf_views[cur_buffer_index].StrideInBytes = _buffer_layout.GetStride(stream_index);
		s_vertex_buf_views[cur_buffer_index].SizeInBytes = size;
		_buffer_layout_indexer.emplace(std::make_pair(_buffer_layout[stream_index].Name, stream_index));
	}

	uint32_t D3DVertexBuffer::GetVertexCount() const
	{
		return _vertices_count;
	}

	//-----------------------------------------------------------------D3DDynamicVertexBuffer----------------------------------------------------------
	D3DDynamicVertexBuffer::D3DDynamicVertexBuffer(VertexBufferLayout layout)
	{
		_size_pos_buf = RenderConstants::KMaxDynamicVertexNum * sizeof(Vector3f);
		_size_color_buf = RenderConstants::KMaxDynamicVertexNum * sizeof(Vector4f);
		_ime_vertex_data_offset = _ime_color_data_offset = 0;
		_p_ime_vertex_data = new uint8_t[12 * RenderConstants::KMaxDynamicVertexNum];
		_p_ime_color_data = new uint8_t[16 * RenderConstants::KMaxDynamicVertexNum];
		auto device = D3DContext::GetInstance()->GetDevice();
		D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(_size_pos_buf);
		ThrowIfFailed(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&_p_vertex_buf)));
		bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(_size_color_buf);
		ThrowIfFailed(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&_p_color_buf)));
		_p_vertex_buf->Map(0, nullptr, reinterpret_cast<void**>(&_p_vertex_data));
		_p_color_buf->Map(0, nullptr, reinterpret_cast<void**>(&_p_color_data));
		D3D12_VERTEX_BUFFER_VIEW buf_view{};
		buf_view.BufferLocation = _p_vertex_buf->GetGPUVirtualAddress();
		buf_view.StrideInBytes = sizeof(Vector3f);
		buf_view.SizeInBytes = _size_pos_buf;
		_buf_views[0] = buf_view;
		buf_view.BufferLocation = _p_color_buf->GetGPUVirtualAddress();
		buf_view.StrideInBytes = sizeof(Vector4f);
		buf_view.SizeInBytes = _size_color_buf;
		_buf_views[1] = buf_view;
	}
	D3DDynamicVertexBuffer::~D3DDynamicVertexBuffer()
	{
		delete[] _p_ime_vertex_data;
		delete[] _p_ime_color_data;
	}
	void D3DDynamicVertexBuffer::Bind() const
	{
		RenderingStates::s_vertex_num += _vertex_num;
		D3DContext::GetInstance()->GetCmdList()->IASetVertexBuffers(0, 2, _buf_views);
	}
	void D3DDynamicVertexBuffer::UploadData()
	{
		_vertex_num = _ime_vertex_data_offset / 12;
		//memcpy(_p_vertex_data, _p_ime_vertex_data, _size_pos_buf);
		//memcpy(_p_color_data, _p_ime_color_data, _size_color_buf);
		_ime_vertex_data_offset = 0;
		_ime_color_data_offset = 0;
	}
	void D3DDynamicVertexBuffer::AppendData(float* data0, uint32_t num0, float* data1, uint32_t num1)
	{
		memcpy(_p_vertex_data + _ime_vertex_data_offset, data0, num0 * 4);
		memcpy(_p_color_data + _ime_color_data_offset, data1, num1 * 4);
		_ime_vertex_data_offset += num0 * 4;
		_ime_color_data_offset += num1 * 4;
	}
	//-----------------------------------------------------------------D3DDynamicVertexBuffer----------------------------------------------------------

	//-----------------------------------------------------------------IndexBuffer---------------------------------------------------------------------
	D3DIndexBuffer::D3DIndexBuffer(uint32_t* indices, uint32_t count) : _index_count(count)
	{
		auto d3d_conetxt = D3DContext::GetInstance();
		auto size = sizeof(uint32_t) * count;
		auto heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		auto res_desc = CD3DX12_RESOURCE_DESC::Buffer(size);
		ComPtr<ID3D12Resource> temp_buffer = nullptr;
		ThrowIfFailed(d3d_conetxt->GetDevice()->CreateCommittedResource(
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
		RenderingStates::s_triangle_num += _index_count / 3;
		D3DContext::GetInstance()->GetCmdList()->IASetIndexBuffer(&s_index_buf_views[_buf_view_index]);
	}
	uint32_t D3DIndexBuffer::GetCount() const
	{
		return _index_count;
	}
	//-----------------------------------------------------------------IndexBuffer---------------------------------------------------------------------


	//-----------------------------------------------------------------ConstBuffer---------------------------------------------------------------------
	static u32 CalcConstantBufferByteSize(u32 byte_size)
	{
		return (byte_size + 255) & ~255;
	}

	D3DConstantBuffer::D3DConstantBuffer(u32 size)
	{
		static bool b_init = false;	
		if (!b_init)
		{
			s_global_index = 0u;
			s_global_offset = 0u;
			auto device = D3DContext::GetInstance()->GetDevice();
			s_p_d3d_heap = D3DContext::GetInstance()->GetDescriptorHeap();
			//constbuffer
			auto heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
			s_total_size = RenderConstants::kPerFrameTotalSize * RenderConstants::kFrameCount;
			s_desc_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			auto res_desc = CD3DX12_RESOURCE_DESC::Buffer(s_total_size);
			ThrowIfFailed(device->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &res_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&s_p_d3d_res)));
			// Map and initialize the constant buffer. We don't unmap this until the
			// app closes. Keeping things mapped for the lifetime of the resource is okay.
			CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
			ThrowIfFailed(s_p_d3d_res->Map(0, &readRange, reinterpret_cast<void**>(&_p_data)));
			b_init = true;
		}
		size = CalculateConstantBufferByteSize(size);
		AL_ASSERT(s_global_offset + size <= s_total_size, "Constant buffer overflow");
		_offset = s_global_offset;
		_index = s_global_index;
		{
			D3D12_CPU_DESCRIPTOR_HANDLE cbvHandle;
			cbvHandle.ptr = s_p_d3d_heap->GetCPUDescriptorHandleForHeapStart().ptr + _index * s_desc_size;
			D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
			cbv_desc.BufferLocation = s_p_d3d_res->GetGPUVirtualAddress() + _offset;
			cbv_desc.SizeInBytes = size;
			D3DContext::GetInstance()->GetDevice()->CreateConstantBufferView(&cbv_desc, cbvHandle);
			s_cbuf_views.emplace_back(cbv_desc);
		}
		s_global_offset += size;
		s_global_index++;
	}
	D3DConstantBuffer::~D3DConstantBuffer()
	{
	}
	void D3DConstantBuffer::Bind(u8 bind_slot) const
	{
		static auto cmd = D3DContext::GetInstance()->GetCmdList();
		cmd->SetGraphicsRootConstantBufferView(bind_slot, s_cbuf_views[_index].BufferLocation);
		return;
	}
	//-----------------------------------------------------------------ConstBuffer---------------------------------------------------------------------

}
