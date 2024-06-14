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
		TextureHandle GetNativeTextureHandle() final 
		{ 
			return _is_ready_for_rendering? _main_srv_handle.ptr : 0;
		};
		void Bind(CommandBuffer* cmd, u8 slot, bool compute_pipiline = false) final;
		TextureHandle GetView(u16 mimmap, bool random_access = false, ECubemapFace::ECubemapFace face = ECubemapFace::kUnknown, u16 array_slice = 0) final;
		void CreateView() final;
		void ReleaseView() final;
		void Name(const String& new_name) final;
		D3D12_GPU_DESCRIPTOR_HANDLE GetMainGPUSRVHandle() const { return _main_srv_handle; };
	private:
		D3D12_GPU_DESCRIPTOR_HANDLE _main_srv_handle;
		D3D12_GPU_DESCRIPTOR_HANDLE _main_uav_handle;
		ComPtr<ID3D12Resource> _p_d3dres;
		GPUVisibleDescriptorAllocation _allocation;
		GPUVisibleDescriptorAllocation _mimmap_allocation;
		//srv and uav
		Vector<D3D12_GPU_DESCRIPTOR_HANDLE> _mipmap_handles;
	};

	class D3DCubeMap : public CubeMap
	{
	public:
		D3DCubeMap(u16 width, bool mipmap_chain = true, ETextureFormat::ETextureFormat format = ETextureFormat::kRGBA32, bool linear = false, bool random_access = false);
		~D3DCubeMap();
		void Apply() final;
		TextureHandle GetNativeTextureHandle() final { return _main_srv_handle.ptr; };
		void Bind(CommandBuffer* cmd, u8 slot, bool compute_pipiline = false) final;

		TextureHandle GetView(u16 mimmap, bool random_access = false, ECubemapFace::ECubemapFace face = ECubemapFace::kUnknown, u16 array_slice = 0) final;
		void CreateView() final;
	private:
		D3D12_GPU_DESCRIPTOR_HANDLE _main_srv_handle;
		Vector<ComPtr<ID3D12Resource>> _textures;
		Vector<ComPtr<ID3D12Resource>> _upload_textures;
		GPUVisibleDescriptorAllocation _allocation;
		GPUVisibleDescriptorAllocation _mimmap_allocation;
		Map<ECubemapFace::ECubemapFace, Vector<D3D12_GPU_DESCRIPTOR_HANDLE>> _mipmap_srv_handles;
	};

	class ComputeShader;
	class D3DRenderTexture : public RenderTexture
	{
		inline const static Vector4f kClearColor = Colors::kBlack;
	public:
		D3DRenderTexture(const RenderTextureDesc& desc);
		~D3DRenderTexture();
		void Bind(CommandBuffer* cmd, u8 slot, bool compute_pipiline = false) final;
		void CreateView() final;
		void Name(const String& value) final;
		TextureHandle GetNativeTextureHandle() final;
		TextureHandle GetView(u16 mimmap, bool random_access = false, ECubemapFace::ECubemapFace face = ECubemapFace::kUnknown, u16 array_slice = 0) final;
		TextureHandle ColorRenderTargetHandle(u16 index = 0, CommandBuffer* cmd = nullptr) final;
		TextureHandle DepthRenderTargetHandle(u16 index = 0, CommandBuffer* cmd = nullptr) final;
		TextureHandle ColorTexture(u16 index = 0,CommandBuffer* cmd = nullptr) final;
		TextureHandle DepthTexture(u16 index = 0,CommandBuffer* cmd = nullptr) final;
		void GenerateMipmap(CommandBuffer* cmd) final;
		D3D12_CPU_DESCRIPTOR_HANDLE* TargetCPUHandle(u16 index = 0);
		D3D12_CPU_DESCRIPTOR_HANDLE* TargetCPUHandle(CommandBuffer* cmd, u16 index = 0);
	private:
		void MakesureResourceState(ID3D12GraphicsCommandList* cmd,D3D12_RESOURCE_STATES target_state);
		//gen mipmap for 1~4
		inline static Ref<ComputeShader> _p_mipmapgen_cs0 = nullptr;
		//gen mipmap for 5~max
		inline static Ref<ComputeShader> _p_mipmapgen_cs1 = nullptr;
		D3D12_RESOURCE_STATES _cur_res_state;
		D3D12_SHADER_RESOURCE_VIEW_DESC _srv_desc{};
		D3D12_CPU_DESCRIPTOR_HANDLE _main_srv_cpu_handle;
		D3D12_GPU_DESCRIPTOR_HANDLE _main_srv_gpu_handle;
		Vector<D3D12_CPU_DESCRIPTOR_HANDLE> _rtv_or_dsv_cpu_handles;
		ComPtr<ID3D12Resource> _p_buffer;
		CPUVisibleDescriptorAllocation _cpu_allocation;
		GPUVisibleDescriptorAllocation _gpu_allocation;
		GPUVisibleDescriptorAllocation _mimmap_srv_allocation;
		CPUVisibleDescriptorAllocation _mimmap_rtv_allocation;
		GPUVisibleDescriptorAllocation _mimmap_uav_allocation;
		Map<ECubemapFace::ECubemapFace, Vector<D3D12_GPU_DESCRIPTOR_HANDLE>> _mipmap_srv_handles;
		Map<ECubemapFace::ECubemapFace, Vector<D3D12_GPU_DESCRIPTOR_HANDLE>> _mipmap_uav_handles;
		Map<ECubemapFace::ECubemapFace, Vector<D3D12_CPU_DESCRIPTOR_HANDLE>> _mipmap_rtv_handles;
	};
}


#endif // !D3D_TEXTURE2D_H__
