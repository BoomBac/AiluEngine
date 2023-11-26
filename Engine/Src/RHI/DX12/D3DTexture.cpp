#include "pch.h"
#include "RHI/DX12/D3DTexture.h"
#include "RHI/DX12/D3DContext.h"
#include "RHI/DX12/dxhelper.h"

namespace Ailu
{
	D3DTexture2D::D3DTexture2D(const uint16_t& width, const uint16_t& height, EALGFormat format)
	{
		_width = width;
		_height = height;
		_format = format;
		_channel = 4;
	}

	D3DTexture2D::~D3DTexture2D()
	{
		Release();
	}

	void D3DTexture2D::FillData(uint8_t* data)
	{
		Texture2D::FillData(data);
		Construct();
	}
	void D3DTexture2D::FillData(Vector<u8*> datas)
	{
		Texture2D::FillData(datas);
		Construct();
	}
	void D3DTexture2D::Bind(uint8_t slot) const
	{
		static auto cmd = D3DContext::GetInstance()->GetCmdList();
		cmd->SetGraphicsRootDescriptorTable(slot, _gpu_handle);
	}
	void D3DTexture2D::Release()
	{
		for (auto p : _p_datas)
		{
			DESTORY_PTRARR(p);
		}
	}

	void D3DTexture2D::Construct()
	{
		auto p_device{ D3DContext::GetInstance()->GetDevice() };
		auto p_cmdlist{ D3DContext::GetInstance()->GetTaskCmdList() };
		ComPtr<ID3D12Resource> pTextureGPU;
		ComPtr<ID3D12Resource> pTextureUpload;
		D3D12_RESOURCE_DESC textureDesc{};
		textureDesc.MipLevels = _mipmap_count;
		textureDesc.Format = ConvertToDXGIFormat(_format);
		textureDesc.Width = _width;
		textureDesc.Height = _height;
		textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		textureDesc.DepthOrArraySize = 1;
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

		Vector<D3D12_SUBRESOURCE_DATA> subres_datas = {};
		for (size_t i = 0; i < _mipmap_count; i++)
		{
			u16 cur_mip_width = _width >> i;
			u16 cur_mip_height = _height >> i;
			D3D12_SUBRESOURCE_DATA subdata;
			subdata.pData = _p_datas[i];
			subdata.RowPitch = 4 * cur_mip_width;  // pixel size/byte  * width
			subdata.SlicePitch = subdata.RowPitch * cur_mip_height;
			subres_datas.emplace_back(subdata);
		}
		UpdateSubresources(p_cmdlist, pTextureGPU.Get(), pTextureUpload.Get(), 0, 0, subresourceCount, subres_datas.data());


		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(pTextureGPU.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		p_cmdlist->ResourceBarrier(1, &barrier);
		// Describe and create a SRV for the texture.
		//D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		_srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		_srv_desc.Format = textureDesc.Format;
		_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		_srv_desc.Texture2D.MipLevels = _mipmap_count;
		_srv_desc.Texture2D.MostDetailedMip = 0;
		auto [cpu_handle,gpu_handle] = D3DContext::GetInstance()->GetSRVDescriptorHandle();
		_cpu_handle = cpu_handle;
		_gpu_handle = gpu_handle;
		p_device->CreateShaderResourceView(pTextureGPU.Get(), &_srv_desc, _cpu_handle);
		_textures.emplace_back(pTextureGPU);
		_upload_textures.emplace_back(pTextureUpload);
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
		auto p_device{ D3DContext::GetInstance()->GetDevice() };
		auto p_cmdlist{ D3DContext::GetInstance()->GetTaskCmdList() };
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
		_cpu_handle = cpu_handle;
		_gpu_handle = gpu_handle;
		p_device->CreateShaderResourceView(pTextureGPU.Get(), &_srv_desc, _cpu_handle);
		_textures.emplace_back(pTextureGPU);
		_upload_textures.emplace_back(pTextureUpload);
	}

	void D3DTextureCubeMap::Bind(uint8_t slot) const
	{
		D3DContext::GetInstance()->GetCmdList()->SetGraphicsRootDescriptorTable(slot, _gpu_handle);
	}

	void D3DTextureCubeMap::Release()
	{
		for (u8* p : _p_datas)
			DESTORY_PTR(p);
	}
	//----------------------------------------------------------D3DTextureCubeMap---------------------------------------------------------------------
}