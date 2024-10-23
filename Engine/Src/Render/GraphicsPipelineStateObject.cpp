#include "Render/GraphicsPipelineStateObject.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/ThreadPool.h"
#include "Framework/Common/TimeMgr.h"
#include "RHI/DX12/D3DGraphicsPipelineState.h"
#include "Render/Renderer.h"
#include "Render/Shader.h"
#include "pch.h"

namespace Ailu
{
    //------------------------------------------------------------------------------GraphicsPipelineStateObject---------------------------------------------------------------------------------
    Scope<GraphicsPipelineStateObject> GraphicsPipelineStateObject::Create(const GraphicsPipelineStateInitializer &initializer)
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

    PSOHash GraphicsPipelineStateObject::ConstructPSOHash(u8 input_layout, u64 shader, u8 topology, u8 blend_state, u8 raster_state, u8 ds_state, u8 rt_state)
    {
        PSOHash hash;
        ConstructPSOHash(hash, input_layout, shader, topology, blend_state, raster_state, ds_state, rt_state);
        return hash;
    }

    void GraphicsPipelineStateObject::ConstructPSOHash(PSOHash &hash, u8 input_layout, u64 shader, u8 topology, u8 blend_state, u8 raster_state, u8 ds_state, u8 rt_state)
    {
        AL_ASSERT(input_layout < (u32) pow(2, StateHashStruct::kInputlayout._size));
        AL_ASSERT(shader < (u64) pow(2, StateHashStruct::kShader._size));
        AL_ASSERT(blend_state < (u32) pow(2, StateHashStruct::kBlendState._size));
        AL_ASSERT(raster_state < (u32) pow(2, StateHashStruct::kRasterState._size));
        AL_ASSERT(ds_state < (u32) pow(2, StateHashStruct::kDepthStencilState._size));
        AL_ASSERT(rt_state < (u32) pow(2, StateHashStruct::kRenderTargetState._size));
        hash.Set(StateHashStruct::kInputlayout._pos, StateHashStruct::kInputlayout._size, input_layout);
        hash.Set(StateHashStruct::kShader._pos, StateHashStruct::kShader._size, shader);
        hash.Set(StateHashStruct::kTopology._pos, StateHashStruct::kTopology._size, topology);
        hash.Set(StateHashStruct::kBlendState._pos, StateHashStruct::kBlendState._size, blend_state);
        hash.Set(StateHashStruct::kRasterState._pos, StateHashStruct::kRasterState._size, raster_state);
        hash.Set(StateHashStruct::kDepthStencilState._pos, StateHashStruct::kDepthStencilState._size, ds_state);
        hash.Set(StateHashStruct::kRenderTargetState._pos, StateHashStruct::kRenderTargetState._size, rt_state);
    }

    PSOHash GraphicsPipelineStateObject::ConstructPSOHash(const GraphicsPipelineStateInitializer &initializer, u16 pass_index, ShaderVariantHash variant_hash)
    {
        return ConstructPSOHash(initializer._p_vertex_shader->PipelineInputLayout(pass_index, variant_hash).Hash(), Shader::ConstructHash(initializer._p_pixel_shader->ID(), pass_index, variant_hash),
                                ALHash::Hasher(initializer._p_pixel_shader->PipelineTopology()), initializer._blend_state.Hash(),
                                initializer._raster_state.Hash(), initializer._depth_stencil_state.Hash(), initializer._rt_state.Hash());
    }

    void GraphicsPipelineStateObject::ExtractPSOHash(const PSOHash &pso_hash, u8 &input_layout, u64 &shader, u8 &topology, u8 &blend_state, u8 &raster_state, u8 &ds_state, u8 &rt_state)
    {
        input_layout = static_cast<u8>(pso_hash.Get(StateHashStruct::kInputlayout._pos, StateHashStruct::kInputlayout._size));
        shader = pso_hash.Get(StateHashStruct::kShader._pos, StateHashStruct::kShader._size);
        topology = static_cast<u8>(pso_hash.Get(StateHashStruct::kTopology._pos, StateHashStruct::kTopology._size));
        blend_state = static_cast<u8>(pso_hash.Get(StateHashStruct::kBlendState._pos, StateHashStruct::kBlendState._size));
        raster_state = static_cast<u8>(pso_hash.Get(StateHashStruct::kRasterState._pos, StateHashStruct::kRasterState._size));
        ds_state = static_cast<u8>(pso_hash.Get(StateHashStruct::kDepthStencilState._pos, StateHashStruct::kDepthStencilState._size));
        rt_state = static_cast<u8>(pso_hash.Get(StateHashStruct::kRenderTargetState._pos, StateHashStruct::kRenderTargetState._size));
    }
    void GraphicsPipelineStateObject::ExtractPSOHash(const PSOHash &pso_hash, u64 &shader)
    {
        shader = pso_hash.Get(StateHashStruct::kShader._pos, StateHashStruct::kShader._size);
    }
    //------------------------------------------------------------------------------GraphicsPipelineStateObject---------------------------------------------------------------------------------


    //------------------------------------------------------------------------------GraphicsPipelineStateMgr---------------------------------------------------------------------------------

    RenderTargetState GraphicsPipelineStateMgr::_s_render_target_state;

    void GraphicsPipelineStateMgr::BuildPSOCache()
    {
        LOG_WARNING("Begin initialize PSO cache...");
        g_pTimeMgr->Mark();

        Shader *shader = g_pResourceMgr->Get<Shader>(L"Shaders/defered_standard_lit.alasset");
        auto pso_desc = GraphicsPipelineStateInitializer::GetNormalOpaquePSODesc();
        pso_desc._input_layout = shader->PipelineInputLayout();
        pso_desc._p_vertex_shader = shader;
        pso_desc._p_pixel_shader = shader;
        pso_desc._rt_state = RenderTargetState{{EALGFormat::EALGFormat::kALGFormatR16G16_FLOAT, EALGFormat::EALGFormat::kALGFormatR8G8B8A8_UNORM,
                                                EALGFormat::EALGFormat::kALGFormatR8G8B8A8_UNORM, EALGFormat::EALGFormat::kALGFormatR16G16_FLOAT,
                                                EALGFormat::EALGFormat::kALGFormatR16G16B16A16_FLOAT},
                                               EALGFormat::EALGFormat::kALGFormatD24S8_UINT};
        pso_desc._depth_stencil_state = shader->PipelineDepthStencilState();
        auto stand_pso = GraphicsPipelineStateObject::Create(pso_desc);
        stand_pso->Build();
        AddPSO(std::move(stand_pso));

        memset(&pso_desc, 0, sizeof(GraphicsPipelineStateInitializer));
        shader = g_pResourceMgr->Get<Shader>(L"Shaders/deferred_lighting.alasset");
        pso_desc = GraphicsPipelineStateInitializer::GetNormalOpaquePSODesc();
        pso_desc._input_layout = shader->PipelineInputLayout();
        pso_desc._p_vertex_shader = shader;
        pso_desc._p_pixel_shader = shader;
        pso_desc._rt_state = RenderTargetState{{EALGFormat::EALGFormat::kALGFormatR11G11B10_FLOAT}, EALGFormat::EALGFormat::kALGFormatD24S8_UINT};
        pso_desc._depth_stencil_state = shader->PipelineDepthStencilState();
        stand_pso = GraphicsPipelineStateObject::Create(pso_desc);
        stand_pso->Build();
        AddPSO(std::move(stand_pso));

        memset(&pso_desc, 0, sizeof(GraphicsPipelineStateInitializer));
        shader = g_pResourceMgr->Get<Shader>(L"Shaders/blit.alasset");
        for (int i = 0; i < shader->PassCount(); i++)
        {
            auto &pass = shader->GetPassInfo(i);
            pso_desc = GraphicsPipelineStateInitializer::GetNormalOpaquePSODesc();
            pso_desc._input_layout = shader->PipelineInputLayout(i);
            pso_desc._p_vertex_shader = shader;
            pso_desc._p_pixel_shader = shader;
            pso_desc._rt_state = RenderTargetState{{EALGFormat::EALGFormat::kALGFormatR11G11B10_FLOAT}, EALGFormat::EALGFormat::kALGFormatUnknown};
            pso_desc._depth_stencil_state = TStaticDepthStencilState<false, ECompareFunc::kAlways>::GetRHI();
            stand_pso = GraphicsPipelineStateObject::Create(pso_desc);
            stand_pso->Build(i);
            AddPSO(std::move(stand_pso));
        }


        memset(&pso_desc, 0, sizeof(GraphicsPipelineStateInitializer));
        shader = g_pResourceMgr->Get<Shader>(L"Shaders/wireframe.alasset");
        pso_desc._p_vertex_shader = shader;
        pso_desc._p_pixel_shader = shader;
        pso_desc._depth_stencil_state = TStaticDepthStencilState<false, ECompareFunc::kLessEqual>::GetRHI();
        pso_desc._raster_state = TStaticRasterizerState<ECullMode::kBack, EFillMode::kWireframe>::GetRHI();
        pso_desc._rt_state = RenderTargetState{{EALGFormat::EALGFormat::kALGFormatR11G11B10_FLOAT}, EALGFormat::EALGFormat::kALGFormatD24S8_UINT};
        auto wireframe_pso = GraphicsPipelineStateObject::Create(pso_desc);
        wireframe_pso->Build();
        AddPSO(std::move(wireframe_pso));

        memset(&pso_desc, 0, sizeof(GraphicsPipelineStateInitializer));
        shader = g_pResourceMgr->Get<Shader>(L"Shaders/gizmo.alasset");
        pso_desc = GraphicsPipelineStateInitializer::GetNormalTransparentPSODesc();
        pso_desc._blend_state._color_mask = EColorMask::k_RGB;//不写入alpha，否则其最后拷贝到GameView时还会和imgui背景色混合，会导致颜色不对
        pso_desc._depth_stencil_state = TStaticDepthStencilState<false, ECompareFunc::kAlways>::GetRHI();
        pso_desc._raster_state = TStaticRasterizerState<ECullMode::kBack, EFillMode::kWireframe>::GetRHI();
        pso_desc._topology = ETopology::kLine;
        pso_desc._p_vertex_shader = shader;
        pso_desc._p_pixel_shader = shader;
        pso_desc._rt_state = RenderTargetState{{EALGFormat::EALGFormat::kALGFormatR11G11B10_FLOAT}, EALGFormat::EALGFormat::kALGFormatD24S8_UINT};
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
        pso_desc._rt_state = RenderTargetState{{EALGFormat::EALGFormat::kALGFormatUnknown}, EALGFormat::EALGFormat::kALGFormatD24S8_UINT};
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
        pso_desc._rt_state = RenderTargetState{{EALGFormat::EALGFormat::kALGFormatR11G11B10_FLOAT}, EALGFormat::EALGFormat::kALGFormatUnknown};
        pso = std::move(GraphicsPipelineStateObject::Create(pso_desc));
        pso->Build();
        GraphicsPipelineStateMgr::AddPSO(std::move(pso));

        memset(&pso_desc, 0, sizeof(GraphicsPipelineStateInitializer));
        shader = g_pResourceMgr->Get<Shader>(L"Shaders/filter_irradiance.alasset");
        for (int i = 0; i < shader->PassCount(); i++)
        {
            auto &pass = shader->GetPassInfo(i);
            pso_desc._input_layout = shader->PipelineInputLayout(i);
            pso_desc._blend_state = shader->PipelineBlendState(i);
            pso_desc._raster_state = shader->PipelineRasterizerState(i);
            pso_desc._depth_stencil_state = shader->PipelineDepthStencilState(i);
            pso_desc._topology = shader->PipelineTopology(i);
            pso_desc._p_pixel_shader = shader;
            pso_desc._p_vertex_shader = shader;
            pso_desc._rt_state = RenderTargetState{{EALGFormat::EALGFormat::kALGFormatR11G11B10_FLOAT}, EALGFormat::EALGFormat::kALGFormatUnknown};
            pso = std::move(GraphicsPipelineStateObject::Create(pso_desc));
            pso->Build(i);
            GraphicsPipelineStateMgr::AddPSO(std::move(pso));
        }

        memset(&pso_desc, 0, sizeof(GraphicsPipelineStateInitializer));
        shader = g_pResourceMgr->Get<Shader>(L"Shaders/hlsl/debug.hlsl");
        for (int i = 0; i < shader->PassCount(); i++)
        {
            auto &pass = shader->GetPassInfo(i);
            pso_desc._input_layout = shader->PipelineInputLayout(i);
            pso_desc._blend_state = shader->PipelineBlendState(i);
            pso_desc._raster_state = shader->PipelineRasterizerState(i);
            pso_desc._depth_stencil_state = shader->PipelineDepthStencilState(i);
            pso_desc._topology = shader->PipelineTopology(i);
            pso_desc._p_pixel_shader = shader;
            pso_desc._p_vertex_shader = shader;
            pso_desc._rt_state = RenderTargetState{{EALGFormat::EALGFormat::kALGFormatR11G11B10_FLOAT}, EALGFormat::EALGFormat::kALGFormatD24S8_UINT};
            pso = std::move(GraphicsPipelineStateObject::Create(pso_desc));
            pso->Build(i);
            GraphicsPipelineStateMgr::AddPSO(std::move(pso));
        }
        memset(&pso_desc, 0, sizeof(GraphicsPipelineStateInitializer));
        shader = g_pResourceMgr->Get<Shader>(L"Shaders/forwardlit.alasset");
        for (int i = 0; i < shader->PassCount(); i++)
        {
            auto &pass = shader->GetPassInfo(i);
            pso_desc._input_layout = shader->PipelineInputLayout(i);
            pso_desc._blend_state = shader->PipelineBlendState(i);
            pso_desc._raster_state = shader->PipelineRasterizerState(i);
            pso_desc._depth_stencil_state = shader->PipelineDepthStencilState(i);
            pso_desc._topology = shader->PipelineTopology(i);
            pso_desc._p_pixel_shader = shader;
            pso_desc._p_vertex_shader = shader;
            pso_desc._rt_state = RenderTargetState{{EALGFormat::EALGFormat::kALGFormatR11G11B10_FLOAT}, EALGFormat::EALGFormat::kALGFormatD24S8_UINT};
            pso = std::move(GraphicsPipelineStateObject::Create(pso_desc));
            pso->Build(i);
            GraphicsPipelineStateMgr::AddPSO(std::move(pso));
        }

        LOG_WARNING("Initialize PSO cache done after {}ms!", g_pTimeMgr->GetElapsedSinceLastMark());
    }

    void GraphicsPipelineStateMgr::AddPSO(Scope<GraphicsPipelineStateObject> p_gpso)
    {
        auto it = s_pso_library.find(p_gpso->Hash());
        if (it != s_pso_library.end())
        {
            LOG_WARNING("Pso id: {} already exist!", p_gpso->Name());
            s_pso_library[p_gpso->Hash()] = std::move(p_gpso);
        }
        else
        {
            LOG_WARNING("Add Pso id: {} to library!", p_gpso->Name());
            s_pso_library.insert(std::make_pair(p_gpso->Hash(), std::move(p_gpso)));
        }
    }

    void GraphicsPipelineStateMgr::EndConfigurePSO(CommandBuffer *cmd)
    {
        s_is_ready = false;
        _s_render_target_state.Hash(RenderTargetState::_s_hash_obj.GenHash(_s_render_target_state));
        ConfigureRenderTarget(_s_render_target_state.Hash());
        GraphicsPipelineStateObject::ConstructPSOHash(s_cur_pos_hash, s_hash_input_layout, s_hash_shader, s_hash_topology, s_hash_blend_state, s_hash_raster_state, s_hash_depth_stencil_state, s_hash_rt_state);
        auto it = s_pso_library.find(s_cur_pos_hash);
        u8 input_layout, topology, blend_state, raster_state, ds_state, rt_state;
        u64 shader_hash;
        if (it != s_pso_library.end())
        {
            GraphicsPipelineStateObject::ExtractPSOHash(s_cur_pos_hash, input_layout, shader_hash, topology, blend_state, raster_state, ds_state, rt_state);
            it->second->Bind(cmd);
            std::map<u8, std::set<PipelineResourceInfo>> c;
            for (auto res_it = s_bind_resource_list.begin(); res_it != s_bind_resource_list.end();)
            {
                if (!res_it->_name.empty())
                {
                    res_it->_slot = it->second->NameToSlot(res_it->_name);
                }

                if (res_it->_slot == -1)
                {
                    res_it = s_bind_resource_list.erase(res_it);// 删除元素并更新迭代器
                }
                else
                {
                    if (!c.contains(res_it->_slot))
                    {
                        c[res_it->_slot] = std::set<PipelineResourceInfo>();
                    }
                    c[res_it->_slot].insert(*res_it);
                    ++res_it;// 只有在不删除元素的情况下递增迭代器
                }
            }

            for (auto &[slot, res_list]: c)
            {
                if (slot == -1)
                    continue;
                auto &res_info = *res_list.begin();
                it->second->SetPipelineResource(cmd, res_info._p_resource, res_info._res_type, slot);
            }

            s_is_ready = true;
        }
        else
        {
            GraphicsPipelineStateObject::ExtractPSOHash(s_cur_pos_hash, input_layout, shader_hash, topology, blend_state, raster_state, ds_state, rt_state);
            LOG_WARNING("Pos used currently not ready with Input Layout: {}, Shader: {}, Topology: {}, Blend State: {}, Raster State: {}, Depth Stencil State: {}, Render Target State: {}",
                        (u32) input_layout,
                        shader_hash, (u32) topology, (u32) blend_state, (u32) raster_state, (u32) ds_state, (u32) rt_state);
            {
                u32 shader_id;
                u16 pass_index = 0;
                ShaderVariantHash variant_hash = 0;
                Shader::ExtractInfoFromHash(shader_hash, shader_id, pass_index, variant_hash);
                auto new_shader = g_pResourceMgr->Get<Shader>(shader_id);
                auto variant_state = new_shader->GetVariantState(pass_index, variant_hash);
                if (variant_state == EShaderVariantState::kReady)
                {
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
                    pso->Build(pass_index, variant_hash);
                    for (auto &[hash, pso]: s_pso_library)
                    {
                        u64 similar_shader_hash;
                        GraphicsPipelineStateObject::ExtractPSOHash(hash, input_layout, similar_shader_hash, topology, blend_state, raster_state, ds_state, rt_state);
                        if (similar_shader_hash == shader_hash)
                        {
                            LOG_INFO("Similar pso name:[{}], Input Layout: {}, Shader: {}, Topology: {}, Blend State: {}, Raster State: {}, Depth Stencil State: {}, Render Target State: {}",
                                     pso->Name(), (u32) input_layout, shader_hash, (u32) topology, (u32) blend_state, (u32) raster_state, (u32) ds_state, (u32) rt_state);
                        }
                    }
                    GraphicsPipelineStateObject::ExtractPSOHash(pso->Hash(), input_layout, shader_hash, topology, blend_state, raster_state, ds_state, rt_state);
                    LOG_WARNING("New Pso name:[{}], Input Layout: {}, Shader: {}, Topology: {}, Blend State: {}, Raster State: {}, Depth Stencil State: {}, Render Target State: {}",
                                pso->Name(), (u32) input_layout, shader_hash, (u32) topology, (u32) blend_state, (u32) raster_state, (u32) ds_state, (u32) rt_state);
                    GraphicsPipelineStateMgr::AddPSO(std::move(pso));
                }
            }
        }
        s_bind_resource_list.clear();
    }

    void GraphicsPipelineStateMgr::OnShaderRecompiled(Shader *shader, u16 pass_id, ShaderVariantHash variant_hash)
    {
        auto &pass = shader->GetPassInfo(pass_id);
        GraphicsPipelineStateInitializer gpso_desc;
        u8 input_layout, topology, blend_state, raster_state, ds_state, rt_state;
        u64 shader_hash;
        gpso_desc._input_layout = shader->PipelineInputLayout(pass_id, variant_hash);
        gpso_desc._p_pixel_shader = shader;
        gpso_desc._p_vertex_shader = shader;
        u64 new_pso_shader_hash = Shader::ConstructHash(shader->ID(), pass_id, variant_hash);
        //更新库中pso的筛选条件：
        //- pass_name一致即可
        for (auto &pso: s_pso_library)
        {
            GraphicsPipelineStateObject::ExtractPSOHash(pso.first, input_layout, shader_hash, topology, blend_state, raster_state, ds_state, rt_state);
            //这里还需要处理关键字组的增删导致的hash相同但实际关键字序列不同的情况，暂时没做
            if (shader_hash == new_pso_shader_hash)
            {
                gpso_desc._blend_state = BlendState::_s_hash_obj.Get(blend_state);
                gpso_desc._raster_state = RasterizerState::_s_hash_obj.Get(raster_state);
                gpso_desc._depth_stencil_state = DepthStencilState::_s_hash_obj.Get(ds_state);
                gpso_desc._topology = GetTopologyByHash(topology);
                gpso_desc._rt_state = RenderTargetState::_s_hash_obj.Get(rt_state);
                auto pso = GraphicsPipelineStateObject::Create(gpso_desc);
                pso->Build(pass_id, variant_hash);
                {
                    std::lock_guard<std::mutex> l(s_pso_lock);
                    s_update_pso.emplace(std::move(pso));
                }
            }
        }
        gpso_desc._blend_state = shader->PipelineBlendState(pass_id);
        gpso_desc._raster_state = shader->PipelineRasterizerState(pass_id);
        gpso_desc._depth_stencil_state = shader->PipelineDepthStencilState(pass_id);
        gpso_desc._topology = shader->PipelineTopology(pass_id);
        gpso_desc._rt_state = RenderTargetState{};
        auto pso = GraphicsPipelineStateObject::Create(gpso_desc);
        pso->Build(pass_id, variant_hash);
        {
            std::lock_guard<std::mutex> l(s_pso_lock);
            s_update_pso.emplace(std::move(pso));
        }
    }

    void GraphicsPipelineStateMgr::ConfigureShader(const u64 &shader_hash)
    {
        s_hash_shader = shader_hash;
    }

    void GraphicsPipelineStateMgr::ConfigureVertexInputLayout(const u8 &hash)
    {
        s_hash_input_layout = hash;
    }
    void GraphicsPipelineStateMgr::ConfigureTopology(const u8 &hash)
    {
        s_hash_topology = hash;
    }
    void GraphicsPipelineStateMgr::ConfigureBlendState(const u8 &hash)
    {
        s_hash_blend_state = hash;
    }
    void GraphicsPipelineStateMgr::ConfigureRasterizerState(const u8 &hash)
    {
        s_hash_raster_state = hash;
    }
    void GraphicsPipelineStateMgr::ConfigureDepthStencilState(const u8 &hash)
    {
        s_hash_depth_stencil_state = hash;
    }

    void GraphicsPipelineStateMgr::ConfigureRenderTarget(const u8 &hash)
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

    void GraphicsPipelineStateMgr::SubmitBindResource(void *res, const EBindResDescType &res_type, u8 slot, u16 priority)
    {
        s_bind_resource_list.emplace_back(PipelineResourceInfo{res, res_type, slot, {}, priority});
    }
    void Ailu::GraphicsPipelineStateMgr::SubmitBindResource(void *res, const EBindResDescType &res_type, const String &name, u16 priority)
    {
        s_bind_resource_list.emplace_back(PipelineResourceInfo{res, res_type, (u8) -1, name, priority});
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
}// namespace Ailu
