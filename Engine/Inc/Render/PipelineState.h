#pragma once
#ifndef __PIPELINE_STATE_H__
#define __PIPELINE_STATE_H__
#include "GlobalMarco.h"
#include "AlgFormat.h"
#include "Framework/Math/ALMath.hpp"
#include "RenderConstants.h"

namespace Ailu
{
	template<typename T>
	T& PODCopy(const T* src,T* dst)
	{
		if (src != dst)
			memcpy(dst, src, sizeof(*dst));
		return *dst;
	}

	enum class ECompareFunc : u8
	{
		kLess = 0, //depth default
		kLessEqual, kGreater, kGreaterEqual, kEqual, kNotEqual, kNever,
		kAlways //stencil default
	};

	enum class EStencilOp : u8
	{
		kStencilZero = 0,
		kKeep,	//(default)
		kReplace, kIncrementAndClamp, kDecrementAndClamp, kInvert, kIncrementAndWrap, kDecrementAndWrap
	};

	enum class EBlendOp : u8
	{
		kAdd = 0, kSubtract, kReverseSubtract, kMin, kMax
	};

	enum class EBlendFactor : u8
	{
		kZero = 0, kOne, kSrcColor, kOneMinusSrcColor, kDstColor, kOneMinusDstColor, kSrcAlpha, kOneMinusSrcAlpha, kDstAlpha, kOneMinusDstAlpha, kConstantColor, kOneMinusConstantColor,
		kConstantAlpha, kOneMinusConstantAlpha, kSrcAlphaSaturate, kSrc1Color, kOneMinusSrc1Color, kSrc1Alpha, kOneMinusSrc1Alpha
	};

	enum class EColorMask : u8
	{
		kRED = 0x01,
		kGREEN = 0x02,
		kBLUE = 0x04,
		kALPHA = 0x08,

		k_NONE = 0,
		k_RGB = kRED | kGREEN | kBLUE,
		k_RGBA = kRED | kGREEN | kBLUE | kALPHA,
		k_RG = kRED | kGREEN,
		k_BA = kBLUE | kALPHA,
	};

	enum class ETopology : u8
	{
		kPoint = 0, kLine, kTriangle, kPatch
	};

	enum class ECullMode : u8
	{
		kNone = 0, kFront, kBack
	};
	enum class EFillMode : u8
	{
		kWireframe = 0, kSolid
	};

	enum class EShaderDateType
	{
		kNone = 0, kFloat, kFloat2, kFloat3, kFloat4, kMat3, kMat4, kInt, kInt2, kInt3, kInt4, kuInt, kuInt2, kuInt3, kuInt4, kBool
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
		VertexBufferLayoutDesc(std::string name, EShaderDateType type, u8 stream) :
			Name(name), Type(type), Size(ShaderDateTypeSize(Type)), Stream(stream), Offset(0u)
		{}
		bool operator==(const VertexBufferLayoutDesc& other) const
		{
			return (Name == other.Name && Type == other.Type && Size == other.Size && Offset == other.Offset && Stream == other.Stream);
		}
		VertexBufferLayoutDesc& operator=(const VertexBufferLayoutDesc& other)
		{
			Name = other.Name;
			Type = other.Type;
			Size = other.Size;
			Offset = other.Offset;
			Stream = other.Stream;
			return *this;
		}
	};

	template<class T>
	class PipelineStateHash
	{
	public:
		PipelineStateHash() = default;
		u8 GenHash(const T& obj)
		{
			for (u32 i = 0; i < _s_data.size(); i++)
			{
				if (_s_data[i] == obj)
					return i;
			}
			_s_data.push_back(obj);
			return static_cast<u8>(_s_data.size() - 1);
		}
		const T& Get(const u8& hash) const
		{
			if (hash < _s_data.size())
			{
				return _s_data[hash];
			}
			throw std::out_of_range("Invalid hash");
		}
	private:
		Vector<T> _s_data;
	};
	
	class VertexBufferLayout
	{
		DECLARE_PRIVATE_PROPERTY(hash, Hash, u8)
	public:
		inline static PipelineStateHash<VertexBufferLayout> _s_hash_obj{};
	public:
		VertexBufferLayout() = default;
		VertexBufferLayout(const std::initializer_list<VertexBufferLayoutDesc>& desc_list);
		VertexBufferLayout(const Vector<VertexBufferLayoutDesc>& desc_list);
		inline const u8 GetStreamCount() const { return _stream_count; }
		inline const std::vector<VertexBufferLayoutDesc>& GetBufferDesc() const { return _buffer_descs; }
		inline const u8 GetDescCount() const { return static_cast<u8>(_buffer_descs.size()); };
		inline const u8 GetStride(int stream) const { return _stride[stream]; }
		std::vector<VertexBufferLayoutDesc>::iterator begin() { return _buffer_descs.begin(); }
		std::vector<VertexBufferLayoutDesc>::iterator end() { return _buffer_descs.end(); }
		const std::vector<VertexBufferLayoutDesc>::const_iterator begin() const { return _buffer_descs.begin(); }
		const std::vector<VertexBufferLayoutDesc>::const_iterator end() const { return _buffer_descs.end(); }
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
		VertexBufferLayout& operator=(const VertexBufferLayout& other)
		{
			_hash = other._hash;
			_stream_count = other._stream_count;
			_buffer_descs = other._buffer_descs;
			memcpy(_stride, other._stride,sizeof(u8) * kMaxStreamCount);
			_hash = static_cast<u8>(ALHash::CommonRuntimeHasher(*this));
			return *this;
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
				desc.Offset += offset[min(desc.Stream, kMaxStreamCount)];
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
		u8 _stride[kMaxStreamCount]{};
	};

	using VertexInputLayout = VertexBufferLayout;

	struct RasterizerState
	{
		DECLARE_PRIVATE_PROPERTY(hash, Hash, u8)
	public:
		ECullMode _cull_mode;
		EFillMode _fill_mode;
		bool _b_cw;
		inline static PipelineStateHash<RasterizerState> _s_hash_obj{};
	public:
		RasterizerState();
		RasterizerState(ECullMode cullMode, EFillMode fillMode, bool cw = true);
		bool operator==(const RasterizerState& other) const
		{
			return (_cull_mode == other._cull_mode &&
				_fill_mode == other._fill_mode &&
				_b_cw == other._b_cw);
		}
		RasterizerState& operator=(const RasterizerState& other)
		{
			_hash = other._hash;
			_cull_mode = other._cull_mode;
			_fill_mode = other._fill_mode;
			_b_cw = other._b_cw;
			return *this;
		}
	};

	struct DepthStencilState
	{
		DECLARE_PRIVATE_PROPERTY(hash, Hash, u8)
	public:
		bool _b_depth_write;
		ECompareFunc _depth_test_func;
		bool _b_front_stencil;
		ECompareFunc _stencil_test_func;
		EStencilOp _stencil_test_fail_op;
		EStencilOp _stencil_depth_test_fail_op;
		EStencilOp _stencil_test_pass_op;
		inline static PipelineStateHash<DepthStencilState> _s_hash_obj{};
	public:
		DepthStencilState();
		DepthStencilState(bool depthWrite, ECompareFunc depthTest, bool frontStencil = false,
			ECompareFunc stencilTest = ECompareFunc::kAlways, EStencilOp stencilFailOp = EStencilOp::kKeep,
			EStencilOp stencilDepthFailOp = EStencilOp::kKeep, EStencilOp stencilPassOp = EStencilOp::kKeep);


		bool operator==(const DepthStencilState& other) const
		{
			return (_b_depth_write == other._b_depth_write &&
				_depth_test_func == other._depth_test_func &&
				_b_front_stencil == other._b_front_stencil &&
				_stencil_test_func == other._stencil_test_func &&
				_stencil_test_fail_op == other._stencil_test_fail_op &&
				_stencil_depth_test_fail_op == other._stencil_depth_test_fail_op &&
				_stencil_test_pass_op == other._stencil_test_pass_op);
		}

		DepthStencilState& operator=(const DepthStencilState& other)
		{
			return PODCopy(&other, this);
			//if (this != &other) // self-assignment check
			//{
			//	_b_depth_write = other._b_depth_write;
			//	_depth_test_func = other._depth_test_func;
			//	_b_front_stencil = other._b_front_stencil;
			//	_stencil_test_func = other._stencil_test_func;
			//	_stencil_test_fail_op = other._stencil_test_fail_op;
			//	_stencil_depth_test_fail_op = other._stencil_depth_test_fail_op;
			//	_stencil_test_pass_op = other._stencil_test_pass_op;

			//	_hash = other._hash;
			//}
			//return *this;
		}
	};

	struct BlendState
	{
		DECLARE_PRIVATE_PROPERTY(hash, Hash, u8)
	public:
		bool _b_enable;
		EBlendFactor _dst_alpha;
		EBlendFactor _dst_color;
		EBlendFactor _src_alpha;
		EBlendFactor _src_color;
		EBlendOp _color_op;
		EBlendOp _alpha_op;
		EColorMask _color_mask;
		inline static PipelineStateHash<BlendState> _s_hash_obj{};
	public:
		//default is disable blend
		BlendState(bool enable = false,
			EBlendFactor srcColor = EBlendFactor::kSrcAlpha,
			EBlendFactor dstColor = EBlendFactor::kOneMinusSrcAlpha,
			EBlendFactor srcAlpha = EBlendFactor::kOne,
			EBlendFactor dstAlpha = EBlendFactor::kZero,
			EBlendOp colorOp = EBlendOp::kAdd,
			EBlendOp alphaOp = EBlendOp::kAdd,
			EColorMask colorMask = EColorMask::k_RGBA);
			

		bool operator==(const BlendState& other) const
		{
			return (_b_enable == other._b_enable &&
				_dst_alpha == other._dst_alpha &&
				_dst_color == other._dst_color &&
				_src_alpha == other._src_alpha &&
				_src_color == other._src_color &&
				_color_op == other._color_op &&
				_alpha_op == other._alpha_op &&
				_color_mask == other._color_mask);
		}

		BlendState& operator=(const BlendState& other)
		{
			return PODCopy(&other, this);
			//if (this != &other) // self-assignment check
			//{
			//	_b_enable = other._b_enable;
			//	_dst_alpha = other._dst_alpha;
			//	_dst_color = other._dst_color;
			//	_src_alpha = other._src_alpha;
			//	_src_color = other._src_color;
			//	_color_op = other._color_op;
			//	_alpha_op = other._alpha_op;
			//	_color_mask = other._color_mask;

			//	_hash = other._hash;
			//}
			//return *this;
		}
	};


	struct RenderTargetState
	{
		DECLARE_PRIVATE_PROPERTY(hash, Hash, u8)
	public:
		static constexpr EALGFormat kDefaultColorRTFormat = RenderConstants::kColorRange == EColorRange::kLDR ? RenderConstants::kLDRFormat : RenderConstants::kHDRFormat;
		static constexpr EALGFormat kDefaultDepthRTFormat = EALGFormat::kALGFormatD24S8_UINT;
		EALGFormat _color_rt[8];
		u8 _color_rt_num = 0;
		EALGFormat _depth_rt;
	public:
		//static const RenderTargetState& Get(const u8& hash)
		//{
		//	AL_ASSERT(hash >= 8, "RenderTargetState hash is overflow 8");
		//	return PipelineStateAA::ps[hash];
		//}

		RenderTargetState();

		RenderTargetState(const std::initializer_list<EALGFormat>& color_rt, EALGFormat depth_rt);

		bool operator==(const RenderTargetState& other) const
		{
			bool is_equal = this->_color_rt_num == other._color_rt_num && this->_depth_rt == other._depth_rt;
			if(!is_equal) 
				return false;
			else 
			{
				for(int i = 0; i < _color_rt_num; i++)
					is_equal &= this->_color_rt[i] == other._color_rt[i];
			}
			return is_equal;
		}

		RenderTargetState& operator=(const RenderTargetState& other)
		{
			return PODCopy(&other, this);
		}
		inline static PipelineStateHash<RenderTargetState> _s_hash_obj{};
	private:
		inline static Vector<RenderTargetState> _s_rt_caches{};
	};

	template<bool enable, EBlendFactor src_color, EBlendFactor src_alpha, typename... Rest>
	class TStaticBlendState;

	template<bool enable, EBlendFactor src_color, EBlendFactor src_alpha>
	class TStaticBlendState<enable, src_color, src_alpha>
	{
	public:
		static BlendState GetRHI()
		{
			return BlendState(enable, src_color, src_alpha);
		}
	};


	template<ECullMode cull_mode, EFillMode fill_mode, typename... Rest>
	class TStaticRasterizerState;

	template<ECullMode cull_mode, EFillMode fill_mode>
	class TStaticRasterizerState<cull_mode, fill_mode>
	{
	public:
		static RasterizerState GetRHI()
		{
			return RasterizerState(cull_mode, fill_mode);
		}
	};


	template <bool DepthWrite, ECompareFunc DepthTest, typename... Rest>
	class TStaticDepthStencilState;

	template <bool DepthWrite, ECompareFunc DepthTest>
	class TStaticDepthStencilState<DepthWrite, DepthTest>
	{
	public:
		static DepthStencilState GetRHI()
		{
			return DepthStencilState(DepthWrite, DepthTest);
		}
	};

	template <bool DepthWrite, ECompareFunc DepthTest, typename T3>
	class TStaticDepthStencilState<DepthWrite, DepthTest, T3>
	{
	public:
		static DepthStencilState GetRHI()
		{
			return DepthStencilState(DepthWrite, DepthTest);
		}
	};

	namespace ALHash
	{
		template<>
		static u32 Hasher(const ETopology& obj)
		{
			switch (obj)
			{
			case ETopology::kPoint: return 0;
			case ETopology::kLine: return 1;
			case ETopology::kTriangle: return 2;
			case ETopology::kPatch: return 3;
			}
			return 2;
		}

		template<>
		static u32 Hasher(const EALGFormat& obj)
		{
			//separate color and depth hash
			switch (obj)
			{
			case EALGFormat::kALGFormatR24G8_TYPELESS:return 0;
			case EALGFormat::kALGFormatD32_FLOAT:return 1;
			case EALGFormat::kALGFormatR8G8B8A8_UNORM: return 0;
			case EALGFormat::kALGFormatR32G32B32A32_FLOAT: return 1;
			case EALGFormat::kALGFormatR32G32B32_FLOAT:return 2;
			case EALGFormat::kALGFormatR32_FLOAT:return 3;
			}
		}
	}

	ETopology static GetTopologyByHash(u8 hash)
	{
		static ETopology v[4]{ ETopology::kPoint ,ETopology::kLine, ETopology::kTriangle, ETopology::kPatch };
		return v[hash];
	}

	enum class EBindResDescType : uint8_t
	{
		kConstBuffer = 0, kTexture2D, kCubeMap, kTexture2DArray, kSampler, kCBufferAttribute, kUnknown
	};

	struct PipelineResourceInfo
	{
		void* _p_resource = nullptr;
		EBindResDescType _res_type = EBindResDescType::kUnknown;
		u8 _slot = 0;
	};

	class PipelineState
	{
	public:
		virtual void Bind() = 0;
		virtual void SetPipelineResource(u8 slot, void* res) = 0;
	};

	class GraphicsPipelineState : public PipelineState
	{

	};

	class ComputePipelineState : public PipelineState
	{

	};
}


#endif // !PIPELINE_STATE_H__

