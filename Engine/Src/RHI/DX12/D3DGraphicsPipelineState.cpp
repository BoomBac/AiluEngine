#include "RHI/DX12/D3DGraphicsPipelineState.h"
#include "Framework/Common/ResourceMgr.h"
#include "RHI/DX12/D3DCommandBuffer.h"
#include "RHI/DX12/D3DContext.h"
#include "RHI/DX12/D3DShader.h"
#include "RHI/DX12/dxhelper.h"
#include "Render/RenderingStates.h"
#include "Framework/Common/EngineConfig.h"
#if AILU_ENABLE_FRAME_DEBUGGER
#include "Render/FrameDebugger/FrameCaptureWriter.h"
#endif
#include "pch.h"
using namespace Ailu::Render;

namespace Ailu::RHI::DX12
{
    namespace
    {
        u64 BuildBindingHash(const PipelineResource &resource)
        {
            const auto type_hash = static_cast<u64>(resource._res_type);
            const auto resource_ptr = static_cast<u64>(reinterpret_cast<uintptr_t>(resource._p_resource));
            const auto native_ptr = static_cast<u64>(reinterpret_cast<uintptr_t>(resource._addi_info._native_res_ptr));
            u64 hash = resource_ptr ^ (type_hash << 48) ^ (static_cast<u64>(resource._slot) << 40);
            hash ^= resource._addi_info._gpu_handle;
            hash ^= native_ptr >> 4;
            hash ^= static_cast<u64>(resource._addi_info._view_index) << 16;
            hash ^= static_cast<u64>(resource._addi_info._sub_res);
            return hash;
        }
    }

    D3DGraphicsPipelineState::D3DGraphicsPipelineState(const GraphicsPipelineStateInitializer &initializer) : GraphicsPipelineStateObject(initializer)
    {
        memset(&_d3d_pso_desc, 0, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));//置空，否则编译不加 /sdl时创建pso时会有空指针
    }
    D3DGraphicsPipelineState::~D3DGraphicsPipelineState()
    {
        LOG_INFO("Destory gpso {}", _name);
    }

    void D3DGraphicsPipelineState::UploadImpl(GraphicsContext *ctx, RHICommandBuffer *rhi_cmd, UploadParams *params)
    {
        GpuResource::UploadImpl(ctx, rhi_cmd, params);
        if (rhi_cmd)
        {
            auto d3dcmd = static_cast<D3DCommandBuffer *>(rhi_cmd);
            d3dcmd->MarkUsedResource(this);
        }
        u16 pass_index = 0u;
        ShaderVariantHash variant_hash = 0u;
        if (params)
        {
            if (auto param = dynamic_cast<UploadParamsGPSO *>(params); param != nullptr)
            {
                pass_index = param->_pass_index;
                variant_hash = param->_variant_hash;
            }
        }
        auto d3dshader = static_cast<D3DShader *>(_state_desc._p_vertex_shader);
        _p_bind_res_desc_infos = const_cast<std::unordered_map<std::string, ShaderBindResourceInfo> *>(&d3dshader->GetBindResInfo(pass_index, variant_hash));
        _bind_res_desc_type_lut.clear();
        for (auto &it: *_p_bind_res_desc_infos)
        {
            _bind_res_desc_type_lut.insert(std::make_pair((i16)it.second._bind_slot, it.second._res_type));
            _bind_res_name_lut.insert(std::make_pair(it.second._bind_slot, it.first));
        }
        auto it = _p_bind_res_desc_infos->find(RenderConstants::kCBufNamePerScene);
        if (it != _p_bind_res_desc_infos->end())
        {
            _per_frame_cbuf_bind_slot = it->second._bind_slot;
        }
        auto [desc, count] = d3dshader->GetVertexInputLayout(pass_index, variant_hash);
        _d3d_pso_desc.InputLayout = {desc, count};
        _d3d_pso_desc.pRootSignature = d3dshader->GetSignature(pass_index, variant_hash);
        _d3d_pso_desc.VS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<ID3DBlob *>(_state_desc._p_vertex_shader->GetByteCode(EShaderType::kVertex, pass_index, variant_hash)));
        _d3d_pso_desc.PS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<ID3DBlob *>(_state_desc._p_pixel_shader->GetByteCode(EShaderType::kPixel, pass_index, variant_hash)));
        if (auto gs_blob = _state_desc._p_pixel_shader->GetByteCode(EShaderType::kGeometry, pass_index, variant_hash); gs_blob != nullptr)
            _d3d_pso_desc.GS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<ID3DBlob *>(gs_blob));
        _d3d_pso_desc.RasterizerState = D3DConvertUtils::ConvertToD3D12RasterizerDesc(_state_desc._raster_state);
        _d3d_pso_desc.BlendState = D3DConvertUtils::ConvertToD3D12BlendDesc(_state_desc._blend_state, _state_desc._rt_state._color_rt_num);
        _d3d_pso_desc.DepthStencilState = D3DConvertUtils::ConvertToD3D12DepthStencilDesc(_state_desc._depth_stencil_state);
        _d3d_pso_desc.SampleMask = UINT_MAX;
        _d3d_pso_desc.PrimitiveTopologyType = D3DConvertUtils::ConvertToDXTopologyType(_state_desc._topology);
        _d3d_topology = D3DConvertUtils::ConvertToDXTopology(_state_desc._topology);
        Vector<D3D12_INPUT_ELEMENT_DESC> v;
        //d3dshader中存储的D3D12_INPUT_ELEMENT_DESC中的SemanticName似乎被析构了导致语义不对，所以这里使用shader层的string semantic来重新构建
        //但为什么之前没事不清楚。
        auto &layout_descs = _state_desc._p_vertex_shader->PipelineInputLayout(pass_index, variant_hash).GetBufferDesc();
        for (u32 i = 0; i < _d3d_pso_desc.InputLayout.NumElements; i++)
        {
            D3D12_INPUT_ELEMENT_DESC desc = *(_d3d_pso_desc.InputLayout.pInputElementDescs + i);
            desc.SemanticName = layout_descs[i].Name.c_str();
            desc.SemanticIndex = (u32) layout_descs[i]._semantic_index;
            v.push_back(desc);
        }
        _d3d_pso_desc.NumRenderTargets = _state_desc._rt_state._color_rt[0] == EALGFormat::kALGFormatUNKOWN ? 0 : _state_desc._rt_state._color_rt_num;
        if (_state_desc._depth_stencil_state._b_depth_write || _state_desc._depth_stencil_state._depth_test_func != ECompareFunc::kAlways ||
            _state_desc._depth_stencil_state._b_front_stencil)
            _d3d_pso_desc.DSVFormat = ConvertToDXGIFormat(_state_desc._rt_state._depth_rt);
        for (u16 i = 0; i < _d3d_pso_desc.NumRenderTargets; i++)
        {
            _d3d_pso_desc.RTVFormats[i] = ConvertToDXGIFormat(_state_desc._rt_state._color_rt[i]);
        }
        _p_sig = d3dshader->GetSignature(pass_index, variant_hash);
        _d3d_pso_desc.SampleDesc.Count = 1;

        //// 创建输入布局
        D3D12_INPUT_LAYOUT_DESC inputLayoutDesc = {};
        inputLayoutDesc.pInputElementDescs = v.data();
        inputLayoutDesc.NumElements = (UINT) v.size();
        _d3d_pso_desc.InputLayout = inputLayoutDesc;
        auto hr = static_cast<D3DContext &>(GraphicsContext::Get()).GetDevice()->CreateGraphicsPipelineState(&_d3d_pso_desc, IID_PPV_ARGS(&_p_plstate));
        AL_ASSERT(hr == S_OK);
        _hash = ConstructPSOHash(_state_desc, pass_index, variant_hash);
        //u8 input_layout, topology, blend_state, raster_state, ds_state, rt_state;
        //u64 shader_hash;
        //GraphicsPipelineStateObject::ExtractPSOHash(_hash, input_layout, shader_hash, topology, blend_state, raster_state, ds_state, rt_state);
        //if (_state_desc._raster_state.Hash() != raster_state)
        //{
        //	LogMgr::Get().LogErrorFormat("PSO {}: _state_desc._raster_state.Hash() != raster_state",_name);
        //}
        _root_parameters = d3dshader->GetRootParameters(pass_index, variant_hash);
        _defines = _state_desc._p_vertex_shader->ActiveKeywords(pass_index, variant_hash);
        _pass_name = _state_desc._p_vertex_shader->GetPassInfo(pass_index)._name;
        WString debug_name = ToWChar(std::format("{}_{}_{}", d3dshader->Name(), pass_index, variant_hash));
        _p_plstate->SetName(debug_name.c_str());
        _p_sig->SetName(debug_name.c_str());
    }

    void D3DGraphicsPipelineState::BindImpl(RHICommandBuffer *rhi_cmd, const BindParams& params)
    {
        if (!IsReady())
        {
            LOG_ERROR("PipelineState must be build before it been bind!");
            return;
        }
        auto d3dcmd = static_cast<D3DCommandBuffer *>(rhi_cmd);
        auto native_cmd = d3dcmd->NativeCmdList();
        const bool is_same_pso = d3dcmd->IsGraphicsPSOActive(this);
        auto& recording_ctx = d3dcmd->RecordingContext();
        if (!is_same_pso)
        {
            if (_state_desc._depth_stencil_state._b_front_stencil)
                native_cmd->OMSetStencilRef(_state_desc._depth_stencil_state._stencil_ref_value);
            native_cmd->SetGraphicsRootSignature(_p_sig.Get());
            native_cmd->SetPipelineState(_p_plstate.Get());
            native_cmd->IASetPrimitiveTopology(_d3d_topology);
            d3dcmd->SetGraphicsPSOActive(this);
            recording_ctx.RenderingStatesData().GfxPsoBindCount++;
        }
#if AILU_ENABLE_FRAME_DEBUGGER
        auto *capture_writer = rhi_cmd->CaptureWriter();
        if (capture_writer && !is_same_pso)
        {
            auto &cap_cache = capture_writer->StateCache();
            cap_cache._pso_invalidated_slot_mask = cap_cache._valid_slot_mask;
            cap_cache._valid_slot_mask = 0u;
            cap_cache._pso_ever_bound = false;
        }
        if (capture_writer)
            capture_writer->StateCache()._pso_ever_bound = true;
#endif
        u32 resolved_slot_mask = 0u;
        for (auto& res : recording_ctx.ResolvedBindResources())
        {
            const u16 slot = res._slot;
            if (slot >= 32u)
                continue;
            resolved_slot_mask |= 1u << slot;
            const u64 binding_hash = BuildBindingHash(res);
#if AILU_ENABLE_FRAME_DEBUGGER
            if (capture_writer)
            {
                auto cap_type = Render::FrameDebugger::ECaptureObjectType::kUnknown;
                switch (res._res_type) {
                case EBindResDescType::kConstBuffer: case EBindResDescType::kConstBufferRaw: cap_type = Render::FrameDebugger::ECaptureObjectType::kConstantBuffer; break;
                case EBindResDescType::kTexture2D: case EBindResDescType::kTexture2DArray: case EBindResDescType::kCubeMap:
                case EBindResDescType::kUAVTexture2D: case EBindResDescType::kTexture3D: case EBindResDescType::kRWTexture3D:
                    cap_type = Render::FrameDebugger::ECaptureObjectType::kTexture; break;
                case EBindResDescType::kBuffer: case EBindResDescType::kRWBuffer: cap_type = Render::FrameDebugger::ECaptureObjectType::kBuffer; break;
                default: break;
                }
                Render::FrameDebugger::PipelineBindingKey key;
                const String resource_name = res._p_resource != nullptr ? res._p_resource->Name() : res._name;
                key._resource_id = capture_writer->RegisterObject(res._p_resource, cap_type, capture_writer->InternString(resource_name));
                key._resource_type = (u32)res._res_type;
                key._gpu_handle = res._addi_info._gpu_handle;
                key._native_resource = (u64)res._addi_info._native_res_ptr;
                key._view_index = res._addi_info._view_index;
                key._sub_resource = res._addi_info._sub_res;
                key._slot = slot;
                key._descriptor_heap_id = d3dcmd->GetDescriptorHeapId();

                auto &cap_cache = capture_writer->StateCache();
                Render::FrameDebugger::PipelineBindingCapture binding_cap;
                binding_cap._slot = slot;
                binding_cap._source = (u8)Render::FrameDebugger::EBindingSource::kUnknown;
                if (res._priority == PipelineResource::kPriorityGlobal)
                    binding_cap._source = (u8)Render::FrameDebugger::EBindingSource::kGlobal;
                else if (res._priority == PipelineResource::kPriorityCmd)
                    binding_cap._source = (u8)Render::FrameDebugger::EBindingSource::kCommand;
                else if (res._priority == PipelineResource::kPriorityLocal)
                    binding_cap._source = (u8)Render::FrameDebugger::EBindingSource::kMaterial;
                binding_cap._key = key;
                const auto name_it = _bind_res_name_lut.find(slot);
                if (name_it != _bind_res_name_lut.end())
                    binding_cap._slot_name = capture_writer->InternString(name_it->second);
                const auto type_it = _bind_res_desc_type_lut.find(slot);
                if (type_it != _bind_res_desc_type_lut.end())
                    binding_cap._shader_resource_type = (u32)type_it->second;
                const bool is_slot_valid = (cap_cache._valid_slot_mask & (1u << slot)) != 0u;
                const bool is_slot_up_to_date = d3dcmd->IsGraphicsSlotUpToDate(slot, binding_hash);
                binding_cap._cache_result = is_slot_up_to_date ? (u8)Render::FrameDebugger::EBindingCacheResult::kSkipped
                                                                : (u8)Render::FrameDebugger::EBindingCacheResult::kBound;
                if (!is_slot_valid)
                    binding_cap._invalid_reasons = (cap_cache._pso_invalidated_slot_mask & (1u << slot)) != 0u
                        ? (u32)Render::FrameDebugger::EBindingInvalidReason::kPsoChanged
                        : (u32)Render::FrameDebugger::EBindingInvalidReason::kSlotUninitialized;
                else if (!is_slot_up_to_date)
                {
                    const auto &old_key = cap_cache._slot_keys[slot];
                    if (old_key._resource_id != key._resource_id)
                        binding_cap._invalid_reasons |= (u32)Render::FrameDebugger::EBindingInvalidReason::kResourceChanged;
                    if (old_key._resource_type != key._resource_type)
                        binding_cap._invalid_reasons |= (u32)Render::FrameDebugger::EBindingInvalidReason::kResourceTypeChanged;
                    if (old_key._gpu_handle != key._gpu_handle)
                        binding_cap._invalid_reasons |= (u32)Render::FrameDebugger::EBindingInvalidReason::kGpuAddressChanged;
                    if (old_key._native_resource != key._native_resource)
                        binding_cap._invalid_reasons |= (u32)Render::FrameDebugger::EBindingInvalidReason::kNativeResourceChanged;
                    if (old_key._view_index != key._view_index)
                        binding_cap._invalid_reasons |= (u32)Render::FrameDebugger::EBindingInvalidReason::kViewIndexChanged;
                    if (old_key._sub_resource != key._sub_resource)
                        binding_cap._invalid_reasons |= (u32)Render::FrameDebugger::EBindingInvalidReason::kSubResourceChanged;
                    if (old_key._descriptor_heap_id != key._descriptor_heap_id)
                        binding_cap._invalid_reasons |= (u32)Render::FrameDebugger::EBindingInvalidReason::kDescriptorHeapChanged;
                }
                capture_writer->RecordBinding(binding_cap);
                cap_cache._slot_keys[slot] = key;
                cap_cache._slot_last_event_ids[slot] = capture_writer->CurrentDrawEventId();
                cap_cache._valid_slot_mask |= (1u << slot);
            }
#endif
            if (!d3dcmd->IsGraphicsSlotUpToDate(slot, binding_hash))
            {
                BindResource(rhi_cmd, res);
                d3dcmd->UpdateGraphicsSlot(slot, binding_hash);
                ++recording_ctx.RenderingStatesData().GfxResBindCount;
                ++recording_ctx.RenderingStatesData().ActualRootSlotBindCount;
            }
            else
            {
                ++recording_ctx.RenderingStatesData().SkippedRootSlotBindCount;
            }
        }
        // A draw may intentionally omit optional bindings that were used by the previous draw
        // with the same PSO. Those root slots remain valid until the PSO changes, where the cache
        // is reset by SetGraphicsPSOActive().
#if AILU_ENABLE_FRAME_DEBUGGER
        if (capture_writer)
        {
            const auto &cap_cache = capture_writer->StateCache();
            for (u16 slot = 0u; slot < 32u; ++slot)
            {
                const bool slot_resolved = (resolved_slot_mask & (1u << slot)) != 0u;
                if ((cap_cache._valid_slot_mask & (1u << slot)) != 0u && !slot_resolved)
                {
                    Render::FrameDebugger::PipelineBindingCapture binding_cap;
                    binding_cap._slot = slot;
                    binding_cap._source = (u8)Render::FrameDebugger::EBindingSource::kInherited;
                    binding_cap._cache_result = (u8)Render::FrameDebugger::EBindingCacheResult::kInherited;
                    binding_cap._key = cap_cache._slot_keys[slot];
                    binding_cap._inherited_from_event = cap_cache._slot_last_event_ids[slot];
                    const auto name_it = _bind_res_name_lut.find(slot);
                    if (name_it != _bind_res_name_lut.end())
                        binding_cap._slot_name = capture_writer->InternString(name_it->second);
                    const auto type_it = _bind_res_desc_type_lut.find(slot);
                    if (type_it != _bind_res_desc_type_lut.end())
                        binding_cap._shader_resource_type = (u32)type_it->second;
                    capture_writer->RecordBinding(binding_cap);
                }
                // PSO 声明了该 slot 但当前 draw 未提交、也没有继承值 → required but unbound。
                else if (!slot_resolved && _bind_res_desc_type_lut.count(slot) != 0u)
                {
                    Render::FrameDebugger::PipelineBindingCapture binding_cap;
                    binding_cap._slot = slot;
                    binding_cap._source = (u8)Render::FrameDebugger::EBindingSource::kUnknown;
                    binding_cap._cache_result = (u8)Render::FrameDebugger::EBindingCacheResult::kUnbound;
                    binding_cap._is_required = true;
                    const auto name_it = _bind_res_name_lut.find(slot);
                    if (name_it != _bind_res_name_lut.end())
                        binding_cap._slot_name = capture_writer->InternString(name_it->second);
                    const auto type_it = _bind_res_desc_type_lut.find(slot);
                    if (type_it != _bind_res_desc_type_lut.end())
                        binding_cap._shader_resource_type = (u32)type_it->second;
                    capture_writer->RecordBinding(binding_cap);
                }
            }
        }
#endif
    }

    void D3DGraphicsPipelineState::BindResource(RHICommandBuffer *cmd, const PipelineResource &res)
    {
        if (res._p_resource == nullptr)
            return;
        static_cast<D3DCommandBuffer *>(cmd)->MarkUsedResource(res._p_resource);
        switch (res._res_type)
        {
            case EBindResDescType::kConstBuffer:
            {
                BindParams params;
                params._is_compute_pipeline = false;
                params._slot = res._slot;
                res._p_resource->Bind(cmd, params);
            }
            break;
            case EBindResDescType::kConstBufferRaw:
            {
                BindParams params;
                params._is_compute_pipeline = false;
                params._slot = res._slot;
                params._params._ub_binder._gpu_ptr = res._addi_info._gpu_handle;
                res._p_resource->Bind(cmd, params);
            }
            break;
            case EBindResDescType::kBuffer:
            {
                BindParams params;
                params._is_compute_pipeline = false;
                params._slot = res._slot;
                res._p_resource->Bind(cmd, params);
            }
            break;
            case EBindResDescType::kRWBuffer:
            {
                BindParams params;
                params._is_compute_pipeline = false;
                params._slot = res._slot;
                params._is_random_access = true;
                res._p_resource->Bind(cmd, params);
            }
            break;
            case EBindResDescType::kTexture2D:
            case EBindResDescType::kTexture2DArray:
            case EBindResDescType::kCubeMap:
            {
                BindParams params;
                params._is_compute_pipeline = false;
                params._slot = res._slot;
                params._params._texture_binder._sub_res = res._addi_info._sub_res;
                params._params._texture_binder._view_idx = res._addi_info._view_index;
                res._p_resource->Bind(cmd, params);
            }
            break;
            case EBindResDescType::kUAVTexture2D:
            {
                BindParams params;
                params._is_compute_pipeline = false;
                params._slot = res._slot;
                params._is_random_access = true;
                params._params._texture_binder._sub_res = res._addi_info._sub_res;
                params._params._texture_binder._view_idx = res._addi_info._view_index;
                res._p_resource->Bind(cmd, params);
            }
            break;
            case EBindResDescType::kTexture3D:
            {
                BindParams params;
                params._is_compute_pipeline = false;
                params._slot = res._slot;
                params._params._texture_binder._sub_res = res._addi_info._sub_res;
                params._params._texture_binder._view_idx = res._addi_info._view_index;
                res._p_resource->Bind(cmd, params);
            }
            break;
            case EBindResDescType::kRWTexture3D:
            {
                BindParams params;
                params._is_compute_pipeline = false;
                params._slot = res._slot;
                params._is_random_access = true;
                params._params._texture_binder._sub_res = res._addi_info._sub_res;
                params._params._texture_binder._view_idx = res._addi_info._view_index;
                res._p_resource->Bind(cmd, params);
            }
            break;
            case EBindResDescType::kSampler:
                break;
            case EBindResDescType::kCBufferAttribute:
                break;
            default:
                break;
        }
    }
}// namespace Ailu::RHI::DX12
