#include "pch.h"
#include "Render/Renderer.h"
#include "Render/Shader.h"
#include "Render/GraphicsPipelineStateObject.h"
#include "RHI/DX12/D3DGraphicsPipelineState.h"
#include "Framework/Common/TimeMgr.h"
#include "Framework/Common/ThreadPool.h"

namespace Ailu
{
	//------------------------------------------------------------------------------GraphicsPipelineStateObject---------------------------------------------------------------------------------
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
		AL_ASSERT(false, "Unsupported render api!");
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
		return ConstructPSOHash(initializer._p_vertex_shader->PipelineInputLayout().Hash(), initializer._p_pixel_shader->ActualID(),
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
		auto p_standard_shader = ShaderLibrary::Load("Shaders/shaders.hlsl");
		auto pso_desc0 = GraphicsPipelineStateInitializer::GetNormalOpaquePSODesc();
		pso_desc0._p_vertex_shader = p_standard_shader.get();
		pso_desc0._p_pixel_shader = p_standard_shader.get();
		auto stand_pso = GraphicsPipelineStateObject::Create(pso_desc0);
		stand_pso->Build();
		s_standard_shadering_pso = stand_pso.get();
		s_pso_pool.insert(std::make_pair(EGraphicsPSO::kStandShadering, std::move(stand_pso)));

		auto p_wireframe_shader = ShaderLibrary::Load("Shaders/wireframe.hlsl");
		pso_desc0._p_vertex_shader = p_wireframe_shader.get();
		pso_desc0._p_pixel_shader = p_wireframe_shader.get();
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
		pso_desc1._p_vertex_shader = p_gizmo_shader.get();
		pso_desc1._p_pixel_shader = p_gizmo_shader.get();
		auto gizmo_pso = GraphicsPipelineStateObject::Create(pso_desc1);
		gizmo_pso->Build();
		s_gizmo_pso = gizmo_pso.get();
		s_pso_pool.insert(std::make_pair(EGraphicsPSO::kGizmo, std::move(gizmo_pso)));

		auto p_shadowcast_shader = ShaderLibrary::Load("Shaders/depth_only.hlsl");
		GraphicsPipelineStateInitializer gpso_desc;
		gpso_desc._input_layout = p_shadowcast_shader->PipelineInputLayout();
		gpso_desc._blend_state = p_shadowcast_shader->PipelineBlendState();
		gpso_desc._raster_state = p_shadowcast_shader->PipelineRasterizerState();
		gpso_desc._depth_stencil_state = p_shadowcast_shader->PipelineDepthStencilState();
		gpso_desc._topology = p_shadowcast_shader->PipelineTopology();
		gpso_desc._p_pixel_shader = p_shadowcast_shader.get();
		gpso_desc._p_vertex_shader = p_shadowcast_shader.get();
		gpso_desc._rt_state = RenderTargetState{ {EALGFormat::kALGFormatUnknown},EALGFormat::kALGFormatD24S8_UINT };
		auto pso = GraphicsPipelineStateObject::Create(gpso_desc);
		pso->Build();
		GraphicsPipelineStateMgr::AddPSO(std::move(pso));

		auto p_cubemap_gen_shader = ShaderLibrary::Load("Shaders/cubemap_gen.hlsl");
		GraphicsPipelineStateInitializer cgen_desc;
		cgen_desc._input_layout = p_cubemap_gen_shader->PipelineInputLayout();
		cgen_desc._blend_state = p_cubemap_gen_shader->PipelineBlendState();
		cgen_desc._raster_state = p_cubemap_gen_shader->PipelineRasterizerState();
		cgen_desc._depth_stencil_state = p_cubemap_gen_shader->PipelineDepthStencilState();
		cgen_desc._topology = p_cubemap_gen_shader->PipelineTopology();
		cgen_desc._p_pixel_shader = p_cubemap_gen_shader.get();
		cgen_desc._p_vertex_shader = p_cubemap_gen_shader.get();
		cgen_desc._rt_state = RenderTargetState{ {EALGFormat::kALGFormatR16G16B16A16_FLOAT},EALGFormat::kALGFormatUnknown };
		pso = std::move(GraphicsPipelineStateObject::Create(cgen_desc));
		pso->Build();
		GraphicsPipelineStateMgr::AddPSO(std::move(pso));

		memset(&cgen_desc, 0, sizeof(GraphicsPipelineStateInitializer));
		auto shader = ShaderLibrary::Load("Shaders/filter_irradiance.hlsl");
		cgen_desc._input_layout = shader->PipelineInputLayout();
		cgen_desc._blend_state = shader->PipelineBlendState();
		cgen_desc._raster_state = shader->PipelineRasterizerState();
		cgen_desc._depth_stencil_state = shader->PipelineDepthStencilState();
		cgen_desc._topology = shader->PipelineTopology();
		cgen_desc._p_pixel_shader = shader.get();
		cgen_desc._p_vertex_shader = shader.get();
		cgen_desc._rt_state = RenderTargetState{ {EALGFormat::kALGFormatR16G16B16A16_FLOAT},EALGFormat::kALGFormatUnknown };
		pso = std::move(GraphicsPipelineStateObject::Create(cgen_desc));
		pso->Build();
		GraphicsPipelineStateMgr::AddPSO(std::move(pso));

		LOG_WARNING("Initialize PSO cache done after {}ms!", g_pTimeMgr->GetElapsedSinceLastMark());
	}

	void GraphicsPipelineStateMgr::AddPSO(Scope<GraphicsPipelineStateObject> p_gpso)
	{
		auto it = s_pso_library.find(p_gpso->Hash());
		if (it != s_pso_library.end())
		{
			LOG_WARNING("Pso id: {} already exist!", p_gpso->Hash().ToString());
			s_pso_library[p_gpso->Hash()] = std::move(p_gpso);
		}
		else
		{
			LOG_WARNING("Add Pso id: {} to library!", p_gpso->Hash().ToString());
			s_pso_library.insert(std::make_pair(p_gpso->Hash(), std::move(p_gpso)));
		}
	}
	GraphicsPipelineStateObject* GraphicsPipelineStateMgr::GetPso(const u32& id)
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
		while (!s_update_pso.empty())
		{
			AddPSO(std::move(s_update_pso.front()));
			s_update_pso.pop();
		}
		s_is_ready = false;
		_s_render_target_state.Hash(RenderTargetState::_s_hash_obj.GenHash(_s_render_target_state));
		ConfigureRenderTarget(_s_render_target_state.Hash());
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
			it->second->Bind(nullptr);
			for (auto& res_info : s_bind_resource_list)
			{
				it->second->SetPipelineResource(nullptr,res_info._p_resource, res_info._res_type, res_info._slot);
			}
			//LOG_INFO("PSO: {} been set", it->second->Name());
			s_is_ready = true;
		}
		else
		{
			GraphicsPipelineStateObject::ExtractPSOHash(s_cur_pos_hash, input_layout, shader, topology, blend_state, raster_state, ds_state, rt_state);
			LOG_INFO("s_cur_pos_hash////  Input Layout: {}, Shader: {}, Topology: {}, Blend State: {}, Raster State: {}, Depth Stencil State: {}, Render Target State: {}, Hash: {}",
				(u32)input_layout, shader, (u32)topology, (u32)blend_state, (u32)raster_state, (u32)ds_state, (u32)rt_state, s_cur_pos_hash.ToString());
			LOG_ERROR("Pso in need be create");
			{
				//g_thread_pool->Enqueue([=]() {
				//	auto new_shader = ShaderLibrary::Get(shader);
				//	GraphicsPipelineStateInitializer new_desc;
				//	new_desc._input_layout = VertexInputLayout::_s_hash_obj.Get(input_layout);
				//	new_desc._blend_state = BlendState::_s_hash_obj.Get(blend_state);
				//	new_desc._raster_state = RasterizerState::_s_hash_obj.Get(raster_state);
				//	new_desc._depth_stencil_state = DepthStencilState::_s_hash_obj.Get(ds_state);
				//	new_desc._topology = GetTopologyByHash(topology);
				//	new_desc._p_pixel_shader = new_shader.get();
				//	new_desc._p_vertex_shader = new_shader.get();
				//	new_desc._rt_state = RenderTargetState::_s_hash_obj.Get(rt_state);
				//	auto pso = std::move(GraphicsPipelineStateObject::Create(new_desc));
				//	pso->Build();
				//	GraphicsPipelineStateMgr::AddPSO(std::move(pso));
				//});
				auto new_shader = ShaderLibrary::Get(shader);
				GraphicsPipelineStateInitializer new_desc;
				new_desc._input_layout = VertexInputLayout::_s_hash_obj.Get(input_layout);
				new_desc._blend_state = BlendState::_s_hash_obj.Get(blend_state);
				new_desc._raster_state = RasterizerState::_s_hash_obj.Get(raster_state);
				new_desc._depth_stencil_state = DepthStencilState::_s_hash_obj.Get(ds_state);
				new_desc._topology = GetTopologyByHash(topology);
				new_desc._p_pixel_shader = new_shader.get();
				new_desc._p_vertex_shader = new_shader.get();
				new_desc._rt_state = RenderTargetState::_s_hash_obj.Get(rt_state);
				auto pso = std::move(GraphicsPipelineStateObject::Create(new_desc));
				pso->Build();
				for (auto& [hash, pso] : s_pso_library)
				{
					GraphicsPipelineStateObject::ExtractPSOHash(hash, input_layout, shader, topology, blend_state, raster_state, ds_state, rt_state);
					LOG_INFO("Pso name:[{}], Input Layout: {}, Shader: {}, Topology: {}, Blend State: {}, Raster State: {}, Depth Stencil State: {}, Render Target State: {}, Hash: {}",
						pso->Name(), (u32)input_layout, shader, (u32)topology, (u32)blend_state, (u32)raster_state, (u32)ds_state, (u32)rt_state, hash.ToString());
				}
				GraphicsPipelineStateObject::ExtractPSOHash(pso->Hash(), input_layout, shader, topology, blend_state, raster_state, ds_state, rt_state);
				LOG_WARNING("New Pso name:[{}], Input Layout: {}, Shader: {}, Topology: {}, Blend State: {}, Raster State: {}, Depth Stencil State: {}, Render Target State: {}, Hash: {}",
					pso->Name(), (u32)input_layout, shader, (u32)topology, (u32)blend_state, (u32)raster_state, (u32)ds_state, (u32)rt_state, pso->Hash().ToString());
				//LOG_WARNING("new hash {}", pso->Hash().ToString());
				GraphicsPipelineStateMgr::AddPSO(std::move(pso));
			}
		}
		s_bind_resource_list.clear();
	}

	void GraphicsPipelineStateMgr::EndConfigurePSO(CommandBuffer* cmd)
	{
		while (!s_update_pso.empty())
		{
			AddPSO(std::move(s_update_pso.front()));
			s_update_pso.pop();
		}
		s_is_ready = false;
		_s_render_target_state.Hash(RenderTargetState::_s_hash_obj.GenHash(_s_render_target_state));
		ConfigureRenderTarget(_s_render_target_state.Hash());
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
			it->second->Bind(cmd);
			for (auto& res_info : s_bind_resource_list)
			{
				it->second->SetPipelineResource(cmd, res_info._p_resource, res_info._res_type, res_info._slot);
			}
			//LOG_INFO("PSO: {} been set", it->second->Name());
			s_is_ready = true;
		}
		else
		{
			GraphicsPipelineStateObject::ExtractPSOHash(s_cur_pos_hash, input_layout, shader, topology, blend_state, raster_state, ds_state, rt_state);
			LOG_INFO("s_cur_pos_hash////  Input Layout: {}, Shader: {}, Topology: {}, Blend State: {}, Raster State: {}, Depth Stencil State: {}, Render Target State: {}, Hash: {}",
				(u32)input_layout, shader, (u32)topology, (u32)blend_state, (u32)raster_state, (u32)ds_state, (u32)rt_state, s_cur_pos_hash.ToString());
			LOG_ERROR("Pso in need be create");
			{
				//g_thread_pool->Enqueue([=]() {
				//	auto new_shader = ShaderLibrary::Get(shader);
				//	GraphicsPipelineStateInitializer new_desc;
				//	new_desc._input_layout = VertexInputLayout::_s_hash_obj.Get(input_layout);
				//	new_desc._blend_state = BlendState::_s_hash_obj.Get(blend_state);
				//	new_desc._raster_state = RasterizerState::_s_hash_obj.Get(raster_state);
				//	new_desc._depth_stencil_state = DepthStencilState::_s_hash_obj.Get(ds_state);
				//	new_desc._topology = GetTopologyByHash(topology);
				//	new_desc._p_pixel_shader = new_shader.get();
				//	new_desc._p_vertex_shader = new_shader.get();
				//	new_desc._rt_state = RenderTargetState::_s_hash_obj.Get(rt_state);
				//	auto pso = std::move(GraphicsPipelineStateObject::Create(new_desc));
				//	pso->Build();
				//	GraphicsPipelineStateMgr::AddPSO(std::move(pso));
				//});
				auto new_shader = ShaderLibrary::Get(shader);
				GraphicsPipelineStateInitializer new_desc;
				new_desc._input_layout = VertexInputLayout::_s_hash_obj.Get(input_layout);
				new_desc._blend_state = BlendState::_s_hash_obj.Get(blend_state);
				new_desc._raster_state = RasterizerState::_s_hash_obj.Get(raster_state);
				new_desc._depth_stencil_state = DepthStencilState::_s_hash_obj.Get(ds_state);
				new_desc._topology = GetTopologyByHash(topology);
				new_desc._p_pixel_shader = new_shader.get();
				new_desc._p_vertex_shader = new_shader.get();
				new_desc._rt_state = RenderTargetState::_s_hash_obj.Get(rt_state);
				auto pso = std::move(GraphicsPipelineStateObject::Create(new_desc));
				pso->Build();
				for (auto& [hash, pso] : s_pso_library)
				{
					GraphicsPipelineStateObject::ExtractPSOHash(hash, input_layout, shader, topology, blend_state, raster_state, ds_state, rt_state);
					LOG_INFO("Pso name:[{}], Input Layout: {}, Shader: {}, Topology: {}, Blend State: {}, Raster State: {}, Depth Stencil State: {}, Render Target State: {}, Hash: {}",
						pso->Name(), (u32)input_layout, shader, (u32)topology, (u32)blend_state, (u32)raster_state, (u32)ds_state, (u32)rt_state, hash.ToString());
				}
				GraphicsPipelineStateObject::ExtractPSOHash(pso->Hash(), input_layout, shader, topology, blend_state, raster_state, ds_state, rt_state);
				LOG_WARNING("New Pso name:[{}], Input Layout: {}, Shader: {}, Topology: {}, Blend State: {}, Raster State: {}, Depth Stencil State: {}, Render Target State: {}, Hash: {}",
					pso->Name(), (u32)input_layout, shader, (u32)topology, (u32)blend_state, (u32)raster_state, (u32)ds_state, (u32)rt_state, pso->Hash().ToString());
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
	void GraphicsPipelineStateMgr::SetRenderTargetState(EALGFormat color_format, EALGFormat depth_format, u8 color_rt_id)
	{
		_s_render_target_state._color_rt[color_rt_id] = color_format;
		_s_render_target_state._color_rt_num = color_format == EALGFormat::kALGFormatUnknown ? 0 : std::max(_s_render_target_state._color_rt_num, static_cast<u8>(color_rt_id + 1u));
		_s_render_target_state._depth_rt = depth_format;
	}
	void GraphicsPipelineStateMgr::SetRenderTargetState(EALGFormat color_format, u8 color_rt_id)
	{
		_s_render_target_state._color_rt[color_rt_id] = color_format;
		_s_render_target_state._color_rt_num = color_format == EALGFormat::kALGFormatUnknown ? 0 : std::max(_s_render_target_state._color_rt_num, static_cast<u8>(color_rt_id + 1u));
	}
	void GraphicsPipelineStateMgr::SubmitBindResource(void* res, const EBindResDescType& res_type, u8 slot)
	{
		s_bind_resource_list.emplace_back(PipelineResourceInfo{ res, res_type, slot });
	}
	void GraphicsPipelineStateMgr::SubmitBindResource(Texture* texture, u8 slot)
	{
		s_bind_resource_list.emplace_back(PipelineResourceInfo{ texture, EBindResDescType::kTexture2D, slot });
	}

	//------------------------------------------------------------------------------GraphicsPipelineStateMgr---------------------------------------------------------------------------------
}

