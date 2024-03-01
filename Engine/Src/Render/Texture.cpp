#include "pch.h"
#include "Render/Texture.h"
#include "Render/Renderer.h"
#include "RHI/DX12/D3DTexture.h"
#include "Framework/Parser/AssetParser.h"

namespace Ailu
{
	Ref<Texture2D> Texture2D::Create(const uint16_t& width, const uint16_t& height, EALGFormat res_format, bool read_only)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT(false, "None render api used!")
				return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
		{
			return MakeRef<D3DTexture2D>(width, height, res_format, read_only);
		}
		}
		AL_ASSERT(false, "Unsupport render api!");
		return nullptr;
	}

	Ref<Texture2D> Texture2D::Create(const TextureDesc& desc)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT(false, "None render api used!")
				return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
		{
			return MakeRef<D3DTexture2D>(desc);
		}
		}
		AL_ASSERT(false, "Unsupport render api!");
		return nullptr;
	}

	void Texture2D::FillData(uint8_t* data)
	{
		_mipmap_count = 1;
		_p_datas.push_back(data);
	}
	void Texture2D::FillData(Vector<u8*> datas)
	{
		_mipmap_count = static_cast<u8>(datas.size());
		for (auto p : datas)
			_p_datas.emplace_back(p);
	}
	void Texture2D::Bind(uint8_t slot)
	{

	}
	uint8_t* Texture2D::GetCPUNativePtr()
	{
		return _p_datas.front();
	}
	void* Texture2D::GetNativeCPUHandle()
	{
		return nullptr;
	}
	void* Texture2D::GetGPUNativePtr()
	{
		return nullptr;
	}
	const ETextureType Texture2D::GetTextureType() const
	{
		return ETextureType::kTexture2D;
	}
	void Texture2D::Name(const std::string& name)
	{
		_name = name;
	}
	const std::string& Texture2D::Name() const
	{
		return _name;
	}
	Ref<Texture> TexturePool::Get(const std::string& name)
	{
		auto it = s_res_pool.find(name);
		if (it != s_res_pool.end()) 
			return it->second;
		return nullptr;
	}
	Ref<Texture2D> TexturePool::GetTexture2D(const std::string& name)
	{
		auto it = s_res_pool.find(name);
		if (it != s_res_pool.end()) 
			return std::dynamic_pointer_cast<Texture2D>(it->second);
		return nullptr;
	}
	Ref<TextureCubeMap> TexturePool::GetCubemap(const std::string& name)
	{
		auto it = s_res_pool.find(name);
		if (it != s_res_pool.end())
			return std::dynamic_pointer_cast<TextureCubeMap>(it->second);
		else
			return nullptr;
	}
	//----------------------------------------------------------TextureCubeMap---------------------------------------------------------------------
	Ref<TextureCubeMap> TextureCubeMap::Create(const uint16_t& width, const uint16_t& height, EALGFormat format)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT(false, "None render api used!")
				return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
		{
			return MakeRef<D3DTextureCubeMap>(width, height, format);
		}
		}
		AL_ASSERT(false, "Unsupport render api!");
		return nullptr;
	}
	void TextureCubeMap::FillData(Vector<u8*>& data)
	{
		_p_datas = std::move(data);
	}

	void TextureCubeMap::Bind(u8 slot)
	{
	}

	uint8_t* TextureCubeMap::GetCPUNativePtr()
	{
		return _p_datas[0];
	}
	void* TextureCubeMap::GetNativeCPUHandle()
	{
		return nullptr;
	}
	void* TextureCubeMap::GetGPUNativePtr()
	{
		return nullptr;
	}
	const ETextureType TextureCubeMap::GetTextureType() const
	{
		return ETextureType::kTextureCubeMap;
	}
	//----------------------------------------------------------TextureCubeMap---------------------------------------------------------------------

	//----------------------------------------------------------RenderTexture---------------------------------------------------------------------
	Ref<RenderTexture> RenderTexture::Create(const uint16_t& width, const uint16_t& height, String name, EALGFormat format, bool is_cubemap)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT(false, "None render api used!")
				return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
		{
			return MakeRef<D3DRenderTexture>(width, height, name, 1, format, is_cubemap);
		}
		}
		AL_ASSERT(false, "Unsupport render api!");
		return nullptr;
	}

	void RenderTexture::Bind(uint8_t slot)
	{
		Transition(ETextureResState::kShaderResource);
	}

	void* RenderTexture::GetNativeCPUHandle()
	{
		return nullptr;
	}
	void* RenderTexture::GetNativeCPUHandle(u16 index)
	{
		return nullptr;
	}
	void* RenderTexture::GetGPUNativePtr()
	{
		return nullptr;
	}
	const ETextureType RenderTexture::GetTextureType() const
	{
		return ETextureType::kRenderTexture;
	}
	void RenderTexture::Transition(ETextureResState state)
	{
		_state = state;
	}
	//----------------------------------------------------------RenderTexture---------------------------------------------------------------------
}
