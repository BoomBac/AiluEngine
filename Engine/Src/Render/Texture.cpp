#include "pch.h"
#include "Render/Texture.h"
#include "Render/Renderer.h"
#include "RHI/DX12/D3DTexture.h"
#include "Framework/Parser/AssetParser.h"
#include "Framework/Common/Asset.h"

namespace Ailu
{
	TextureNew::TextureNew() : _width(0), _height(0), _mipmap_count(0), _pixel_format(EALGFormat::kALGFormatUnknown),_dimension(ETextureDimension::kUnknown),_filter_mode(EFilterMode::kBilinear)
		,_wrap_mode(EWrapMode::kClamp), _is_readble(false), _is_srgb(false)
	{

	}

	TextureNew::TextureNew(u16 width, u16 height) : _width(width), _height(height), _mipmap_count(1), _pixel_format(EALGFormat::kALGFormatUnknown), _dimension(ETextureDimension::kUnknown), 
		_filter_mode(EFilterMode::kBilinear), _wrap_mode(EWrapMode::kClamp),_is_readble(false), _is_srgb(false)
	{
	}

	TextureNew::~TextureNew()
	{ 
	}

	InPtr TextureNew::GetNativeTexturePtr()
	{
		return nullptr;
	}

	Texture2DNew::Texture2DNew(u16 width, u16 height, ETextureFormat::ETextureFormat format, u16 mipmap_count, bool linear) : TextureNew(width, height)
	{
		_pixel_format = ConvertTextureFormatToPixelFormat(format);
		_mipmap_count = mipmap_count;
		_is_readble = false;
		_is_srgb = linear;
	}

	Texture2DNew::Texture2DNew(u16 width, u16 height, bool mipmap_chain, ETextureFormat::ETextureFormat format, bool linear) : TextureNew(width, height)
	{
		_pixel_format = ConvertTextureFormatToPixelFormat(format);
		_mipmap_count = mipmap_chain? 12 : 1;
		_is_readble = false;
		_is_srgb = linear;
	}

	Texture2DNew::~Texture2DNew()
	{
	}

	void Texture2DNew::Apply()
	{
	}

	Color32 Texture2DNew::GetPixel32(u16 x, u16 y)
	{
		return Color32();
	}

	Color Texture2DNew::GetPixel(u16 x, u16 y)
	{
		return Color();
	}

	Color Texture2DNew::GetPixelBilinear(float u, float v)
	{
		return Color();
	}

	InPtr Texture2DNew::GetPixelData(u16 mipmap)
	{
		return InPtr();
	}

	void Texture2DNew::SetPixel(u16 x, u16 y, Color color, u16 mipmap)
	{
	}

	void Texture2DNew::SetPixel32(u16 x, u16 y, Color32 color, u16 mipmap)
	{
	}




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

	void Texture2D::FillData(u8* data)
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
	u8* Texture2D::GetPixelData()
	{
		return _p_datas.front();
	}
	void* Texture2D::GetNativeCPUHandle()
	{
		return nullptr;
	}

	const ETextureType Texture2D::GetTextureType() const
	{
		return ETextureType::kTexture2D;
	}
	const Guid& Texture2D::GetGuid() const
	{
		if (_p_asset_owned_this)
		{
			return _p_asset_owned_this->GetGuid();
		}
		return Guid::EmptyGuid();
	}

	void Texture2D::AttachToAsset(Asset* asset)
	{
		AL_ASSERT(asset->_p_inst_asset != nullptr, "Asset is nullptr!");
		_p_asset_owned_this = asset;
		asset->_p_inst_asset = this;
		asset->_name = ToWChar(_name);
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

	u8* TextureCubeMap::GetPixelData()
	{
		return _p_datas[0];
	}
	void* TextureCubeMap::GetNativeCPUHandle()
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
			auto rt = MakeRef<D3DRenderTexture>(width, height, name, 1, format, is_cubemap);
			s_all_render_texture.emplace_back(rt);
			return rt;
		}
		}
		AL_ASSERT(false, "Unsupport render api!");
		return nullptr;
	}

	void RenderTexture::Bind(CommandBuffer* cmd, u8 slot)
	{
		Transition(cmd,ETextureResState::kShaderResource);
	}

	void* RenderTexture::GetNativeCPUHandle()
	{
		return nullptr;
	}
	void* RenderTexture::GetNativeCPUHandle(u16 index)
	{
		return nullptr;
	}

	const ETextureType RenderTexture::GetTextureType() const
	{
		return ETextureType::kRenderTexture;
	}
	void RenderTexture::Transition(CommandBuffer* cmd, ETextureResState state)
	{
		_state = state;
	}
	//----------------------------------------------------------RenderTexture---------------------------------------------------------------------
}


