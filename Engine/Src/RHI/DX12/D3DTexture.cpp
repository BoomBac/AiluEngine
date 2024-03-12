#include "pch.h"
#include "RHI/DX12/D3DTexture.h"
#include "RHI/DX12/D3DContext.h"
#include "RHI/DX12/dxhelper.h"
#include "RHI/DX12/D3DCommandBuffer.h"

namespace Ailu
{
	D3DTexture2D::D3DTexture2D(const uint16_t& width, const uint16_t& height, EALGFormat format, bool read_only)
	{
		_width = width;
		_height = height;
		_format = format;
		_channel = GetFormatChannel(_format);
		_res_format = ConvertToDXGIFormat(_format);
		_srv_format = ConvertToDXGIFormat(_format);
		_uav_format = ConvertToDXGIFormat(_format);
		_read_only = read_only;
	}

	D3DTexture2D::D3DTexture2D(const TextureDesc& desc)
	{
		_width = desc._width;
		_height = desc._height;
		_format = desc._res_format;
		_channel = GetFormatChannel(_format);
		_res_format = ConvertToDXGIFormat(desc._res_format);
		_srv_format = ConvertToDXGIFormat(desc._srv_format);
		_uav_format = ConvertToDXGIFormat(desc._uav_format);
		_read_only = desc._read_only;
		_mipmap_count = desc._mipmap;
	}

	D3DTexture2D::~D3DTexture2D()
	{
		Release();
	}

	void D3DTexture2D::FillData(u8* data)
	{
		Texture2D::FillData(data);
	}
	void D3DTexture2D::FillData(Vector<u8*> datas)
	{
		Texture2D::FillData(datas);
	}
	void D3DTexture2D::Bind(CommandBuffer* cmd, u8 slot)
	{
		static_cast<D3DCommandBuffer*>(cmd)->GetCmdList()->SetGraphicsRootDescriptorTable(slot, _srv_gpu_handle);
	}
	void D3DTexture2D::Release()
	{
		for (auto p : _p_datas)
		{
			DESTORY_PTRARR(p);
		}
	}

	void* D3DTexture2D::GetGPUNativePtr()
	{
		return reinterpret_cast<void*>(_srv_gpu_handle.ptr);
	}

	void D3DTexture2D::BuildRHIResource()
	{
		auto p_device{ D3DContext::GetInstance()->GetDevice() };
		auto cmd = CommandBufferPool::Get();
		auto p_cmdlist = static_cast<D3DCommandBuffer*>(cmd.get())->GetCmdList();
		ComPtr<ID3D12Resource> pTextureGPU;
		ComPtr<ID3D12Resource> pTextureUpload;
		D3D12_RESOURCE_DESC textureDesc{};
		textureDesc.MipLevels = _mipmap_count;
		textureDesc.Format = _res_format;
		textureDesc.Width = _width;
		textureDesc.Height = _height;
		textureDesc.Flags = _read_only ? D3D12_RESOURCE_FLAG_NONE : D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		textureDesc.DepthOrArraySize = 1;
		textureDesc.SampleDesc.Count = 1;
		textureDesc.SampleDesc.Quality = 0;
		textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		CD3DX12_HEAP_PROPERTIES heap_prop(D3D12_HEAP_TYPE_DEFAULT);

		ThrowIfFailed(p_device->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &textureDesc, _read_only ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_COMMON,
			nullptr, IID_PPV_ARGS(pTextureGPU.GetAddressOf())));

		if (_read_only)
		{
			const UINT subresourceCount = textureDesc.DepthOrArraySize * textureDesc.MipLevels;
			const UINT64 uploadBufferSize = GetRequiredIntermediateSize(pTextureGPU.Get(), 0, subresourceCount);
			heap_prop.Type = D3D12_HEAP_TYPE_UPLOAD;
			auto upload_buf_desc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
			ThrowIfFailed(p_device->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &upload_buf_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr, IID_PPV_ARGS(pTextureUpload.GetAddressOf())));

			Vector<D3D12_SUBRESOURCE_DATA> subres_datas = {};
			for (size_t i = 0; i < _mipmap_count; i++)
			{
				u16 cur_mip_width = _width >> i;
				u16 cur_mip_height = _height >> i;
				D3D12_SUBRESOURCE_DATA subdata;
				subdata.pData = _p_datas[i];
				subdata.RowPitch = GetPixelByteSize(_format) * cur_mip_width;  // pixel size/byte  * width
				subdata.SlicePitch = subdata.RowPitch * cur_mip_height;
				subres_datas.emplace_back(subdata);
			}
			UpdateSubresources(p_cmdlist, pTextureGPU.Get(), pTextureUpload.Get(), 0, 0, subresourceCount, subres_datas.data());
			auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(pTextureGPU.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			p_cmdlist->ResourceBarrier(1, &barrier);
		}

		// Describe and create a SRV for the texture.
		//D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		_srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		_srv_desc.Format = _srv_format;
		_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		_srv_desc.Texture2D.MipLevels = _mipmap_count;
		_srv_desc.Texture2D.MostDetailedMip = 0;
		auto [cpu_handle, gpu_handle] = D3DContext::GetInstance()->GetSRVDescriptorHandle();
		_srv_cpu_handle = cpu_handle;
		_srv_gpu_handle = gpu_handle;
		p_device->CreateShaderResourceView(pTextureGPU.Get(), &_srv_desc, _srv_cpu_handle);
		if (!_read_only)
		{
			_uav_desc.Format = _uav_format;
			_uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			_uav_desc.Texture2D.MipSlice = 0;
			auto [uav_cpu_handle, uav_gpu_handle] = D3DContext::GetInstance()->GetUAVDescriptorHandle();
			_uav_cpu_handle = uav_cpu_handle;
			_uav_gpu_handle = uav_gpu_handle;
			p_device->CreateUnorderedAccessView(pTextureGPU.Get(), nullptr, &_uav_desc, _uav_cpu_handle);
		}
		_textures.emplace_back(pTextureGPU);
		_upload_textures.emplace_back(pTextureUpload);
		D3DContext::GetInstance()->ExecuteCommandBuffer(cmd);
		CommandBufferPool::Release(cmd);
	}

	//----------------------------------------------------------D3DTextureCubeMap---------------------------------------------------------------------
	D3DTextureCubeMap::D3DTextureCubeMap(const uint16_t& width, const uint16_t& height, EALGFormat format)
	{
		_width = width;
		_height = height;
		_format = format;
		_channel = 4;
	}

	D3DTextureCubeMap::~D3DTextureCubeMap()
	{
		Release();
	}

	void D3DTextureCubeMap::FillData(Vector<u8*>& data)
	{
		TextureCubeMap::FillData(data);
	}

	void D3DTextureCubeMap::BuildRHIResource()
	{
		auto p_device{ D3DContext::GetInstance()->GetDevice() };
		auto cmd = CommandBufferPool::Get();
		auto p_cmdlist = static_cast<D3DCommandBuffer*>(cmd.get())->GetCmdList();
		ComPtr<ID3D12Resource> pTextureGPU;
		ComPtr<ID3D12Resource> pTextureUpload;
		D3D12_RESOURCE_DESC textureDesc{};
		textureDesc.MipLevels = 1;
		textureDesc.Format = ConvertToDXGIFormat(_format);
		textureDesc.Width = _width;
		textureDesc.Height = _height;
		textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		textureDesc.DepthOrArraySize = 6;
		textureDesc.SampleDesc.Count = 1;
		textureDesc.SampleDesc.Quality = 0;
		textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		CD3DX12_HEAP_PROPERTIES heap_prop(D3D12_HEAP_TYPE_DEFAULT);
		ThrowIfFailed(p_device->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &textureDesc, D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr, IID_PPV_ARGS(pTextureGPU.GetAddressOf())));

		const UINT subresourceCount = textureDesc.DepthOrArraySize * textureDesc.MipLevels;
		const UINT64 uploadBufferSize = GetRequiredIntermediateSize(pTextureGPU.Get(), 0, subresourceCount);
		heap_prop.Type = D3D12_HEAP_TYPE_UPLOAD;
		auto upload_buf_desc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
		ThrowIfFailed(p_device->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &upload_buf_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr, IID_PPV_ARGS(pTextureUpload.GetAddressOf())));
		Vector<D3D12_SUBRESOURCE_DATA> cubemap_datas;
		for (int i = 0; i < 6; ++i)
		{
			for (int j = 0; j < textureDesc.MipLevels; ++j)
			{
				D3D12_SUBRESOURCE_DATA textureData = {};
				textureData.pData = _p_datas[i];
				textureData.RowPitch = 4 * _width;  // pixel size/byte  * width
				textureData.SlicePitch = textureData.RowPitch * _height;
				cubemap_datas.emplace_back(textureData);
			}
		}
		UpdateSubresources(p_cmdlist, pTextureGPU.Get(), pTextureUpload.Get(), 0, 0, subresourceCount, cubemap_datas.data());
		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(pTextureGPU.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		p_cmdlist->ResourceBarrier(1, &barrier);
		// Describe and create a SRV for the texture.
		//D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		_srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		_srv_desc.Format = textureDesc.Format;
		_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		_srv_desc.Texture2D.MipLevels = 1;
		_srv_desc.Texture2D.MostDetailedMip = 0;
		auto [cpu_handle, gpu_handle] = D3DContext::GetInstance()->GetSRVDescriptorHandle();
		_srv_cpu_handle = cpu_handle;
		_srv_gpu_handle = gpu_handle;
		p_device->CreateShaderResourceView(pTextureGPU.Get(), &_srv_desc, _srv_cpu_handle);
		_textures.emplace_back(pTextureGPU);
		_upload_textures.emplace_back(pTextureUpload);
		D3DContext::GetInstance()->ExecuteCommandBuffer(cmd);
		CommandBufferPool::Release(cmd);
	}

	void D3DTextureCubeMap::Bind(CommandBuffer* cmd, u8 slot)
	{
		static_cast<D3DCommandBuffer*>(cmd)->GetCmdList()->SetGraphicsRootDescriptorTable(slot, _srv_gpu_handle);
	}

	void D3DTextureCubeMap::Release()
	{
		for (u8* p : _p_datas)
			DESTORY_PTR(p);
	}
	//----------------------------------------------------------D3DTextureCubeMap---------------------------------------------------------------------

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
	D3DRenderTexture::D3DRenderTexture(const uint16_t& width, const uint16_t& height, String name, int mipmap, EALGFormat format)
	{

	}
	D3DRenderTexture::D3DRenderTexture(const uint16_t& width, const uint16_t& height, String name, int mipmap, EALGFormat format, bool is_cubemap)
	{
		_width = width;
		_height = height;
		_format = format;
		_channel = 4;
		_name = name;
		_rt_handle._name = _name;
		_is_cubemap = is_cubemap;
		_state = ETextureResState::kDefault;
		_mipmap_count = mipmap;
		bool need_uav = _mipmap_count > 1;
		u16 texture_num = 1u;
		if (is_cubemap)
		{
			texture_num = 6u;
		}
		bool is_shadowmap = IsShadowMapFormat(_format);
		auto p_device{ D3DContext::GetInstance()->GetDevice() };
		D3D12_RESOURCE_DESC tex_desc{};
		tex_desc.MipLevels = _mipmap_count;
		tex_desc.Format = ConvertToDXGIFormat(_format);
		tex_desc.Flags = is_shadowmap ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		if(need_uav)
			tex_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		tex_desc.Width = _width;
		tex_desc.Height = _height;
		tex_desc.DepthOrArraySize = texture_num;
		tex_desc.SampleDesc.Count = 1;
		tex_desc.SampleDesc.Quality = 0;
		tex_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		D3D12_CLEAR_VALUE clear_value{};
		//clear_value.Format = is_shadowmap? DXGI_FORMAT_D24_UNORM_S8_UINT : tex_desc.Format;
		clear_value.Format = tex_desc.Format;
		if (is_shadowmap)
			clear_value.DepthStencil = { 1.0f,0 };
		else
			memcpy(clear_value.Color, kClearColor, sizeof(kClearColor));
		CD3DX12_HEAP_PROPERTIES heap_prop(D3D12_HEAP_TYPE_DEFAULT);
		ThrowIfFailed(p_device->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &tex_desc, is_shadowmap ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET,
			&clear_value, IID_PPV_ARGS(_p_buffer.GetAddressOf())));
		_srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		_srv_desc.Format = is_shadowmap ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS : tex_desc.Format;
		//_srv_desc.Format = tex_desc.Format;
		_srv_desc.ViewDimension = _is_cubemap? D3D12_SRV_DIMENSION_TEXTURECUBE : D3D12_SRV_DIMENSION_TEXTURE2D;
		_srv_desc.Texture2D.MostDetailedMip = 0;
		_srv_desc.Texture2D.MipLevels = 1;
		auto [s_ch, s_gh] = D3DContext::GetInstance()->GetSRVDescriptorHandle();
		_srv_gpu_handle = s_gh;
		_srv_cpu_handle = s_ch;
		p_device->CreateShaderResourceView(_p_buffer.Get(), &_srv_desc, _srv_cpu_handle);
		for (u16 i = 0; i < texture_num; ++i)
		{
			if (!is_shadowmap)
			{
				auto handle = D3DContext::GetInstance()->GetRTVDescriptorHandle();
				_d3drt_handles[i]._color_handle._srv_cpu_handle = handle;
				D3D12_RENDER_TARGET_VIEW_DESC rtv_desc{};
				rtv_desc.ViewDimension = _is_cubemap ? D3D12_RTV_DIMENSION_TEXTURE2DARRAY :  D3D12_RTV_DIMENSION_TEXTURE2D;
				rtv_desc.Format = tex_desc.Format;
				rtv_desc.Texture2D.MipSlice = 0;
				rtv_desc.Texture2D.PlaneSlice = 0;
				if (_is_cubemap)
				{
					rtv_desc.Texture2DArray.FirstArraySlice = i;
					rtv_desc.Texture2DArray.ArraySize = 1;
				}
				p_device->CreateRenderTargetView(_p_buffer.Get(), &rtv_desc, _d3drt_handles[i]._color_handle._srv_cpu_handle);
			}
			else
			{
				auto handle = D3DContext::GetInstance()->GetDSVDescriptorHandle();
				_d3drt_handles[i]._depth_handle._srv_cpu_handle = handle;
				D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc{};
				dsv_desc.Format = tex_desc.Format;
				dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
				dsv_desc.Texture2D.MipSlice = 0;
				p_device->CreateDepthStencilView(_p_buffer.Get(), &dsv_desc, _d3drt_handles[i]._depth_handle._srv_cpu_handle);
			}
		}

		_state = is_shadowmap ? ETextureResState::kDepthTarget : ETextureResState::kColorTagret;
	}
	void D3DRenderTexture::Bind(CommandBuffer* cmd, u8 slot)
	{
		RenderTexture::Bind(cmd,slot);
		static_cast<D3DCommandBuffer*>(cmd)->GetCmdList()->SetGraphicsRootDescriptorTable(slot, _srv_gpu_handle);
	}
	u8* D3DRenderTexture::GetCPUNativePtr()
	{
		return nullptr;
	}
	void* D3DRenderTexture::GetNativeCPUHandle()
	{
		return reinterpret_cast<void*>(&_d3drt_handles[0]._color_handle._srv_cpu_handle);
	}
	void* D3DRenderTexture::GetNativeCPUHandle(u16 index)
	{
		index = _is_cubemap? index : 0;
		return reinterpret_cast<void*>(&_d3drt_handles[index]._color_handle._srv_cpu_handle);
	}
	void D3DRenderTexture::Release()
	{
	}
	void D3DRenderTexture::Transition(CommandBuffer* cmd, ETextureResState state)
	{
		if (state == _state) return;
		auto old_state = _state;
		RenderTexture::Transition(cmd,state);
		auto state_before = ConvertToD3DResourceState(old_state);
		auto state_after = ConvertToD3DResourceState(_state);
		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(_p_buffer.Get(), state_before, state_after);
		static_cast<D3DCommandBuffer*>(cmd)->GetCmdList()->ResourceBarrier(1, &barrier);
	}
	//----------------------------------------------------------D3DRenderTexture----------------------------------------------------------------------
}