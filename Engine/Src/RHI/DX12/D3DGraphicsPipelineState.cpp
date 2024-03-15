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
	}

	void D3DGraphicsPipelineState::Build()
	{
		if (!_b_build)
		{
			auto d3dshader = static_cast<D3DShader*>(_state_desc._p_vertex_shader);
			_p_bind_res_desc_infos = const_cast<std::unordered_map<std::string, ShaderBindResourceInfo>*>(&d3dshader->GetBindResInfo());
			auto it = _p_bind_res_desc_infos->find(RenderConstants::kCBufNameSceneState);
			if (it != _p_bind_res_desc_infos->end())
			{
				_per_frame_cbuf_bind_slot = it->second._bind_slot;
			}
			auto [desc, count] = d3dshader->GetVertexInputLayout();
			_d3d_pso_desc.InputLayout = { desc, count };
			_d3d_pso_desc.pRootSignature = d3dshader->GetSignature();
			_d3d_pso_desc.VS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<ID3DBlob*>(_state_desc._p_vertex_shader->GetByteCode(EShaderType::kVertex)));
			_d3d_pso_desc.PS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<ID3DBlob*>(_state_desc._p_pixel_shader->GetByteCode(EShaderType::kPixel)));
			_d3d_pso_desc.RasterizerState = D3DConvertUtils::ConvertToD3D12RasterizerDesc(_state_desc._raster_state);
			_d3d_pso_desc.BlendState = D3DConvertUtils::ConvertToD3D12BlendDesc(_state_desc._blend_state, _state_desc._rt_state._color_rt_num);
			_d3d_pso_desc.DepthStencilState = D3DConvertUtils::ConvertToD3D12DepthStencilDesc(_state_desc._depth_stencil_state);
			_d3d_pso_desc.SampleMask = UINT_MAX;
			_d3d_pso_desc.PrimitiveTopologyType = D3DConvertUtils::ConvertToDXTopologyType(_state_desc._topology);

			_d3d_pso_desc.NumRenderTargets = _state_desc._rt_state._color_rt[0] == EALGFormat::kALGFormatUnknown? 0 : _state_desc._rt_state._color_rt_num;
			if (_state_desc._depth_stencil_state._b_depth_write) _d3d_pso_desc.DSVFormat = ConvertToDXGIFormat(_state_desc._rt_state._depth_rt);
			for (u16 i = 0; i < _d3d_pso_desc.NumRenderTargets; i++)
			{
				_d3d_pso_desc.RTVFormats[i] = ConvertToDXGIFormat(_state_desc._rt_state._color_rt[i]);
			}
			_p_sig = d3dshader->GetSignature();
			_d3d_pso_desc.SampleDesc.Count = 1;
			ThrowIfFailed(D3DContext::GetInstance()->GetDevice()->CreateGraphicsPipelineState(&_d3d_pso_desc, IID_PPV_ARGS(&_p_plstate)));
			_b_build = true;
			//LOG_INFO("rt hash {}", (u32)_state_desc._rt_state.Hash());
			_hash = ConstructPSOHash(_state_desc);
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
		_p_cmd->SetDescriptorHeaps(1, D3DContext::GetInstance()->GetDescriptorHeap().GetAddressOf());
	}

	void D3DGraphicsPipelineState::SetPipelineResource(CommandBuffer* cmd,void* res, const EBindResDescType& res_type, u8 slot)
	{
		BindResource(cmd,res,res_type,slot);
	}

	void D3DGraphicsPipelineState::SetPipelineResource(CommandBuffer* cmd,void* res, const EBindResDescType& res_type, const String& name)
	{
		if (_p_bind_res_desc_infos == nullptr)
		{
			AL_ASSERT(true, "SubmitBindResource: bind shader missing!");
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
		auto it = std::find_if(_p_bind_res_desc_infos->begin(), _p_bind_res_desc_infos->end(), [=](const auto& element)->bool {
			return element.second._bind_slot == slot;
			});
		if (it == _p_bind_res_desc_infos->end() || it->second._res_type != res_type)
			return false;
		return true;
	}

	void D3DGraphicsPipelineState::BindResource(CommandBuffer* cmd,void* res, const EBindResDescType& res_type, u8 slot)
	{
		static auto context = D3DContext::GetInstance();
		switch (res_type)
		{
		case Ailu::EBindResDescType::kConstBuffer:
		{
			u8 bind_slot = slot == 255 ? _per_frame_cbuf_bind_slot : slot;
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
