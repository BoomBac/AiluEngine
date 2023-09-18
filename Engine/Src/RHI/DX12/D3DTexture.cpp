#include "pch.h"
#include "RHI/DX12/dxhelper.h"
#include "RHI/DX12/D3DTexture.h"
#include "RHI/DX12/D3DContext.h"

namespace Ailu
{
	D3DTexture2D::D3DTexture2D(const uint16_t& width, const uint16_t& height, EALGFormat format)
	{

	}
	void D3DTexture2D::FillData(uint8_t* data)
	{
		//auto p_device{ D3DContext::GetInstance()->GetDevice() };
		//auto p_cmdlist{ D3DContext::GetInstance()->GetCmdList() };
		//DXGI_FORMAT texture_format = ConvertToDXGIFormat(_format);
		//ComPtr<ID3D12Resource> pTextureGPU;
		//ComPtr<ID3D12Resource> pTextureUpload;
		//D3D12_RESOURCE_DESC textureDesc{};
		//textureDesc.MipLevels = 1;
		//textureDesc.Format = texture_format;
		//textureDesc.Width = _width;
		//textureDesc.Height = _height;
		//textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		//textureDesc.DepthOrArraySize = 1;
		//textureDesc.SampleDesc.Count = 1;
		//textureDesc.SampleDesc.Quality = 0;
		//textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		//CD3DX12_HEAP_PROPERTIES heap_prop(D3D12_HEAP_TYPE_DEFAULT);
		//ThrowIfFailed(p_device->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &textureDesc, D3D12_RESOURCE_STATE_COPY_DEST,
		//	nullptr, IID_PPV_ARGS(pTextureGPU.GetAddressOf())));

		//const UINT subresourceCount = textureDesc.DepthOrArraySize * textureDesc.MipLevels;
		//const UINT64 uploadBufferSize = GetRequiredIntermediateSize(pTextureGPU.Get(), 0, subresourceCount);
		//heap_prop.Type = D3D12_HEAP_TYPE_UPLOAD;
		//auto upload_buf_desc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
		//ThrowIfFailed(p_device->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &upload_buf_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
		//	nullptr, IID_PPV_ARGS(pTextureUpload.GetAddressOf())));
		//D3D12_SUBRESOURCE_DATA textureData = {};
		//textureData.pData = data;
		//textureData.RowPitch = (((_channel * 8 * _width) >> 3) + 3) & ~3;
		//textureData.SlicePitch = image.pitch * image.height * 4;
		//UpdateSubresources(p_cmdlist, pTextureGPU.Get(), pTextureUpload.Get(), 0, 0, subresourceCount, &textureData);

		//D3D12_RESOURCE_BARRIER barrier = {};
		//barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		//barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		//barrier.Transition.pResource = pTextureGPU.Get();
		//barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		//barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
		//barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		//p_cmdlist_->ResourceBarrier(1, &barrier);
		//// Describe and create a SRV for the texture.
		//D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		//srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		//srvDesc.Format = textureDesc.Format;
		//srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		//srvDesc.Texture2D.MipLevels = 1;
		//srvDesc.Texture2D.MostDetailedMip = 0;
		//D3D12_CPU_DESCRIPTOR_HANDLE srvHandle{};

		//srvHandle.ptr = p_cbv_heap_->GetCPUDescriptorHandleForHeapStart().ptr + (kTextureDescStartIndex + texture_id) * cbv_srv_uav_desc_size_;
		//p_device_->CreateShaderResourceView(pTextureGPU.Get(), &srvDesc, srvHandle);
		//texture_index_[texture.GetName()] = texture_id;
		//buffers_.push_back(pTextureUpload.Get());
		//textures_[texture_id++] = pTextureGPU.Get();
	}
}