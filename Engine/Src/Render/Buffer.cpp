#include "pch.h"
#include "Render/Buffer.h"
#include "Render/Renderer.h"
#include "RHI/DX12/D3DBuffer.h"

namespace Ailu
{
	VertexBuffer* VertexBuffer::Create(VertexBufferLayout layout)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT(false, "None render api used!")
				return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
			return new D3DVertexBuffer(layout);
		}
		AL_ASSERT(false, "Unsupport render api!")
			return nullptr;
	}

	IndexBuffer* IndexBuffer::Create(uint32_t* indices, uint32_t count)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT(false, "None render api used!")
				return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
			return new D3DIndexBuffer(indices, count);
		}
		AL_ASSERT(false, "Unsupport render api!")
			return nullptr;
	}

	Ref<DynamicVertexBuffer> DynamicVertexBuffer::Create(const VertexBufferLayout& layout)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT(false, "None render api used!")
				return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
			return std::static_pointer_cast<DynamicVertexBuffer>(MakeRef<D3DDynamicVertexBuffer>(layout));
		}
		AL_ASSERT(false, "Unsupport render api!")
			return nullptr;
	}
	Ref<DynamicVertexBuffer> DynamicVertexBuffer::Create()
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT(false, "None render api used!")
				return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
			return std::static_pointer_cast<DynamicVertexBuffer>(MakeRef<D3DDynamicVertexBuffer>());
		}
		AL_ASSERT(false, "Unsupport render api!")
			return nullptr;
	}
}
