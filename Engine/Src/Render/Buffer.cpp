#include "pch.h"
#include "Render/Buffer.h"
#include "Render/Renderer.h"
#include "RHI/DX12/D3DBuffer.h"
#include "GlobalMarco.h"

namespace Ailu
{
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

	IIndexBuffer* IIndexBuffer::Create(u32* indices, u32 count, const String& name)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT_MSG(false, "None render api used!");
			return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
			return new D3DIndexBuffer(indices, count);
		}
		AL_ASSERT_MSG(false, "Unsupported render api!");
		return nullptr;
	}

	Ref<IDynamicVertexBuffer> IDynamicVertexBuffer::Create(const VertexBufferLayout& layout, const String& name)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT_MSG(false, "None render api used!");
			return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
			return std::static_pointer_cast<IDynamicVertexBuffer>(MakeRef<D3DDynamicVertexBuffer>(layout));
		}
		AL_ASSERT_MSG(false, "Unsupported render api!");
		return nullptr;
	}
	Ref<IDynamicVertexBuffer> IDynamicVertexBuffer::Create(const String& name)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT_MSG(false, "None render api used!");
			return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
			return std::static_pointer_cast<IDynamicVertexBuffer>(MakeRef<D3DDynamicVertexBuffer>());
		}
		AL_ASSERT_MSG(false, "Unsupported render api!");
		return nullptr;
	}

	IConstantBuffer* IConstantBuffer::Create(u32 size, bool compute_buffer, const String& name)
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
	void IConstantBuffer::Release(u8* ptr)
	{
		DESTORY_PTRARR(ptr);
	}
}
