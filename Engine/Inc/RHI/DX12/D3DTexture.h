#pragma once
#ifndef __D3D_TEXTURE2D_H__
#define __D3D_TEXTURE2D_H__

#include "d3dx12.h"
#include <map>

#include "Render/Texture.h"
#include "Framework/Math/ALMath.hpp"
#include "RHI/DX12/CPUDescriptorManager.h"
#include "RHI/DX12/GPUDescriptorManager.h"
using Microsoft::WRL::ComPtr;

namespace Ailu
{
	class D3DTexture2D : public Texture2D
	{
	public:
		D3DTexture2D(u16 width, u16 height, bool mipmap_chain = true, ETextureFormat::ETextureFormat format = ETextureFormat::kRGBA32, bool linear = false, bool random_access = false);
		~D3DTexture2D();
		void Apply() final;
		TextureHandle GetNativeTextureHandle() const final { return _main_srv_handle.ptr; };
		void Bind(CommandBuffer* cmd, u8 slot) final;
		TextureHandle GetView(u16 mimmap) final;
		void CreateView() final;
		D3D12_GPU_DESCRIPTOR_HANDLE GetMainGPUSRVHandle() const { return _main_srv_handle; };
	private:
		D3D12_GPU_DESCRIPTOR_HANDLE _main_srv_handle;
		Vector<ComPtr<ID3D12Resource>> _textures;
		Vector<ComPtr<ID3D12Resource>> _upload_textures;
		GPUVisibleDescriptorAllocation _allocation;
		GPUVisibleDescriptorAllocation _mimmap_allocation;
		Vector<D3D12_GPU_DESCRIPTOR_HANDLE> _mipmap_srv_handles;
	};

	class D3DCubeMap : public CubeMap
	{
	public:
		D3DCubeMap(u16 width, bool mipmap_chain = true, ETextureFormat::ETextureFormat format = ETextureFormat::kRGBA32, bool linear = false, bool random_access = false);
		~D3DCubeMap();
		void Apply() final;
		TextureHandle GetNativeTextureHandle() const final { return _main_srv_handle.ptr; };
		void Bind(CommandBuffer* cmd, u8 slot) final;

		TextureHandle GetView(ECubemapFace::ECubemapFace face, u16 mimmap) final;
		void CreateView() final;
	private:
		D3D12_GPU_DESCRIPTOR_HANDLE _main_srv_handle;
		Vector<ComPtr<ID3D12Resource>> _textures;
		Vector<ComPtr<ID3D12Resource>> _upload_textures;
		GPUVisibleDescriptorAllocation _allocation;
		GPUVisibleDescriptorAllocation _mimmap_allocation;
		Map<ECubemapFace::ECubemapFace, Vector<D3D12_GPU_DESCRIPTOR_HANDLE>> _mipmap_srv_handles;
	};

	class D3DRenderTexture : public RenderTexture
	{
		inline const static Vector4f kClearColor = Colors::kBlack;
	public:
		D3DRenderTexture(const RenderTextureDesc& desc);
		~D3DRenderTexture();
		void Bind(CommandBuffer* cmd, u8 slot) final;
		void CreateView() final;
		TextureHandle ColorRenderTargetHandle(u16 index = 0) final;
		TextureHandle DepthRenderTargetHandle(u16 index = 0) final;
		TextureHandle ColorTexture(u16 index = 0) final;
		TextureHandle DepthTexture(u16 index = 0) final;
		D3D12_CPU_DESCRIPTOR_HANDLE* TargetCPUHandle(u16 index = 0);
		void Transition(CommandBuffer* cmd, D3D12_RESOURCE_STATES state);
	private:
		D3D12_RESOURCE_STATES _cur_res_state;
		D3D12_SHADER_RESOURCE_VIEW_DESC _srv_desc{};
		D3D12_CPU_DESCRIPTOR_HANDLE _main_srv_cpu_handle;
		D3D12_GPU_DESCRIPTOR_HANDLE _main_srv_gpu_handle;
		//D3D12_GPU_DESCRIPTOR_HANDLE _srv_gpu_handles[7];
		Vector<D3D12_CPU_DESCRIPTOR_HANDLE> _rtv_or_dsv_cpu_handles;
		ComPtr<ID3D12Resource> _p_buffer;
		CPUVisibleDescriptorAllocation _cpu_allocation;
		GPUVisibleDescriptorAllocation _gpu_allocation;
	};
}


#endif // !D3D_TEXTURE2D_H__
