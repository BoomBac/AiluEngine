#pragma once
#ifndef __GFX_PIPELINE_STATE_H__
#define __GFX_PIPELINE_STATE_H__
#include <map>

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
        kLessEqual,kGreater,kGreaterEqual,kEqual,kNotEqual,kNever,
        kAlways //stencil default
    };

    enum class EStencilOp : uint8_t
    {
        kStencilZero = 0,
        kKeep,	//(default)
        kReplace,kIncrementAndClamp,kDecrementAndClamp,kInvert,kIncrementAndWrap,kDecrementAndWrap
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
        kPoint = 0,kLine,kTriangle,kPatch
    };

    enum class ECullMode : uint8_t
    {
        kNone = 0, kFront,kBack
    };
    enum class EFillMode : uint8_t
    {
        kWireframe = 0, kSolid
    };

    struct RasterizerState
    {
        ECullMode _cull_mode;
        EFillMode _fill_mode;
        bool _b_cw;
        RasterizerState() : RasterizerState(ECullMode::kBack,EFillMode::kSolid) {}
        RasterizerState(ECullMode cullMode, EFillMode fillMode,bool cw = true)
            : _cull_mode(cullMode), _fill_mode(fillMode),_b_cw(cw)
        {
        }
    };

    struct DepthStencilState
    {
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
        }

        DepthStencilState() : DepthStencilState(true, ECompareFunc::kLess) {}
    };

    struct BlendState
    {
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
            EBlendFactor srcAlpha = EBlendFactor::kOne,
            EBlendFactor dstColor = EBlendFactor::kOneMinusSrcAlpha,
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
        }
    };



    template<bool enable,EBlendFactor src_color, EBlendFactor src_alpha, typename... Rest>
    class TStaticBlendState;

    template<bool enable,EBlendFactor src_color,EBlendFactor src_alpha>
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

    struct GraphicsPipelineStateInitializer
    {
        VertexInputLayout _input_layout;
        Ref<Shader> _p_vertex_shader;
        Ref<Shader> _p_pixel_shader;
        ETopology _topology;
        BlendState _blend_state;
        RasterizerState _raster_state;
        DepthStencilState _depth_stencil_state;
        bool _b_has_rt;
        uint8_t _rt_nums;
        EALGFormat _rt_formats[8];
        EALGFormat _ds_format;
    };

	class GraphicsPipelineState
	{
    public:
        inline static GraphicsPipelineState* sCurrent = 0;
        static GraphicsPipelineState* Create(const GraphicsPipelineStateInitializer& initializer);
        virtual ~GraphicsPipelineState() = default;
        virtual void Build() = 0;
        virtual void Bind() = 0;
        virtual void CommitBindResource(uint16_t slot, void* res, EBindResDescType res_type) = 0;
    private:
        inline static std::map<uint32_t, Scope<GraphicsPipelineState>> s_pipelinestates{};
        inline static uint32_t s_pso_index = 0u;
	};

    class GraphicsPipelineStateMgr
    {
    //public:
    //    static void BuildPSOCache();
    //private:
    //    inline static 
    };
}


#endif // !GFX_PIPELINE_STATE_H__

