#pragma once
#ifndef __GFX_PIPELINE_STATE_H__
#define __GFX_PIPELINE_STATE_H__

#include <set>

#include "Framework/Math/ALMath.hpp"
#include "GlobalMarco.h"
#include "Buffer.h"
#include "Shader.h"
#include "AlgFormat.h"


//ref
//https://zhuanlan.zhihu.com/p/582020846
//https://alpqr.github.io/qtrhi/qrhigraphicspipeline.html#CompareOp-enum
//https://learn.microsoft.com/en-us/windows/win32/direct3d12/managing-graphics-pipeline-state-in-direct3d-12

namespace Ailu
{
	enum class ECompareFunc : uint8_t
	{
		kLess = 0, //depth default
		kLessEqual, kGreater, kGreaterEqual, kEqual, kNotEqual, kNever,
		kAlways //stencil default
	};

	enum class EStencilOp : uint8_t
	{
		kStencilZero = 0,
		kKeep,	//(default)
		kReplace, kIncrementAndClamp, kDecrementAndClamp, kInvert, kIncrementAndWrap, kDecrementAndWrap
	};

	enum class EBlendOp : uint8_t
	{
		kAdd = 0, kSubtract, kReverseSubtract, kMin, kMax
	};

	enum class EBlendFactor : uint8_t
	{
		kZero = 0, kOne, kSrcColor, kOneMinusSrcColor, kDstColor, kOneMinusDstColor, kSrcAlpha, kOneMinusSrcAlpha, kDstAlpha, kOneMinusDstAlpha, kConstantColor, kOneMinusConstantColor,
		kConstantAlpha, kOneMinusConstantAlpha, kSrcAlphaSaturate, kSrc1Color, kOneMinusSrc1Color, kSrc1Alpha, kOneMinusSrc1Alpha
	};

	enum class EColorMask : uint8_t
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

	enum class ETopology : uint8_t
	{
		kPoint = 0, kLine, kTriangle, kPatch
	};

	enum class ECullMode : uint8_t
	{
		kNone = 0, kFront, kBack
	};
	enum class EFillMode : uint8_t
	{
		kWireframe = 0, kSolid
	};

	struct RasterizerState
	{
		DECLARE_PRIVATE_PROPERTY_RO(hash, Hash, u8)
	public:
		ECullMode _cull_mode;
		EFillMode _fill_mode;
		bool _b_cw;
		RasterizerState() : RasterizerState(ECullMode::kBack, EFillMode::kSolid) {}
		RasterizerState(ECullMode cullMode, EFillMode fillMode, bool cw = true)
			: _cull_mode(cullMode), _fill_mode(fillMode), _b_cw(cw)
		{
			_hash = ALHash::CommonRuntimeHasher(*this);
		}

		bool operator==(const RasterizerState& other) const
		{
			return (_cull_mode == other._cull_mode &&
				_fill_mode == other._fill_mode &&
				_b_cw == other._b_cw);
		}
	};

	struct DepthStencilState
	{
		DECLARE_PRIVATE_PROPERTY_RO(hash, Hash, u8)
	public:
		bool _b_depth_write;
		ECompareFunc _depth_test_func;
		bool _b_front_stencil;
		ECompareFunc _stencil_test_func;
		EStencilOp _stencil_test_fail_op;
		EStencilOp _stencil_depth_test_fail_op;
		EStencilOp _stencil_test_pass_op;

		DepthStencilState(bool depthWrite, ECompareFunc depthTest, bool frontStencil = false,
			ECompareFunc stencilTest = ECompareFunc::kAlways, EStencilOp stencilFailOp = EStencilOp::kKeep,
			EStencilOp stencilDepthFailOp = EStencilOp::kKeep, EStencilOp stencilPassOp = EStencilOp::kKeep)
			: _b_depth_write(depthWrite),
			_depth_test_func(depthTest),
			_b_front_stencil(frontStencil),
			_stencil_test_func(stencilTest),
			_stencil_test_fail_op(stencilFailOp),
			_stencil_depth_test_fail_op(stencilDepthFailOp),
			_stencil_test_pass_op(stencilPassOp)
		{
			_hash = ALHash::CommonRuntimeHasher(*this);
		}

		DepthStencilState() : DepthStencilState(true, ECompareFunc::kLess) {}

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
	};

	struct BlendState
	{
		DECLARE_PRIVATE_PROPERTY_RO(hash, Hash, u8)
	public:
		bool _b_enable;
		EBlendFactor _dst_alpha;
		EBlendFactor _dst_color;
		EBlendFactor _src_alpha;
		EBlendFactor _src_color;
		EBlendOp _color_op;
		EBlendOp _alpha_op;
		EColorMask _color_mask;
		BlendState(bool enable = false,
			EBlendFactor srcColor = EBlendFactor::kSrcAlpha,
			EBlendFactor dstColor = EBlendFactor::kOneMinusSrcAlpha,
			EBlendFactor srcAlpha = EBlendFactor::kOne,
			EBlendFactor dstAlpha = EBlendFactor::kZero,
			EBlendOp colorOp = EBlendOp::kAdd,
			EBlendOp alphaOp = EBlendOp::kAdd,
			EColorMask colorMask = EColorMask::k_RGBA)
			: _b_enable(enable),
			_dst_alpha(dstAlpha),
			_dst_color(dstColor),
			_src_alpha(srcAlpha),
			_src_color(srcColor),
			_color_op(colorOp),
			_alpha_op(alphaOp),
			_color_mask(colorMask)
		{
			_hash = ALHash::CommonRuntimeHasher(*this);
		}
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
			case EALGFormat::kALGFormatRGB32_FLOAT:return 1;
			case EALGFormat::kALGFormatRGBA32_FLOAT:return 2;
			case EALGFormat::kALGFormatR32_FLOAT:return 3;
			}
		}
	}

	struct GraphicsPipelineStateInitializer
	{
		VertexInputLayout _input_layout; //0~3 4
		Ref<Shader> _p_vertex_shader; //4~13 10
		Ref<Shader> _p_pixel_shader; // 14~35 22
		ETopology _topology; //36~37 2
		BlendState _blend_state; // 38~40 3
		RasterizerState _raster_state;// 41 ~ 43 3
		DepthStencilState _depth_stencil_state;// 44~46 3
		bool _b_has_rt;
		uint8_t _rt_nums; // 47~49 3
		EALGFormat _rt_formats[8]; //50~52 3
		EALGFormat _ds_format; //53~54 2
		static GraphicsPipelineStateInitializer GetNormalOpaquePSODesc()
		{
			GraphicsPipelineStateInitializer pso_desc{};
			pso_desc._blend_state = BlendState{};
			pso_desc._b_has_rt = true;
			pso_desc._depth_stencil_state = TStaticDepthStencilState<true, ECompareFunc::kLess>::GetRHI();
			pso_desc._ds_format = EALGFormat::kALGFormatD24S8_UINT;
			pso_desc._rt_formats[0] = EALGFormat::kALGFormatR8G8B8A8_UNORM;
			pso_desc._rt_nums = 1;
			pso_desc._topology = ETopology::kTriangle;
			pso_desc._raster_state = TStaticRasterizerState<ECullMode::kBack, EFillMode::kSolid>::GetRHI();
			return pso_desc;
		}

		static GraphicsPipelineStateInitializer GetNormalTransparentPSODesc()
		{
			GraphicsPipelineStateInitializer pso_desc{};
			pso_desc._blend_state = TStaticBlendState<true, EBlendFactor::kSrcAlpha, EBlendFactor::kOneMinusSrcAlpha>::GetRHI();
			pso_desc._b_has_rt = true;
			pso_desc._depth_stencil_state = TStaticDepthStencilState<false, ECompareFunc::kAlways>::GetRHI();
			pso_desc._ds_format = EALGFormat::kALGFormatD24S8_UINT;
			pso_desc._rt_formats[0] = EALGFormat::kALGFormatR8G8B8A8_UNORM;
			pso_desc._rt_nums = 1;
			pso_desc._topology = ETopology::kTriangle;
			pso_desc._raster_state = TStaticRasterizerState<ECullMode::kBack, EFillMode::kSolid>::GetRHI();
			return pso_desc;
		}
	};

	class GraphicsPipelineState
	{
	public:
		inline static GraphicsPipelineState* sCurrent = 0;
		static Scope<GraphicsPipelineState> Create(const GraphicsPipelineStateInitializer& initializer);
		virtual ~GraphicsPipelineState() = default;
		virtual void Build() = 0;
		virtual void Bind() = 0;
		virtual void SubmitBindResource(void* res, const EBindResDescType& res_type, short slot = -1) = 0;
		virtual void SubmitBindResource(void* res, const EBindResDescType& res_type, const std::string& name) = 0;
		virtual const ALHash::Hash<64>& Hash() = 0;
	};

	enum EGraphicsPSO : uint32_t
	{
		kStandShadering = 0u,
		kWireFrame = 1u,
		kShadedWireFrame = 3u,
		kGizmo = 4u,
	};

	class GraphicsPipelineStateMgr
	{
	public:
		static void BuildPSOCache();
		static GraphicsPipelineState* GetPso(const uint32_t& id);
		inline static GraphicsPipelineState* s_standard_shadering_pso = nullptr;
		inline static GraphicsPipelineState* s_gizmo_pso = nullptr;
		inline static GraphicsPipelineState* s_wireframe_pso = nullptr;
		static void ConfigureShader(const u32& vs_hash, const u32& ps_hash);
		static void ConfigureVertexInputLayout(const u8& hash); //0~3 4
		static void ConfigureTopology(const u8& hash); //36~37 2
		static void ConfigureBlendState(const u8& hash); // 38~40 3
		static void ConfigureRasterizerState(const u8& hash);// 41 ~ 43 3
		static void ConfigureDepthStencilState(const u8& hash);// 44~46 3
		static void ConfigureRenderTarget(const u8& color_hash, const u8& depth_hash);// 44~46 3
		static void ConfigureRenderTarget(const u8& color_hash);// 44~46 3
	private:
		inline static std::unordered_map<uint32_t, Scope<GraphicsPipelineState>> s_pso_pool{};
		inline static uint32_t s_reserved_pso_id = 32u;
	};
}


#endif // !GFX_PIPELINE_STATE_H__