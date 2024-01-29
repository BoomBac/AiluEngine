#pragma once
#ifndef __BUFFER_H__
#define __BUFFER_H__

#include "GlobalMarco.h"
#include "PipelineState.h"

namespace Ailu
{
	class VertexBuffer
	{
	public:
		static VertexBuffer* Create(VertexBufferLayout layout);
		virtual ~VertexBuffer() = default;
		virtual void Bind() const = 0;
		virtual void Bind(const VertexBufferLayout& pipeline_input_layout) const = 0;
		virtual void SetLayout(VertexBufferLayout layout) = 0;
		virtual void SetStream(float* vertices, uint32_t size, u8 stream_index) = 0;
		virtual void SetStream(u8* data, uint32_t size, u8 stream_index,bool dynamic = false) = 0;
		virtual u8* GetStream(u8 index) = 0;
		virtual const VertexBufferLayout& GetLayout() const = 0;
		virtual uint32_t GetVertexCount() const = 0;
	};
	
	class DynamicVertexBuffer
	{
	public:
		virtual ~DynamicVertexBuffer() = default;
		virtual void Bind() const = 0;
		static Ref<DynamicVertexBuffer> Create(const VertexBufferLayout& layout);
		static Ref<DynamicVertexBuffer> Create();
		virtual void UploadData() = 0;
		virtual void AppendData(float* data0, uint32_t num0,float* data1,uint32_t num1) = 0;
	};
	class IndexBuffer
	{
	public:
		virtual ~IndexBuffer() = default;
		virtual void Bind()const  = 0;
		virtual uint32_t GetCount() const = 0;
		static IndexBuffer* Create(uint32_t* indices, uint32_t count);
	};
}

#endif // !BUFFER_H__

