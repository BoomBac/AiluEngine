#include "pch.h"
#include "Render/Texture.h"
#include "Render/Renderer.h"
#include "RHI/DX12/D3DTexture.h"
#include "Framework/Parser/AssetParser.h"
#include "Framework/Common/Asset.h"

namespace Ailu
{
	//-----------------------------------------------------------------------TextureNew----------------------------------------------------------------------------------
	u16 Texture::MaxMipmapCount(u16 w, u16 h)
	{
		u16 lw = 0, lh = 0;
		while (w != 1)
		{
			w >>= 1;
			++lw;
		}
		while (h != 1)
		{
			h >>= 1;
			++lh;
		}
		return min(min(lw, lh),7);
	}

	Texture::Texture() : Texture(0u,0u)
	{

	}

	Texture::Texture(u16 width, u16 height) : _width(width), _height(height), _mipmap_count(0), _pixel_format(EALGFormat::kALGFormatUnknown), _dimension(ETextureDimension::kUnknown), 
		_filter_mode(EFilterMode::kBilinear), _wrap_mode(EWrapMode::kClamp),_is_readble(false), _is_srgb(false), _pixel_size(0), _is_random_access(false)
	{
	}

	Texture::~Texture()
	{ 
	}

	Ptr Texture::GetNativeTexturePtr()
	{
		return nullptr;
	}

	const Guid& Texture::GetGuid() const
	{
		if (_p_asset)
			return _p_asset->GetGuid();
		else
			return Guid::EmptyGuid();
	}

	void Texture::AttachToAsset(Asset* owner)
	{
		_p_asset = owner;
		_p_asset->_p_inst_asset = this;
	}

	bool Texture::IsValidSize(u16 width, u16 height, u16 mipmap) const
	{
		u16 w = _width, h = _height;
		while (mipmap--)
		{
			w >>= 1;
			h >>= 1;
		}
		return width <= w && height <= h;
	}

	std::tuple<u16, u16> Texture::CurMipmapSize(u16 mipmap) const
	{
		u16 w = _width, h = _height;
		while (mipmap--)
		{
			w >>= 1;
			h >>= 1;
		}
		return std::make_tuple(w, h);
	}
	//-----------------------------------------------------------------------TextureNew----------------------------------------------------------------------------------

	Ref<Texture2D> Texture2D::Create(u16 width, u16 height, bool mipmap_chain, ETextureFormat::ETextureFormat format, bool linear, bool random_access)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT(false, "None render api used!")
				return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
		{
			return MakeRef<Texture2D>(width,height,mipmap_chain,format,linear,random_access);
		}
		}
		AL_ASSERT(false, "Unsupport render api!");
		return nullptr;
	}

	//-----------------------------------------------------------------------Texture2DNew----------------------------------------------------------------------------------
	Texture2D::Texture2D(u16 width, u16 height, bool mipmap_chain, ETextureFormat::ETextureFormat format, bool linear, bool random_access) : Texture(width, height)
	{
		_pixel_format = ConvertTextureFormatToPixelFormat(format);
		_dimension = ETextureDimension::kTex2D;
		_format = format;
		_mipmap_count = mipmap_chain ? MaxMipmapCount(width, height) : 0;
		_is_readble = false;
		_is_srgb = linear;
		_pixel_size = GetPixelByteSize(_pixel_format);
		_pixel_data.resize(_mipmap_count + 1);
		_is_random_access = random_access;
		for (int i = 0; i < _pixel_data.size(); i++)
		{
			auto [w, h] = CurMipmapSize(i);
			_pixel_data[i] = new u8[w * h * _pixel_size];
		}
	}

	Texture2D::~Texture2D()
	{
		for (auto p : _pixel_data)
		{
			DESTORY_PTRARR(p);
		}
	}

	Color32 Texture2D::GetPixel32(u16 x, u16 y)
	{
		return Color32();
	}

	Color Texture2D::GetPixel(u16 x, u16 y)
	{
		return Color();
	}

	Color Texture2D::GetPixelBilinear(float u, float v)
	{
		return Color();
	}

	Ptr Texture2D::GetPixelData(u16 mipmap)
	{
		if (IsValidMipmap(mipmap))
			return _pixel_data[mipmap];
		return nullptr;
	}

	void Texture2D::SetPixel(u16 x, u16 y, Color color, u16 mipmap)
	{
		if (!IsValidMipmap(mipmap) ||!IsValidSize(x,y,mipmap))
			return;
		u16 row_pixel_size = std::get<0>(CurMipmapSize(mipmap)) * _pixel_size;
		memcpy(_pixel_data[mipmap] + _pixel_size * x + row_pixel_size * y,color,sizeof(Color));
	}

	void Texture2D::SetPixel32(u16 x, u16 y, Color32 color, u16 mipmap)
	{
		if (!IsValidMipmap(mipmap) || !IsValidSize(x, y, mipmap))
			return;
		u16 row_pixel_size = std::get<0>(CurMipmapSize(mipmap)) * _pixel_size;
		memcpy(_pixel_data[mipmap] + _pixel_size * x + row_pixel_size * y, color, sizeof(Color32));
	}

	void Texture2D::SetPixelData(u8* data, u16 mipmap, u64 offset)
	{
		auto [w, h] = CurMipmapSize(mipmap);
		memcpy(_pixel_data[mipmap], data + offset, w * h * _pixel_size);
	}
	//-----------------------------------------------------------------------Texture2DNew----------------------------------------------------------------------------------



	Ref<CubeMap> CubeMap::Create(u16 width, bool mipmap_chain, ETextureFormat::ETextureFormat format, bool linear, bool random_access)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT(false, "None render api used!")
				return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
		{
			return MakeRef<D3DCubeMap>(width, mipmap_chain, format, linear, random_access);
		}
		}
		AL_ASSERT(false, "Unsupport render api!");
		return nullptr;
	}

	//-----------------------------------------------------------------------CubeMap----------------------------------------------------------------------------------
	CubeMap::CubeMap(u16 width, bool mipmap_chain, ETextureFormat::ETextureFormat format, bool linear, bool random_access)
	 : Texture(width, width)
	{
		_pixel_format = ConvertTextureFormatToPixelFormat(format);
		_format = format;
		_mipmap_count = mipmap_chain ? MaxMipmapCount(width, width) : 0;
		_is_readble = false;
		_is_srgb = linear;
		_pixel_size = GetPixelByteSize(_pixel_format);
		_pixel_data.resize((_mipmap_count + 1) * 6);
		_is_random_access = random_access;
		_dimension = ETextureDimension::kCube;
		for (int i = 0; i < _mipmap_count + 1; i++)
		{
			auto [w, h] = CurMipmapSize(i);
			for(int j = 0; j < 6; j++)
				_pixel_data[i * 6 + j] = new u8[w * h * _pixel_size];
		}
	}

	CubeMap::~CubeMap()
	{
		for (auto p : _pixel_data)
		{
			DESTORY_PTRARR(p);
		}
	}

	Color32 CubeMap::GetPixel32(ECubemapFace::ECubemapFace face, u16 x, u16 y)
	{
		return Color32();
	}

	Color CubeMap::GetPixel(ECubemapFace::ECubemapFace face, u16 x, u16 y)
	{
		return Color();
	}

	Ptr CubeMap::GetPixelData(ECubemapFace::ECubemapFace face, u16 mipmap)
	{
		return Ptr();
	}

	void CubeMap::SetPixel(ECubemapFace::ECubemapFace face, u16 x, u16 y, Color color, u16 mipmap)
	{
		if (!IsValidMipmap(mipmap) || !IsValidSize(x, y, mipmap))
			return;
		u16 row_pixel_size = std::get<0>(CurMipmapSize(mipmap)) * _pixel_size;
		memcpy(_pixel_data[mipmap * 6 + face] + _pixel_size * x + row_pixel_size * y, color, sizeof(Color));
	}

	void CubeMap::SetPixel32(ECubemapFace::ECubemapFace face, u16 x, u16 y, Color32 color, u16 mipmap)
	{
		if (!IsValidMipmap(mipmap) || !IsValidSize(x, y, mipmap))
			return;
		u16 row_pixel_size = std::get<0>(CurMipmapSize(mipmap)) * _pixel_size;
		memcpy(_pixel_data[mipmap * 6 + face] + _pixel_size * x + row_pixel_size * y, color, sizeof(Color32));
	}

	void CubeMap::SetPixelData(ECubemapFace::ECubemapFace face, u8* data, u16 mipmap, u64 offset)
	{
		auto [w, h] = CurMipmapSize(mipmap);
		memcpy(_pixel_data[mipmap * 6 + face], data + offset, w * h * _pixel_size);
	}
	//-----------------------------------------------------------------------CubeMap----------------------------------------------------------------------------------

	//----------------------------------------------------------RenderTexture---------------------------------------------------------------------
	Ref<RenderTexture> RenderTexture::Create(u16 width, u16 height, String name, ERenderTargetFormat::ERenderTargetFormat format, bool mipmap_chain, bool linear, bool random_access)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT(false, "None render api used!")
				return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
		{
			RenderTextureDesc desc(width,height,format);
			auto rt = MakeRef<D3DRenderTexture>(desc);
			rt->Name(name);
			s_all_render_texture.emplace_back(rt);
			return rt;
		}
		}
		AL_ASSERT(false, "Unsupport render api!");
		return nullptr;
	}

	RenderTexture::RenderTexture(const RenderTextureDesc& desc) : Texture(desc._width, desc._height)
	{
		_depth = desc._depth;
		_pixel_format = ConvertRenderTextureFormatToPixelFormat(desc._depth > 0? desc._depth_format : desc._color_format);
		_dimension = desc._dimension;
		_slice_num = desc._slice_num;
		_mipmap_count = desc._mipmap_count;
		_is_readble = false;
		_is_srgb = false;
		_pixel_size = GetPixelByteSize(_pixel_format);
		_is_random_access = desc._random_access;
	}


	void RenderTexture::Bind(CommandBuffer* cmd, u8 slot)
	{

	}
	//----------------------------------------------------------RenderTexture---------------------------------------------------------------------
}


