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
    #pragma region GraphicsPipelineStateObject
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

    PSOHash GraphicsPipelineStateObject::ConstructPSOHash(u8 input_layout, u64 shader, u8 blend_state, u8 raster_state, u8 ds_state, u8 rt_state)
    {
        PSOHash hash;
        ConstructPSOHash(hash, input_layout, shader, blend_state, raster_state, ds_state, rt_state);
        return hash;
    }

    void GraphicsPipelineStateObject::ConstructPSOHash(PSOHash &hash, u8 input_layout, u64 shader ,u8 blend_state, u8 raster_state, u8 ds_state, u8 rt_state)
    {
        AL_ASSERT(input_layout < (u32) pow(2, StateHashStruct::kInputLayout._size));
        AL_ASSERT(shader < (u64) pow(2, StateHashStruct::kShader._size));
        AL_ASSERT(blend_state < (u32) pow(2, StateHashStruct::kBlendState._size));
        AL_ASSERT(raster_state < (u32) pow(2, StateHashStruct::kRasterState._size));
        AL_ASSERT(ds_state < (u32) pow(2, StateHashStruct::kDepthStencilState._size));
        AL_ASSERT(rt_state < (u32) pow(2, StateHashStruct::kRenderTargetState._size));
        hash.Set(StateHashStruct::kInputLayout._pos, StateHashStruct::kInputLayout._size, input_layout);
        hash.Set(StateHashStruct::kShader._pos, StateHashStruct::kShader._size, shader);
        hash.Set(StateHashStruct::kBlendState._pos, StateHashStruct::kBlendState._size, blend_state);
        hash.Set(StateHashStruct::kRasterState._pos, StateHashStruct::kRasterState._size, raster_state);
        hash.Set(StateHashStruct::kDepthStencilState._pos, StateHashStruct::kDepthStencilState._size, ds_state);
        hash.Set(StateHashStruct::kRenderTargetState._pos, StateHashStruct::kRenderTargetState._size, rt_state);
        //hash.Set(StateHashStruct::kTopology._pos, StateHashStruct::kTopology._size, topology);
    }

    PSOHash GraphicsPipelineStateObject::ConstructPSOHash(const GraphicsPipelineStateInitializer &initializer, u16 pass_index, ShaderVariantHash variant_hash)
    {
        return ConstructPSOHash(initializer._p_vertex_shader->PipelineInputLayout(pass_index, variant_hash).Hash(), Shader::ConstructHash(initializer._p_pixel_shader->ID(), pass_index, variant_hash),
                                initializer._blend_state.Hash(),
                                initializer._raster_state.Hash(), initializer._depth_stencil_state.Hash(), initializer._rt_state.Hash());
    }

    void GraphicsPipelineStateObject::ExtractPSOHash(const PSOHash &pso_hash, u8 &input_layout, u64 &shader, u8 &blend_state, u8 &raster_state, u8 &ds_state, u8 &rt_state)
    {
        input_layout = static_cast<u8>(pso_hash.Get(StateHashStruct::kInputLayout._pos, StateHashStruct::kInputLayout._size));
        shader = pso_hash.Get(StateHashStruct::kShader._pos, StateHashStruct::kShader._size);
        //topology = static_cast<u8>(pso_hash.Get(StateHashStruct::kTopology._pos, StateHashStruct::kTopology._size));
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
    #pragma endregion

    //------------------------------------------------------------------------------GraphicsPipelineStateMgr---------------------------------------------------------------------------------
    #pragma region GraphicsPipelineStateMgr
    GraphicsPipelineStateMgr* g_pPSOMgr = nullptr;
    void GraphicsPipelineStateMgr::Init()
    {
        if (!g_pPSOMgr)
            g_pPSOMgr = new GraphicsPipelineStateMgr();
    }
    void GraphicsPipelineStateMgr::Shutdown()
    {
        DESTORY_PTR(g_pPSOMgr);
    }

    GraphicsPipelineStateMgr& GraphicsPipelineStateMgr::Get()
    {
        return *g_pPSOMgr;
    }

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
                                               EALGFormat::EALGFormat::kALGFormatD32_FLOAT_S8X24_UINT};
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
        pso_desc._rt_state = RenderTargetState{{EALGFormat::EALGFormat::kALGFormatR11G11B10_FLOAT}, EALGFormat::EALGFormat::kALGFormatD32_FLOAT_S8X24_UINT};
        pso_desc._depth_stencil_state = shader->PipelineDepthStencilState();
        stand_pso = GraphicsPipelineStateObject::Create(pso_desc);
        stand_pso->Build();
        AddPSO(std::move(stand_pso));

        memset(&pso_desc, 0, sizeof(GraphicsPipelineStateInitializer));
        shader = g_pResourceMgr->Get<Shader>(L"Shaders/blit.alasset");
        for (i16 i = 0; i < shader->PassCount(); i++)
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
        pso_desc._rt_state = RenderTargetState{{EALGFormat::EALGFormat::kALGFormatR11G11B10_FLOAT}, EALGFormat::EALGFormat::kALGFormatD32_FLOAT_S8X24_UINT};
        auto wireframe_pso = GraphicsPipelineStateObject::Create(pso_desc);
        wireframe_pso->Build();
        AddPSO(std::move(wireframe_pso));

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
        auto pso = std::move(GraphicsPipelineStateObject::Create(pso_desc));
        pso->Build();
        GraphicsPipelineStateMgr::AddPSO(std::move(pso));

        memset(&pso_desc, 0, sizeof(GraphicsPipelineStateInitializer));
        shader = g_pResourceMgr->Get<Shader>(L"Shaders/filter_irradiance.alasset");
        for (i16 i = 0; i < shader->PassCount(); i++)
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
        for (i16 i = 0; i < shader->PassCount(); i++)
        {
            auto &pass = shader->GetPassInfo(i);
            pso_desc._input_layout = shader->PipelineInputLayout(i);
            pso_desc._blend_state = shader->PipelineBlendState(i);
            pso_desc._raster_state = shader->PipelineRasterizerState(i);
            pso_desc._depth_stencil_state = shader->PipelineDepthStencilState(i);
            pso_desc._topology = shader->PipelineTopology(i);
            pso_desc._p_pixel_shader = shader;
            pso_desc._p_vertex_shader = shader;
            pso_desc._rt_state = RenderTargetState{{EALGFormat::EALGFormat::kALGFormatR11G11B10_FLOAT}, EALGFormat::EALGFormat::kALGFormatD32_FLOAT_S8X24_UINT};
            pso = std::move(GraphicsPipelineStateObject::Create(pso_desc));
            pso->Build(i);
            GraphicsPipelineStateMgr::AddPSO(std::move(pso));
        }
        memset(&pso_desc, 0, sizeof(GraphicsPipelineStateInitializer));
        shader = g_pResourceMgr->Get<Shader>(L"Shaders/forwardlit.alasset");
        for (i16 i = 0; i < shader->PassCount(); i++)
        {
            auto &pass = shader->GetPassInfo(i);
            pso_desc._input_layout = shader->PipelineInputLayout(i);
            pso_desc._blend_state = shader->PipelineBlendState(i);
            pso_desc._raster_state = shader->PipelineRasterizerState(i);
            pso_desc._depth_stencil_state = shader->PipelineDepthStencilState(i);
            pso_desc._topology = shader->PipelineTopology(i);
            pso_desc._p_pixel_shader = shader;
            pso_desc._p_vertex_shader = shader;
            pso_desc._rt_state = RenderTargetState{{EALGFormat::EALGFormat::kALGFormatR11G11B10_FLOAT}, EALGFormat::EALGFormat::kALGFormatD32_FLOAT_S8X24_UINT};
            pso = std::move(GraphicsPipelineStateObject::Create(pso_desc));
            pso->Build(i);
            GraphicsPipelineStateMgr::AddPSO(std::move(pso));
        }

        LOG_WARNING("Initialize PSO cache done after {}ms!", g_pTimeMgr->GetElapsedSinceLastMark());
    }

    void GraphicsPipelineStateMgr::AddPSO(Scope<GraphicsPipelineStateObject> p_gpso)
    {
        auto it = g_pPSOMgr->_pso_library.find(p_gpso->Hash());
        if (it != g_pPSOMgr->_pso_library.end())
        {
            g_pPSOMgr->_pso_library[p_gpso->Hash()] = std::move(p_gpso);
        }
        else
        {
            LOG_INFO("Add Pso id: {} to library!", p_gpso->Name());
            g_pPSOMgr->_pso_library.insert(std::make_pair(p_gpso->Hash(), std::move(p_gpso)));
        }
    }

    GraphicsPipelineStateObject* GraphicsPipelineStateMgr::FindMatchPSO()
    {
        g_pPSOMgr->_render_target_state.Hash(RenderTargetState::_s_hash_obj.GenHash(g_pPSOMgr->_render_target_state));
        ConfigureRenderTarget(g_pPSOMgr->_render_target_state.Hash());
        GraphicsPipelineStateObject::ConstructPSOHash(g_pPSOMgr->_cur_pos_hash, g_pPSOMgr->_hash_input_layout, 
            g_pPSOMgr->_hash_shader, g_pPSOMgr->_hash_blend_state, g_pPSOMgr->_hash_raster_state, g_pPSOMgr->_hash_depth_stencil_state, g_pPSOMgr->_hash_rt_state);
        auto it = g_pPSOMgr->_pso_library.find(g_pPSOMgr->_cur_pos_hash);
        u8 input_layout, blend_state, raster_state, ds_state, rt_state;
        u64 shader_hash;
        GraphicsPipelineStateObject::ExtractPSOHash(g_pPSOMgr->_cur_pos_hash, input_layout, shader_hash, blend_state, raster_state, ds_state, rt_state);
        u32 shader_id;
        u16 pass_index = 0;
        ShaderVariantHash variant_hash = 0;
        Shader::ExtractInfoFromHash(shader_hash, shader_id, pass_index, variant_hash);
        GraphicsPipelineStateObject* matched_pso = nullptr;
        auto new_shader = g_pResourceMgr->Get<Shader>(shader_id);
        if (it != g_pPSOMgr->_pso_library.end())
        {
            matched_pso = it->second.get();
        }
        else
        {
            // LOG_WARNING("Pos used currently not ready with Input Layout: {}, Shader: {},Blend State: {}, Raster State: {}, DepthBit Stencil State: {}, Render Target State: {}",
            //             (u32) input_layout,
            //             shader_hash,(u32) blend_state, (u32) raster_state, (u32) ds_state, (u32) rt_state);
            {
                auto variant_state = new_shader->GetVariantState(pass_index, variant_hash);
                if (variant_state == EShaderVariantState::kReady)
                {
                    GraphicsPipelineStateInitializer new_desc;
                    new_desc._input_layout = VertexInputLayout::_s_hash_obj.Get(input_layout);
                    new_desc._blend_state = BlendState::_s_hash_obj.Get(blend_state);
                    new_desc._raster_state = RasterizerState::_s_hash_obj.Get(raster_state);
                    new_desc._depth_stencil_state = DepthStencilState::_s_hash_obj.Get(ds_state);
                    new_desc._topology = new_shader->GetTopology();
                    new_desc._p_pixel_shader = new_shader;
                    new_desc._p_vertex_shader = new_shader;
                    new_desc._rt_state = RenderTargetState::_s_hash_obj.Get(rt_state);
                    auto pso = std::move(GraphicsPipelineStateObject::Create(new_desc));
                    pso->Build(pass_index, variant_hash);
                    matched_pso = pso.get();
                    for (auto &[hash, pso]: g_pPSOMgr->_pso_library)
                    {
                        u64 similar_shader_hash;
                        GraphicsPipelineStateObject::ExtractPSOHash(hash, input_layout, similar_shader_hash, blend_state, raster_state, ds_state, rt_state);
                        if (similar_shader_hash == shader_hash)
                        {
                            // LOG_INFO("Similar pso name:[{}], Input Layout: {}, Shader: {}, Blend State: {}, Raster State: {}, DepthBit Stencil State: {}, Render Target State: {}",
                            //          pso->Name(), (u32) input_layout, shader_hash, (u32) blend_state, (u32) raster_state, (u32) ds_state, (u32) rt_state);
                        }
                    }
                    GraphicsPipelineStateObject::ExtractPSOHash(pso->Hash(), input_layout, shader_hash, blend_state, raster_state, ds_state, rt_state);
                    // LOG_WARNING("New Pso name:[{}], Input Layout: {}, Shader: {}, Blend State: {}, Raster State: {}, DepthBit Stencil State: {}, Render Target State: {}",
                    //             pso->Name(), (u32) input_layout, shader_hash, (u32) blend_state, (u32) raster_state, (u32) ds_state, (u32) rt_state);
                    GraphicsPipelineStateMgr::AddPSO(std::move(pso));
                }
            }
        }
        if (matched_pso != nullptr)
        {
            GraphicsPipelineStateObject* pso = matched_pso;
            AL_ASSERT(pso->StateDescriptor()._p_vertex_shader == new_shader);
            pso->SetTopology(new_shader->GetTopology());
            pso->SetStencilRef(new_shader->_stencil_ref);
            std::map<i16, std::set<PipelineResource>> c;
            for (auto res_it = g_pPSOMgr->_bind_resource_list.begin(); res_it != g_pPSOMgr->_bind_resource_list.end();)
            {
                if (!res_it->_name.empty())
                {
                    res_it->_slot = pso->NameToSlot(res_it->_name);
                }
                if (res_it->_slot == -1)
                {
                    res_it = g_pPSOMgr->_bind_resource_list.erase(res_it);// 删除元素并更新迭代器
                }
                else
                {
                    if (!c.contains(res_it->_slot))
                    {
                        c[res_it->_slot] = std::set<PipelineResource>();
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
                pso->SetPipelineResource(res_info);
            }
            g_pPSOMgr->_bind_resource_list.clear();
        }
        return matched_pso;
    }

    void GraphicsPipelineStateMgr::OnShaderRecompiled(Shader *shader, u16 pass_id, ShaderVariantHash variant_hash)
    {
        auto &pass = shader->GetPassInfo(pass_id);
        GraphicsPipelineStateInitializer gpso_desc;
        u8 input_layout, blend_state, raster_state, ds_state, rt_state;
        u64 shader_hash;
        gpso_desc._input_layout = shader->PipelineInputLayout(pass_id, variant_hash);
        gpso_desc._p_pixel_shader = shader;
        gpso_desc._p_vertex_shader = shader;
        u64 new_pso_shader_hash = Shader::ConstructHash(shader->ID(), pass_id, variant_hash);
        //更新库中pso的筛选条件：
        //- pass_name一致即可
        for (auto &pso: g_pPSOMgr->_pso_library)
        {
            GraphicsPipelineStateObject::ExtractPSOHash(pso.first, input_layout, shader_hash, blend_state, raster_state, ds_state, rt_state);
            //这里还需要处理关键字组的增删导致的hash相同但实际关键字序列不同的情况，暂时没做
            if (shader_hash == new_pso_shader_hash)
            {
                gpso_desc._blend_state = BlendState::_s_hash_obj.Get(blend_state);
                gpso_desc._raster_state = RasterizerState::_s_hash_obj.Get(raster_state);
                gpso_desc._depth_stencil_state = DepthStencilState::_s_hash_obj.Get(ds_state);
                gpso_desc._topology = shader->PipelineTopology(pass_id);
                gpso_desc._rt_state = RenderTargetState::_s_hash_obj.Get(rt_state);
                auto pso = GraphicsPipelineStateObject::Create(gpso_desc);
                pso->Build(pass_id, variant_hash);
                {
                    std::lock_guard<std::mutex> l(g_pPSOMgr->_pso_lock);
                    g_pPSOMgr->_update_pso.emplace(std::move(pso));
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
            std::lock_guard<std::mutex> l(g_pPSOMgr->_pso_lock);
            g_pPSOMgr->_update_pso.emplace(std::move(pso));
        }
    }

    void GraphicsPipelineStateMgr::ConfigureShader(const u64 &shader_hash)
    {
        g_pPSOMgr->_hash_shader = shader_hash;
    }

    void GraphicsPipelineStateMgr::ConfigureVertexInputLayout(const u8 &hash)
    {
        g_pPSOMgr->_hash_input_layout = hash;
    }
    void GraphicsPipelineStateMgr::ConfigureTopology(const u8 &hash)
    {
        g_pPSOMgr->_hash_topology = hash;
    }
    void GraphicsPipelineStateMgr::ConfigureBlendState(const u8 &hash)
    {
        g_pPSOMgr->_hash_blend_state = hash;
    }
    void GraphicsPipelineStateMgr::ConfigureRasterizerState(const u8 &hash)
    {
        g_pPSOMgr->_hash_raster_state = hash;
    }
    void GraphicsPipelineStateMgr::ConfigureDepthStencilState(const u8 &hash)
    {
        g_pPSOMgr->_hash_depth_stencil_state = hash;
    }

    void GraphicsPipelineStateMgr::ConfigureRenderTarget(const u8 &hash)
    {
        g_pPSOMgr->_hash_rt_state = hash;
    }
    void GraphicsPipelineStateMgr::SetRenderTargetState(EALGFormat::EALGFormat color_format, EALGFormat::EALGFormat depth_format, u8 color_rt_id)
    {
        g_pPSOMgr->_render_target_state._color_rt[color_rt_id] = color_format;
        g_pPSOMgr->_render_target_state._color_rt_num = color_format == EALGFormat::EALGFormat::kALGFormatUnknown ? 0 : static_cast<u8>(color_rt_id + 1u);
        g_pPSOMgr->_render_target_state._depth_rt = depth_format;
    }
    void GraphicsPipelineStateMgr::SetRenderTargetState(EALGFormat::EALGFormat color_format, u8 color_rt_id)
    {
        g_pPSOMgr->_render_target_state._color_rt[color_rt_id] = color_format;
        g_pPSOMgr->_render_target_state._color_rt_num = color_format == EALGFormat::EALGFormat::kALGFormatUnknown ? 0 : static_cast<u8>(color_rt_id + 1u);
    }
    void GraphicsPipelineStateMgr::ResetRenderTargetState()
    {
        g_pPSOMgr->_render_target_state._color_rt_num = 0;
        for (int i = 0; i < 8; i++)
            g_pPSOMgr->_render_target_state._color_rt[i] = EALGFormat::EALGFormat::kALGFormatUnknown;
        g_pPSOMgr->_render_target_state._depth_rt = EALGFormat::EALGFormat::kALGFormatUnknown;
    }
    void GraphicsPipelineStateMgr::SubmitBindResource(PipelineResource resource)
    {
        g_pPSOMgr->_bind_resource_list.emplace_back(resource);
    }
    void GraphicsPipelineStateMgr::UpdateAllPSOObject()
    {
        while (!g_pPSOMgr->_update_pso.empty())
        {
            AddPSO(std::move(g_pPSOMgr->_update_pso.front()));
            g_pPSOMgr->_update_pso.pop();
        }
    }

#pragma endregion

    //------------------------------------------------------------------------------GraphicsPipelineStateMgr---------------------------------------------------------------------------------
}// namespace Ailu
