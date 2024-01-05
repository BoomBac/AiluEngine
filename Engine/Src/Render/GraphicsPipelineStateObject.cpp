#include "pch.h"
#include "Render/Renderer.h"
#include "Render/Shader.h"
#include "Render/GraphicsPipelineStateObject.h"
#include "RHI/DX12/D3DGraphicsPipelineState.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/TimeMgr.h"

namespace Ailu
{
	Scope<GraphicsPipelineStateObject> GraphicsPipelineStateObject::Create(const GraphicsPipelineStateInitializer& initializer)
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
	ALHash::Hash<64> GraphicsPipelineStateObject::ConstuctPSOHash(u8 input_layout, u16 vs, u32 ps, u8 topology, u8 blend_state, u8 raster_state, u8 ds_state, u8 rt_state)
	{
		ALHash::Hash<64> hash;
		hash.Set(0, 4, input_layout);
		hash.Set(4, 10, vs);
		hash.Set(14, 22,ps);
		hash.Set(36, 2, topology);
		hash.Set(38, 3, blend_state);
		hash.Set(41, 3, raster_state);
		hash.Set(44, 3, ds_state);
		hash.Set(47, 3, rt_state);
		return hash;
	}

	ALHash::Hash<64> GraphicsPipelineStateObject::ConstuctPSOHash(const GraphicsPipelineStateInitializer& initializer)
	{
		return ConstuctPSOHash(initializer._p_vertex_shader->PipelineInputLayout().Hash(), initializer._p_vertex_shader->GetID(), initializer._p_pixel_shader->GetID(),
			ALHash::Hasher(initializer._p_pixel_shader->PipelineTopology()), initializer._p_pixel_shader->PipelineBlendState().Hash(),
			initializer._p_pixel_shader->PipelineRasterizerState().Hash(), initializer._p_pixel_shader->PipelineDepthStencilState().Hash(), initializer._rt_state.Hash());
	}

	void GraphicsPipelineStateMgr::BuildPSOCache()
	{
		LOG_WARNING("Begin initialize PSO cache...");
		g_pTimeMgr->Mark();
		auto p_standard_shader = ShaderLibrary::Load("Shaders/shaders.hlsl");
		auto pso_desc0 = GraphicsPipelineStateInitializer::GetNormalOpaquePSODesc();
		pso_desc0._p_vertex_shader = p_standard_shader;
		pso_desc0._p_pixel_shader = p_standard_shader;
		auto stand_pso = GraphicsPipelineStateObject::Create(pso_desc0);
		stand_pso->Build();
		s_standard_shadering_pso = stand_pso.get();
		s_pso_pool.insert(std::make_pair(EGraphicsPSO::kStandShadering,std::move(stand_pso)));

		auto p_wireframe_shader = ShaderLibrary::Load("Shaders/wireframe.hlsl");
		pso_desc0._p_vertex_shader = p_wireframe_shader;
		pso_desc0._p_pixel_shader = p_wireframe_shader;
		pso_desc0._depth_stencil_state = TStaticDepthStencilState<false, ECompareFunc::kLessEqual>::GetRHI();
		pso_desc0._raster_state = TStaticRasterizerState<ECullMode::kBack, EFillMode::kWireframe>::GetRHI();
		auto wireframe_pso = GraphicsPipelineStateObject::Create(pso_desc0);
		wireframe_pso->Build();
		s_wireframe_pso = wireframe_pso.get();
		s_pso_pool.insert(std::make_pair(EGraphicsPSO::kWireFrame, std::move(wireframe_pso)));

		auto p_gizmo_shader = ShaderLibrary::Load("Shaders/gizmo.hlsl");
		auto pso_desc1 = GraphicsPipelineStateInitializer::GetNormalTransparentPSODesc();
		pso_desc1._raster_state = TStaticRasterizerState<ECullMode::kBack, EFillMode::kWireframe>::GetRHI();
		pso_desc1._topology = ETopology::kLine;
		pso_desc1._p_vertex_shader = p_gizmo_shader;
		pso_desc1._p_pixel_shader = p_gizmo_shader;
		auto gizmo_pso = GraphicsPipelineStateObject::Create(pso_desc1);
		gizmo_pso->Build();
		s_gizmo_pso = gizmo_pso.get();
		s_pso_pool.insert(std::make_pair(EGraphicsPSO::kGizmo, std::move(gizmo_pso)));

		LOG_WARNING("Initialize PSO cache done after {}ms!",g_pTimeMgr->GetElapsedSinceLastMark());
	}

	void GraphicsPipelineStateMgr::AddPSO(Scope<GraphicsPipelineStateObject> p_gpso)
	{
		s_pso_library.insert(std::make_pair(p_gpso->Hash(), std::move(p_gpso)));
	}
	GraphicsPipelineStateObject* GraphicsPipelineStateMgr::GetPso(const uint32_t& id)
	{
		auto it = s_pso_pool.find(id);
		if (it != s_pso_pool.end()) return it->second.get();
		else
		{
			LOG_WARNING("Pso id: {} can't find!", id);
			return nullptr;
		}
	}

	void GraphicsPipelineStateMgr::EndConfigurePSO()
	{
		ConfigurePSOHash(s_cur_pos_hash,s_hash_input_layout,s_hash_vs,s_hash_ps,s_hash_topology,s_hash_blend_state,s_hash_raster_state,s_hash_depth_stencil_state,s_hash_rt_state);
		auto it = s_pso_library.find(s_cur_pos_hash);
		if (it != s_pso_library.end())
			it->second->Bind();
		else
		{
			//create pso
		}
	}

	void GraphicsPipelineStateMgr::ConfigurePSOHash(ALHash::Hash<64>& pso_hash, u8 input_layout, u16 vs, u32 ps, u8 topology, u8 blend_state, u8 raster_state, u8 ds_state, u8 rt_state)
	{
		pso_hash.Set(0, 4, input_layout);
		pso_hash.Set(4, 10, vs);
		pso_hash.Set(14, 22, ps);
		pso_hash.Set(36, 2, topology);
		pso_hash.Set(38, 3, blend_state);
		pso_hash.Set(41, 3, raster_state);
		pso_hash.Set(44, 3, ds_state);
		pso_hash.Set(47, 3, rt_state);
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

