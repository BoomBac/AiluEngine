#pragma once
#ifndef __BUFFER_H__
#define __BUFFER_H__

#include "GlobalMarco.h"

namespace Ailu
{
	enum class EShaderDateType
	{
		kNone = 0,kFloat, kFloat2, kFloat3, kFloat4,kMat3, kMat4,kInt, kInt2, kInt3, kInt4, kuInt, kuInt2, kuInt3, kuInt4,kBool
	};

	static uint8_t ShaderDateTypeSize(EShaderDateType type)
	{
		switch (type)
		{
			case Ailu::EShaderDateType::kFloat:	 return 4;
			case Ailu::EShaderDateType::kFloat2: return 8;
			case Ailu::EShaderDateType::kFloat3: return 12;
			case Ailu::EShaderDateType::kFloat4: return 16;
			case Ailu::EShaderDateType::kMat3:	 return 36;
			case Ailu::EShaderDateType::kMat4:	 return 64;
			case Ailu::EShaderDateType::kInt:	 return 4;
			case Ailu::EShaderDateType::kInt2:	 return 8;
			case Ailu::EShaderDateType::kInt3:	 return 12;
			case Ailu::EShaderDateType::kInt4:	 return 16;
			case Ailu::EShaderDateType::kuInt:	 return 4;
			case Ailu::EShaderDateType::kuInt2:	 return 8;
			case Ailu::EShaderDateType::kuInt3:	 return 12;
			case Ailu::EShaderDateType::kuInt4:	 return 16;
			case Ailu::EShaderDateType::kBool:	 return 1;
		}
		AL_ASSERT(true, "Unknown ShaderDateType!");
		return 0;
	}

	struct VertexBufferLayoutDesc
	{
		std::string Name;
		EShaderDateType Type;
		uint8_t Size;
		uint8_t Offset;
		uint8_t Stream;
		VertexBufferLayoutDesc(std::string name,EShaderDateType type,uint8_t stream) : 
			Name(name),Type(type),Size(ShaderDateTypeSize(Type)),Stream(stream),Offset(0u)
		{}
	};
	class VertexBufferLayout
	{
	public:
		VertexBufferLayout() = default;
		VertexBufferLayout(const std::initializer_list<VertexBufferLayoutDesc>& desc_list) : _buffer_descs(desc_list) 
		{
			CalculateSizeAndStride();
		}
		inline const uint8_t GetStreamCount() const {return _stream_count;}
		inline const std::vector<VertexBufferLayoutDesc>& GetBufferDesc() const { return _buffer_descs; }
		inline const uint8_t GetDescCount() const { return static_cast<uint8_t>(_buffer_descs.size()); };
		inline const uint8_t GetStride(int stream) const { return _stride[stream]; }
		std::vector<VertexBufferLayoutDesc>::iterator begin() { return _buffer_descs.begin(); }
		std::vector<VertexBufferLayoutDesc>::iterator end() { return _buffer_descs.end(); }
		const std::vector<VertexBufferLayoutDesc>::const_iterator begin() const{ return _buffer_descs.begin(); }
		const std::vector<VertexBufferLayoutDesc>::const_iterator end() const{ return _buffer_descs.end(); }
	private:
		void CalculateSizeAndStride()
		{
			uint8_t offset[kMaxStreamCount] = {};
			for (auto& desc : _buffer_descs)
			{
				if (desc.Stream >= kMaxStreamCount)
				{
					AL_ASSERT(true, "Stream count must less than kMaxStreamCount");
					return;
				}
				desc.Offset += offset[min(desc.Stream,kMaxStreamCount)];
				offset[desc.Stream] += desc.Size;
				_stride[desc.Stream] += desc.Size;
			}
			for (size_t i = 0; i < kMaxStreamCount; i++)
			{
				if (_stride[i] != 0) ++_stream_count;
			}
		}
	private:
		static constexpr uint8_t kMaxStreamCount = 6;
		uint8_t _stream_count = 0;
		std::vector<VertexBufferLayoutDesc> _buffer_descs; 
		//uint32_t _size[kMaxStreamCount] ;
		uint8_t _stride[kMaxStreamCount]{};
	};

	using VertexInputLayout = VertexBufferLayout;

	class VertexBuffer
	{
	public:
		virtual ~VertexBuffer() = default;
		virtual void Bind() const = 0;
		//static VertexBuffer* Create(float* vertices,uint32_t size);
		static VertexBuffer* Create(VertexBufferLayout layout);
		virtual void SetLayout(VertexBufferLayout layout) = 0;
		virtual void SetStream(float* vertices, uint32_t size, uint8_t stream_index) = 0;
		virtual const VertexBufferLayout& GetLayout() const = 0;
		virtual uint32_t GetVertexCount() const = 0;
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

