#pragma once
#ifndef __BUFFER_H__
#define __BUFFER_H__

#include "GlobalMarco.h"

#include "Framework/Math/ALMath.hpp"

namespace Ailu
{
	enum class EShaderDateType
	{
		kNone = 0,kFloat, kFloat2, kFloat3, kFloat4,kMat3, kMat4,kInt, kInt2, kInt3, kInt4, kuInt, kuInt2, kuInt3, kuInt4,kBool
	};

	static u8 ShaderDateTypeSize(EShaderDateType type)
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
		u8 Size;
		u8 Offset;
		u8 Stream;
		VertexBufferLayoutDesc(std::string name,EShaderDateType type,u8 stream) : 
			Name(name),Type(type),Size(ShaderDateTypeSize(Type)),Stream(stream),Offset(0u)
		{}
		bool operator==(const VertexBufferLayoutDesc& other) const 
		{
			return (Name == other.Name &&Type == other.Type &&Size == other.Size &&Offset == other.Offset &&Stream == other.Stream);
		}
	};
	class VertexBufferLayout
	{
		DECLARE_PRIVATE_PROPERTY_RO(hash,Hash,u8)
	public:
		VertexBufferLayout() = default;
		VertexBufferLayout(const std::initializer_list<VertexBufferLayoutDesc>& desc_list) : _buffer_descs(desc_list) 
		{
			CalculateSizeAndStride();
			_hash = static_cast<u8>(ALHash::CommonRuntimeHasher(*this));
		}
		VertexBufferLayout(const Vector<VertexBufferLayoutDesc>& desc_list) : _buffer_descs(desc_list)
		{
			CalculateSizeAndStride();
			_hash = static_cast<u8>(ALHash::CommonRuntimeHasher(*this));
		}

		inline const u8 GetStreamCount() const {return _stream_count;}
		inline const std::vector<VertexBufferLayoutDesc>& GetBufferDesc() const { return _buffer_descs; }
		inline const u8 GetDescCount() const { return static_cast<u8>(_buffer_descs.size()); };
		inline const u8 GetStride(int stream) const { return _stride[stream]; }
		std::vector<VertexBufferLayoutDesc>::iterator begin() { return _buffer_descs.begin(); }
		std::vector<VertexBufferLayoutDesc>::iterator end() { return _buffer_descs.end(); }
		const std::vector<VertexBufferLayoutDesc>::const_iterator begin() const{ return _buffer_descs.begin(); }
		const std::vector<VertexBufferLayoutDesc>::const_iterator end() const{ return _buffer_descs.end(); }
		const VertexBufferLayoutDesc& operator[](const int& index)
		{
			return _buffer_descs[index];
		}
		bool operator==(const VertexBufferLayout& other) const
		{
			bool is_same = _stream_count == other._stream_count;
			is_same = is_same && (_buffer_descs.size() == other._buffer_descs.size());
			if (!is_same) 
				return false;
			for (size_t i = 0; i < _buffer_descs.size(); i++)
			{
				is_same = is_same && (_buffer_descs[i] == other._buffer_descs[i]);
				if (!is_same)
					return false;
			}
			return true;
		}
	private:
		void CalculateSizeAndStride()
		{
			u8 offset[kMaxStreamCount] = {};
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
		static constexpr u8 kMaxStreamCount = 6;
		u8 _stream_count = 0;
		std::vector<VertexBufferLayoutDesc> _buffer_descs; 
		//uint32_t _size[kMaxStreamCount] ;
		u8 _stride[kMaxStreamCount]{};
	};

	using VertexInputLayout = VertexBufferLayout;

	//namespace ALHash
	//{
	//	template<>
	//	static u32 Hasher(const VertexInputLayout& obj)
	//	{
	//		static Vector<VertexInputLayout> hashes;
	//		auto it = std::find(hashes.begin(), hashes.end(), obj);
	//		if (it != hashes.end())
	//			return std::distance(hashes.begin(), it);
	//		else
	//		{
	//			hashes.emplace_back(obj);
	//			return hashes.size() - 1;
	//		}
	//	}
	//}

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

