#include "pch.h"
#include "Render/Renderer.h"
#include "Render/Shader.h"
#include "Render/GraphicsPipelineStateObject.h"
#include "RHI/DX12/D3DGraphicsPipelineState.h"
#include "Framework/Common/TimeMgr.h"
#include "Framework/Common/ThreadPool.h"
#include "Framework/Common/ResourceMgr.h"

namespace Ailu
{
	//------------------------------------------------------------------------------GraphicsPipelineStateObject---------------------------------------------------------------------------------
	Scope<GraphicsPipelineStateObject> GraphicsPipelineStateObject::Create(const GraphicsPipelineStateInitializer& initializer)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT_MSG(false, "None render api used!");
			return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
			return std::move(MakeScope<D3DGraphicsPipelineState>(initializer));
		}
		AL_ASSERT_MSG(false, "Unsupported render api!");
		return nullptr;
	}

	ALHash::Hash<64> GraphicsPipelineStateObject::ConstructPSOHash(u8 input_layout, u32 shader, u8 topology, u8 blend_state, u8 raster_state, u8 ds_state, u8 rt_state)
	{
		ALHash::Hash<64> hash;
		ConstructPSOHash(hash, input_layout, shader, topology, blend_state, raster_state, ds_state, rt_state);
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
		return ConstructPSOHash(initializer._p_vertex_shader->PipelineInputLayout().Hash(), initializer._p_pixel_shader->ID(),
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
	void GraphicsPipelineStateObject::ExtractPSOHash(const ALHash::Hash<64>& pso_hash, u32& shader)
	{
		shader = pso_hash.Get(4, 32);
	}
	//------------------------------------------------------------------------------GraphicsPipelineStateObject---------------------------------------------------------------------------------



	//------------------------------------------------------------------------------GraphicsPipelineStateMgr---------------------------------------------------------------------------------

	RenderTargetState GraphicsPipelineStateMgr::_s_render_target_state;

	void GraphicsPipelineStateMgr::BuildPSOCache()
	{
		LOG_WARNING("Begin initialize PSO cache...");
		g_pTimeMgr->Mark();
		
		Shader* shader = g_pResourceMgr->Get<Shader>(L"Shaders/defered_standard_lit.alasset");
		auto pso_desc = GraphicsPipelineStateInitializer::GetNormalOpaquePSODesc();
		pso_desc._input_layout = shader->PipelineInputLayout();
		pso_desc._p_vertex_shader = shader;
		pso_desc._p_pixel_shader = shader;
		pso_desc._rt_state = RenderTargetState{ {EALGFormat::EALGFormat::kALGFormatR16G16_FLOAT,EALGFormat::EALGFormat::kALGFormatR8G8B8A8_UNORM,EALGFormat::EALGFormat::kALGFormatR8G8B8A8_UNORM},EALGFormat::EALGFormat::kALGFormatD24S8_UINT };
		auto stand_pso = GraphicsPipelineStateObject::Create(pso_desc);
		stand_pso->Build();
		AddPSO(std::move(stand_pso));

		memset(&pso_desc, 0, sizeof(GraphicsPipelineStateInitializer));
		shader = g_pResourceMgr->Get<Shader>(L"Shaders/deferred_lighting.alasset");
		pso_desc = GraphicsPipelineStateInitializer::GetNormalOpaquePSODesc();
		pso_desc._input_layout = shader->PipelineInputLayout();
		pso_desc._p_vertex_shader = shader;
		pso_desc._p_pixel_shader = shader;
		pso_desc._rt_state = RenderTargetState{ {EALGFormat::EALGFormat::kALGFormatR16G16B16A16_FLOAT},EALGFormat::EALGFormat::kALGFormatUnknown };
		stand_pso = GraphicsPipelineStateObject::Create(pso_desc);
		stand_pso->Build();
		AddPSO(std::move(stand_pso));

		memset(&pso_desc, 0, sizeof(GraphicsPipelineStateInitializer));
		shader = g_pResourceMgr->Get<Shader>(L"Shaders/blit.alasset");
		pso_desc = GraphicsPipelineStateInitializer::GetNormalOpaquePSODesc();
		pso_desc._input_layout = shader->PipelineInputLayout();
		pso_desc._p_vertex_shader = shader;
		pso_desc._p_pixel_shader = shader;
		pso_desc._rt_state = RenderTargetState{ {EALGFormat::EALGFormat::kALGFormatR16G16B16A16_FLOAT},EALGFormat::EALGFormat::kALGFormatUnknown };
		stand_pso = GraphicsPipelineStateObject::Create(pso_desc);
		stand_pso->Build();
		AddPSO(std::move(stand_pso));

		memset(&pso_desc, 0, sizeof(GraphicsPipelineStateInitializer));
		shader = g_pResourceMgr->Get<Shader>(L"Shaders/wireframe.alasset");
		pso_desc._p_vertex_shader = shader;
		pso_desc._p_pixel_shader = shader;
		pso_desc._depth_stencil_state = TStaticDepthStencilState<false, ECompareFunc::kLessEqual>::GetRHI();
		pso_desc._raster_state = TStaticRasterizerState<ECullMode::kBack, EFillMode::kWireframe>::GetRHI();
		auto wireframe_pso = GraphicsPipelineStateObject::Create(pso_desc);
		wireframe_pso->Build();
		AddPSO(std::move(wireframe_pso));

		memset(&pso_desc, 0, sizeof(GraphicsPipelineStateInitializer));
		shader = g_pResourceMgr->Get<Shader>(L"Shaders/gizmo.alasset");
		pso_desc = GraphicsPipelineStateInitializer::GetNormalTransparentPSODesc();
		pso_desc._blend_state._color_mask = EColorMask::k_RGB;//不写入alpha，否则其最后拷贝到GameView时还会和imgui背景色混合，会导致颜色不对
		pso_desc._depth_stencil_state = TStaticDepthStencilState<false, ECompareFunc::kLess>::GetRHI();
		pso_desc._raster_state = TStaticRasterizerState<ECullMode::kBack, EFillMode::kWireframe>::GetRHI();
		pso_desc._topology = ETopology::kLine;
		pso_desc._p_vertex_shader = shader;
		pso_desc._p_pixel_shader = shader;
		auto gizmo_pso = GraphicsPipelineStateObject::Create(pso_desc);
		gizmo_pso->Build();
		s_gizmo_pso = std::move(gizmo_pso);
		//AddPSO(std::move(gizmo_pso));

		memset(&pso_desc, 0, sizeof(GraphicsPipelineStateInitializer));
		shader = g_pResourceMgr->Get<Shader>(L"Shaders/depth_only.alasset");
		pso_desc._input_layout = shader->PipelineInputLayout();
		pso_desc._blend_state = shader->PipelineBlendState();
		pso_desc._raster_state = shader->PipelineRasterizerState();
		pso_desc._depth_stencil_state = shader->PipelineDepthStencilState();
		pso_desc._topology = shader->PipelineTopology();
		pso_desc._p_pixel_shader = shader;
		pso_desc._p_vertex_shader = shader;
		pso_desc._rt_state = RenderTargetState{ {EALGFormat::EALGFormat::kALGFormatUnknown},EALGFormat::EALGFormat::kALGFormatD24S8_UINT };
		auto pso = GraphicsPipelineStateObject::Create(pso_desc);
		pso->Build();
		GraphicsPipelineStateMgr::AddPSO(std::move(pso));

		memset(&pso_desc, 0, sizeof(GraphicsPipelineStateInitializer));
		shader = g_pResourceMgr->Get<Shader>(L"Shaders/cubemap_gen.alasset");
		pso_desc._input_layout = shader->PipelineInputLayout();
		pso_desc._blend_state = shader->PipelineBlendState();
		pso_desc._raster_state = shader->PipelineRasterizerState();
		pso_desc._depth_stencil_state = shader->PipelineDepthStencilState();
		pso_desc._topology = shader->PipelineTopology();
		pso_desc._p_pixel_shader = shader;
		pso_desc._p_vertex_shader = shader;
		pso_desc._rt_state = RenderTargetState{ {EALGFormat::EALGFormat::kALGFormatR16G16B16A16_FLOAT},EALGFormat::EALGFormat::kALGFormatUnknown };
		pso = std::move(GraphicsPipelineStateObject::Create(pso_desc));
		pso->Build();
		GraphicsPipelineStateMgr::AddPSO(std::move(pso));

		memset(&pso_desc, 0, sizeof(GraphicsPipelineStateInitializer));
		shader = g_pResourceMgr->Get<Shader>(L"Shaders/filter_irradiance.alasset");
		pso_desc._input_layout = shader->PipelineInputLayout();
		pso_desc._blend_state = shader->PipelineBlendState();
		pso_desc._raster_state = shader->PipelineRasterizerState();
		pso_desc._depth_stencil_state = shader->PipelineDepthStencilState();
		pso_desc._topology = shader->PipelineTopology();
		pso_desc._p_pixel_shader = shader;
		pso_desc._p_vertex_shader = shader;
		pso_desc._rt_state = RenderTargetState{ {EALGFormat::EALGFormat::kALGFormatR16G16B16A16_FLOAT},EALGFormat::EALGFormat::kALGFormatUnknown };
		pso = std::move(GraphicsPipelineStateObject::Create(pso_desc));
		pso->Build();
		GraphicsPipelineStateMgr::AddPSO(std::move(pso));

		LOG_WARNING("Initialize PSO cache done after {}ms!", g_pTimeMgr->GetElapsedSinceLastMark());
	}

	void GraphicsPipelineStateMgr::AddPSO(Scope<GraphicsPipelineStateObject> p_gpso)
	{
		auto it = s_pso_library.find(p_gpso->Hash());
		if (it != s_pso_library.end())
		{
			LOG_WARNING("Pso id: {} already exist!", p_gpso->Name() + p_gpso->Hash().ToString());
			s_pso_library[p_gpso->Hash()] = std::move(p_gpso);
		}
		else
		{
			LOG_WARNING("Add Pso id: {} to library!", p_gpso->Name() + p_gpso->Hash().ToString());
			s_pso_library.insert(std::make_pair(p_gpso->Hash(), std::move(p_gpso)));
		}
	}

	void GraphicsPipelineStateMgr::EndConfigurePSO(CommandBuffer* cmd)
	{
		s_is_ready = false;
		_s_render_target_state.Hash(RenderTargetState::_s_hash_obj.GenHash(_s_render_target_state));
		ConfigureRenderTarget(_s_render_target_state.Hash());
		GraphicsPipelineStateObject::ConstructPSOHash(s_cur_pos_hash, s_hash_input_layout, s_hash_shader, s_hash_topology, s_hash_blend_state, s_hash_raster_state, s_hash_depth_stencil_state, s_hash_rt_state);
		auto it = s_pso_library.find(s_cur_pos_hash);
		u8 input_layout, topology, blend_state, raster_state, ds_state, rt_state;
		u32 shader_id;
		if (it != s_pso_library.end())
		{
			it->second->Bind(cmd);
			for (auto& res_info : s_bind_resource_list)
			{
				it->second->SetPipelineResource(cmd, res_info._p_resource, res_info._res_type, res_info._slot);
			}
			s_is_ready = true;
		}
		else
		{
			GraphicsPipelineStateObject::ExtractPSOHash(s_cur_pos_hash, input_layout, shader_id, topology, blend_state, raster_state, ds_state, rt_state);
			LOG_INFO("Pos hash currently used:  Input Layout: {}, Shader: {}, Topology: {}, Blend State: {}, Raster State: {}, Depth Stencil State: {}, Render Target State: {}, Hash: {}",
				(u32)input_layout, shader_id, (u32)topology, (u32)blend_state, (u32)raster_state, (u32)ds_state, (u32)rt_state, s_cur_pos_hash.ToString());
			{
				auto new_shader = g_pResourceMgr->Get<Shader>(shader_id);
				GraphicsPipelineStateInitializer new_desc;
				new_desc._input_layout = VertexInputLayout::_s_hash_obj.Get(input_layout);
				new_desc._blend_state = BlendState::_s_hash_obj.Get(blend_state);
				new_desc._raster_state = RasterizerState::_s_hash_obj.Get(raster_state);
				new_desc._depth_stencil_state = DepthStencilState::_s_hash_obj.Get(ds_state);
				new_desc._topology = GetTopologyByHash(topology);
				new_desc._p_pixel_shader = new_shader;
				new_desc._p_vertex_shader = new_shader;
				new_desc._rt_state = RenderTargetState::_s_hash_obj.Get(rt_state);
				auto pso = std::move(GraphicsPipelineStateObject::Create(new_desc));
				pso->Build();
				for (auto& [hash, pso] : s_pso_library)
				{
					GraphicsPipelineStateObject::ExtractPSOHash(hash, input_layout, shader_id, topology, blend_state, raster_state, ds_state, rt_state);
					if (shader_id == new_shader->ID())
					{
						LOG_INFO("Similar pso name:[{}], Input Layout: {}, Shader: {}, Topology: {}, Blend State: {}, Raster State: {}, Depth Stencil State: {}, Render Target State: {}, Hash: {}",
							pso->Name(), (u32)input_layout, shader_id, (u32)topology, (u32)blend_state, (u32)raster_state, (u32)ds_state, (u32)rt_state, hash.ToString());
					}
				}
				GraphicsPipelineStateObject::ExtractPSOHash(pso->Hash(), input_layout, shader_id, topology, blend_state, raster_state, ds_state, rt_state);
				LOG_WARNING("New Pso name:[{}], Input Layout: {}, Shader: {}, Topology: {}, Blend State: {}, Raster State: {}, Depth Stencil State: {}, Render Target State: {}, Hash: {}",
					pso->Name(), (u32)input_layout, shader_id, (u32)topology, (u32)blend_state, (u32)raster_state, (u32)ds_state, (u32)rt_state, pso->Hash().ToString());
				//LOG_WARNING("new hash {}", pso->Hash().ToString());
				GraphicsPipelineStateMgr::AddPSO(std::move(pso));
			}
		}
		s_bind_resource_list.clear();
	}

	void GraphicsPipelineStateMgr::OnShaderRecompiled(Shader* shader)
	{
		//g_thread_pool->Enqueue([shader]() {
		//	GraphicsPipelineStateInitializer gpso_desc;
		//	gpso_desc._input_layout = shader->PipelineInputLayout();
		//	gpso_desc._blend_state = shader->PipelineBlendState();
		//	gpso_desc._raster_state = shader->PipelineRasterizerState();
		//	gpso_desc._depth_stencil_state = shader->PipelineDepthStencilState();
		//	gpso_desc._topology = shader->PipelineTopology();
		//	gpso_desc._p_pixel_shader = shader;
		//	gpso_desc._p_vertex_shader = shader;
		//	gpso_desc._rt_state = RenderTargetState{};
		//	auto pso = GraphicsPipelineStateObject::Create(gpso_desc);
		//	pso->Build();
		//	s_update_pso.emplace(std::move(pso));
		//	});

		GraphicsPipelineStateInitializer gpso_desc;
		u8 input_layout, topology, blend_state, raster_state, ds_state, rt_state;
		u32 shader_hash;
		gpso_desc._input_layout = shader->PipelineInputLayout();
		gpso_desc._p_pixel_shader = shader;
		gpso_desc._p_vertex_shader = shader;
		for (auto& pso : s_pso_library)
		{
			GraphicsPipelineStateObject::ExtractPSOHash(pso.first, input_layout, shader_hash, topology, blend_state, raster_state, ds_state, rt_state);
			if (shader_hash == shader->ID())
			{
				gpso_desc._blend_state = BlendState::_s_hash_obj.Get(blend_state);
				gpso_desc._raster_state = RasterizerState::_s_hash_obj.Get(raster_state);
				gpso_desc._depth_stencil_state = DepthStencilState::_s_hash_obj.Get(ds_state);
				gpso_desc._topology = GetTopologyByHash(topology);
				gpso_desc._rt_state = RenderTargetState::_s_hash_obj.Get(rt_state);
				auto pso = GraphicsPipelineStateObject::Create(gpso_desc);
				pso->Build();
				s_update_pso.emplace(std::move(pso));
			}
		}
		gpso_desc._blend_state = shader->PipelineBlendState();
		gpso_desc._raster_state = shader->PipelineRasterizerState();
		gpso_desc._depth_stencil_state = shader->PipelineDepthStencilState();
		gpso_desc._topology = shader->PipelineTopology();
		gpso_desc._rt_state = RenderTargetState{};
		auto pso = GraphicsPipelineStateObject::Create(gpso_desc);
		pso->Build();
		s_update_pso.emplace(std::move(pso));
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
	void GraphicsPipelineStateMgr::SetRenderTargetState(EALGFormat::EALGFormat color_format, EALGFormat::EALGFormat depth_format, u8 color_rt_id)
	{
		_s_render_target_state._color_rt[color_rt_id] = color_format;
		_s_render_target_state._color_rt_num = color_format == EALGFormat::EALGFormat::kALGFormatUnknown ? 0 : static_cast<u8>(color_rt_id + 1u);
		_s_render_target_state._depth_rt = depth_format;
	}
	void GraphicsPipelineStateMgr::SetRenderTargetState(EALGFormat::EALGFormat color_format, u8 color_rt_id)
	{
		_s_render_target_state._color_rt[color_rt_id] = color_format;
		_s_render_target_state._color_rt_num = color_format == EALGFormat::EALGFormat::kALGFormatUnknown ? 0 : static_cast<u8>(color_rt_id + 1u);
	}
	void GraphicsPipelineStateMgr::ResetRenderTargetState()
	{
		_s_render_target_state._color_rt_num = 0;
		for (int i = 0; i < 8; i++)
			_s_render_target_state._color_rt[i] = EALGFormat::EALGFormat::kALGFormatUnknown;
		_s_render_target_state._depth_rt = EALGFormat::EALGFormat::kALGFormatUnknown;
	}

	void GraphicsPipelineStateMgr::SubmitBindResource(void* res, const EBindResDescType& res_type, u8 slot)
	{
		s_bind_resource_list.emplace_back(PipelineResourceInfo{ res, res_type, slot });
	}
	void GraphicsPipelineStateMgr::SubmitBindResource(Texture* texture, u8 slot)
	{
		s_bind_resource_list.emplace_back(PipelineResourceInfo{ texture, EBindResDescType::kTexture2D, slot });
	}

	void GraphicsPipelineStateMgr::UpdateAllPSOObject()
	{
		while (!s_update_pso.empty())
		{
			AddPSO(std::move(s_update_pso.front()));
			s_update_pso.pop();
		}
	}

	//------------------------------------------------------------------------------GraphicsPipelineStateMgr---------------------------------------------------------------------------------
}

