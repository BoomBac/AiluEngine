#include "pch.h"
#include "Render/Renderer.h"
#include "Render/GraphicsPipelineState.h"
#include "RHI/DX12/D3DGraphicsPipelineState.h"
#include "Framework/Common/TimeMgr.h"

namespace Ailu
{
	GraphicsPipelineState* GraphicsPipelineState::Create(const GraphicsPipelineStateInitializer& initializer)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT(false, "None render api used!");
			return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
		{
			s_pipelinestates.insert(std::make_pair(s_pso_index, std::make_unique<D3DGraphicsPipelineState>(initializer)));
			sCurrent = s_pipelinestates[s_pso_index++].get();
			return sCurrent;
		}
		}
		AL_ASSERT(false, "Unsupport render api!");
		return nullptr;
	}
	void GraphicsPipelineStateMgr::BuildPSOCache()
	{
		LOG_WARNING("Begin initialize PSO cache...");
		auto p_standard_shader = ShaderLibrary::Add(GetResPath("Shaders/shaders.hlsl"));
		GraphicsPipelineStateInitializer pso_initializer{};
		pso_initializer._blend_state = BlendState{};
		pso_initializer._b_has_rt = true;
		pso_initializer._depth_stencil_state = TStaticDepthStencilState<true, ECompareFunc::kLess>::GetRHI();
		pso_initializer._ds_format = EALGFormat::kALGFormatD32_FLOAT;
		pso_initializer._rt_formats[0] = EALGFormat::kALGFormatR8G8B8A8_UNORM;
		pso_initializer._rt_nums = 1;
		pso_initializer._topology = ETopology::kTriangle;
		pso_initializer._p_vertex_shader = p_standard_shader;
		pso_initializer._p_pixel_shader = p_standard_shader;
		pso_initializer._raster_state = TStaticRasterizerState<ECullMode::kNone, EFillMode::kSolid>::GetRHI();
		GraphicsPipelineState* pso = GraphicsPipelineState::Create(pso_initializer);
		pso->Build();
		s_pso_pool.insert(EGraphicsPSO::kStandShaded,MakeScope<GraphicsPipelineState>(pso));

		g_pTimeMgr->Mark();

		LOG_WARNING("Initialize PSO cache done after {}ms!",g_pTimeMgr->GetElapsedSinceLastMark());
	}
	GraphicsPipelineState* GraphicsPipelineStateMgr::GetPso(const uint32_t& id)
	{
		return nullptr;
	}
}

