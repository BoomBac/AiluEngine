#include "pch.h"
#include "RHI/DX12/D3DGraphicsPipelineState.h"
#include "RHI/DX12/D3DShader.h"
#include "RHI/DX12/dxhelper.h"
#include "RHI/DX12/D3DContext.h"

namespace Ailu
{
	static D3D12_RASTERIZER_DESC ConvertToD3D12RasterizerDesc(const RasterizerState& state)
	{
		auto raster_state = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		if (state._cull_mode == ECullMode::kFront) raster_state.CullMode = D3D12_CULL_MODE_FRONT;
		else if (state._cull_mode == ECullMode::kNone) raster_state.CullMode = D3D12_CULL_MODE_NONE;
		else raster_state.CullMode = D3D12_CULL_MODE_BACK;
		if (state._fill_mode == EFillMode::kWireframe) raster_state.FillMode = D3D12_FILL_MODE_WIREFRAME;
		else raster_state.FillMode = D3D12_FILL_MODE_SOLID;
		return raster_state;
	}

	static D3D12_BLEND ConvertToD3D12BlendFactor(const EBlendFactor& factor)
	{
		switch (factor)
		{
		case EBlendFactor::kZero:                   return D3D12_BLEND_ZERO;
		case EBlendFactor::kOne:                    return D3D12_BLEND_ONE;
		case EBlendFactor::kSrcColor:               return D3D12_BLEND_SRC_COLOR;
		case EBlendFactor::kOneMinusSrcColor:       return D3D12_BLEND_INV_SRC_COLOR;
		case EBlendFactor::kDstColor:               return D3D12_BLEND_DEST_COLOR;
		case EBlendFactor::kOneMinusDstColor:       return D3D12_BLEND_INV_DEST_COLOR;
		case EBlendFactor::kSrcAlpha:               return D3D12_BLEND_SRC_ALPHA;
		case EBlendFactor::kOneMinusSrcAlpha:       return D3D12_BLEND_INV_SRC_ALPHA;
		case EBlendFactor::kDstAlpha:               return D3D12_BLEND_DEST_ALPHA;
		case EBlendFactor::kOneMinusDstAlpha:       return D3D12_BLEND_INV_DEST_ALPHA;
		//case EBlendFactor::kConstantColor:          return
		//case EBlendFactor::kOneMinusConstantColor:  return
		//case EBlendFactor::kConstantAlpha:          return
		//case EBlendFactor::kOneMinusConstantAlpha:  return
		//case EBlendFactor::kSrcAlphaSaturate:       return
		case EBlendFactor::kSrc1Color:              return D3D12_BLEND_SRC1_COLOR;
		case EBlendFactor::kOneMinusSrc1Color:      return D3D12_BLEND_INV_SRC1_COLOR;
		case EBlendFactor::kSrc1Alpha:              return D3D12_BLEND_SRC1_ALPHA;
		case EBlendFactor::kOneMinusSrc1Alpha:      return D3D12_BLEND_INV_SRC1_ALPHA;
		}
		return D3D12_BLEND_ZERO;
	}

	static D3D12_BLEND_OP ConvertToD3D12BlendOp(const EBlendOp& op)
	{
		switch (op)
		{
		case EBlendOp::kAdd:             return D3D12_BLEND_OP_ADD;
		case EBlendOp::kMax:             return D3D12_BLEND_OP_MAX;
		case EBlendOp::kMin:             return D3D12_BLEND_OP_MIN;
		case EBlendOp::kReverseSubtract: return D3D12_BLEND_OP_REV_SUBTRACT;
		case EBlendOp::kSubtract:        return D3D12_BLEND_OP_SUBTRACT;
		}
		return D3D12_BLEND_OP_ADD;
	}

	static uint8_t ConvertToD3DBlendColorMask(const EColorMask& mask)
	{
		switch (mask)
		{
		case Ailu::EColorMask::kRED:    return D3D12_COLOR_WRITE_ENABLE_RED;
		case Ailu::EColorMask::kGREEN:  return D3D12_COLOR_WRITE_ENABLE_GREEN;
		case Ailu::EColorMask::kBLUE:   return D3D12_COLOR_WRITE_ENABLE_BLUE;
		case Ailu::EColorMask::kALPHA:  return D3D12_COLOR_WRITE_ENABLE_ALPHA;
		case Ailu::EColorMask::k_NONE:  return 0u;
		case Ailu::EColorMask::k_RGB:   return (D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN | D3D12_COLOR_WRITE_ENABLE_BLUE);
		case Ailu::EColorMask::k_RGBA:  return D3D12_COLOR_WRITE_ENABLE_ALL;
		case Ailu::EColorMask::k_RG:    return (D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN);
		case Ailu::EColorMask::k_BA:    return (D3D12_COLOR_WRITE_ENABLE_BLUE | D3D12_COLOR_WRITE_ENABLE_ALPHA);
		}
		return D3D12_COLOR_WRITE_ENABLE_ALL;
	}

	static D3D12_BLEND_DESC ConvertToD3D12BlendDesc(const BlendState& state,const uint8_t& rt_num)
	{
		auto blend_state = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		for (size_t i = 0; i < rt_num; i++)
		{
			blend_state.RenderTarget[i].BlendEnable = state._b_enable;
			blend_state.RenderTarget[i].BlendOp = ConvertToD3D12BlendOp(state._color_op);
			blend_state.RenderTarget[i].BlendOpAlpha = ConvertToD3D12BlendOp(state._alpha_op);
			blend_state.RenderTarget[i].SrcBlend = ConvertToD3D12BlendFactor(state._src_color);
			blend_state.RenderTarget[i].SrcBlendAlpha = ConvertToD3D12BlendFactor(state._src_alpha);
			blend_state.RenderTarget[i].DestBlend = ConvertToD3D12BlendFactor(state._dst_color);
			blend_state.RenderTarget[i].DestBlendAlpha = ConvertToD3D12BlendFactor(state._dst_alpha);
			blend_state.RenderTarget[i].RenderTargetWriteMask = ConvertToD3DBlendColorMask(state._color_mask);
		}
		return blend_state;
	}

	static D3D12_DEPTH_STENCIL_DESC ConvertToD3D12DepthStencilDesc(const DepthStencilState& state)
	{
		auto ds_state = CD3DX12_DEPTH_STENCIL_DESC(CD3DX12_DEFAULT{});
		ds_state.DepthEnable = state._b_depth_write;
		switch (state._depth_test_func)
		{
		case ECompareFunc::kLess: ds_state.DepthFunc = D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_LESS; break;
		case ECompareFunc::kLessEqual: ds_state.DepthFunc = D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_LESS_EQUAL; break;
		case ECompareFunc::kGreater: ds_state.DepthFunc = D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_GREATER; break;
		case ECompareFunc::kGreaterEqual: ds_state.DepthFunc = D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_GREATER_EQUAL; break;
		case ECompareFunc::kAlways: ds_state.DepthFunc = D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_ALWAYS; break;
		case ECompareFunc::kNever: ds_state.DepthFunc = D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_NEVER; break;
		case ECompareFunc::kEqual: ds_state.DepthFunc = D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_EQUAL; break;
		case ECompareFunc::kNotEqual: ds_state.DepthFunc = D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_NOT_EQUAL; break;
		}
		ds_state.StencilEnable = state._b_front_stencil;
		return ds_state;
	}

	static D3D12_PRIMITIVE_TOPOLOGY_TYPE ConvertToDXTopologyType(const ETopology& Topology)
	{
		switch (Topology)
		{
		case ETopology::kPoint: return D3D12_PRIMITIVE_TOPOLOGY_TYPE::D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
		case ETopology::kLine: return D3D12_PRIMITIVE_TOPOLOGY_TYPE::D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
		case ETopology::kTriangle: return D3D12_PRIMITIVE_TOPOLOGY_TYPE::D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		case ETopology::kPatch: return D3D12_PRIMITIVE_TOPOLOGY_TYPE::D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
		}
		return D3D12_PRIMITIVE_TOPOLOGY_TYPE::D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	}

	static D3D_PRIMITIVE_TOPOLOGY ConvertToDXTopology(const ETopology& Topology)
	{
		switch (Topology)
		{
		case ETopology::kPoint: return D3D12_PRIMITIVE_TOPOLOGY::D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
		case ETopology::kLine: return D3D12_PRIMITIVE_TOPOLOGY::D3D_PRIMITIVE_TOPOLOGY_LINELIST;
		case ETopology::kTriangle: return D3D12_PRIMITIVE_TOPOLOGY::D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		case ETopology::kPatch: return D3D12_PRIMITIVE_TOPOLOGY::D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST;
		}
		return D3D12_PRIMITIVE_TOPOLOGY::D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
	}

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
				rootParameters[root_param_index + 3].InitAsShaderResourceView(slot,0,D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC,D3D12_SHADER_VISIBILITY_PIXEL);
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
		
	}

	void D3DGraphicsPipelineState::Build()
	{
		if (!_b_build)
		{
			auto d3dshader = std::static_pointer_cast<D3DShader>(_state_desc._p_vertex_shader);
			_p_sig = d3dshader->GetSignature().Get();
			_p_bind_res_desc_infos = const_cast<std::unordered_map<std::string, ShaderBindResourceInfo>*>(&d3dshader->GetBindResInfo());
			auto it = _p_bind_res_desc_infos->find(D3DConstants::kCBufNameSceneState);
			if (it != _p_bind_res_desc_infos->end())
			{
				_per_frame_cbuf_bind_slot = it->second._bind_slot;
			}
			auto [desc, count] = d3dshader->GetVertexInputLayout();
			_d3d_pso_desc.InputLayout = { desc, count };
			_d3d_pso_desc.pRootSignature = _p_sig.Get();
			_d3d_pso_desc.VS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<ID3DBlob*>(_state_desc._p_vertex_shader->GetByteCode(EShaderType::kVertex)));
			_d3d_pso_desc.PS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<ID3DBlob*>(_state_desc._p_pixel_shader->GetByteCode(EShaderType::kPixel)));
			_d3d_pso_desc.RasterizerState = ConvertToD3D12RasterizerDesc(_state_desc._raster_state);
			_d3d_pso_desc.BlendState = ConvertToD3D12BlendDesc(_state_desc._blend_state, _state_desc._rt_nums);
			_d3d_pso_desc.DepthStencilState = ConvertToD3D12DepthStencilDesc(_state_desc._depth_stencil_state);
			_d3d_pso_desc.SampleMask = UINT_MAX;
			_d3d_pso_desc.PrimitiveTopologyType = ConvertToDXTopologyType(_state_desc._topology);
			_d3d_pso_desc.NumRenderTargets = _state_desc._rt_nums;
			if (_state_desc._depth_stencil_state._b_depth_write) _d3d_pso_desc.DSVFormat = ConvertToDXGIFormat(_state_desc._ds_format);
			for (int i = 0; i < _state_desc._rt_nums; i++)
			{
				_d3d_pso_desc.RTVFormats[i] = ConvertToDXGIFormat(_state_desc._rt_formats[i]);
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
		D3DContext::GetInstance()->GetCmdList()->IASetPrimitiveTopology(ConvertToDXTopology(_state_desc._topology));
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
		shader = ShaderLibrary::Add(GetResPath("Shaders/gizmo.hlsl"));
		GraphicsPipelineStateInitializer pso_initializer{};
		pso_initializer._blend_state = TStaticBlendState<true,EBlendFactor::kSrcAlpha,EBlendFactor::kOneMinusSrcAlpha>::GetRHI();
		pso_initializer._b_has_rt = true;
		pso_initializer._depth_stencil_state = TStaticDepthStencilState<false, ECompareFunc::kAlways>::GetRHI();
		pso_initializer._ds_format = EALGFormat::kALGFormatD32_FLOAT;
		pso_initializer._rt_formats[0] = EALGFormat::kALGFormatR8G8B8A8_UNORM;
		pso_initializer._rt_nums = 1;
		pso_initializer._topology = ETopology::kLine;
		pso_initializer._p_vertex_shader = shader;
		pso_initializer._p_pixel_shader = shader;
		pso_initializer._raster_state = TStaticRasterizerState<ECullMode::kBack, EFillMode::kWireframe>::GetRHI();
		auto d3dpso = Ref<D3DGraphicsPipelineState>(new D3DGraphicsPipelineState(pso_initializer));
		d3dpso->Build();
		return d3dpso;
	}
}
