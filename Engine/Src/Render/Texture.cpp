#include "pch.h"
#include "Render/Texture.h"
#include "Render/Renderer.h"
#include "RHI/DX12/D3DTexture.h"
#include "Framework/Parser/AssetParser.h"
#include "Framework/Common/Asset.h"
#include "Framework/Common/Application.h"

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

	Texture::Texture(u16 width, u16 height) : _width(width), _height(height), _mipmap_count(0), _pixel_format(EALGFormat::EALGFormat::kALGFormatUnknown), _dimension(ETextureDimension::kUnknown), 
		_filter_mode(EFilterMode::kBilinear), _wrap_mode(EWrapMode::kClamp),_is_readble(false), _is_srgb(false), _pixel_size(0), _is_random_access(false),_p_asset(nullptr)
	{
	}

	Texture::~Texture()
	{ 
	}

	Ptr Texture::GetNativeTexturePtr()
	{
		return nullptr;
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
			AL_ASSERT_MSG(false, "None render api used!");
				return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
		{
			return MakeRef<D3DTexture2D>(width,height,mipmap_chain,format,linear,random_access);
		}
		}
		AL_ASSERT_MSG(false, "Unsupport render api!");
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
			u64 cur_mipmap_byte_size = w * h * _pixel_size;
			_pixel_data[i] = new u8[cur_mipmap_byte_size];
			memset(_pixel_data[i], -1, cur_mipmap_byte_size);
			_gpu_memery_size += cur_mipmap_byte_size;
		}
		s_gpu_memory_size += _gpu_memery_size;
	}

	Texture2D::~Texture2D()
	{
		for (auto p : _pixel_data)
		{
			DESTORY_PTRARR(p);
		}
		s_gpu_memory_size -= _gpu_memery_size;
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
			AL_ASSERT_MSG(false, "None render api used!");
				return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
		{
			return MakeRef<D3DCubeMap>(width, mipmap_chain, format, linear, random_access);
		}
		}
		AL_ASSERT_MSG(false, "Unsupport render api!");
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
	Scope<RenderTexture> RenderTexture::Create(u16 width, u16 height, String name, ERenderTargetFormat::ERenderTargetFormat format, bool mipmap_chain, bool linear, bool random_access)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT_MSG(false, "None render api used!");
				return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
		{
			RenderTextureDesc desc(width,height,format);
			desc._dimension = ETextureDimension::kTex2D;
			desc._mipmap_count = mipmap_chain? MaxMipmapCount(width, height) : 0;
			desc._random_access = random_access;
			auto rt = MakeScope<D3DRenderTexture>(desc);
			rt->Name(name);
			return rt;
		}
		}
		AL_ASSERT_MSG(false, "Unsupport render api!");
		return nullptr;
	}

	Scope<RenderTexture> RenderTexture::Create(u16 width, u16 height, u16 array_slice, String name, ERenderTargetFormat::ERenderTargetFormat format, bool mipmap_chain, bool linear, bool random_access)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT_MSG(false, "None render api used!");
			return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
		{
			RenderTextureDesc desc(width, height, format);
			desc._slice_num = array_slice;
			desc._dimension = ETextureDimension::kTex2DArray;
			desc._mipmap_count = mipmap_chain ? MaxMipmapCount(width, height) : 0;
			desc._random_access = random_access;
			auto rt = MakeScope<D3DRenderTexture>(desc);
			rt->Name(name);
			return rt;
		}
		}
		AL_ASSERT_MSG(false, "Unsupport render api!");
		return nullptr;
	}

	Scope<RenderTexture> RenderTexture::Create(u16 width, String name, ERenderTargetFormat::ERenderTargetFormat format, bool mipmap_chain, bool linear, bool random_access)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT_MSG(false, "None render api used!");
				return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
		{
			RenderTextureDesc desc(width, width, format);
			desc._dimension = ETextureDimension::kCube;
			desc._mipmap_count = mipmap_chain ? MaxMipmapCount(width, width) : 0;
			desc._random_access = random_access;
			auto rt = MakeScope<D3DRenderTexture>(desc);
			rt->Name(name);
			return rt;
		}
		}
		AL_ASSERT_MSG(false, "Unsupport render api!");
		return nullptr;
	}

	Scope<RenderTexture> RenderTexture::Create(u16 width, String name, ERenderTargetFormat::ERenderTargetFormat format, u16 array_slice, bool linear, bool random_access)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT_MSG(false, "None render api used!");
			return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
		{
			RenderTextureDesc desc(width, width, format);
			desc._slice_num = array_slice;
			desc._dimension = ETextureDimension::kCubeArray;
			desc._mipmap_count = 0;
			desc._random_access = random_access;
			auto rt = MakeScope<D3DRenderTexture>(desc);
			rt->Name(name);
			return rt;
		}
		}
		AL_ASSERT_MSG(false, "Unsupport render api!");
		return nullptr;
	}

	RTHandle RenderTexture::GetTempRT(u16 width, u16 height, String name, ERenderTargetFormat::ERenderTargetFormat format, bool mipmap_chain, bool linear, bool random_access)
	{
		RTHash rt_hash;
		rt_hash.Set(0,12,width);
		rt_hash.Set(13,24,height);
		rt_hash.Set(25, 32, format);
		auto rt = g_pRenderTexturePool->GetByIDHash(rt_hash);
		if(rt.has_value())
			return RTHandle(rt.value());
		auto new_rt = Create(width, height, name.empty() ? std::format("TempRT_{}", g_pRenderTexturePool->Size()) : name, format, mipmap_chain, linear, random_access);
		return RTHandle(g_pRenderTexturePool->Add(rt_hash, std::move(new_rt)));
	}

	void RenderTexture::ReleaseTempRT(RTHandle handle)
	{
		g_pRenderTexturePool->ReleaseRT(handle);
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
		for (int i = 0; i < _mipmap_count + 1; i++)
		{
			auto [w, h] = CurMipmapSize(i);
			u64 cur_mipmap_byte_size = w * h * _pixel_size;
			_gpu_memery_size += cur_mipmap_byte_size;
		}
		s_gpu_memory_size += _gpu_memery_size;
		g_pRenderTexturePool->Register(this);
	}

	RenderTexture::~RenderTexture()
	{
		s_gpu_memory_size -= _gpu_memery_size;
		g_pRenderTexturePool->UnRegister(ID());
	}

	//----------------------------------------------------------RenderTexture-------------------------------------------------------------------------


	RenderTexturePool::~RenderTexturePool()
	{
		_pool.clear();
		_lut_pool.clear();
		_persistent_rts.clear();
	}

	//----------------------------------------------------------RenderTexturePool---------------------------------------------------------------------
	u32 RenderTexturePool::Add(RTHash hash, Scope<RenderTexture> rt)
	{
		auto id = rt->ID();
		RTInfo info;
		info._is_available = false;
		info._last_access_frame_count = Application::s_frame_count;
		info._id = id;
		info._rt = std::move(rt);
		auto it = _pool.emplace(std::make_pair(hash, std::move(info)));
		_lut_pool.emplace(std::make_pair(id, it));
		g_pLogMgr->LogWarningFormat("Expand rt pool to {}", _pool.size());
		return id;
	}
	std::optional<u32> RenderTexturePool::GetByIDHash(RTHash hash)
	{
		auto range = _pool.equal_range(hash);
		for (auto it = range.first; it != range.second; ++it)
		{
			auto& info = it->second;
			if (info._is_available)
			{			
				if (!g_pGfxContext->IsResourceReferencedByGPU(info._rt.get()))
				{
					it->second._is_available = false;
					it->second._last_access_frame_count = Application::s_frame_count;
					return it->second._id;
				}
			}
		}
		return {};
	}
	void RenderTexturePool::ReleaseRT(RTHandle handle)
	{
		auto it = _lut_pool.find(handle._id);
		if (it == _lut_pool.end())
			return;
		it->second->second._is_available = true;
	}
	void RenderTexturePool::TryRelease()
	{
		constexpr u64 MaxInactiveFrames = 500; // 假设超过100帧未使用的 RenderTexture 将被释放
		u32 released_rt_num = 0;
		for (auto it = _pool.begin(); it != _pool.end();)
		{
			if (!it->second._is_available)
			{
				++it;
				continue;
			}
			u64 inactiveFrames = Application::s_frame_count - it->second._last_access_frame_count;
			if (inactiveFrames > MaxInactiveFrames)
			{
				it = _pool.erase(it);
				++released_rt_num;
			}
			else
			{
				++it;
			}
		}
		_lut_pool.clear();
		for (auto it = _pool.begin(); it != _pool.end(); it++)
		{
			_lut_pool.emplace(std::make_pair(it->second._id,it));
		}
		g_pLogMgr->LogFormat("RT pool release {} rt",released_rt_num);
	}
	void RenderTexturePool::Register(RenderTexture* rt)
	{
		_persistent_rts[rt->ID()] = rt;
	}
	void RenderTexturePool::UnRegister(u32 rt_id)
	{
		_persistent_rts.erase(rt_id);
	}
	//----------------------------------------------------------RenderTexturePool---------------------------------------------------------------------
}


