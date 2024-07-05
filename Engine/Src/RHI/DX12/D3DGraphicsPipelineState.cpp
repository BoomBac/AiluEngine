#include "pch.h"
#include "RHI/DX12/D3DGraphicsPipelineState.h"
#include "RHI/DX12/D3DShader.h"
#include "RHI/DX12/dxhelper.h"
#include "Framework/Common/ResourceMgr.h"
#include "RHI/DX12/D3DContext.h"
#include "RHI/DX12/D3DCommandBuffer.h"

namespace Ailu
{
	D3DGraphicsPipelineState::D3DGraphicsPipelineState(const GraphicsPipelineStateInitializer& initializer) : _state_desc(initializer)
	{
		_hash = GraphicsPipelineStateObject::ConstructPSOHash(initializer);
		_name = initializer._p_vertex_shader->Name();
                ZeroMemory(&_d3d_pso_desc,sizeof(_d3d_pso_desc));
	}

	void D3DGraphicsPipelineState::Build(u16 pass_index, ShaderVariantHash variant_hash)
	{
		if (!_b_build)
		{
			auto d3dshader = static_cast<D3DShader*>(_state_desc._p_vertex_shader);
			_p_bind_res_desc_infos = const_cast<std::unordered_map<std::string, ShaderBindResourceInfo>*>(&d3dshader->GetBindResInfo(pass_index, variant_hash));
			_bind_res_desc_type_lut.clear();
			for(auto& it : *_p_bind_res_desc_infos)
			{
				_bind_res_desc_type_lut.insert(std::make_pair(it.second._bind_slot,it.second._res_type));
			}
			auto it = _p_bind_res_desc_infos->find(RenderConstants::kCBufNameSceneState);
			if (it != _p_bind_res_desc_infos->end())
			{
				_per_frame_cbuf_bind_slot = it->second._bind_slot;
			}
			auto [desc, count] = d3dshader->GetVertexInputLayout(pass_index, variant_hash);
			_d3d_pso_desc.InputLayout = { desc, count };
			_d3d_pso_desc.pRootSignature = d3dshader->GetSignature(pass_index, variant_hash);
			_d3d_pso_desc.VS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<ID3DBlob*>(_state_desc._p_vertex_shader->GetByteCode(EShaderType::kVertex, pass_index, variant_hash)));
			_d3d_pso_desc.PS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<ID3DBlob*>(_state_desc._p_pixel_shader->GetByteCode(EShaderType::kPixel, pass_index, variant_hash)));
			_d3d_pso_desc.RasterizerState = D3DConvertUtils::ConvertToD3D12RasterizerDesc(_state_desc._raster_state);
			_d3d_pso_desc.BlendState = D3DConvertUtils::ConvertToD3D12BlendDesc(_state_desc._blend_state, _state_desc._rt_state._color_rt_num);
			_d3d_pso_desc.DepthStencilState = D3DConvertUtils::ConvertToD3D12DepthStencilDesc(_state_desc._depth_stencil_state);
			_d3d_pso_desc.SampleMask = UINT_MAX;
			_d3d_pso_desc.PrimitiveTopologyType = D3DConvertUtils::ConvertToDXTopologyType(_state_desc._topology);
			Vector<D3D12_INPUT_ELEMENT_DESC> v;
			//d3dshader中存储的D3D12_INPUT_ELEMENT_DESC中的SemanticName似乎被析构了导致语义不对，所以这里使用shader层的string semantic来重新构建
			//但为什么之前没事不清楚。
			auto& layout_descs = _state_desc._p_vertex_shader->PipelineInputLayout(pass_index, variant_hash).GetBufferDesc();
			for (int i = 0; i < _d3d_pso_desc.InputLayout.NumElements; i++)
			{
				D3D12_INPUT_ELEMENT_DESC desc = *(_d3d_pso_desc.InputLayout.pInputElementDescs + i);
				desc.SemanticName = layout_descs[i].Name.data();
				v.push_back(desc);
			}
			_d3d_pso_desc.NumRenderTargets = _state_desc._rt_state._color_rt[0] == EALGFormat::EALGFormat::kALGFormatUnknown? 0 : _state_desc._rt_state._color_rt_num;
			if (_state_desc._depth_stencil_state._b_depth_write || _state_desc._depth_stencil_state._depth_test_func != ECompareFunc::kAlways)
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
			inputLayoutDesc.NumElements = v.size();
			_d3d_pso_desc.InputLayout = inputLayoutDesc;

			auto hr = D3DContext::Get()->GetDevice()->CreateGraphicsPipelineState(&_d3d_pso_desc, IID_PPV_ARGS(&_p_plstate));
			ThrowIfFailed(hr);
			_b_build = true;
			//LOG_INFO("rt hash {}", (u32)_state_desc._rt_state.Hash());
			_hash = ConstructPSOHash(_state_desc,pass_index, variant_hash);
			u8 input_layout, topology, blend_state, raster_state, ds_state, rt_state;
			u32 shader_hash;
			GraphicsPipelineStateObject::ExtractPSOHash(_hash, input_layout, shader_hash, topology, blend_state, raster_state, ds_state, rt_state);
			if (_state_desc._raster_state.Hash() != raster_state)
			{
				g_pLogMgr->LogErrorFormat("PSO {}: _state_desc._raster_state.Hash() != raster_state",_name);
			}
		}
		static u16 count = 0;
		//LOG_WARNING("PipelineState build {}!", count++);
	}

	void D3DGraphicsPipelineState::Bind(CommandBuffer* cmd)
	{
		if (!_b_build)
		{
			LOG_ERROR("PipelineState must be build before it been bind!");
			return;
		}
		_p_cmd = static_cast<D3DCommandBuffer*>(cmd)->GetCmdList();
		_p_cmd->SetGraphicsRootSignature(_p_sig.Get());
		_p_cmd->SetPipelineState(_p_plstate.Get());
		_p_cmd->IASetPrimitiveTopology(D3DConvertUtils::ConvertToDXTopology(_state_desc._topology));
		//_p_cmd->SetDescriptorHeaps(1, D3DContext::Get()->GetDescriptorHeap().GetAddressOf());
	}

	void D3DGraphicsPipelineState::SetPipelineResource(CommandBuffer* cmd,void* res, const EBindResDescType& res_type, u8 slot)
	{
		BindResource(cmd,res,res_type,slot);
	}

	void D3DGraphicsPipelineState::SetPipelineResource(CommandBuffer* cmd,void* res, const EBindResDescType& res_type, const String& name)
	{
		if (_p_bind_res_desc_infos == nullptr)
		{
			AL_ASSERT_MSG(true, "SubmitBindResource: bind shader missing!");
			return;
		}
		auto it = _p_bind_res_desc_infos->find(name);
		if (it != _p_bind_res_desc_infos->end())
		{
			BindResource(cmd,res, res_type, it->second._bind_slot);
		}
	}

	bool D3DGraphicsPipelineState::IsValidPipelineResource(const EBindResDescType& res_type, u8 slot) const
	{
		if (_bind_res_desc_type_lut.contains(slot))
		{
			auto range = _bind_res_desc_type_lut.equal_range(slot);
			for (auto it = range.first; it != range.second; ++it) 
			{
				if(it->second == res_type)
					return true;
			}
		}
		return false;
	}

	void D3DGraphicsPipelineState::BindResource(CommandBuffer* cmd,void* res, const EBindResDescType& res_type, u8 slot)
	{
		if (res == nullptr)
			return;
		u8 bind_slot = slot == 255 ? _per_frame_cbuf_bind_slot : slot;
		if (!IsValidPipelineResource(res_type, bind_slot))
		{
			LOG_WARNING("PSO:{} submitBindResource: bind slot {} not found!",_name,(u16)bind_slot);
			return;
		}
		static auto context = D3DContext::Get();
		switch (res_type)
		{
		case Ailu::EBindResDescType::kConstBuffer:
		{
			//D3D12_CONSTANT_BUFFER_VIEW_DESC view = *reinterpret_cast<D3D12_CONSTANT_BUFFER_VIEW_DESC*>(res);
			//context->GetCmdList()->SetGraphicsRootConstantBufferView(bind_slot, view.BufferLocation);
			static_cast<ConstantBuffer*>(res)->Bind(cmd,bind_slot);
			return;
		}
		break;
		case Ailu::EBindResDescType::kTexture2D:
		{
			reinterpret_cast<Texture*>(res)->Bind(cmd,slot);
		}
		break;
		case Ailu::EBindResDescType::kSampler:
			break;
		case Ailu::EBindResDescType::kCBufferAttribute:
			break;
		}
	}
}
