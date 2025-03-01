#include "RHI/DX12/D3DGraphicsPipelineState.h"
#include "Framework/Common/ResourceMgr.h"
#include "RHI/DX12/D3DCommandBuffer.h"
#include "RHI/DX12/D3DContext.h"
#include "RHI/DX12/D3DShader.h"
#include "RHI/DX12/dxhelper.h"
//#include "RHI/DX12/UploadBuffer.h"
#include "pch.h"

namespace Ailu
{
    D3DGraphicsPipelineState::D3DGraphicsPipelineState(const GraphicsPipelineStateInitializer &initializer) : _state_desc(initializer)
    {
        _hash = GraphicsPipelineStateObject::ConstructPSOHash(initializer);
        _name = initializer._p_vertex_shader->Name();
        memset(&_d3d_pso_desc, 0, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));//置空，否则编译不加 /sdl时创建pso时会有空指针
    }

    void D3DGraphicsPipelineState::Build(u16 pass_index, ShaderVariantHash variant_hash)
    {
        if (!_b_build)
        {
            auto d3dshader = static_cast<D3DShader *>(_state_desc._p_vertex_shader);
            _p_bind_res_desc_infos = const_cast<std::unordered_map<std::string, ShaderBindResourceInfo> *>(&d3dshader->GetBindResInfo(pass_index, variant_hash));
            _bind_res_desc_type_lut.clear();
            for (auto &it: *_p_bind_res_desc_infos)
            {
                _bind_res_desc_type_lut.insert(std::make_pair(it.second._bind_slot, it.second._res_type));
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
                desc.SemanticName = layout_descs[i].Name.data();
                desc.SemanticIndex = (u32)layout_descs[i]._semantic_index;
                v.push_back(desc);
            }
            _d3d_pso_desc.NumRenderTargets = _state_desc._rt_state._color_rt[0] == EALGFormat::EALGFormat::kALGFormatUnknown ? 0 : _state_desc._rt_state._color_rt_num;
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
            inputLayoutDesc.NumElements = (UINT)v.size();
            _d3d_pso_desc.InputLayout = inputLayoutDesc;
            ThrowIfFailed(D3DContext::Get()->GetDevice()->CreateGraphicsPipelineState(&_d3d_pso_desc, IID_PPV_ARGS(&_p_plstate)));
            _b_build = true;
            _hash = ConstructPSOHash(_state_desc, pass_index, variant_hash);
            //u8 input_layout, topology, blend_state, raster_state, ds_state, rt_state;
            //u64 shader_hash;
            //GraphicsPipelineStateObject::ExtractPSOHash(_hash, input_layout, shader_hash, topology, blend_state, raster_state, ds_state, rt_state);
            //if (_state_desc._raster_state.Hash() != raster_state)
            //{
            //	g_pLogMgr->LogErrorFormat("PSO {}: _state_desc._raster_state.Hash() != raster_state",_name);
            //}
            _defines = _state_desc._p_vertex_shader->ActiveKeywords(pass_index, variant_hash);
            _pass_name = _state_desc._p_vertex_shader->GetPassInfo(pass_index)._name;
            WString debug_name = ToWStr(std::format("{}_{}_{}", d3dshader->Name(), pass_index, variant_hash));
            _p_plstate->SetName(debug_name.c_str());
            _p_sig->SetName(debug_name.c_str());
        }
    }

    void D3DGraphicsPipelineState::Bind(CommandBuffer *cmd)
    {
        _state.Track();
        if (!_b_build)
        {
            LOG_ERROR("PipelineState must be build before it been bind!");
            return;
        }
        _p_cmd = static_cast<D3DCommandBuffer *>(cmd)->GetCmdList();
        if (_state_desc._depth_stencil_state._b_front_stencil)
            _p_cmd->OMSetStencilRef(_state_desc._depth_stencil_state._stencil_ref_value);
        _p_cmd->SetGraphicsRootSignature(_p_sig.Get());
        _p_cmd->SetPipelineState(_p_plstate.Get());
        _p_cmd->IASetPrimitiveTopology(_d3d_topology);
        //_p_cmd->SetDescriptorHeaps(1, D3DContext::Get()->GetDescriptorHeap().GetAddressOf());
    }

    void D3DGraphicsPipelineState::SetPipelineResource(CommandBuffer *cmd, void *res, const EBindResDescType &res_type, u8 slot)
    {
        BindResource(cmd, res, res_type, slot);
    }

    bool D3DGraphicsPipelineState::IsValidPipelineResource(const EBindResDescType &res_type, u8 slot) const
    {
        EBindResDescType aka_type = res_type == EBindResDescType::kConstBufferRaw ? EBindResDescType::kConstBuffer : res_type;
        if (_bind_res_desc_type_lut.contains(slot))
        {
            auto range = _bind_res_desc_type_lut.equal_range(slot);
            for (auto it = range.first; it != range.second; ++it)
            {
                if (it->second == aka_type)
                    return true;
            }
        }
        return false;
    }


    const String &D3DGraphicsPipelineState::SlotToName(u8 slot)
    {
        const static String empty_str = {};
        if (_bind_res_name_lut.contains(slot))
            return _bind_res_name_lut.at(slot);
        return empty_str;
    }

    const u8 D3DGraphicsPipelineState::NameToSlot(const String &name)
    {
        if (_p_bind_res_desc_infos->contains(name))
            return _p_bind_res_desc_infos->at(name)._bind_slot;
        return (u8)-1;
    }

    void D3DGraphicsPipelineState::BindResource(CommandBuffer *cmd, void *res, const EBindResDescType &res_type, u8 slot)
    {
        if (res == nullptr)
            return;
        u8 bind_slot = slot == 255 ? _per_frame_cbuf_bind_slot : slot;
        if (!IsValidPipelineResource(res_type, bind_slot))
        {
            //LOG_WARNING("PSO:{} submitBindResource: bind slot {} not found!", _name, (u16) bind_slot);
            return;
        }
        static auto context = D3DContext::Get();
        switch (res_type)
        {
            case Ailu::EBindResDescType::kConstBuffer:
            {
                static_cast<IConstantBuffer *>(res)->Bind(cmd, bind_slot);
            }
            break;
            case Ailu::EBindResDescType::kConstBufferRaw:
            {
                UploadBuffer::Allocation alloc = *static_cast<UploadBuffer::Allocation *>(res);
                //D3D12_GPU_VIRTUAL_ADDRESS address = *static_cast<D3D12_GPU_VIRTUAL_ADDRESS *>(res);
                static_cast<D3DCommandBuffer *>(cmd)->GetCmdList()->SetGraphicsRootConstantBufferView(bind_slot, alloc.GPU);
            }
            break;
            case EBindResDescType::kBuffer:
            case EBindResDescType::kRWBuffer:
            {
                reinterpret_cast<IGPUBuffer *>(res)->Bind(cmd,slot);
            }
            break;
            case Ailu::EBindResDescType::kTexture2D:
            {
                Texture2D* tex = reinterpret_cast<Texture2D *>(res);
                tex->Bind(cmd, Texture::kMainSRVIndex, slot,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);
            }
            break;
            case Ailu::EBindResDescType::kSampler:
                break;
            case Ailu::EBindResDescType::kCBufferAttribute:
                break;
        }
    }
    void D3DGraphicsPipelineState::SetTopology(ETopology topology)
    {
        _state_desc._topology = topology;
        _d3d_topology = D3DConvertUtils::ConvertToDXTopology(_state_desc._topology);
    }

    void D3DGraphicsPipelineState::SetStencilRef(u8 ref) 
    {
        _state_desc._depth_stencil_state._stencil_ref_value;
    }
}// namespace Ailu
