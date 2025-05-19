#include "pch.h"
#include "Render/Buffer.h"
#include "Render/Renderer.h"
#include "RHI/DX12/D3DBuffer.h"
#include "GlobalMarco.h"

namespace Ailu::Render
{
	#pragma region GPUBuffer
    GPUBuffer *GPUBuffer::Create(GPUBufferDesc desc, const String &name)
    {
        switch (Renderer::GetAPI())
        {
            case RendererAPI::ERenderAPI::kNone:
                AL_ASSERT_MSG(false, "None render api used!");
                return nullptr;
            case RendererAPI::ERenderAPI::kDirectX12:
            {
                auto buf = new RHI::DX12::D3DGPUBuffer(desc);
                buf->Name(name);
				GraphicsContext::Get().CreateResource(buf);
                return buf;
            }
        }
        AL_ASSERT_MSG(false, "Unsupported render api!");
        return nullptr;
    }

	GPUBuffer *GPUBuffer::Create(EGPUBufferTarget target,u32 element_size,u32 element_num, const String &name)
	{
		GPUBufferDesc desc;
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
			return new RHI::DX12::D3DIndexBuffer(indices, count,is_dynamic);
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
        memcpy(_data, data, size * sizeof(u32));
        _count = size / sizeof(u32);
	}
	#pragma endregion

	#pragma region ConstantBuffer
    ConstantBuffer *ConstantBuffer::Create(u32 size, bool compute_buffer, const String& name)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT_MSG(false, "None render api used!");
			return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
			return new RHI::DX12::D3DConstantBuffer(size, compute_buffer);
		}
		AL_ASSERT_MSG(false, "Unsupported render api!");
		return nullptr;
	}
	void ConstantBuffer::Release(ConstantBuffer* ptr)
	{
		DESTORY_PTR(ptr);
	}
	#pragma endregion
}
