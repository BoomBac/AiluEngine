#include "pch.h"
#include "RHI/DX12/D3DGraphicsPipelineState.h"
#include "RHI/DX12/D3DShader.h"
#include "RHI/DX12/dxhelper.h"
#include "Framework/Common/ResourceMgr.h"
#include "RHI/DX12/D3DContext.h"

namespace Ailu
{
	static ID3D12RootSignature* GenerateRootSignature(std::initializer_list<Ref<Shader>> shaders)
	{
		bool b_render_object_scene = true;
		if (b_render_object_scene)
		{
			ID3D12RootSignature* p_signature = nullptr;
			D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
			featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
			auto device = D3DContext::GetInstance()->GetDevice();
			if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
			{
				featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
			}
			static CD3DX12_DESCRIPTOR_RANGE1 ranges[32]{};
			static CD3DX12_ROOT_PARAMETER1 rootParameters[32]{};
			rootParameters[0].InitAsConstantBufferView(0u);
			rootParameters[1].InitAsConstantBufferView(1u);
			rootParameters[2].InitAsConstantBufferView(2u);
			std::vector<std::tuple<uint8_t, std::string>> ps_tex_bind_info{};
			for (auto it = shaders.begin(); it != shaders.end(); it++)
			{             
				auto reflection = static_cast<D3DShader*>(it->get())->GetD3DReflectionInfo();
				if (reflection == nullptr)
				{
					LOG_WARNING(L"未获取到着色器反射信息！");
					continue;
				}
				D3D12_SHADER_DESC desc{};
				reflection->GetDesc(&desc);
				std::vector<D3D12_SHADER_INPUT_BIND_DESC> bind_desc(desc.BoundResources);
				for (uint32_t i = 0u; i < desc.BoundResources; i++)
				{
					auto resc_desc = reflection->GetResourceBindingDesc(i, &bind_desc[i]);
					auto bind_type = bind_desc[i].Type;
					if (bind_type == D3D_SHADER_INPUT_TYPE::D3D_SIT_CBUFFER)
					{

					}
					else if (bind_type == D3D_SHADER_INPUT_TYPE::D3D_SIT_TEXTURE)
					{
						LOG_INFO("{}", bind_desc[i].Name);
						ps_tex_bind_info.emplace_back(std::make_tuple(bind_desc[i].BindPoint, bind_desc[i].Name));
					}
					else if (bind_type == D3D_SHADER_INPUT_TYPE::D3D_SIT_SAMPLER)
					{

					}
				}
			}
			uint16_t root_param_index = 0u;
			for (auto& [slot, name] : ps_tex_bind_info)
			{
				//ranges[root_param_index].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, slot, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
				//rootParameters[root_param_index + 3].InitAsShaderResourceView(slot,0,D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC,D3D12_SHADER_VISIBILITY_PIXEL);
				rootParameters[root_param_index + 3].InitAsShaderResourceView(slot,0,D3D12_ROOT_DESCRIPTOR_FLAG_NONE,D3D12_SHADER_VISIBILITY_PIXEL);
			}

			D3D12_STATIC_SAMPLER_DESC sampler = {};
			sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
			sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
			sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
			sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
			sampler.MipLODBias = 0;
			sampler.MaxAnisotropy = 0;
			sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
			sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
			sampler.MinLOD = 0.0f;
			sampler.MaxLOD = D3D12_FLOAT32_MAX;
			sampler.ShaderRegister = 0;
			sampler.RegisterSpace = 0;
			sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

			D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
				D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
				D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
				D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
				D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
			CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
			uint16_t root_param_count = 3u + root_param_index;
			rootSignatureDesc.Init_1_1(root_param_count, rootParameters, 1u, &sampler, rootSignatureFlags);
			ComPtr<ID3DBlob> signature;
			ComPtr<ID3DBlob> error;
			ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
			ThrowIfFailed(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&p_signature)));
			return p_signature;
		}
		else
		{
			return nullptr;
		}
	}

	static ID3D12RootSignature* GenerateEmptyRootSignature()
	{
		auto device = D3DContext::GetInstance()->GetDevice();
		ID3D12RootSignature* p_signature = nullptr;
		CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init(0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
		ComPtr<ID3DBlob> signature;
		ComPtr<ID3DBlob> error;
		ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
		ThrowIfFailed(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&p_signature)));
	}

	static ID3D12RootSignature* GenerateRootSignature(Ref<Shader>& vs, Ref<Shader>& ps)
	{
		bool b_render_object_scene = true;
		if (b_render_object_scene)
		{
			ID3D12RootSignature* p_signature = nullptr;
			D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
			featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
			auto device = D3DContext::GetInstance()->GetDevice();
			if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
			{
				featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
			}
			static CD3DX12_DESCRIPTOR_RANGE1 ranges[32]{};
			static CD3DX12_ROOT_PARAMETER1 rootParameters[32]{};
			rootParameters[0].InitAsConstantBufferView(0u);
			rootParameters[1].InitAsConstantBufferView(1u);
			rootParameters[2].InitAsConstantBufferView(2u);
			std::vector<std::tuple<uint8_t, std::string>> ps_tex_bind_info{};
			auto reflection = static_cast<D3DShader*>(ps.get())->GetD3DReflectionInfo();
			if (reflection == nullptr)
			{
				LOG_WARNING(L"未获取到着色器反射信息！");
				return nullptr;
			}
			D3D12_SHADER_DESC desc{};
			reflection->GetDesc(&desc);
			std::vector<D3D12_SHADER_INPUT_BIND_DESC> bind_desc(desc.BoundResources);
			for (uint32_t i = 0u; i < desc.BoundResources; i++)
			{
				auto resc_desc = reflection->GetResourceBindingDesc(i, &bind_desc[i]);
				auto bind_type = bind_desc[i].Type;
				if (bind_type == D3D_SHADER_INPUT_TYPE::D3D_SIT_CBUFFER)
				{

				}
				else if (bind_type == D3D_SHADER_INPUT_TYPE::D3D_SIT_TEXTURE)
				{
					LOG_INFO("{}", bind_desc[i].Name);
					ps_tex_bind_info.emplace_back(std::make_tuple(bind_desc[i].BindPoint, bind_desc[i].Name));
				}
				else if (bind_type == D3D_SHADER_INPUT_TYPE::D3D_SIT_SAMPLER)
				{

				}
			}
			uint16_t root_param_index = 0u;
			for (auto& [slot, name] : ps_tex_bind_info)
			{
				ranges[root_param_index].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, slot, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
				rootParameters[root_param_index + 3].InitAsDescriptorTable(1,&ranges[root_param_index]);
				root_param_index++;
				//rootParameters[root_param_index + 3].InitAsShaderResourceView(slot, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_PIXEL);
				//rootParameters[root_param_index++ + 3].InitAsShaderResourceView(slot, 0);
			}
			//rootParameters[3].InitAsShaderResourceView(0);
			D3D12_STATIC_SAMPLER_DESC sampler = {};
			sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
			sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
			sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
			sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
			sampler.MipLODBias = 0;
			sampler.MaxAnisotropy = 0;
			sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
			sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
			sampler.MinLOD = 0.0f;
			sampler.MaxLOD = D3D12_FLOAT32_MAX;
			sampler.ShaderRegister = 0;
			sampler.RegisterSpace = 0;
			sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

			D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
				D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
				D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
				D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
				D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
			CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
			uint16_t root_param_count = 3u + root_param_index;
			rootSignatureDesc.Init_1_1(root_param_count, rootParameters, 1u, &sampler, rootSignatureFlags);
			//rootSignatureDesc.Init_1_1(root_param_count, rootParameters, 0u, nullptr, rootSignatureFlags);
			ComPtr<ID3DBlob> signature;
			ComPtr<ID3DBlob> error;
			ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
			ThrowIfFailed(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&p_signature)));
			return p_signature;
		}
		else
		{
			return nullptr;
		}
	}

	D3DGraphicsPipelineState::D3DGraphicsPipelineState(const GraphicsPipelineStateInitializer& initializer) : _state_desc(initializer)
	{
		_hash = GraphicsPipelineStateObject::ConstuctPSOHash(initializer);
	}

	void D3DGraphicsPipelineState::Build()
	{
		if (!_b_build)
		{
			auto d3dshader = std::static_pointer_cast<D3DShader>(_state_desc._p_vertex_shader);
			_p_sig = d3dshader->GetSignature();
			_p_bind_res_desc_infos = const_cast<std::unordered_map<std::string, ShaderBindResourceInfo>*>(&d3dshader->GetBindResInfo());
			auto it = _p_bind_res_desc_infos->find(RenderConstants::kCBufNameSceneState);
			if (it != _p_bind_res_desc_infos->end())
			{
				_per_frame_cbuf_bind_slot = it->second._bind_slot;
			}
			auto [desc, count] = d3dshader->GetVertexInputLayout();
			_d3d_pso_desc.InputLayout = { desc, count };
			_d3d_pso_desc.pRootSignature = _p_sig.Get();
			_d3d_pso_desc.VS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<ID3DBlob*>(_state_desc._p_vertex_shader->GetByteCode(EShaderType::kVertex)));
			_d3d_pso_desc.PS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<ID3DBlob*>(_state_desc._p_pixel_shader->GetByteCode(EShaderType::kPixel)));
			_d3d_pso_desc.RasterizerState = D3DConvertUtils::ConvertToD3D12RasterizerDesc(_state_desc._raster_state);
			_d3d_pso_desc.BlendState = D3DConvertUtils::ConvertToD3D12BlendDesc(_state_desc._blend_state, _state_desc._rt_state._color_rt_num);
			_d3d_pso_desc.DepthStencilState = D3DConvertUtils::ConvertToD3D12DepthStencilDesc(_state_desc._depth_stencil_state);
			_d3d_pso_desc.SampleMask = UINT_MAX;
			_d3d_pso_desc.PrimitiveTopologyType = D3DConvertUtils::ConvertToDXTopologyType(_state_desc._topology);

			_d3d_pso_desc.NumRenderTargets = _state_desc._rt_state._color_rt_num;
			if (_state_desc._depth_stencil_state._b_depth_write) _d3d_pso_desc.DSVFormat = ConvertToDXGIFormat(_state_desc._rt_state._depth_rt);
			for (int i = 0; i < _state_desc._rt_state._color_rt_num; i++)
			{
				_d3d_pso_desc.RTVFormats[i] = ConvertToDXGIFormat(_state_desc._rt_state._color_rt[i]);
			}
			_d3d_pso_desc.SampleDesc.Count = 1;
			ThrowIfFailed(D3DContext::GetInstance()->GetDevice()->CreateGraphicsPipelineState(&_d3d_pso_desc, IID_PPV_ARGS(&_p_plstate)));
			_b_build = true;
		}
	}

	void D3DGraphicsPipelineState::Bind()
	{
		if (!_b_build)
		{
			LOG_ERROR("PipelineState must be build before it been bind!");
			return;
		}
		D3DContext::GetInstance()->GetCmdList()->SetGraphicsRootSignature(_p_sig.Get());
		D3DContext::GetInstance()->GetCmdList()->SetPipelineState(_p_plstate.Get());
		D3DContext::GetInstance()->GetCmdList()->IASetPrimitiveTopology(D3DConvertUtils::ConvertToDXTopology(_state_desc._topology));
	}

	void D3DGraphicsPipelineState::SubmitBindResource(void* res, const EBindResDescType& res_type, short slot)
	{
		static auto context = D3DContext::GetInstance();
		switch (res_type)
		{
		case Ailu::EBindResDescType::kConstBuffer:
		{
			short bind_slot = slot == -1 ? _per_frame_cbuf_bind_slot : slot;
			D3D12_CONSTANT_BUFFER_VIEW_DESC view = *reinterpret_cast<D3D12_CONSTANT_BUFFER_VIEW_DESC*>(res);
			context->GetCmdList()->SetGraphicsRootConstantBufferView(bind_slot, view.BufferLocation);
			return;
		}
			break;
		case Ailu::EBindResDescType::kTexture2D:
			break;
		case Ailu::EBindResDescType::kSampler:
			break;
		case Ailu::EBindResDescType::kCBufferAttribute:
			break;
		}
	}

	void D3DGraphicsPipelineState::SubmitBindResource(void* res, const EBindResDescType& res_type, const std::string& name)
	{
		static auto context = D3DContext::GetInstance();
		if (_p_bind_res_desc_infos == nullptr)
		{
			AL_ASSERT(true, "SubmitBindResource: bind shader missing!");
			return;
		}
		auto it = _p_bind_res_desc_infos->find(name);
		if (it != _p_bind_res_desc_infos->end())
		{
			switch (res_type)
			{
			case Ailu::EBindResDescType::kConstBuffer:
			{
				D3D12_CONSTANT_BUFFER_VIEW_DESC view = *reinterpret_cast<D3D12_CONSTANT_BUFFER_VIEW_DESC*>(res);
				context->GetCmdList()->SetGraphicsRootConstantBufferView(it->second._bind_slot, view.BufferLocation);
				return;
			}
			break;
			case Ailu::EBindResDescType::kTexture2D:
				break;
			case Ailu::EBindResDescType::kSampler:
				break;
			case Ailu::EBindResDescType::kCBufferAttribute:
				break;
			}
		}
	}

	Ref<D3DGraphicsPipelineState> D3DGraphicsPipelineState::GetGizmoPSO()
	{
		Ref<Shader> shader;
		shader = ShaderLibrary::Load(GetResPath("Shaders/gizmo.hlsl"));
		GraphicsPipelineStateInitializer pso_initializer{};
		pso_initializer._blend_state = TStaticBlendState<true,EBlendFactor::kSrcAlpha,EBlendFactor::kOneMinusSrcAlpha>::GetRHI();
		pso_initializer._depth_stencil_state = TStaticDepthStencilState<false, ECompareFunc::kAlways>::GetRHI();
		pso_initializer._rt_state = RenderTargetState{};
		pso_initializer._topology = ETopology::kLine;
		pso_initializer._p_vertex_shader = shader;
		pso_initializer._p_pixel_shader = shader;
		pso_initializer._raster_state = TStaticRasterizerState<ECullMode::kBack, EFillMode::kWireframe>::GetRHI();
		auto d3dpso = Ref<D3DGraphicsPipelineState>(new D3DGraphicsPipelineState(pso_initializer));
		d3dpso->Build();
		return d3dpso;
	}
}
