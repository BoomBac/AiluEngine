#pragma once
#ifndef __D3D_TEXTURE2D_H__
#define __D3D_TEXTURE2D_H__

#include "d3dx12.h"
#include <map>

#include "Render/Texture.h"
#include "Framework/Math/ALMath.hpp"
using Microsoft::WRL::ComPtr;

namespace Ailu
{
	class D3DTexture2D : public Texture2D
	{
	public:
		D3DTexture2D(const uint16_t& width, const uint16_t& height, EALGFormat format);
		D3DTexture2D(const uint16_t& width, const uint16_t& height, u8 channel,EALGFormat format);
		D3DTexture2D(const uint16_t& width, const uint16_t& height, EALGFormat format,const String& asset_path);
		~D3DTexture2D();
		void FillData(uint8_t* data) final;
		void FillData(Vector<u8*> datas) final;
		void Bind(uint8_t slot) final;
		void Release() final;
		D3D12_SHADER_RESOURCE_VIEW_DESC& GetSRVDesc() { return _srv_desc; }
		void* GetGPUNativePtr() final;
		D3D12_CPU_DESCRIPTOR_HANDLE& GetCPUHandle() { return _cpu_handle; }
		D3D12_GPU_DESCRIPTOR_HANDLE& GetGPUHandle() { return _gpu_handle; }
	private:
		void Construct();
	private:
		D3D12_SHADER_RESOURCE_VIEW_DESC _srv_desc{};
		D3D12_GPU_DESCRIPTOR_HANDLE _gpu_handle;
		D3D12_CPU_DESCRIPTOR_HANDLE _cpu_handle;
		Vector<ComPtr<ID3D12Resource>> _textures;
		Vector<ComPtr<ID3D12Resource>> _upload_textures;
		Vector<u32> _submited_tasks;
	};

	class D3DTextureCubeMap : public TextureCubeMap
	{
	public:
		D3DTextureCubeMap(const uint16_t& width, const uint16_t& height, EALGFormat format);
		~D3DTextureCubeMap();
		void FillData(Vector<u8*>& data) final;
		void Bind(uint8_t slot) final;
		void Release() final;
		D3D12_SHADER_RESOURCE_VIEW_DESC& GetSRVDesc() { return _srv_desc; }
		D3D12_CPU_DESCRIPTOR_HANDLE& GetCPUHandle() { return _cpu_handle; }
		D3D12_GPU_DESCRIPTOR_HANDLE& GetGPUHandle() { return _gpu_handle; }
	private:
		D3D12_SHADER_RESOURCE_VIEW_DESC _srv_desc{};
		D3D12_GPU_DESCRIPTOR_HANDLE _gpu_handle;
		D3D12_CPU_DESCRIPTOR_HANDLE _cpu_handle;
		std::vector<ComPtr<ID3D12Resource>> _textures;
		std::vector<ComPtr<ID3D12Resource>> _upload_textures;
	};


	class D3DRenderTexture : public RenderTexture
	{
		inline const static Vector4f kClearColor = Colors::kBlack;
		struct InnerDescHandle
		{
			D3D12_GPU_DESCRIPTOR_HANDLE _gpu_handle;
			D3D12_CPU_DESCRIPTOR_HANDLE _cpu_handle;
		};
		union D3DRTHandle
		{
			InnerDescHandle _color_handle;
			InnerDescHandle _depth_handle;
		};
	public:
		D3DRenderTexture(const uint16_t& width, const uint16_t& height, String name, int mipmap = 1,EALGFormat format = EALGFormat::kALGFormatR8G8B8A8_UNORM);
		void Bind(uint8_t slot) final;
		uint8_t* GetCPUNativePtr() final;
		void* GetNativeCPUHandle() final;
		void Release() final;
		void Transition(ETextureResState state) final;
	private:
		D3D12_SHADER_RESOURCE_VIEW_DESC _srv_desc{};
		D3D12_GPU_DESCRIPTOR_HANDLE _srv_gpu_handle;
		D3D12_CPU_DESCRIPTOR_HANDLE _srv_cpu_handle;
		D3DRTHandle _d3d_handle;
		ComPtr<ID3D12Resource> _p_buffer;
	};
}


#endif // !D3D_TEXTURE2D_H__
