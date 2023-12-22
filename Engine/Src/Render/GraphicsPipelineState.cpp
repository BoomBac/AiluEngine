#include "pch.h"
#include "Render/Renderer.h"
#include "Render/Shader.h"
#include "Render/GraphicsPipelineState.h"
#include "RHI/DX12/D3DGraphicsPipelineState.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/TimeMgr.h"

namespace Ailu
{
	Scope<GraphicsPipelineState> GraphicsPipelineState::Create(const GraphicsPipelineStateInitializer& initializer)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT(false, "None render api used!");
			return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
			return std::move(MakeScope<D3DGraphicsPipelineState>(initializer));
		}
		AL_ASSERT(false, "Unsupport render api!");
		return nullptr;
	}
	void GraphicsPipelineStateMgr::BuildPSOCache()
	{
		LOG_WARNING("Begin initialize PSO cache...");
		g_pTimeMgr->Mark();
		auto p_standard_shader = ShaderLibrary::Load("Shaders/shaders.hlsl");
		auto pso_desc0 = GraphicsPipelineStateInitializer::GetNormalOpaquePSODesc();
		pso_desc0._p_vertex_shader = p_standard_shader;
		pso_desc0._p_pixel_shader = p_standard_shader;
		auto stand_pso = GraphicsPipelineState::Create(pso_desc0);
		stand_pso->Build();
		s_standard_shadering_pso = stand_pso.get();
		s_pso_pool.insert(std::make_pair(EGraphicsPSO::kStandShadering,std::move(stand_pso)));

		auto p_wireframe_shader = ShaderLibrary::Load("Shaders/wireframe.hlsl");
		pso_desc0._p_vertex_shader = p_wireframe_shader;
		pso_desc0._p_pixel_shader = p_wireframe_shader;
		pso_desc0._depth_stencil_state = TStaticDepthStencilState<false, ECompareFunc::kLessEqual>::GetRHI();
		pso_desc0._raster_state = TStaticRasterizerState<ECullMode::kBack, EFillMode::kWireframe>::GetRHI();
		auto wireframe_pso = GraphicsPipelineState::Create(pso_desc0);
		wireframe_pso->Build();
		s_wireframe_pso = wireframe_pso.get();
		s_pso_pool.insert(std::make_pair(EGraphicsPSO::kWireFrame, std::move(wireframe_pso)));

		auto p_gizmo_shader = ShaderLibrary::Load("Shaders/gizmo.hlsl");
		auto pso_desc1 = GraphicsPipelineStateInitializer::GetNormalTransparentPSODesc();
		pso_desc1._raster_state = TStaticRasterizerState<ECullMode::kBack, EFillMode::kWireframe>::GetRHI();
		pso_desc1._topology = ETopology::kLine;
		pso_desc1._p_vertex_shader = p_gizmo_shader;
		pso_desc1._p_pixel_shader = p_gizmo_shader;
		auto gizmo_pso = GraphicsPipelineState::Create(pso_desc1);
		gizmo_pso->Build();
		s_gizmo_pso = gizmo_pso.get();
		s_pso_pool.insert(std::make_pair(EGraphicsPSO::kGizmo, std::move(gizmo_pso)));

		LOG_WARNING("Initialize PSO cache done after {}ms!",g_pTimeMgr->GetElapsedSinceLastMark());
	}
	GraphicsPipelineState* GraphicsPipelineStateMgr::GetPso(const uint32_t& id)
	{
		auto it = s_pso_pool.find(id);
		if (it != s_pso_pool.end()) return it->second.get();
		else
		{
			LOG_WARNING("Pso id: {} can't find!", id);
			return nullptr;
		}
	}
	void GraphicsPipelineStateMgr::ConfigureShader(const u32& vs_hash, const u32& ps_hash)
	{
	}
	void GraphicsPipelineStateMgr::ConfigureVertexInputLayout(const u8& hash)
	{
	}
	void GraphicsPipelineStateMgr::ConfigureTopology(const u8& hash)
	{
	}
	void GraphicsPipelineStateMgr::ConfigureBlendState(const u8& hash)
	{
	}
	void GraphicsPipelineStateMgr::ConfigureRasterizerState(const u8& hash)
	{
	}
	void GraphicsPipelineStateMgr::ConfigureDepthStencilState(const u8& hash)
	{
	}
	void GraphicsPipelineStateMgr::ConfigureRenderTarget(const u8& color_hash, const u8& depth_hash)
	{
	}
	void GraphicsPipelineStateMgr::ConfigureRenderTarget(const u8& color_hash)
	{
	}
}

