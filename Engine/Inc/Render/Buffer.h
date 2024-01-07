#pragma once
#ifndef __BUFFER_H__
#define __BUFFER_H__

#include "GlobalMarco.h"

#include "Framework/Math/ALMath.hpp"

#include "PipelineState.h"

namespace Ailu
{
	class VertexBuffer
	{
	public:
		virtual ~VertexBuffer() = default;
		virtual void Bind() const = 0;
		virtual void Bind(const Vector<String>& input_layout) = 0;
		//static VertexBuffer* Create(float* vertices,uint32_t size);
		static VertexBuffer* Create(VertexBufferLayout layout);
		virtual void SetLayout(VertexBufferLayout layout) = 0;
		virtual void SetStream(float* vertices, uint32_t size, u8 stream_index) = 0;
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

