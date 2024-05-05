#pragma once
#ifndef __BUFFER_H__
#define __BUFFER_H__

#include "GlobalMarco.h"
#include "PipelineState.h"

namespace Ailu
{
	class CommandBuffer;
	class VertexBuffer
	{
	public:
		static VertexBuffer* Create(VertexBufferLayout layout);
		virtual ~VertexBuffer() = default;
		virtual void Bind(CommandBuffer* cmd,const VertexBufferLayout& pipeline_input_layout) const = 0;
		virtual void SetLayout(VertexBufferLayout layout) = 0;
		virtual void SetStream(float* vertices, u32 size, u8 stream_index) = 0;
		virtual void SetStream(u8* data, u32 size, u8 stream_index,bool dynamic = false) = 0;
		virtual u8* GetStream(u8 index) = 0;
		virtual const VertexBufferLayout& GetLayout() const = 0;
		virtual u32 GetVertexCount() const = 0;
	};
	
	class DynamicVertexBuffer
	{
	public:
		virtual ~DynamicVertexBuffer() = default;
		virtual void Bind(CommandBuffer* cmd) const = 0;
		static Ref<DynamicVertexBuffer> Create(const VertexBufferLayout& layout);
		static Ref<DynamicVertexBuffer> Create();
		virtual void UploadData() = 0;
		virtual void AppendData(float* data0, u32 num0,float* data1,u32 num1) = 0;
	};
	class IndexBuffer
	{
	public:
		virtual ~IndexBuffer() = default;
		virtual void Bind(CommandBuffer* cmd) const = 0;
		virtual u32 GetCount() const = 0;
		static IndexBuffer* Create(u32* indices, u32 count);
	};

	class ConstantBuffer
	{
	public:
		static ConstantBuffer* Create(u32 size,bool compute_buffer = false);
		static void Release(u8* ptr);
		virtual ~ConstantBuffer() = default;
		virtual void Bind(CommandBuffer* cmd,u8 bind_slot) const = 0;
		virtual u8* GetData() = 0;
	};
}

#endif // !BUFFER_H__

