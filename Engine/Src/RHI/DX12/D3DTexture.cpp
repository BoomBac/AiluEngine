#include "pch.h"
#include "RHI/DX12/D3DTexture.h"
#include "RHI/DX12/D3DContext.h"
#include "RHI/DX12/dxhelper.h"
#include "RHI/DX12/D3DCommandBuffer.h"

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
		ComPtr<ID3D12Resource> pTextureGPU;
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
			nullptr, IID_PPV_ARGS(pTextureGPU.GetAddressOf())));

		const UINT subresourceCount = textureDesc.DepthOrArraySize * textureDesc.MipLevels;
		const UINT64 uploadBufferSize = GetRequiredIntermediateSize(pTextureGPU.Get(), 0, subresourceCount);
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
		UpdateSubresources(p_cmdlist, pTextureGPU.Get(), pTextureUpload.Get(), 0, 0, subresourceCount, subres_datas.data());
		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(pTextureGPU.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
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
		p_device->CreateShaderResourceView(pTextureGPU.Get(), &srv_desc, cpu_handle);
		_textures.emplace_back(pTextureGPU);
		_upload_textures.emplace_back(pTextureUpload);
		D3DContext::Get()->ExecuteCommandBuffer(cmd);
		CommandBufferPool::Release(cmd);
		pTextureGPU->SetName(ToWChar(_name));
	}

	void D3DTexture2D::Bind(CommandBuffer* cmd, u8 slot)
	{
		_allocation.CommitDescriptorsForDraw(static_cast<D3DCommandBuffer*>(cmd), slot);
	}

	TextureHandle D3DTexture2D::GetView(u16 mimmap)
	{
		return _mipmap_srv_handles[mimmap - 1].ptr;
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
			p_device->CreateShaderResourceView(_textures[0].Get(), &srv_desc, ch);
			_mipmap_srv_handles.emplace_back(gh);
		}
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
		_upload_textures.emplace_back(pTextureUpload);
		D3DContext::Get()->ExecuteCommandBuffer(cmd);
		CommandBufferPool::Release(cmd);
		pTextureGPU->SetName(ToWChar(_name));
	}

	void D3DCubeMap::Bind(CommandBuffer* cmd, u8 slot)
	{
		_allocation.CommitDescriptorsForDraw(static_cast<D3DCommandBuffer*>(cmd), slot);
	}

	TextureHandle D3DCubeMap::GetView(ECubemapFace::ECubemapFace face, u16 mimmap)
	{
		return _mipmap_srv_handles[face][mimmap].ptr;
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
				p_device->CreateShaderResourceView(_textures[0].Get(), &slice_srv_desc,ch);
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
				rtv_desc.ViewDimension = texture_num >1 ? D3D12_RTV_DIMENSION_TEXTURE2DARRAY : D3D12_RTV_DIMENSION_TEXTURE2D;
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
		_cur_res_state = is_for_depth ? D3D12_RESOURCE_STATE_RENDER_TARGET : D3D12_RESOURCE_STATE_DEPTH_WRITE;
	}

	D3DRenderTexture::~D3DRenderTexture()
	{
		g_pCPUDescriptorAllocator->Free(std::move(_cpu_allocation));
		g_pGPUDescriptorAllocator->Free(std::move(_gpu_allocation));
	}

	void D3DRenderTexture::Bind(CommandBuffer* cmd, u8 slot)
	{
		RenderTexture::Bind(cmd, slot);
		_gpu_allocation.CommitDescriptorsForDraw(static_cast<D3DCommandBuffer*>(cmd), slot);
		//static_cast<D3DCommandBuffer*>(cmd)->GetCmdList()->SetGraphicsRootDescriptorTable(slot, _srv_gpu_handles[0]);
	}

	void D3DRenderTexture::CreateView()
	{
		//if (is_cubemap)
//{
//	for (u32 i = 1; i < 7; i++)
//	{
//		auto [ch, gh] = D3DContext::Get()->GetSRVDescriptorHandle();
//		_srv_cpu_handles[i] = ch;
//		_srv_gpu_handles[i] = gh;
//		D3D12_SHADER_RESOURCE_VIEW_DESC slice_srv_desc{};
//		_srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
//		_srv_desc.Format = is_shadowmap ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS : tex_desc.Format;
//		_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
//		_srv_desc.Texture2DArray.ArraySize = 1;
//		_srv_desc.Texture2DArray.FirstArraySlice = i - 1;
//		p_device->CreateShaderResourceView(_p_buffer.Get(), &_srv_desc, _srv_cpu_handles[i]);
//	}
//}
	}

	TextureHandle D3DRenderTexture::ColorRenderTargetHandle(u16 index)
	{
		if (index < _rtv_or_dsv_cpu_handles.size())
		{
			return _rtv_or_dsv_cpu_handles[index].ptr;
		}
		return 0;
	}

	TextureHandle D3DRenderTexture::DepthRenderTargetHandle(u16 index)
	{
		if (index < _rtv_or_dsv_cpu_handles.size())
		{
			return _rtv_or_dsv_cpu_handles[index].ptr;
		}
		return 0;
	}

	TextureHandle D3DRenderTexture::ColorTexture(u16 index)
	{
		return _main_srv_gpu_handle.ptr;
	}

	TextureHandle D3DRenderTexture::DepthTexture(u16 index)
	{
		return _main_srv_gpu_handle.ptr;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE* D3DRenderTexture::TargetCPUHandle(u16 index)
	{
		if (index < _rtv_or_dsv_cpu_handles.size())
		{
			return &_rtv_or_dsv_cpu_handles[index];
		}
		return nullptr;
	}

	void D3DRenderTexture::Transition(CommandBuffer* cmd, D3D12_RESOURCE_STATES state)
	{
		if (_cur_res_state == state) 
			return;
		auto old_state = _cur_res_state;
		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(_p_buffer.Get(), old_state, state);
		static_cast<D3DCommandBuffer*>(cmd)->GetCmdList()->ResourceBarrier(1, &barrier);
		_cur_res_state = state;
	}

	//----------------------------------------------------------D3DRenderTexture----------------------------------------------------------------------
}