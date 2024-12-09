#include "pch.h"
#include "Render/PipelineState.h"

namespace Ailu
{
	//------------------------------------------------------------------------------------RenderTargetState--------------------------------------------------------------------------------------------------
	RenderTargetState::RenderTargetState()
	{
		_color_rt[0] = kDefaultColorRTFormat;
		_color_rt_num = 1;
		_depth_rt = kDefaultDepthRTFormat;
		_hash = _s_hash_obj.GenHash(*this);
	}
	RenderTargetState::RenderTargetState(const std::initializer_list<EALGFormat::EALGFormat>& color_rt, EALGFormat::EALGFormat depth_rt)
	{
		for (auto& f : color_rt)
			_color_rt[_color_rt_num++] = f;
		if (_color_rt[0] == EALGFormat::EALGFormat::kALGFormatUnknown)
			_color_rt_num = 0;
		_depth_rt = depth_rt;
		_hash = _s_hash_obj.GenHash(*this);
	}
	//------------------------------------------------------------------------------------RenderTargetState--------------------------------------------------------------------------------------------------

	//------------------------------------------------------------------------------------BlendState--------------------------------------------------------------------------------------------------
	BlendState::BlendState(bool enable, EBlendFactor srcColor, EBlendFactor dstColor, EBlendFactor srcAlpha, EBlendFactor dstAlpha, EBlendOp colorOp, EBlendOp alphaOp, EColorMask colorMask)
		: _b_enable(enable),
		_dst_alpha(dstAlpha),
		_dst_color(dstColor),
		_src_alpha(srcAlpha),
		_src_color(srcColor),
		_color_op(colorOp),
		_alpha_op(alphaOp),
		_color_mask(colorMask)
	{
		_hash = _s_hash_obj.GenHash(*this);
	}


	//------------------------------------------------------------------------------------BlendState--------------------------------------------------------------------------------------------------
	


	//------------------------------------------------------------------------------------DepthStencilState--------------------------------------------------------------------------------------------------
	DepthStencilState::DepthStencilState() : DepthStencilState(true, ECompareFunc::kLessEqual)
	{
	}

	DepthStencilState::DepthStencilState(bool depthWrite, ECompareFunc depthTest, bool frontStencil, ECompareFunc stencilTest, EStencilOp stencilFailOp, EStencilOp stencilDepthFailOp, EStencilOp stencilPassOp)
		: _b_depth_write(depthWrite),
		_depth_test_func(depthTest),
		_b_front_stencil(frontStencil),
		_stencil_test_func(stencilTest),
		_stencil_test_fail_op(stencilFailOp),
		_stencil_depth_test_fail_op(stencilDepthFailOp),
		_stencil_test_pass_op(stencilPassOp)
	{
		_hash = _s_hash_obj.GenHash(*this);
	}
	//------------------------------------------------------------------------------------DepthStencilState--------------------------------------------------------------------------------------------------
	// 
	// 
	//------------------------------------------------------------------------------------RasterizerState--------------------------------------------------------------------------------------------------
	RasterizerState::RasterizerState() : RasterizerState(ECullMode::kBack, EFillMode::kSolid)
	{
	}
	RasterizerState::RasterizerState(ECullMode cullMode, EFillMode fillMode, bool cw)
        : _cull_mode(cullMode), _fill_mode(fillMode), _b_cw(cw), _is_conservative(false)
	{
		_hash = _s_hash_obj.GenHash(*this);
	}
	//------------------------------------------------------------------------------------RasterizerState--------------------------------------------------------------------------------------------------
	// 
	// 
	// 
	//------------------------------------------------------------------------------------VertexBufferLayout--------------------------------------------------------------------------------------------------
	VertexBufferLayout::VertexBufferLayout(const std::initializer_list<VertexBufferLayoutDesc>& desc_list) : _buffer_descs(desc_list)
	{
		CalculateSizeAndStride();
		_hash = _s_hash_obj.GenHash(*this);
	}
	VertexBufferLayout::VertexBufferLayout(const Vector<VertexBufferLayoutDesc>& desc_list) : _buffer_descs(desc_list)
	{
		CalculateSizeAndStride();
		_hash = _s_hash_obj.GenHash(*this);
	}
	//------------------------------------------------------------------------------------VertexBufferLayout--------------------------------------------------------------------------------------------------
}