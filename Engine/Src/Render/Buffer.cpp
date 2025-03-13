#include "pch.h"
#include "Render/Buffer.h"
#include "Render/Renderer.h"
#include "RHI/DX12/D3DBuffer.h"
#include "GlobalMarco.h"

namespace Ailu
{
    GPUBuffer *GPUBuffer::Create(GPUBufferDesc desc, const String &name)
    {
        switch (Renderer::GetAPI())
        {
            case RendererAPI::ERenderAPI::kNone:
                AL_ASSERT_MSG(false, "None render api used!");
                return nullptr;
            case RendererAPI::ERenderAPI::kDirectX12:
            {
                auto buf = new D3DGPUBuffer(desc);
                buf->Name(name);
                return buf;
            }
        }
        AL_ASSERT_MSG(false, "Unsupported render api!");
        return nullptr;
    }
	IVertexBuffer* IVertexBuffer::Create(VertexBufferLayout layout, const String& name)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT_MSG(false, "None render api used!");
			return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
		{
			auto buf = new D3DVertexBuffer(layout);
			return buf;
		}
		}
		AL_ASSERT_MSG(false, "Unsupported render api!");
		return nullptr;
	}

	IIndexBuffer* IIndexBuffer::Create(u32* indices, u32 count, const String& name,bool is_dynamic )
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT_MSG(false, "None render api used!");
			return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
			return new D3DIndexBuffer(indices, count,is_dynamic);
		}
		AL_ASSERT_MSG(false, "Unsupported render api!");
		return nullptr;
	}

    ConstantBuffer *ConstantBuffer::Create(u32 size, bool compute_buffer, const String& name)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT_MSG(false, "None render api used!");
			return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
			return new D3DConstantBuffer(size, compute_buffer);
		}
		AL_ASSERT_MSG(false, "Unsupported render api!");
		return nullptr;
	}
	void ConstantBuffer::Release(u8* ptr)
	{
		DESTORY_PTRARR(ptr);
	}
}
