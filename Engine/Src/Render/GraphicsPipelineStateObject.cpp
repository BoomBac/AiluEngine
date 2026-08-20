#include "Render/GraphicsPipelineStateObject.h"
#include "Framework/Common/Allocator.hpp"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/ThreadPool.h"
#include "Framework/Common/TimeMgr.h"
#include "RHI/DX12/D3DGraphicsPipelineState.h"
#include "Render/Renderer.h"
#include "Render/RenderingStates.h"
#include "Render/Shader.h"
#include "pch.h"

namespace Ailu::Render
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
                return std::move(MakeScope<RHI::DX12::D3DGraphicsPipelineState>(initializer));
        }
        AL_ASSERT_MSG(false, "Unsupported render api!");
        return nullptr;
    }

    PSOHash GraphicsPipelineStateObject::ConstructPSOHash(u8 input_layout, u64 shader, u8 blend_state, u8 raster_state, u8 ds_state,
                                                          u8 rt_state)
    {
        PSOHash hash;
        ConstructPSOHash(hash, input_layout, shader, blend_state, raster_state, ds_state, rt_state);
        return hash;
    }

    void GraphicsPipelineStateObject::ConstructPSOHash(PSOHash &hash, u8 input_layout, u64 shader, u8 blend_state, u8 raster_state,
                                                       u8 ds_state, u8 rt_state)
    {
        AL_ASSERT(input_layout < (1u << StateHashStruct::kInputLayout._size));
        AL_ASSERT(shader < (1ull << StateHashStruct::kShader._size));
        AL_ASSERT(blend_state < (1u << StateHashStruct::kBlendState._size));
        AL_ASSERT(raster_state < (1u << StateHashStruct::kRasterState._size));
        AL_ASSERT(ds_state < (1u << StateHashStruct::kDepthStencilState._size));
        AL_ASSERT(rt_state < (1u << StateHashStruct::kRenderTargetState._size));
        hash.Set(StateHashStruct::kInputLayout._pos, StateHashStruct::kInputLayout._size, input_layout);
        hash.Set(StateHashStruct::kShader._pos, StateHashStruct::kShader._size, shader);
        hash.Set(StateHashStruct::kBlendState._pos, StateHashStruct::kBlendState._size, blend_state);
        hash.Set(StateHashStruct::kRasterState._pos, StateHashStruct::kRasterState._size, raster_state);
        hash.Set(StateHashStruct::kDepthStencilState._pos, StateHashStruct::kDepthStencilState._size, ds_state);
        hash.Set(StateHashStruct::kRenderTargetState._pos, StateHashStruct::kRenderTargetState._size, rt_state);
        //hash.Set(StateHashStruct::kTopology._pos, StateHashStruct::kTopology._size, topology);
    }

    PSOHash GraphicsPipelineStateObject::ConstructPSOHash(const GraphicsPipelineStateInitializer &initializer, u16 pass_index,
                                                          ShaderVariantHash variant_hash)
    {
        return ConstructPSOHash(initializer._p_vertex_shader->PipelineInputLayout(pass_index, variant_hash).Hash(),
                                Shader::ConstructHash(initializer._p_pixel_shader->ID(), pass_index, variant_hash),
                                initializer._blend_state.Hash(), initializer._raster_state.Hash(), initializer._depth_stencil_state.Hash(),
                                initializer._rt_state.Hash());
    }

    void GraphicsPipelineStateObject::ExtractPSOHash(const PSOHash &pso_hash, u8 &input_layout, u64 &shader, u8 &blend_state,
                                                     u8 &raster_state, u8 &ds_state, u8 &rt_state)
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
    { shader = pso_hash.Get(StateHashStruct::kShader._pos, StateHashStruct::kShader._size); }
    GraphicsPipelineStateObject::GraphicsPipelineStateObject(const GraphicsPipelineStateInitializer &initializer) : _state_desc(initializer)
    {
        _hash = GraphicsPipelineStateObject::ConstructPSOHash(initializer);
        _name = initializer._p_vertex_shader->Name();
        _res_type = EGpuResType::kGraphicsPSO;
    }
    const String &GraphicsPipelineStateObject::SlotToName(u8 slot) const
    {
        const static String empty_str = {};
        if (_bind_res_name_lut.contains(slot)) return _bind_res_name_lut.at(slot);
        return empty_str;
    }
    const i16 GraphicsPipelineStateObject::NameToSlot(const String &name) const
    {
        if (_p_bind_res_desc_infos->contains(name)) return _p_bind_res_desc_infos->at(name)._bind_slot;
        return -1;
    }
    bool GraphicsPipelineStateObject::IsValidPipelineResource(const EBindResDescType &res_type, i16 slot) const
    {
        if (slot < 0) return false;
        EBindResDescType aka_type = res_type == EBindResDescType::kConstBufferRaw ? EBindResDescType::kConstBuffer : res_type;
        if (_bind_res_desc_type_lut.contains(slot))
        {
            auto range = _bind_res_desc_type_lut.equal_range(slot);
            for (auto it = range.first; it != range.second; ++it)
            {
                if (it->second == aka_type) return true;
            }
        }
        else
        {
            LOG_WARNING("PSO({}):ValidPipelineResource with slot {}", _name, slot);
        }
        return false;
    }
    bool GraphicsPipelineStateObject::IsValidPipelineResource(const EBindResDescType &res_type, const String &name) const
    { return IsValidPipelineResource(res_type, NameToSlot(name)); }

//------------------------------------------------------------------------------GraphicsPipelineStateObject---------------------------------------------------------------------------------
#pragma endregion

//------------------------------------------------------------------------------GraphicsPipelineStateMgr---------------------------------------------------------------------------------
#pragma region GraphicsPipelineStateMgr
    static GraphicsPipelineStateMgr *g_pPSOMgr = nullptr;
    GraphicsPipelineStateMgr::GraphicsPipelineStateMgr() {}
    GraphicsPipelineStateMgr::~GraphicsPipelineStateMgr() {}
    void GraphicsPipelineStateMgr::Init()
    {
        if (!g_pPSOMgr)
            g_pPSOMgr = AL_NEW_TAG(EMemoryTag::kRenderer, GraphicsPipelineStateMgr);
    }
    void GraphicsPipelineStateMgr::Shutdown()
    {
        AL_DELETE(g_pPSOMgr);
    }

    GraphicsPipelineStateMgr &GraphicsPipelineStateMgr::Get() { return *g_pPSOMgr; }

    GraphicsPipelineStateObject *GraphicsPipelineStateMgr::FindReadyPSO(const PSOHash &hash)
    {
        std::lock_guard<std::mutex> lock(_pso_lock);
        auto it = _pso_library.find(hash);
        return it != _pso_library.end() && it->second->IsReady() ? it->second.get() : nullptr;
    }

    void GraphicsPipelineStateMgr::RequestPSOCreation(const PSOCreateRequest &request)
    {
        while (_pso_create_queue.Full())
            std::this_thread::yield();
        _pso_create_queue.Push(request);
    }

    void GraphicsPipelineStateMgr::ProcessPendingPSOCreationRequests()
    {
        while (!_pso_create_queue.Empty())
        {
            if (auto request = _pso_create_queue.Pop(); request.has_value())
                CreatePSO(request.value());
        }
    }

    void GraphicsPipelineStateMgr::BuildPSOCache()
    {
        LOG_WARNING("Begin initialize PSO cache...");
        TimeMgr::Get().Mark();

        Shader *shader = ResourceMgr::Get().Get<Shader>(L"Shaders/hlsl/defered_standard_lit.alasset");
        auto pso_desc = GraphicsPipelineStateInitializer::GetNormalOpaquePSODesc();
        pso_desc._input_layout = shader->PipelineInputLayout();
        pso_desc._p_vertex_shader = shader;
        pso_desc._p_pixel_shader = shader;
        pso_desc._rt_state = RenderTargetState{{EALGFormat::kALGFormatR16G16_FLOAT, EALGFormat::kALGFormatR8G8B8A8_UNORM,
                                                EALGFormat::kALGFormatR8G8B8A8_UNORM, EALGFormat::kALGFormatR16G16_FLOAT,
                                                EALGFormat::kALGFormatR16G16B16A16_FLOAT},
                                               EALGFormat::kALGFormatD32_FLOAT_S8X24_UINT};
        pso_desc._depth_stencil_state = shader->PipelineDepthStencilState();
        auto stand_pso = GraphicsPipelineStateObject::Create(pso_desc);
        g_pGfxContext->CreateResource(stand_pso.get());
        AddPSO(std::move(stand_pso));

        memset(&pso_desc, 0, sizeof(GraphicsPipelineStateInitializer));
        shader = ResourceMgr::Get().Get<Shader>(L"Shaders/hlsl/deferred_lighting.alasset");
        pso_desc = GraphicsPipelineStateInitializer::GetNormalOpaquePSODesc();
        pso_desc._input_layout = shader->PipelineInputLayout();
        pso_desc._p_vertex_shader = shader;
        pso_desc._p_pixel_shader = shader;
        pso_desc._rt_state = RenderTargetState{{EALGFormat::kALGFormatR11G11B10_FLOAT}, EALGFormat::kALGFormatD32_FLOAT_S8X24_UINT};
        pso_desc._depth_stencil_state = shader->PipelineDepthStencilState();
        stand_pso = GraphicsPipelineStateObject::Create(pso_desc);
        g_pGfxContext->CreateResource(stand_pso.get());
        AddPSO(std::move(stand_pso));

        memset(&pso_desc, 0, sizeof(GraphicsPipelineStateInitializer));
        shader = ResourceMgr::Get().Get<Shader>(L"Shaders/hlsl/blit.alasset");
        for (i16 i = 0; i < shader->PassCount(); i++)
        {
            auto &pass = shader->GetPassInfo(i);
            pso_desc = GraphicsPipelineStateInitializer::GetNormalOpaquePSODesc();
            pso_desc._input_layout = shader->PipelineInputLayout(i);
            pso_desc._p_vertex_shader = shader;
            pso_desc._p_pixel_shader = shader;
            pso_desc._rt_state = RenderTargetState{{EALGFormat::kALGFormatR11G11B10_FLOAT}, EALGFormat::kALGFormatUNKOWN};
            pso_desc._depth_stencil_state = TStaticDepthStencilState<false, ECompareFunc::kAlways>::GetRHI();
            stand_pso = GraphicsPipelineStateObject::Create(pso_desc);
            g_pGfxContext->CreateResource(stand_pso.get(), AL_NEW_TAG(EMemoryTag::kRenderer, UploadParamsGPSO, i, 0));
            AddPSO(std::move(stand_pso));
        }


        memset(&pso_desc, 0, sizeof(GraphicsPipelineStateInitializer));
        shader = ResourceMgr::Get().Get<Shader>(L"Shaders/hlsl/wireframe.alasset");
        pso_desc._p_vertex_shader = shader;
        pso_desc._p_pixel_shader = shader;
        pso_desc._depth_stencil_state = TStaticDepthStencilState<false, ECompareFunc::kLessEqual>::GetRHI();
        pso_desc._raster_state = TStaticRasterizerState<ECullMode::kBack, EFillMode::kWireframe>::GetRHI();
        pso_desc._rt_state = RenderTargetState{{EALGFormat::kALGFormatR11G11B10_FLOAT}, EALGFormat::kALGFormatD32_FLOAT_S8X24_UINT};
        auto wireframe_pso = GraphicsPipelineStateObject::Create(pso_desc);
        g_pGfxContext->CreateResource(wireframe_pso.get());
        AddPSO(std::move(wireframe_pso));

        memset(&pso_desc, 0, sizeof(GraphicsPipelineStateInitializer));
        shader = ResourceMgr::Get().Get<Shader>(L"Shaders/hlsl/cubemap_gen.alasset");
        pso_desc._input_layout = shader->PipelineInputLayout();
        pso_desc._blend_state = shader->PipelineBlendState();
        pso_desc._raster_state = shader->PipelineRasterizerState();
        pso_desc._depth_stencil_state = shader->PipelineDepthStencilState();
        pso_desc._topology = shader->PipelineTopology();
        pso_desc._p_pixel_shader = shader;
        pso_desc._p_vertex_shader = shader;
        pso_desc._rt_state = RenderTargetState{{EALGFormat::kALGFormatR11G11B10_FLOAT}, EALGFormat::kALGFormatUNKOWN};
        auto pso = std::move(GraphicsPipelineStateObject::Create(pso_desc));
        g_pGfxContext->CreateResource(pso.get());
        GraphicsPipelineStateMgr::AddPSO(std::move(pso));

        memset(&pso_desc, 0, sizeof(GraphicsPipelineStateInitializer));
        shader = ResourceMgr::Get().Get<Shader>(L"Shaders/hlsl/filter_irradiance.alasset");
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
            pso_desc._rt_state = RenderTargetState{{EALGFormat::kALGFormatR11G11B10_FLOAT}, EALGFormat::kALGFormatUNKOWN};
            pso = std::move(GraphicsPipelineStateObject::Create(pso_desc));
            g_pGfxContext->CreateResource(pso.get(), AL_NEW_TAG(EMemoryTag::kRenderer, UploadParamsGPSO, i, 0));
            GraphicsPipelineStateMgr::AddPSO(std::move(pso));
        }

        memset(&pso_desc, 0, sizeof(GraphicsPipelineStateInitializer));
        shader = ResourceMgr::Get().Get<Shader>(L"Shaders/hlsl/debug.hlsl");
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
            pso_desc._rt_state = RenderTargetState{{EALGFormat::kALGFormatR11G11B10_FLOAT}, EALGFormat::kALGFormatD32_FLOAT_S8X24_UINT};
            pso = std::move(GraphicsPipelineStateObject::Create(pso_desc));
            g_pGfxContext->CreateResource(pso.get(), AL_NEW_TAG(EMemoryTag::kRenderer, UploadParamsGPSO, i, 0));
            GraphicsPipelineStateMgr::AddPSO(std::move(pso));
        }
        memset(&pso_desc, 0, sizeof(GraphicsPipelineStateInitializer));
        shader = ResourceMgr::Get().Get<Shader>(L"Shaders/hlsl/forwardlit.alasset");
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
            pso_desc._rt_state = RenderTargetState{{EALGFormat::kALGFormatR11G11B10_FLOAT}, EALGFormat::kALGFormatD32_FLOAT_S8X24_UINT};
            pso = std::move(GraphicsPipelineStateObject::Create(pso_desc));
            g_pGfxContext->CreateResource(pso.get(), AL_NEW_TAG(EMemoryTag::kRenderer, UploadParamsGPSO, i, 0));
            GraphicsPipelineStateMgr::AddPSO(std::move(pso));
        }

        LOG_WARNING("Initialize PSO cache done after {}ms!", TimeMgr::Get().GetElapsedSinceLastMark());
    }

    void GraphicsPipelineStateMgr::ProcessPendingShaderCompiles()
    {
        u32 compiled_shader_num = 0u;
        while (!_shader_compiled_queue.Empty())
        {
            if (auto shader = _shader_compiled_queue.Pop(); shader.has_value())
            {
                ProcessCompiledShader(shader.value());
                ++compiled_shader_num;
            }
        }
        if (compiled_shader_num > 0u)
        {
            LOG_INFO("Compiled {} shaders!", compiled_shader_num);
            UpdateAllPSOObject();
        }
    }

    void GraphicsPipelineStateMgr::AddPSO(Scope<GraphicsPipelineStateObject> p_gpso)
    {
        std::lock_guard<std::mutex> lock(g_pPSOMgr->_pso_lock);
        auto it = g_pPSOMgr->_pso_library.find(p_gpso->Hash());
        if (it != g_pPSOMgr->_pso_library.end()) { g_pPSOMgr->_pso_library[p_gpso->Hash()] = std::move(p_gpso); }
        else
        {
            LOG_INFO("Add Pso id: {} to library!", p_gpso->Name());
            g_pPSOMgr->_pso_library.insert(std::make_pair(p_gpso->Hash(), std::move(p_gpso)));
        }
    }

    void GraphicsPipelineStateMgr::ProcessCompiledShader(const ShaderCompiledInfo &info)
    {
        auto shader = info._shader;
        auto pass_id = info._pass_index;
        auto variant_hash = info._variant_hash;
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
        Vector<PSOHash> pso_hashes;
        {
            std::lock_guard<std::mutex> lock(g_pPSOMgr->_pso_lock);
            pso_hashes.reserve(g_pPSOMgr->_pso_library.size());
            for (auto &pso: g_pPSOMgr->_pso_library)
                pso_hashes.emplace_back(pso.first);
        }
        for (auto &pso_hash: pso_hashes)
        {
            GraphicsPipelineStateObject::ExtractPSOHash(pso_hash, input_layout, shader_hash, blend_state, raster_state, ds_state,
                                                        rt_state);
            //这里还需要处理关键字组的增删导致的hash相同但实际关键字序列不同的情况，暂时没做
            if (shader_hash == new_pso_shader_hash)
            {
                gpso_desc._blend_state = BlendState::_s_hash_obj.Get(blend_state);
                gpso_desc._raster_state = RasterizerState::_s_hash_obj.Get(raster_state);
                gpso_desc._depth_stencil_state = DepthStencilState::_s_hash_obj.Get(ds_state);
                gpso_desc._topology = shader->PipelineTopology(pass_id);
                gpso_desc._rt_state = RenderTargetState::_s_hash_obj.Get(rt_state);
                auto pso = GraphicsPipelineStateObject::Create(gpso_desc);
                g_pGfxContext->CreateResource(pso.get(),
                                              AL_NEW_TAG(EMemoryTag::kRenderer, UploadParamsGPSO, pass_id, variant_hash));
                {
                    std::lock_guard<std::mutex> l(g_pPSOMgr->_pso_lock);
                    g_pPSOMgr->_update_pso.emplace_back(std::move(pso));
                }
            }
        }
        gpso_desc._blend_state = shader->PipelineBlendState(pass_id);
        gpso_desc._raster_state = shader->PipelineRasterizerState(pass_id);
        gpso_desc._depth_stencil_state = shader->PipelineDepthStencilState(pass_id);
        gpso_desc._topology = shader->PipelineTopology(pass_id);
        gpso_desc._rt_state = RenderTargetState{};
        auto pso = GraphicsPipelineStateObject::Create(gpso_desc);
        g_pGfxContext->CreateResource(pso.get(),
                                      AL_NEW_TAG(EMemoryTag::kRenderer, UploadParamsGPSO, pass_id, variant_hash));
        {
            std::lock_guard<std::mutex> l(g_pPSOMgr->_pso_lock);
            g_pPSOMgr->_update_pso.emplace_back(std::move(pso));
        }
    }

    GraphicsPipelineStateObject *GraphicsPipelineStateMgr::CreatePSO(const PSOCreateRequest &request)
    {
        if (auto pso = FindReadyPSO(request._hash); pso != nullptr)
            return pso;
        const PSOHash &hash = request._hash;
        Shader *shader = request._shader;
        const u16 pass_index = request._pass_index;
        const ShaderVariantHash variant_hash = request._variant_hash;
        u8 input_layout, blend_state, raster_state, ds_state, rt_state;
        u64 shader_hash;
        GraphicsPipelineStateObject::ExtractPSOHash(hash, input_layout, shader_hash, blend_state, raster_state, ds_state, rt_state);
        GraphicsPipelineStateObject *matched_pso = nullptr;
        auto new_shader = shader;
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
            g_pGfxContext->CreateResource(pso.get(),
                                          AL_NEW_TAG(EMemoryTag::kRenderer, UploadParamsGPSO, pass_index, variant_hash));
            matched_pso = pso.get();
            GraphicsPipelineStateMgr::AddPSO(std::move(pso));
        }
        return matched_pso;
    }

    void CommandRecordingContext::Clear()
    {
        _render_target_state = RenderTargetState{};
        _cur_pos_hash = PSOHash{};
        _hash_shader = 0u;
        _hash_input_layout = 0u;
        _hash_topology = 0u;
        _hash_blend_state = 0u;
        _hash_raster_state = 0u;
        _hash_depth_stencil_state = 0u;
        _hash_rt_state = 0u;
        _current_pso = nullptr;
        MarkPSODirty();
        _current_shader = nullptr;
        _current_pass_index = 0u;
        _current_variant_hash = 0u;
        _resolved_bind_res.clear();
        memset(&_rendering_states_data, 0, sizeof(CommandRenderingStatesData));
        _stencil_ref = 0u;
        _current_topology = ETopology::kTriangle;
    }

    GraphicsPipelineStateObject *CommandRecordingContext::FindMatchPSO()
    {
        ++_rendering_states_data.PsoLookupCount;
        if (_is_pso_dirty)
        {
            ++_rendering_states_data.GfxPsoDirtyCount;
            GraphicsPipelineStateObject::ConstructPSOHash(_cur_pos_hash, _hash_input_layout, _hash_shader, _hash_blend_state,
                                                          _hash_raster_state, _hash_depth_stencil_state, _hash_rt_state);
            _current_pso = GraphicsPipelineStateMgr::Get().FindReadyPSO(_cur_pos_hash);
            if (_current_pso == nullptr)
            {
                if (!_pso_request_submitted)
                {
                    GraphicsPipelineStateMgr::Get().RequestPSOCreation(
                        PSOCreateRequest{_cur_pos_hash, _current_shader, _current_pass_index, _current_variant_hash});
                    LOG_WARNING("PSO miss during command recording: shader={}, pass={}, variant={}", _current_shader->Name(),
                                _current_pass_index, _current_variant_hash);
                    _pso_request_submitted = true;
                }
            }
            _is_pso_dirty = _current_pso == nullptr;
        }
        else if (_current_pso != nullptr && _current_pso->IsReady())
        {
            ++_rendering_states_data.PsoCacheHitCount;
        }
        else
        {
            ++_rendering_states_data.PsoCacheMissCount;
        }
        _current_topology = _current_shader->GetTopology();
        _stencil_ref = _current_shader->_stencil_ref;
        return _current_pso;
    }

    void CommandRecordingContext::ConfigureShader(Shader *shader, u16 pass_index, ShaderVariantHash variant_hash, const u64 &shader_hash)
    {
        if (shader_hash == _hash_shader && shader == _current_shader && pass_index == _current_pass_index &&
            variant_hash == _current_variant_hash)
            return;
        FrameDebugger::EPsoDirtyReason reason = FrameDebugger::EPsoDirtyReason::kNone;
        if (shader_hash != _hash_shader || shader != _current_shader) reason = reason | FrameDebugger::EPsoDirtyReason::kShaderChanged;
        if (pass_index != _current_pass_index) reason = reason | FrameDebugger::EPsoDirtyReason::kShaderPassChanged;
        if (variant_hash != _current_variant_hash) reason = reason | FrameDebugger::EPsoDirtyReason::kShaderVariantChanged;
        _hash_shader = shader_hash;
        _current_shader = shader;
        _current_pass_index = pass_index;
        _current_variant_hash = variant_hash;
        MarkPSODirty(reason);
    }


    void CommandRecordingContext::ConfigureShader(Shader *shader, const u64 &shader_hash)
    {
        ConfigureShader(shader, _current_pass_index, _current_variant_hash, shader_hash);
    }

    void CommandRecordingContext::ConfigureVertexInputLayout(const u8 &hash)
    {
        if (hash == _hash_input_layout) return;
        _hash_input_layout = hash;
        MarkPSODirty(FrameDebugger::EPsoDirtyReason::kVertexLayoutChanged);
    }
    void CommandRecordingContext::ConfigureTopology(const u8 &hash)
    {
        // if (hash == _hash_topology) return;
        // _hash_topology = hash;
        // _is_pso_dirty = true;
    }
    void CommandRecordingContext::ConfigureBlendState(const u8 &hash)
    {
        if (hash == _hash_blend_state) return;
        _hash_blend_state = hash;
        MarkPSODirty(FrameDebugger::EPsoDirtyReason::kBlendStateChanged);
    }
    void CommandRecordingContext::ConfigureRasterizerState(const u8 &hash)
    {
        if (hash == _hash_raster_state) return;
        _hash_raster_state = hash;
        MarkPSODirty(FrameDebugger::EPsoDirtyReason::kRasterizerStateChanged);
    }
    void CommandRecordingContext::ConfigureDepthStencilState(const u8 &hash)
    {
        if (hash == _hash_depth_stencil_state) return;
        _hash_depth_stencil_state = hash;
        MarkPSODirty(FrameDebugger::EPsoDirtyReason::kDepthStencilStateChanged);
    }

    void CommandRecordingContext::SetRenderTargetState(EALGFormat color_format, EALGFormat depth_format, u8 color_rt_id)
    {
        _render_target_state._color_rt[color_rt_id] = color_format;
        _render_target_state._color_rt_num =
                color_format == EALGFormat::kALGFormatUNKOWN ? 0 : static_cast<u8>(color_rt_id + 1u);
        _render_target_state._depth_rt = depth_format;
        auto cur_rt_hash = RenderTargetState::_s_hash_obj.GenHash(_render_target_state);
        if (cur_rt_hash != _hash_rt_state) 
            MarkPSODirty(FrameDebugger::EPsoDirtyReason::kRenderTargetStateChanged);
        _hash_rt_state = cur_rt_hash;
    }
    void CommandRecordingContext::SetRenderTargetState(EALGFormat color_format, u8 color_rt_id)
    {
        _render_target_state._color_rt[color_rt_id] = color_format;
        _render_target_state._color_rt_num =
                color_format == EALGFormat::kALGFormatUNKOWN ? 0 : static_cast<u8>(color_rt_id + 1u);
        auto cur_rt_hash = RenderTargetState::_s_hash_obj.GenHash(_render_target_state);
        if (cur_rt_hash != _hash_rt_state) 
            MarkPSODirty(FrameDebugger::EPsoDirtyReason::kRenderTargetStateChanged);
        _hash_rt_state = cur_rt_hash;
    }
    void CommandRecordingContext::ResetRenderTargetState()
    {
        _render_target_state._color_rt_num = 0;
        for (int i = 0; i < 8; i++) _render_target_state._color_rt[i] = EALGFormat::kALGFormatUNKOWN;
        _render_target_state._depth_rt = EALGFormat::kALGFormatUNKOWN;
        auto cur_rt_hash = RenderTargetState::_s_hash_obj.GenHash(_render_target_state);
        if (cur_rt_hash != _hash_rt_state)
            MarkPSODirty(FrameDebugger::EPsoDirtyReason::kRenderTargetStateChanged);
        _hash_rt_state = cur_rt_hash;
    }

    void GraphicsPipelineStateMgr::UpdateAllPSOObject()
    {
        if (!g_pPSOMgr->_update_pso.empty())
        {
            std::lock_guard<std::mutex> lock(g_pPSOMgr->_pso_lock);
            Vector<Scope<GraphicsPipelineStateObject>> temp;
            for (auto &it: g_pPSOMgr->_update_pso)
            {
                auto hash = it->Hash();
                if (g_pPSOMgr->_pso_library.contains(hash))
                {
                    auto exist_pso = g_pPSOMgr->_pso_library[hash].get();
                    if (!exist_pso->IsReferenceByGpu()) g_pPSOMgr->_pso_library[hash] = std::move(it);
                    else
                        temp.push_back(std::move(it));
                }
                else
                    g_pPSOMgr->_pso_library.insert(std::make_pair(hash, std::move(it)));
            }
            g_pPSOMgr->_update_pso = std::move(temp);
        }
    }

#pragma endregion

    //------------------------------------------------------------------------------GraphicsPipelineStateMgr---------------------------------------------------------------------------------
}// namespace Ailu::Render
