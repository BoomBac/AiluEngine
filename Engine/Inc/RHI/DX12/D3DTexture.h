#pragma once
#ifndef __D3D_TEXTURE2D_H__
#define __D3D_TEXTURE2D_H__

#include "d3dx12.h"
#include <map>

#include "Render/Texture.h"
using Microsoft::WRL::ComPtr;

namespace Ailu
{
	class D3DTexture2D : public Texture2D
	{
	public:
		D3DTexture2D(const uint16_t& width, const uint16_t& height, EALGFormat format);
		~D3DTexture2D();
		void FillData(uint8_t* data) final;
		void Bind(uint8_t slot) const final;
		void Release() final;
	private:
		inline static uint32_t s_texture_index = 0u;
		uint32_t _current_tex_id = 0u;
		D3D12_GPU_DESCRIPTOR_HANDLE _gpu_handle;
		std::vector<ComPtr<ID3D12Resource>> _textures;
		std::vector<ComPtr<ID3D12Resource>> _upload_textures;
		std::map<std::string, uint32_t> texture_index_;
	};
}


#endif // !D3D_TEXTURE2D_H__
