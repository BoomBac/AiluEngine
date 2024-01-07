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

	ALHash::Hash<64> GraphicsPipelineStateObject::ConstructPSOHash(u8 input_layout, u32 shader, u8 topology, u8 blend_state, u8 raster_state, u8 ds_state, u8 rt_state)
	{
		ALHash::Hash<64> hash;
		ConstructPSOHash(hash,input_layout, shader, topology, blend_state, raster_state, ds_state, rt_state);
		return hash;
	}

	void GraphicsPipelineStateObject::ConstructPSOHash(ALHash::Hash<64>& hash, u8 input_layout, u32 shader, u8 topology, u8 blend_state, u8 raster_state, u8 ds_state, u8 rt_state)
	{
		hash.Set(0, 4, input_layout);
		hash.Set(4, 32, shader);
		hash.Set(36, 2, topology);
		hash.Set(38, 3, blend_state);
		hash.Set(41, 3, raster_state);
		hash.Set(44, 3, ds_state);
		hash.Set(47, 3, rt_state);
	}

	ALHash::Hash<64> GraphicsPipelineStateObject::ConstructPSOHash(const GraphicsPipelineStateInitializer& initializer)
	{
		return ConstructPSOHash(initializer._p_vertex_shader->PipelineInputLayout().Hash(), initializer._p_pixel_shader->GetID(),
			ALHash::Hasher(initializer._p_pixel_shader->PipelineTopology()), initializer._p_pixel_shader->PipelineBlendState().Hash(),
			initializer._p_pixel_shader->PipelineRasterizerState().Hash(), initializer._p_pixel_shader->PipelineDepthStencilState().Hash(), initializer._rt_state.Hash());
	}

	void GraphicsPipelineStateObject::ExtractPSOHash(const ALHash::Hash<64>& pso_hash, u8& input_layout, u32& shader, u8& topology, u8& blend_state, u8& raster_state, u8& ds_state, u8& rt_state)
	{
		input_layout = static_cast<u8>(pso_hash.Get(0, 4));
		shader = pso_hash.Get(4, 32);
		topology = static_cast<u8>(pso_hash.Get(36, 2));
		blend_state = static_cast<u8>(pso_hash.Get(38, 3));
		raster_state = static_cast<u8>(pso_hash.Get(41, 3));
		ds_state = static_cast<u8>(pso_hash.Get(44, 3));
		rt_state = static_cast<u8>(pso_hash.Get(47, 3));
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
		s_is_ready = false;
		GraphicsPipelineStateObject::ConstructPSOHash(s_cur_pos_hash, s_hash_input_layout, s_hash_shader, s_hash_topology, s_hash_blend_state, s_hash_raster_state, s_hash_depth_stencil_state, s_hash_rt_state);
		auto it = s_pso_library.find(s_cur_pos_hash);
		u8 input_layout, topology, blend_state, raster_state, ds_state, rt_state;
		u32 shader;
		//for (auto& [hash, pso] : s_pso_library)
		//{
		//	GraphicsPipelineStateObject::ExtractPSOHash(hash, input_layout, shader, topology, blend_state, raster_state, ds_state, rt_state);
		//	LOG_INFO("Pso name:[{}], Input Layout: {}, Shader: {}, Topology: {}, Blend State: {}, Raster State: {}, Depth Stencil State: {}, Render Target State: {}, Hash: {}",
		//		pso->Name(), (u32)input_layout, shader, (u32)topology, (u32)blend_state, (u32)raster_state, (u32)ds_state, (u32)rt_state, hash.ToString());
		//}
		if (it != s_pso_library.end())
		{
			it->second->Bind();
			for (auto& res_info : s_bind_resource_list)
			{
				it->second->SetPipelineResource(res_info._p_resource,res_info._res_type,res_info._slot);
			}
			//LOG_INFO("PSO: {} been set", it->second->Name());
			s_is_ready = true;
		}
		else
		{
			GraphicsPipelineStateObject::ExtractPSOHash(s_cur_pos_hash, input_layout, shader, topology, blend_state, raster_state, ds_state, rt_state);
			LOG_INFO("s_cur_pos_hash////  Input Layout: {}, Shader: {}, Topology: {}, Blend State: {}, Raster State: {}, Depth Stencil State: {}, Render Target State: {}, Hash: {}",
				(u32)input_layout, shader, (u32)topology, (u32)blend_state, (u32)raster_state, (u32)ds_state, (u32)rt_state, s_cur_pos_hash.ToString());
			LOG_ERROR("Pso in need be create")
		}
		s_bind_resource_list.clear();
	}


	void GraphicsPipelineStateMgr::ConfigureShader(const u32& shader_hash)
	{
		s_hash_shader = shader_hash;
	}

	void GraphicsPipelineStateMgr::ConfigureVertexInputLayout(const u8& hash)
	{
		s_hash_input_layout = hash;
	}
	void GraphicsPipelineStateMgr::ConfigureTopology(const u8& hash)
	{
		s_hash_topology = hash;
	}
	void GraphicsPipelineStateMgr::ConfigureBlendState(const u8& hash)
	{
		s_hash_blend_state = hash;
	}
	void GraphicsPipelineStateMgr::ConfigureRasterizerState(const u8& hash)
	{
		s_hash_raster_state = hash;
	}
	void GraphicsPipelineStateMgr::ConfigureDepthStencilState(const u8& hash)
	{
		s_hash_depth_stencil_state = hash;
	}

	void GraphicsPipelineStateMgr::ConfigureRenderTarget(const u8& hash)
	{
		s_hash_rt_state = hash;
	}
	void GraphicsPipelineStateMgr::SubmitBindResource(void* res, const EBindResDescType& res_type, u8 slot)
	{
		s_bind_resource_list.emplace_back(PipelineResourceInfo{ res, res_type, slot });
	}
	void GraphicsPipelineStateMgr::SubmitBindResource(Texture* texture, u8 slot)
	{
		s_bind_resource_list.emplace_back(PipelineResourceInfo{ texture, EBindResDescType::kTexture2D, slot });
	}
}

