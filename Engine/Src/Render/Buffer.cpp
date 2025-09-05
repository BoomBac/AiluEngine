#include "pch.h"
#include "Render/Buffer.h"
#include "Render/Renderer.h"
#include "RHI/DX12/D3DBuffer.h"
#include "GlobalMarco.h"

namespace Ailu::Render
{
	#pragma region GPUBuffer
	Ref<GPUBuffer> GPUBuffer::Create(BufferDesc desc, const String &name)
	{
		switch (Renderer::GetAPI())
		{
			case RendererAPI::ERenderAPI::kNone:
				AL_ASSERT_MSG(false, "None render api used!");
				return nullptr;
			case RendererAPI::ERenderAPI::kDirectX12:
			{
				auto buf = MakeRef<RHI::DX12::D3DGPUBuffer>(desc);
				buf->Name(name);
				GraphicsContext::Get().CreateResource(buf.get());
				return buf;
			}
		}
		AL_ASSERT_MSG(false, "Unsupported render api!");
		return nullptr;
	}

	Ref<GPUBuffer> GPUBuffer::Create(EGPUBufferTarget target, u32 element_size, u32 element_num, const String &name)
	{
		BufferDesc desc;
		desc._element_num = element_num;
		desc._element_size = element_size;
		desc._size = element_num * element_size;
		desc._target = target;
		return Create(desc,name);
	}
	#pragma endregion

	#pragma region VertexBuffer
	VertexBuffer* VertexBuffer::Create(VertexBufferLayout layout, const String& name)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT_MSG(false, "None render api used!");
			return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
		{
			auto buf = new RHI::DX12::D3DVertexBuffer(layout);
            buf->Name(name);
			return buf;
		}
		}
		AL_ASSERT_MSG(false, "Unsupported render api!");
		return nullptr;
	}
	VertexBuffer::VertexBuffer(VertexBufferLayout layout)
	{
		_buffer_layout = std::move(layout);
		u16 stream_count = _buffer_layout.GetStreamCount();
		_mapped_data.resize(RenderConstants::kMaxVertexAttrNum);
		_stream_data.resize(RenderConstants::kMaxVertexAttrNum);
		_res_type = EGpuResType::kVertexBuffer;
	}
	void VertexBuffer::SetData(u8 *data, u32 size, u8 stream_index, u32 offset)
	{
		if (!_is_ready_for_rendering)
			return;
		AL_ASSERT(stream_index < _mapped_data.size());
		AL_ASSERT(offset + size <= _stream_data[stream_index]._size);
		memcpy(_mapped_data[stream_index], data + offset, size);
		_vertices_count = size / _buffer_layout[stream_index].Size;
	}
	void VertexBuffer::SetStream(u8 *data, u32 size, u8 stream_index, bool is_dynamic)
	{
		if (_buffer_layout.GetStride(stream_index) == 0)
		{
			AL_ASSERT_MSG(false, "Try to set a null stream!");
			return;
		}
		_vertices_count = size / _buffer_layout.GetStride(stream_index);
		_stream_data[stream_index] = {data, size, is_dynamic};
		_mem_size += size;
	}
	#pragma endregion

	#pragma region IndexBuffer
	IndexBuffer* IndexBuffer::Create(u32* indices, u32 count, const String& name,bool is_dynamic )
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT_MSG(false, "None render api used!");
			return nullptr;
        case RendererAPI::ERenderAPI::kDirectX12:
        {
			auto buf = new RHI::DX12::D3DIndexBuffer(indices, count,is_dynamic);
			buf->Name(name);
            return buf;
        }
		}
		AL_ASSERT_MSG(false, "Unsupported render api!");
		return nullptr;
	}
	IndexBuffer::IndexBuffer(u32 *indices, u32 count, bool is_dynamic)
	: _data((u8*)indices),_count(count), _capacity(count), _is_dynamic(is_dynamic)
	{
		_mem_size = sizeof(u32) * count;
		_res_type = EGpuResType::kIndexBUffer;
	}
	void IndexBuffer::SetData(u8* data,u32 size)
	{
		if (_data == nullptr || data == nullptr)
		{
			LOG_WARNING("Try to set a null data to index buffer!");
			return;
		}
		u32 max_size = _capacity * sizeof(u32);
		if (size > max_size)
		{
			size = max_size;
			LOG_WARNING("Try to set a larger data to index buffer! ({} > {})", size, max_size);
		}
		memcpy(_data, data, size);
		_count = size / sizeof(u32);
	}
	#pragma endregion

	#pragma region ConstantBuffer
	ConstantBuffer *ConstantBuffer::Create(u32 size,const String& name)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT_MSG(false, "None render api used!");
			return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
		{
			RHI::DX12::D3DConstantBuffer *buffer = new RHI::DX12::D3DConstantBuffer(size);//AL_NEW(RHI::DX12::D3DConstantBuffer, size);
			buffer->Name(name);
			return buffer;
		}
			return new RHI::DX12::D3DConstantBuffer(size);
		}
		AL_ASSERT_MSG(false, "Unsupported render api!");
		return nullptr;
	}
	void ConstantBuffer::Release(ConstantBuffer* ptr)
	{
		DESTORY_PTR(ptr);
	}
	#pragma endregion

	#pragma region ConstBufferPool
	ConstBufferPool *s_ConstBufferPool = nullptr;
	void ConstBufferPool::Init()
	{
		if (!s_ConstBufferPool)
			s_ConstBufferPool = AL_NEW(ConstBufferPool);
	}
	void ConstBufferPool::ShutDown()
	{
		if (s_ConstBufferPool)
		{
			for (auto& it: s_ConstBufferPool->_buffer_pool)
				ConstantBuffer::Release(it.second._buffer);
			s_ConstBufferPool->_buffer_pool.clear();
			AL_DELETE(s_ConstBufferPool);
		}
	}
	ConstantBuffer *ConstBufferPool::Acquire(u32 size)
	{
		size = (u32)AlignTo(size, 256u);
		u64 cur_frame = GraphicsContext::Get().GetFrameCount();
		for (auto it = s_ConstBufferPool->_buffer_pool.lower_bound(size); it != s_ConstBufferPool->_buffer_pool.end(); it++)
		{
			auto &[buffer, frame_count] = it->second;
			if (cur_frame - frame_count > 2)
			{
				//buffer->Reset();
				frame_count = cur_frame;
				return buffer;
			}
		}
		ConstantBuffer *buffer = ConstantBuffer::Create(size);
		s_ConstBufferPool->_buffer_pool.emplace(size, ConstbufferNode{buffer, cur_frame});
		if (s_ConstBufferPool->_buffer_pool.size()> 100u)
			LOG_INFO("[ConstBufferPool::Acquire]: Create a new constant buffer pool: {}", s_ConstBufferPool->_buffer_pool.size());
		return buffer;
	}
	#pragma endregion
}// namespace Ailu::Render
