#include "pch.h"
#include "RHI/DX12/D3DTexture.h"
#include "RHI/DX12/D3DContext.h"
#include "RHI/DX12/dxhelper.h"
#include "RHI/DX12/D3DCommandBuffer.h"
#include "RHI/DX12/D3DShader.h"

namespace Ailu
{
	//----------------------------------------------------------------------------D3DTexture2DNew-----------------------------------------------------------------------
	D3DTexture2D::D3DTexture2D(u16 width, u16 height, bool mipmap_chain, ETextureFormat::ETextureFormat format, bool linear, bool random_access) :
		Texture2D(width, height, mipmap_chain, format, linear, random_access)
	{

	}

	D3DTexture2D::~D3DTexture2D()
	{
		g_pGPUDescriptorAllocator->Free(std::move(_allocation));
		g_pGPUDescriptorAllocator->Free(std::move(_mimmap_allocation));
	}

	void D3DTexture2D::Apply()
	{
		auto mipmap_level = _mipmap_count + 1;
		auto p_device{ D3DContext::Get()->GetDevice() };
		auto cmd = CommandBufferPool::Get();
		auto p_cmdlist = static_cast<D3DCommandBuffer*>(cmd.get())->GetCmdList();
		//ComPtr<ID3D12Resource> pTextureGPU;
		ComPtr<ID3D12Resource> pTextureUpload;
		D3D12_RESOURCE_DESC textureDesc{};
		textureDesc.MipLevels = mipmap_level;
		textureDesc.Format = ConvertToDXGIFormat(_pixel_format);
		textureDesc.Width = _width;
		textureDesc.Height = _height;
		textureDesc.Flags = _is_random_access ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
		textureDesc.DepthOrArraySize = 1;
		textureDesc.SampleDesc.Count = 1;
		textureDesc.SampleDesc.Quality = 0;
		textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		CD3DX12_HEAP_PROPERTIES heap_prop(D3D12_HEAP_TYPE_DEFAULT);
		ThrowIfFailed(p_device->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &textureDesc, _is_random_access ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr, IID_PPV_ARGS(_p_d3dres.GetAddressOf())));

		const UINT subresourceCount = textureDesc.DepthOrArraySize * textureDesc.MipLevels;
		const UINT64 uploadBufferSize = GetRequiredIntermediateSize(_p_d3dres.Get(), 0, subresourceCount);
		heap_prop.Type = D3D12_HEAP_TYPE_UPLOAD;
		auto upload_buf_desc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
		ThrowIfFailed(p_device->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &upload_buf_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr, IID_PPV_ARGS(pTextureUpload.GetAddressOf())));

		Vector<D3D12_SUBRESOURCE_DATA> subres_datas = {};
		for (size_t i = 0; i < mipmap_level; i++)
		{
			u16 cur_mip_width = _width >> i;
			u16 cur_mip_height = _height >> i;
			D3D12_SUBRESOURCE_DATA subdata;
			subdata.pData = _pixel_data[i];
			subdata.RowPitch = _pixel_size * cur_mip_width;  // pixel size/byte  * width
			subdata.SlicePitch = subdata.RowPitch * cur_mip_height;
			subres_datas.emplace_back(subdata);
		}
		UpdateSubresources(p_cmdlist, _p_d3dres.Get(), pTextureUpload.Get(), 0, 0, subresourceCount, subres_datas.data());
		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(_p_d3dres.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		p_cmdlist->ResourceBarrier(1, &barrier);

		// Describe and create a SRV for the texture.
		D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
		srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv_desc.Format = textureDesc.Format;
		srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srv_desc.Texture2D.MipLevels = mipmap_level;
		srv_desc.Texture2D.MostDetailedMip = 0;
		_allocation = g_pGPUDescriptorAllocator->Allocate(1);
		auto [cpu_handle, gpu_handle] = _allocation.At(0);
		_main_srv_handle = gpu_handle;
		p_device->CreateShaderResourceView(_p_d3dres.Get(), &srv_desc, cpu_handle);
		D3DContext::Get()->TrackResource(pTextureUpload);
		D3DContext::Get()->ExecuteCommandBuffer(cmd);
		CommandBufferPool::Release(cmd);
		_p_d3dres->SetName(ToWChar(_name));
	}

	void D3DTexture2D::Bind(CommandBuffer* cmd, u8 slot)
	{
		_allocation.CommitDescriptorsForDraw(static_cast<D3DCommandBuffer*>(cmd), slot);
	}

	TextureHandle D3DTexture2D::GetView(u16 mipmap, bool random_access, ECubemapFace::ECubemapFace face)
	{
		if (mipmap > _mipmap_count + 1)
			return 0;
		return mipmap == 0? _main_srv_handle.ptr : _mipmap_srv_handles[mipmap - 1].ptr;
	}

	void D3DTexture2D::CreateView()
	{
		auto p_device{ D3DContext::Get()->GetDevice() };
		_mimmap_allocation = g_pGPUDescriptorAllocator->Allocate(_mipmap_count);
		for (int i = 0; i < _mipmap_count; i++)
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
			srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srv_desc.Format = ConvertToDXGIFormat(_pixel_format);
			srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srv_desc.Texture2D.MipLevels = 1;
			srv_desc.Texture2D.MostDetailedMip = i + 1;
			auto [ch, gh] = _mimmap_allocation.At(i);
			p_device->CreateShaderResourceView(_p_d3dres.Get(), &srv_desc, ch);
			_mipmap_srv_handles.emplace_back(gh);
		}
	}
	void D3DTexture2D::Name(const String& new_name)
	{
		_name = new_name;
		if(_p_d3dres)
			_p_d3dres->SetName(ToWChar(new_name));
	}
	//----------------------------------------------------------------------------D3DTexture2DNew-----------------------------------------------------------------------

	//----------------------------------------------------------------------------D3DCubeMap----------------------------------------------------------------------------
	D3DCubeMap::D3DCubeMap(u16 width, bool mipmap_chain, ETextureFormat::ETextureFormat format, bool linear, bool random_access)
		: CubeMap(width, mipmap_chain, format, linear, random_access)
	{

	}

	D3DCubeMap::~D3DCubeMap()
	{
		g_pGPUDescriptorAllocator->Free(std::move(_allocation));
		g_pGPUDescriptorAllocator->Free(std::move(_mimmap_allocation));
	}

	void D3DCubeMap::Apply()
	{
		auto mipmap_level = _mipmap_count + 1;
		auto p_device{ D3DContext::Get()->GetDevice() };
		auto cmd = CommandBufferPool::Get();
		auto p_cmdlist = static_cast<D3DCommandBuffer*>(cmd.get())->GetCmdList();
		ComPtr<ID3D12Resource> pTextureGPU;
		ComPtr<ID3D12Resource> pTextureUpload;
		D3D12_RESOURCE_DESC textureDesc{};
		textureDesc.MipLevels = mipmap_level;
		textureDesc.Format = ConvertToDXGIFormat(_pixel_format);
		textureDesc.Width = _width;
		textureDesc.Height = _width;
		textureDesc.Flags = _is_random_access ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
		textureDesc.DepthOrArraySize = 6;
		textureDesc.SampleDesc.Count = 1;
		textureDesc.SampleDesc.Quality = 0;
		textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		CD3DX12_HEAP_PROPERTIES heap_prop(D3D12_HEAP_TYPE_DEFAULT);
		ThrowIfFailed(p_device->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &textureDesc, _is_random_access ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr, IID_PPV_ARGS(pTextureGPU.GetAddressOf())));

		const UINT subresourceCount = textureDesc.DepthOrArraySize * textureDesc.MipLevels;
		const UINT64 uploadBufferSize = GetRequiredIntermediateSize(pTextureGPU.Get(), 0, subresourceCount);
		heap_prop.Type = D3D12_HEAP_TYPE_UPLOAD;
		auto upload_buf_desc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
		ThrowIfFailed(p_device->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &upload_buf_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr, IID_PPV_ARGS(pTextureUpload.GetAddressOf())));
		Vector<D3D12_SUBRESOURCE_DATA> cubemap_datas;
		for (int i = 0; i < textureDesc.MipLevels; ++i)
		{
			for (int j = 0; j < 6; ++j)
			{
				D3D12_SUBRESOURCE_DATA textureData = {};
				textureData.pData = _pixel_data[i * 6 + j];
				textureData.RowPitch = _pixel_size * _width;
				textureData.SlicePitch = textureData.RowPitch * _width;
				cubemap_datas.emplace_back(textureData);
			}
		}
		UpdateSubresources(p_cmdlist, pTextureGPU.Get(), pTextureUpload.Get(), 0, 0, subresourceCount, cubemap_datas.data());
		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(pTextureGPU.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		p_cmdlist->ResourceBarrier(1, &barrier);
		// Describe and create a SRV for the texture.
		D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
		srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv_desc.Format = textureDesc.Format;
		srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		srv_desc.TextureCube.MipLevels = mipmap_level;
		srv_desc.TextureCube.MostDetailedMip = 0;
		_allocation = g_pGPUDescriptorAllocator->Allocate(1);
		auto [cpu_handle, gpu_handle] = _allocation.At(0);
		_main_srv_handle = gpu_handle;
		p_device->CreateShaderResourceView(pTextureGPU.Get(), &srv_desc, cpu_handle);
		_textures.emplace_back(pTextureGPU);
		D3DContext::Get()->ExecuteCommandBuffer(cmd);
		CommandBufferPool::Release(cmd);
		D3DContext::Get()->TrackResource(pTextureUpload);
		pTextureGPU->SetName(ToWChar(_name));
	}

	void D3DCubeMap::Bind(CommandBuffer* cmd, u8 slot)
	{
		_allocation.CommitDescriptorsForDraw(static_cast<D3DCommandBuffer*>(cmd), slot);
	}

	TextureHandle D3DCubeMap::GetView(u16 mipmap, bool random_access, ECubemapFace::ECubemapFace face)
	{
		if (mipmap > _mipmap_count + 1)
			return 0;
		return _mipmap_srv_handles[face][mipmap].ptr;
	}

	void D3DCubeMap::CreateView()
	{
		auto p_device{ D3DContext::Get()->GetDevice() };
		_mimmap_allocation = g_pGPUDescriptorAllocator->Allocate((_mipmap_count + 1) * 6);
		u16 srv_handle_index = 0;
		//for 6 face raw size
		for (int i = 0; i < 6; i++)
		{
			for (int j = 0; j < _mipmap_count + 1; j++)
			{
				auto [ch, gh] = _mimmap_allocation.At(srv_handle_index++);
				D3D12_SHADER_RESOURCE_VIEW_DESC slice_srv_desc{};
				slice_srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				slice_srv_desc.Format = ConvertToDXGIFormat(_pixel_format);
				slice_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
				slice_srv_desc.Texture2DArray.ArraySize = 1;
				slice_srv_desc.Texture2DArray.FirstArraySlice = i;
				slice_srv_desc.Texture2DArray.MipLevels = 1;
				slice_srv_desc.Texture2DArray.MostDetailedMip = j;
				p_device->CreateShaderResourceView(_textures[0].Get(), &slice_srv_desc, ch);
				_mipmap_srv_handles[(ECubemapFace::ECubemapFace)i].emplace_back(gh);
			}
		}
	}
	//----------------------------------------------------------------------------D3DCubeMap-----------------------------------------------------------------------------

	//----------------------------------------------------------D3DRenderTexture----------------------------------------------------------------------
	static D3D12_RESOURCE_STATES ConvertToD3DResourceState(ETextureResState state)
	{
		switch (state)
		{
		case ETextureResState::kColorTagret: return D3D12_RESOURCE_STATE_RENDER_TARGET;
		case ETextureResState::kShaderResource: return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		case ETextureResState::kDepthTarget: return D3D12_RESOURCE_STATE_DEPTH_WRITE;
		}
		return D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_COMMON;
	}

	D3DRenderTexture::D3DRenderTexture(const RenderTextureDesc& desc) : RenderTexture(desc)
	{
		if (_p_mipmapgen_cs0 == nullptr)
		{
			_p_mipmapgen_cs0 = ComputeShader::Create(PathUtils::GetResSysPath(L"Shaders/Compute/cs_mipmap_gen.hlsl"));
			_p_mipmapgen_cs1 = ComputeShader::Create(PathUtils::GetResSysPath(L"Shaders/Compute/cs_mipmap_gen.hlsl"));
		}

		_cur_res_state = D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_COMMON;
		D3D12_SRV_DIMENSION main_view_dimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		u16 texture_num = 1u;
		if (_dimension == ETextureDimension::kCube)
		{
			main_view_dimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			texture_num = 6u;
		}
		else if (_dimension == ETextureDimension::kTex2DArray)
		{
			main_view_dimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
			texture_num = _slice_num;
		}
		else {}
		auto mipmap_level = _mipmap_count + 1;
		bool is_for_depth = _depth > 0;
		auto p_device{ D3DContext::Get()->GetDevice() };
		D3D12_RESOURCE_DESC tex_desc{};
		tex_desc.MipLevels = mipmap_level;
		tex_desc.Format = ConvertToDXGIFormat(_pixel_format);
		tex_desc.Flags = is_for_depth ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		if (_is_random_access)
			tex_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		tex_desc.Width = _width;
		tex_desc.Height = _height;
		tex_desc.DepthOrArraySize = texture_num;
		tex_desc.SampleDesc.Count = 1;
		tex_desc.SampleDesc.Quality = 0;
		tex_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		D3D12_CLEAR_VALUE clear_value{};
		clear_value.Format = tex_desc.Format;
		if (is_for_depth)
			clear_value.DepthStencil = { 1.0f,0 };
		else
			memcpy(clear_value.Color, kClearColor, sizeof(kClearColor));
		CD3DX12_HEAP_PROPERTIES heap_prop(D3D12_HEAP_TYPE_DEFAULT);
		ThrowIfFailed(p_device->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &tex_desc, is_for_depth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET,
			&clear_value, IID_PPV_ARGS(_p_buffer.GetAddressOf())));
		_p_buffer->SetName(ToWChar(_name));
		_srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		_srv_desc.Format = is_for_depth ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS : tex_desc.Format;
		_srv_desc.ViewDimension = main_view_dimension;
		_srv_desc.Texture2D.MostDetailedMip = 0;
		_srv_desc.Texture2D.MipLevels = mipmap_level;

		_gpu_allocation = g_pGPUDescriptorAllocator->Allocate(1);
		auto [s_ch, s_gh] = _gpu_allocation.At(0);
		_main_srv_cpu_handle = s_ch;
		_main_srv_gpu_handle = s_gh;
		p_device->CreateShaderResourceView(_p_buffer.Get(), &_srv_desc, _main_srv_cpu_handle);

		_cpu_allocation = is_for_depth ? std::move(g_pCPUDescriptorAllocator->Allocate(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, texture_num)) :
			std::move(g_pCPUDescriptorAllocator->Allocate(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, texture_num));
		for (u16 i = 0; i < texture_num; ++i)
		{
			if (!is_for_depth)
			{
				auto handle = _cpu_allocation.At(i);
				_rtv_or_dsv_cpu_handles.emplace_back(handle);
				D3D12_RENDER_TARGET_VIEW_DESC rtv_desc{};
				rtv_desc.ViewDimension = texture_num > 1 ? D3D12_RTV_DIMENSION_TEXTURE2DARRAY : D3D12_RTV_DIMENSION_TEXTURE2D;
				rtv_desc.Format = tex_desc.Format;
				rtv_desc.Texture2D.MipSlice = 0;
				rtv_desc.Texture2D.PlaneSlice = 0;
				if (main_view_dimension == D3D12_SRV_DIMENSION_TEXTURECUBE)
				{
					rtv_desc.Texture2DArray.FirstArraySlice = i;
					rtv_desc.Texture2DArray.ArraySize = 1;
				}
				p_device->CreateRenderTargetView(_p_buffer.Get(), &rtv_desc, _rtv_or_dsv_cpu_handles.back());
			}
			else
			{
				auto handle = _cpu_allocation.At(i);
				_rtv_or_dsv_cpu_handles.emplace_back(handle);
				D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc{};
				dsv_desc.Format = tex_desc.Format;
				dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
				dsv_desc.Texture2D.MipSlice = 0;
				p_device->CreateDepthStencilView(_p_buffer.Get(), &dsv_desc, _rtv_or_dsv_cpu_handles.back());
			}
		}
		_cur_res_state = is_for_depth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET;
	}

	D3DRenderTexture::~D3DRenderTexture()
	{
		g_pCPUDescriptorAllocator->Free(std::move(_cpu_allocation));
		g_pGPUDescriptorAllocator->Free(std::move(_gpu_allocation));
		g_pGPUDescriptorAllocator->Free(std::move(_mimmap_srv_allocation));
		g_pGPUDescriptorAllocator->Free(std::move(_mimmap_uav_allocation));
	}

	void D3DRenderTexture::Bind(CommandBuffer* cmd, u8 slot)
	{
		if (s_current_rt != this)
		{
			RenderTexture::Bind(cmd, slot);
			MakesureResourceState(static_cast<D3DCommandBuffer*>(cmd)->GetCmdList(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			_gpu_allocation.CommitDescriptorsForDraw(static_cast<D3DCommandBuffer*>(cmd), slot);
		}
	}

	void D3DRenderTexture::CreateView()
	{
		if (_dimension == ETextureDimension::kCube)
		{
			auto p_device{ D3DContext::Get()->GetDevice() };
			_mimmap_srv_allocation = g_pGPUDescriptorAllocator->Allocate((_mipmap_count + 1) * 6);
			u16 srv_handle_index = 0;
			//for 6 face raw size
			for (int i = 0; i < 6; i++)
			{
				for (int j = 0; j < _mipmap_count + 1; j++)
				{
					auto [ch, gh] = _mimmap_srv_allocation.At(srv_handle_index++);
					D3D12_SHADER_RESOURCE_VIEW_DESC slice_srv_desc{};
					slice_srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
					slice_srv_desc.Format = ConvertToDXGIFormat(_pixel_format);
					slice_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
					slice_srv_desc.Texture2DArray.ArraySize = 1;
					slice_srv_desc.Texture2DArray.FirstArraySlice = i;
					slice_srv_desc.Texture2DArray.MipLevels = 1;
					slice_srv_desc.Texture2DArray.MostDetailedMip = j;
					p_device->CreateShaderResourceView(_p_buffer.Get(), &slice_srv_desc, ch);
					_mipmap_srv_handles[(ECubemapFace::ECubemapFace)(i + 1)].emplace_back(gh);
				}
			}
			if (_is_random_access)
			{
				_mimmap_uav_allocation = g_pGPUDescriptorAllocator->Allocate(_mipmap_count * 6);
				u16 uav_handle_index = 0;
				for (int i = 0; i < 6; i++)
				{
					//mipmap第一级（原图）不需要随机读写，是给uav只为mipmap创建
					for (int j = 1; j < _mipmap_count + 1; j++)
					{
						auto [ch, gh] = _mimmap_uav_allocation.At(uav_handle_index++);
						D3D12_UNORDERED_ACCESS_VIEW_DESC slice_uav_desc{};
						slice_uav_desc.Format = ConvertToDXGIFormat(_pixel_format);
						slice_uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
						slice_uav_desc.Texture2DArray.ArraySize = 1;
						slice_uav_desc.Texture2DArray.FirstArraySlice = i;
						slice_uav_desc.Texture2DArray.MipSlice = j;
						p_device->CreateUnorderedAccessView(_p_buffer.Get(), nullptr, &slice_uav_desc, ch);
						_mipmap_uav_handles[(ECubemapFace::ECubemapFace)(i + 1)].emplace_back(gh);
					}
				}
			}
		}
		else
		{
			auto p_device{ D3DContext::Get()->GetDevice() };
			_mimmap_srv_allocation = g_pGPUDescriptorAllocator->Allocate(_mipmap_count);
			for (int i = 0; i < _mipmap_count; i++)
			{
				D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
				srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				srv_desc.Format = ConvertToDXGIFormat(_pixel_format);
				srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				srv_desc.Texture2D.MipLevels = 1;
				srv_desc.Texture2D.MostDetailedMip = i + 1;
				auto [ch, gh] = _mimmap_srv_allocation.At(i);
				p_device->CreateShaderResourceView(_p_buffer.Get(), &srv_desc, ch);
				_mipmap_srv_handles[ECubemapFace::kUnknown].emplace_back(gh);
			}
			if (_is_random_access)
			{
				//为原图也创建
				_mimmap_uav_allocation = g_pGPUDescriptorAllocator->Allocate(_mipmap_count + 1);
				u16 uav_handle_index = 0;
				for (int j = 0; j < _mipmap_count + 1; j++)
				{
					auto [ch, gh] = _mimmap_uav_allocation.At(uav_handle_index++);
					D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
					uav_desc.Format = ConvertToDXGIFormat(_pixel_format);
					uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
					uav_desc.Texture2D.MipSlice = j;
					p_device->CreateUnorderedAccessView(_p_buffer.Get(), nullptr, &uav_desc, ch);
					_mipmap_uav_handles[ECubemapFace::kUnknown].emplace_back(gh);
				}
			}
		}
	}

	void D3DRenderTexture::GenerateMipmap(CommandBuffer* cmd)
	{
		auto d3dcmd = static_cast<D3DCommandBuffer*>(cmd)->GetCmdList();
		//_p_test_cs->SetTexture("gInputA", TexturePool::Get("Textures/MyImage01.jpg").get());
		for (int i = 1; i < 7; i++)
		{	
			_p_mipmapgen_cs0->SetInt("SrcMipLevel", 0);
			_p_mipmapgen_cs0->SetInt("NumMipLevels", 4);
			_p_mipmapgen_cs0->SetInt("SrcDimension", 0);
			_p_mipmapgen_cs0->SetBool("IsSRGB", false);
			auto [mip1w, mip1h] = CurMipmapSize(1);
			_p_mipmapgen_cs0->SetVector("TexelSize", Vector4f(1.0f / (float)mip1w, 1.0f / (float)mip1h, 0.0f, 0.0f));
			_p_mipmapgen_cs0->SetTexture("SrcMip", this,  (ECubemapFace::ECubemapFace)i, 0);
			_p_mipmapgen_cs0->SetTexture("OutMip1", this, (ECubemapFace::ECubemapFace)i, 1);
			_p_mipmapgen_cs0->SetTexture("OutMip2", this, (ECubemapFace::ECubemapFace)i, 2);
			_p_mipmapgen_cs0->SetTexture("OutMip3", this, (ECubemapFace::ECubemapFace)i, 3);
			_p_mipmapgen_cs0->SetTexture("OutMip4", this, (ECubemapFace::ECubemapFace)i, 4);
			static_cast<D3DComputeShader*>(_p_mipmapgen_cs0.get())->Bind(cmd, 32, 32, 1);
			auto [mip5w, mip5h] = CurMipmapSize(5);
			_p_mipmapgen_cs1->SetInt("SrcMipLevel", 4);
			_p_mipmapgen_cs1->SetInt("NumMipLevels", min(_mipmap_count - 4,4));
			_p_mipmapgen_cs1->SetInt("SrcDimension", 0);
			_p_mipmapgen_cs1->SetBool("IsSRGB", false);
			_p_mipmapgen_cs1->SetVector("TexelSize", Vector4f(1.0f/ (float)mip5w, 1.0f / (float)mip5h, 0.0f, 0.0f));
			_p_mipmapgen_cs1->SetTexture("SrcMip", this, (ECubemapFace::ECubemapFace)i, 4);
			_p_mipmapgen_cs1->SetTexture("OutMip1", this, (ECubemapFace::ECubemapFace)i, 5);
			_p_mipmapgen_cs1->SetTexture("OutMip2", this, (ECubemapFace::ECubemapFace)i, 6);
			_p_mipmapgen_cs1->SetTexture("OutMip3", this, (ECubemapFace::ECubemapFace)i, 7);
			if(_mipmap_count > 7)
				_p_mipmapgen_cs1->SetTexture("OutMip4", this, (ECubemapFace::ECubemapFace)i, 8);
			static_cast<D3DComputeShader*>(_p_mipmapgen_cs1.get())->Bind(cmd, 32, 32, 1);
		}

	}

	void D3DRenderTexture::Name(const String& value)
	{
		_name = value;
		_p_buffer->SetName(ToWChar(value));
	}

	TextureHandle D3DRenderTexture::GetNativeTextureHandle()
	{
		return _depth > 0? DepthTexture(0) : ColorTexture(0);
	}

	TextureHandle D3DRenderTexture::ColorRenderTargetHandle(u16 index, CommandBuffer* cmd)
	{
		if (index < _rtv_or_dsv_cpu_handles.size())
		{
			if (cmd)
			{
				MakesureResourceState(static_cast<D3DCommandBuffer*>(cmd)->GetCmdList(), D3D12_RESOURCE_STATE_RENDER_TARGET);
			}
			else
			{
				auto cmd = CommandBufferPool::Get("StateTransition");
				MakesureResourceState(static_cast<D3DCommandBuffer*>(cmd.get())->GetCmdList(), D3D12_RESOURCE_STATE_RENDER_TARGET);
				D3DContext::Get()->ExecuteCommandBuffer(cmd);
				CommandBufferPool::Release(cmd);
			}
			s_current_rt = this;
			return _rtv_or_dsv_cpu_handles[index].ptr;
		}
		return 0;
	}

	TextureHandle D3DRenderTexture::DepthRenderTargetHandle(u16 index, CommandBuffer* cmd)
	{
		if (index < _rtv_or_dsv_cpu_handles.size())
		{
			if (cmd)
			{
				MakesureResourceState(static_cast<D3DCommandBuffer*>(cmd)->GetCmdList(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
			}
			else
			{
				auto cmd = CommandBufferPool::Get("StateTransition");
				MakesureResourceState(static_cast<D3DCommandBuffer*>(cmd.get())->GetCmdList(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
				D3DContext::Get()->ExecuteCommandBuffer(cmd);
				CommandBufferPool::Release(cmd);
			}
			s_current_rt = this;
			return _rtv_or_dsv_cpu_handles[index].ptr;
		}
		return 0;
	}

	TextureHandle D3DRenderTexture::ColorTexture(u16 index, CommandBuffer* cmd)
	{
		if (!CanAsShaderResource(this))
			return 0;
		if (cmd)
		{
			MakesureResourceState(static_cast<D3DCommandBuffer*>(cmd)->GetCmdList(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		}
		else
		{
			auto cmd = CommandBufferPool::Get("StateTransition");
			MakesureResourceState(static_cast<D3DCommandBuffer*>(cmd.get())->GetCmdList(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			D3DContext::Get()->ExecuteCommandBuffer(cmd);
			CommandBufferPool::Release(cmd);
		}
		return _main_srv_gpu_handle.ptr;
	}

	TextureHandle D3DRenderTexture::DepthTexture(u16 index, CommandBuffer* cmd)
	{
		if (!CanAsShaderResource(this))
			return 0;
		if (cmd)
		{
			MakesureResourceState(static_cast<D3DCommandBuffer*>(cmd)->GetCmdList(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		}
		else
		{
			auto cmd = CommandBufferPool::Get("StateTransition");
			MakesureResourceState(static_cast<D3DCommandBuffer*>(cmd.get())->GetCmdList(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			D3DContext::Get()->ExecuteCommandBuffer(cmd);
			CommandBufferPool::Release(cmd);
		}
		return _main_srv_gpu_handle.ptr;
	}

	TextureHandle D3DRenderTexture::GetView(u16 mipmap, bool random_access, ECubemapFace::ECubemapFace face)
	{
		if (mipmap > _mipmap_count)
			return 0;
		if (_dimension == ETextureDimension::kCube)
		{
			return random_access ? _mipmap_uav_handles[face][mipmap - 1].ptr : _mipmap_srv_handles[face][mipmap].ptr;
		}
		else
		{
			g_pLogMgr->LogWarning("GetView: texture is not a cube texture");
			return 0;
		}

	}

	D3D12_CPU_DESCRIPTOR_HANDLE* D3DRenderTexture::TargetCPUHandle(u16 index)
	{
		auto cmd = CommandBufferPool::Get("StateTransition");
		MakesureResourceState(static_cast<D3DCommandBuffer*>(cmd.get())->GetCmdList(), _depth > 0 ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET);
		D3DContext::Get()->ExecuteCommandBuffer(cmd);
		CommandBufferPool::Release(cmd);
		s_current_rt = this;
		return &_rtv_or_dsv_cpu_handles[index];
	}

	D3D12_CPU_DESCRIPTOR_HANDLE* D3DRenderTexture::TargetCPUHandle(CommandBuffer* cmd, u16 index)
	{
		MakesureResourceState(static_cast<D3DCommandBuffer*>(cmd)->GetCmdList(), _depth > 0 ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET);
		s_current_rt = this;
		return &_rtv_or_dsv_cpu_handles[index];
	}


	void D3DRenderTexture::MakesureResourceState(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES target_state)
	{
		if (_cur_res_state == target_state)
			return;
		auto old_state = _cur_res_state;
		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(_p_buffer.Get(), old_state, target_state);
		cmd->ResourceBarrier(1, &barrier);
		_cur_res_state = target_state;
	}


	//----------------------------------------------------------D3DRenderTexture----------------------------------------------------------------------
}