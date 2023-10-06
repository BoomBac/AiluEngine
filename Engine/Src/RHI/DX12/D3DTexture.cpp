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
		auto p_device{ D3DContext::GetInstance()->GetDevice() };
		auto p_cmdlist{ D3DContext::GetInstance()->GetCmdList() };
		ComPtr<ID3D12Resource> pTextureGPU;
		ComPtr<ID3D12Resource> pTextureUpload;
		D3D12_RESOURCE_DESC textureDesc{};
		textureDesc.MipLevels = 1;
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
		D3D12_SUBRESOURCE_DATA textureData = {};
		textureData.pData = data;
		textureData.RowPitch = 4 * _width;  // pixel size/byte  * width
		textureData.SlicePitch = textureData.RowPitch * _height;
		UpdateSubresources(p_cmdlist, pTextureGPU.Get(), pTextureUpload.Get(), 0, 0, subresourceCount, &textureData);

		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(pTextureGPU.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		p_cmdlist->ResourceBarrier(1, &barrier);

		// Describe and create a SRV for the texture.
		//D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		_srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		_srv_desc.Format = textureDesc.Format;
		_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		_srv_desc.Texture2D.MipLevels = 1;
		_srv_desc.Texture2D.MostDetailedMip = 0;
		_current_tex_id = s_texture_index;
		_gpu_handle = D3DContext::GetInstance()->GetSRVGPUDescriptorHandle(s_texture_index);
		_cpu_handle = D3DContext::GetInstance()->GetSRVCPUDescriptorHandle(s_texture_index++);
		p_device->CreateShaderResourceView(pTextureGPU.Get(), &_srv_desc, _cpu_handle);
		_textures.emplace_back(pTextureGPU);
		_upload_textures.emplace_back(pTextureUpload);
	}
	void D3DTexture2D::Bind(uint8_t slot) const
	{
		D3DContext::GetInstance()->GetCmdList()->SetGraphicsRootDescriptorTable(slot,_gpu_handle);
	}
	void D3DTexture2D::Release()
	{
		DESTORY_PTR(_p_data);
	}
}