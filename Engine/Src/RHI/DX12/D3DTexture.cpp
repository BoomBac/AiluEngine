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

	D3DTexture2D::D3DTexture2D(const uint16_t& width, const uint16_t& height, EALGFormat format, const String& asset_path) : 
		D3DTexture2D(width, height, format)
	{
		_path = asset_path;
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
		for (auto& id : _submited_tasks)
			D3DContext::GetInstance()->CancelResourceTask(id);
		for (auto p : _p_datas)
		{
			DESTORY_PTRARR(p);
		}
	}

	void* D3DTexture2D::GetGPUNativePtr()
	{
		return reinterpret_cast<void*>(_gpu_handle.ptr);
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


		//auto task_id = D3DContext::GetInstance()->SubmitResourceTask([=]() {
		//	Vector<D3D12_SUBRESOURCE_DATA> subres_datas = {};
		//	for (size_t i = 0; i < _mipmap_count; i++)
		//	{
		//		u16 cur_mip_width = _width >> i;
		//		u16 cur_mip_height = _height >> i;
		//		D3D12_SUBRESOURCE_DATA subdata;
		//		subdata.pData = _p_datas[i];
		//		subdata.RowPitch = 4 * cur_mip_width;  // pixel size/byte  * width
		//		subdata.SlicePitch = subdata.RowPitch * cur_mip_height;
		//		subres_datas.emplace_back(subdata);
		//	}
		//	UpdateSubresources(p_cmdlist, pTextureGPU.Get(), pTextureUpload.Get(), 0, 0, subresourceCount, subres_datas.data());
		//	auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(pTextureGPU.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		//	p_cmdlist->ResourceBarrier(1, &barrier);
		//	});
		//_submited_tasks.emplace_back(task_id);
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
		auto [cpu_handle, gpu_handle] = D3DContext::GetInstance()->GetSRVDescriptorHandle();
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
		_width = width;
		_height = height;
		_format = format;
		_channel = 4;
		_name = name;
		_rt_handle._name = _name;
		_state = ETextureResState::kDefault;
		bool is_shadowmap = IsShadowMapFormat(_format);
		auto p_device{ D3DContext::GetInstance()->GetDevice() };
		auto p_cmdlist{ D3DContext::GetInstance()->GetTaskCmdList() };
		D3D12_RESOURCE_DESC tex_desc{};
		tex_desc.MipLevels = 1;
		tex_desc.Format = ConvertToDXGIFormat(_format);
		tex_desc.Flags = is_shadowmap ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		tex_desc.Width = _width;
		tex_desc.Height = _height;
		tex_desc.DepthOrArraySize = 1;
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
		_srv_desc.Format = is_shadowmap? DXGI_FORMAT_R24_UNORM_X8_TYPELESS : tex_desc.Format;
		//_srv_desc.Format = tex_desc.Format;
		_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		_srv_desc.Texture2D.MostDetailedMip = 0;
		_srv_desc.Texture2D.MipLevels = 1;
		auto [s_ch, s_gh] = D3DContext::GetInstance()->GetSRVDescriptorHandle();
		_srv_gpu_handle = s_gh;
		_srv_cpu_handle = s_ch;
		p_device->CreateShaderResourceView(_p_buffer.Get(), &_srv_desc, _srv_cpu_handle);
		if (!is_shadowmap)
		{
			auto handle = D3DContext::GetInstance()->GetRTVDescriptorHandle();
			_d3d_handle._color_handle._cpu_handle = handle;
			D3D12_RENDER_TARGET_VIEW_DESC rtv_desc{};
			rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
			rtv_desc.Format = tex_desc.Format;
			rtv_desc.Texture2D.MipSlice = 0;
			rtv_desc.Texture2D.PlaneSlice = 0;
			p_device->CreateRenderTargetView(_p_buffer.Get(), &rtv_desc, _d3d_handle._color_handle._cpu_handle);
		}
		else
		{
			auto handle = D3DContext::GetInstance()->GetDSVDescriptorHandle();
			//_d3d_handle._depth_handle._gpu_handle = d_gh;
			_d3d_handle._depth_handle._cpu_handle = handle;
			D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc{};
			//dsv_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
			dsv_desc.Format = tex_desc.Format;
			dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			dsv_desc.Texture2D.MipSlice = 0;
			p_device->CreateDepthStencilView(_p_buffer.Get(), &dsv_desc, _d3d_handle._depth_handle._cpu_handle);
		}
		_state = is_shadowmap ? ETextureResState::kDepthTarget : ETextureResState::kColorTagret;
	}
	void D3DRenderTexture::Bind(uint8_t slot) const
	{
		D3DContext::GetInstance()->GetCmdList()->SetGraphicsRootDescriptorTable(slot, _srv_gpu_handle);
	}
	uint8_t* D3DRenderTexture::GetCPUNativePtr()
	{
		return nullptr;
	}
	void* D3DRenderTexture::GetNativeCPUHandle()
	{
		return reinterpret_cast<void*>(&_d3d_handle._color_handle._cpu_handle);
	}
	void D3DRenderTexture::Release()
	{
	}
	void D3DRenderTexture::Transition(ETextureResState state)
	{
		if (state == _state) return;
		auto old_state = _state;
		RenderTexture::Transition(state);
		auto p_cmdlist{ D3DContext::GetInstance()->GetCmdList() };
		auto state_before = ConvertToD3DResourceState(old_state);
		auto state_after = ConvertToD3DResourceState(_state);
		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(_p_buffer.Get(), state_before, state_after);
		p_cmdlist->ResourceBarrier(1, &barrier);
	}
	//----------------------------------------------------------D3DRenderTexture----------------------------------------------------------------------
}