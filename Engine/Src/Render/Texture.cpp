#include "Render/Texture.h"
#include "Framework/Common/Application.h"
#include "Framework/Common/Asset.h"
#include "Framework/Parser/AssetParser.h"
#include "RHI/DX12/D3DTexture.h"
#include "Render/Renderer.h"
#include "pch.h"

namespace Ailu
{
    //-----------------------------------------------------------------------TextureNew----------------------------------------------------------------------------------
    u16 Texture::MaxMipmapCount(u16 w, u16 h)
    {
        u16 lw = 1, lh = 1;
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
        //return std::min<u16>(std::min<u16>(lw, lh) + 1, 8);
        return std::max<u16>(lw, lh);
    }
    u16 Texture::MaxMipmapCount(u16 w, u16 h, u16 d)
    {
        u16 lw = 1, lh = 1, ld = 1;
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
        while (d != 1)
        {
            d >>= 1;
            ++ld;
        }
        //return std::min<u16>(std::min<u16>(std::min<u16>(lw, lh) + 1, ld), 8);
        return std::max<u16>(ld, std::max<u16>(lw, lh));
    }
    std::tuple<u16, u16> Texture::CalculateMipSize(u16 w, u16 h, u16 mip)
    {
        while (mip--)
        {
            w >>= 1;
            h >>= 1;
        }
        return std::make_tuple(w, h);
    }
    std::tuple<u16, u16, u16> Texture::CalculateMipSize(u16 w, u16 h, u16 d, u16 mip)
    {
        while (mip--)
        {
            w >>= 1;
            h >>= 1;
            d >>= 1;
        }
        return std::make_tuple(w, h, d);
    }
    bool Texture::IsValidSize(u16 w, u16 h, u16 mip, u16 in_w, u16 in_h)
    {
        while (mip--)
        {
            w >>= 1;
            h >>= 1;
        }
        return in_w <= w && in_h <= h;
    }
    bool Texture::IsValidSize(u16 w, u16 h, u16 d, u16 mip, u16 in_w, u16 in_h, u16 in_d)
    {
        while (mip--)
        {
            w >>= 1;
            h >>= 1;
            d >>= 1;
        }
        return in_w <= w && in_h <= h && in_d <= d;
    }

    Texture::Texture() : _mipmap_count(1), _pixel_format(EALGFormat::EALGFormat::kALGFormatUnknown), _dimension(ETextureDimension::kUnknown),
                         _filter_mode(EFilterMode::kBilinear), _wrap_mode(EWrapMode::kClamp), _is_readble(false), _is_srgb(false), _pixel_size(0), _is_random_access(false), _p_asset(nullptr)
    {
    }

    Texture::~Texture()
    {
        s_gpu_mem_usage -= _state.GetSize();
    }
    void Texture::Bind(CommandBuffer *cmd, u16 view_index, u8 slot, u32 sub_res, bool is_target_compute_pipeline)
    {
        _state.Track();
    }
    u16 Texture::CalculateViewIndex(ETextureViewType view_type, u16 mipmap, u16 array_slice) const
    {
        u16 idx = _mipmap_count * array_slice + mipmap;
        return (u16) view_type * 10000u + idx;
    }
    u16 Texture::CalculateViewIndex(ETextureViewType view_type, ECubemapFace::ECubemapFace face, u16 mipmap, u16 array_slice) const
    {
        if (face == ECubemapFace::kUnknown)
            return CalculateViewIndex(view_type, mipmap, array_slice);
        u16 idx = 6 * _mipmap_count * array_slice + ((u16) face - 1) * _mipmap_count + mipmap;
        return (u16) view_type * 10000u + idx;
    }
    u16 Texture::CalculateSubResIndex(u16 mipmap, u16 depth_slice) const
    {
        u16 idx = _mipmap_count * depth_slice + mipmap;
        return idx;
    }
    u16 Texture::CalculateSubResIndex(ECubemapFace::ECubemapFace face, u16 mipmap, u16 depth_slice) const
    {
        if (face == ECubemapFace::kUnknown)
            return CalculateSubResIndex(mipmap, depth_slice);
        u16 idx = 6 * _mipmap_count * depth_slice + ((u16) face - 1) * _mipmap_count + mipmap;
        return idx;
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
                return MakeRef<D3DTexture2D>(width, height, mipmap_chain, format, linear, random_access);
            }
        }
        AL_ASSERT_MSG(false, "Unsupport render api!");
        return nullptr;
    }

    //-----------------------------------------------------------------------Texture2DNew----------------------------------------------------------------------------------
    Texture2D::Texture2D(u16 width, u16 height, bool mipmap_chain, ETextureFormat::ETextureFormat format, bool linear, bool random_access) : Texture()
    {
        _width = std::max<u16>(1u, width);
        _height = std::max<u16>(1u, height);
        _pixel_format = ConvertTextureFormatToPixelFormat(format);
        _dimension = ETextureDimension::kTex2D;
        _format = format;
        _mipmap_count = mipmap_chain ? MaxMipmapCount(width, height) : 1;
        _is_readble = false;
        _is_srgb = linear;
        _pixel_size = GetPixelByteSize(_pixel_format);
        _pixel_data.resize(_mipmap_count);
        _is_random_access = random_access;
        for (int i = 0; i < _pixel_data.size(); i++)
        {
            auto [w, h] = CalculateMipSize(_width, _height, i);
            u64 cur_mipmap_byte_size = w * h * _pixel_size;
            _pixel_data[i] = new u8[cur_mipmap_byte_size];
            memset(_pixel_data[i], -1, cur_mipmap_byte_size);
            _state.SetSize(_state.GetSize() + cur_mipmap_byte_size);
            _total_byte_size += cur_mipmap_byte_size;
        }
        s_gpu_mem_usage += _state.GetSize();
    }

    Texture2D::~Texture2D()
    {
        for (auto p: _pixel_data)
        {
            DESTORY_PTRARR(p);
        }
    }

    void Texture2D::CreateView()
    {
    }


    void Texture2D::GenerateMipmap()
    {
        AL_ASSERT(true);
    }
    Color Texture2D::GetPixel32(u16 x, u16 y)
    {
        return Color();
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
        if (!IsValidMipmap(mipmap) || !Texture::IsValidSize(_width, _height, mipmap, x, y))
            return;
        _is_data_filled = true;
        auto [w, h] = CalculateMipSize(_width, _height, mipmap);
        u16 row_pixel_size = w * _pixel_size;
        memcpy(_pixel_data[mipmap] + _pixel_size * x + row_pixel_size * y, color, sizeof(Color));
    }

    void Texture2D::SetPixel32(u16 x, u16 y, Color32 color, u16 mipmap)
    {
        Color c = {
                color.r / 255.f,
                color.g / 255.f,
                color.b / 255.f,
                color.a / 255.f,
        };
        SetPixel(x, y, c, mipmap);
    }

    void Texture2D::SetPixelData(u8 *data, u16 mipmap, u64 offset)
    {
        AL_ASSERT(mipmap < _mipmap_count);
        _is_data_filled = true;
        auto [w, h] = CalculateMipSize(_width, _height, mipmap);
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
        : Texture()
    {
        _width = std::max<u16>(width, 1u);
        _pixel_format = ConvertTextureFormatToPixelFormat(format);
        _format = format;
        _mipmap_count = mipmap_chain ? MaxMipmapCount(width, width) : 1;
        _is_readble = false;
        _is_srgb = linear;
        _pixel_size = GetPixelByteSize(_pixel_format);
        _pixel_data.resize(_mipmap_count * 6);
        _is_random_access = random_access;
        _dimension = ETextureDimension::kCube;
        for (u16 face = 0; face < 6; ++face)
        {
            for (u16 mipmap = 0; mipmap < _mipmap_count; ++mipmap)
            {
                auto [w, h] = Texture::CalculateMipSize(_width, _width, mipmap);
                u64 cur_mipmap_byte_size = w * h * _pixel_size;
                _pixel_data[face * _mipmap_count + mipmap] = new u8[cur_mipmap_byte_size];
                memset(_pixel_data[face * _mipmap_count + mipmap], 0, cur_mipmap_byte_size);
                _state.SetSize(_state.GetSize() + cur_mipmap_byte_size);
                _total_byte_size += cur_mipmap_byte_size;
            }
        }
        s_gpu_mem_usage += _state.GetSize();
    }

    CubeMap::~CubeMap()
    {
        for (auto p: _pixel_data)
        {
            DESTORY_PTRARR(p);
        }
    }

    Color CubeMap::GetPixel32(ECubemapFace::ECubemapFace face, u16 x, u16 y)
    {
        return Color();
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
        if (!IsValidMipmap(mipmap) || !Texture::IsValidSize(_width, _width, mipmap, x, y))
            return;
        u16 row_pixel_size = std::get<0>(Texture::CalculateMipSize(_width, _width, mipmap)) * _pixel_size;
        u16 face_index = face - 1;
        memcpy(_pixel_data[face_index * _mipmap_count + mipmap] + _pixel_size * x + row_pixel_size * y, color, sizeof(Color));
    }

    void CubeMap::SetPixel32(ECubemapFace::ECubemapFace face, u16 x, u16 y, Color32 color, u16 mipmap)
    {
        Color c = {
                color.r / 255.f,
                color.g / 255.f,
                color.b / 255.f,
                color.a / 255.f,
        };
        SetPixel(face, x, y, c, mipmap);
    }

    void CubeMap::SetPixelData(ECubemapFace::ECubemapFace face, u8 *data, u16 mipmap, u64 offset)
    {
        AL_ASSERT(mipmap < _mipmap_count);
        auto [w, h] = (Texture::CalculateMipSize(_width, _width, mipmap));
        u16 face_index = face - 1;
        memcpy(_pixel_data[face_index * _mipmap_count + mipmap], data + offset, w * h * _pixel_size);
    }
    //-----------------------------------------------------------------------CubeMap----------------------------------------------------------------------------------

    //----------------------------------------------------------Texture3D---------------------------------------------------------------------
    Ref<Texture3D> Texture3D::Create(const Texture3DInitializer &initializer)
    {
        switch (Renderer::GetAPI())
        {
            case RendererAPI::ERenderAPI::kNone:
                AL_ASSERT_MSG(false, "None render api used!");
                return nullptr;
            case RendererAPI::ERenderAPI::kDirectX12:
            {
                return MakeRef<D3DTexture3D>(initializer);
            }
        }
        AL_ASSERT_MSG(false, "Unsupport render api!");
        return nullptr;
    }
    Texture3D::Texture3D(const Texture3DInitializer &initializer) : Texture()
    {
        _width = std::max<u16>(1u, initializer._width);
        _height = std::max<u16>(1u, initializer._height);
        _depth = std::max<u16>(1u, initializer._depth);
        _pixel_format = ConvertTextureFormatToPixelFormat(initializer._format);
        _dimension = ETextureDimension::kTex3D;
        _format = initializer._format;
        _mipmap_count = initializer._is_mipmap_chain ? MaxMipmapCount(_width, _height, _depth) : 1;
        _is_readble = initializer._is_readble;
        _is_srgb = initializer._is_linear;
        _pixel_size = GetPixelByteSize(_pixel_format);
        _pixel_data.resize(_mipmap_count);
        _is_random_access = initializer._is_random_access;
        _is_render_tex = false;
        for (int i = 0; i < _pixel_data.size(); i++)
        {
            auto [w, h, d] = Texture::CalculateMipSize(_width, _width, _depth, i);
            u64 cur_mipmap_byte_size = w * h * d * _pixel_size;
            _pixel_data[i] = new u8[cur_mipmap_byte_size];
            memset(_pixel_data[i], -1, cur_mipmap_byte_size);
            _state.SetSize(_state.GetSize() + cur_mipmap_byte_size);
            _total_byte_size += cur_mipmap_byte_size;
        }
        s_gpu_mem_usage += _state.GetSize();
    }

    Texture3D::~Texture3D()
    {
        for (auto p: _pixel_data)
        {
            DESTORY_PTRARR(p);
        }
    }

    void Texture3D::GenerateMipmap()
    {
        AL_ASSERT(true);
    }

    void Texture3D::CreateView()
    {
        AL_ASSERT(true);
    }

    Color Texture3D::GetPixel32(u16 x, u16 y, u16 z)
    {
        return Color();
    }

    Color Texture3D::GetPixel(u16 x, u16 y, u16 z)
    {
        return Color();
    }

    Color Texture3D::GetPixelTrilinear(f32 u, f32 v, f32 w)
    {
        return Color();
    }

    Ptr Texture3D::GetPixelData(u16 mipmap)
    {
        if (IsValidMipmap(mipmap))
            return _pixel_data[mipmap];
        return nullptr;
    }

    void Texture3D::SetPixel(u16 x, u16 y, u16 z, Color color, u16 mipmap)
    {
        if (!IsValidMipmap(mipmap) || !Texture::IsValidSize(_width, _height, _depth, mipmap, x, y, z))
            return;
        _is_data_filled = true;
        auto [w, h, d] = CalculateMipSize(_width, _height, _depth, mipmap);
        u32 row_pixel_size = w * _pixel_size;
        u32 layer_pixel_size = row_pixel_size * h;
        memcpy(
                _pixel_data[mipmap] + _pixel_size * x + row_pixel_size * y + layer_pixel_size * z,
                color,
                sizeof(Color));
    }

    void Texture3D::SetPixel32(u16 x, u16 y, u16 z, Color32 color, u16 mipmap)
    {
        Color c = Color(color.r / 255.f, color.g / 255.f, color.b / 255.f, color.a / 255.f);
        SetPixel(x, y, z, c, mipmap);
    }

    void Texture3D::SetPixelData(u8 *data, u16 mipmap, u64 offset)
    {
        AL_ASSERT(mipmap < _mipmap_count);
        _is_data_filled = true;
        auto [w, h, d] = CalculateMipSize(_width, _height, _depth, mipmap);
        memcpy(_pixel_data[mipmap], data + offset, w * h * d * _pixel_size);
    }
    //----------------------------------------------------------Texture3D---------------------------------------------------------------------
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
                RenderTextureDesc desc(width, height, format);
                desc._dimension = ETextureDimension::kTex2D;
                desc._mipmap_count = mipmap_chain ? MaxMipmapCount(width, height) : 1;
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
                desc._mipmap_count = mipmap_chain ? MaxMipmapCount(width, height) : 1;
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
                desc._mipmap_count = mipmap_chain ? MaxMipmapCount(width, width) : 1;
                desc._random_access = random_access;
                auto rt = MakeScope<D3DRenderTexture>(desc);
                rt->Name(name);
                return rt;
            }
        }
        AL_ASSERT_MSG(false, "Unsupport render api!");
        return nullptr;
    }


    Scope<RenderTexture> RenderTexture::Create(RenderTextureDesc desc, String name)
    {
        switch (Renderer::GetAPI())
        {
            case RendererAPI::ERenderAPI::kNone:
                AL_ASSERT_MSG(false, "None render api used!");
                return nullptr;
            case RendererAPI::ERenderAPI::kDirectX12:
            {
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
        rt_hash.Set(0, 12, width);
        rt_hash.Set(13, 24, height);
        rt_hash.Set(25, 32, format);
        auto rt = g_pRenderTexturePool->GetByIDHash(rt_hash);
        if (rt.has_value())
            return RTHandle(rt.value());
        auto new_rt = Create(width, height, name, format, mipmap_chain, linear, random_access);
        return RTHandle(g_pRenderTexturePool->Add(rt_hash, std::move(new_rt)));
    }


    RTHandle RenderTexture::GetTempRT(RenderTextureDesc desc, String name)
    {
        RTHash rt_hash;
        rt_hash.Set(0, 12, desc._width);
        rt_hash.Set(13, 24, desc._height);
        rt_hash.Set(25, 32, desc._color_format);
        auto rt = g_pRenderTexturePool->GetByIDHash(rt_hash);
        if (rt.has_value())
            return RTHandle(rt.value());
        auto new_rt = Create(desc._width, desc._height, name, desc._color_format, desc._mipmap_count > 0, desc._is_srgb, desc._random_access);
        return RTHandle(g_pRenderTexturePool->Add(rt_hash, std::move(new_rt)));
    }
    void RenderTexture::ReleaseTempRT(RTHandle handle)
    {
        if (RTHandle::Valid(handle))
            g_pRenderTexturePool->ReleaseRT(handle);
    }

    RenderTexture::RenderTexture(const RenderTextureDesc &desc) : Texture()
    {
        _width = std::max<u16>(1u, desc._width);
        _height = std::max<u16>(1u, desc._height);
        _depth = std::max<u16>(1u, desc._depth);
        _depth_bit = desc._depth_bit;
        _pixel_format = ConvertRenderTextureFormatToPixelFormat(desc._depth_bit > 0 ? desc._depth_format : desc._color_format);
        _dimension = desc._dimension;
        _slice_num = desc._slice_num;
        _mipmap_count = desc._mipmap_count;
        _is_readble = false;
        _is_srgb = false;
        _pixel_size = GetPixelByteSize(_pixel_format);
        _is_random_access = desc._random_access;
        u16 slice_num = 1;
        if (_dimension == ETextureDimension::kTex2DArray)
            slice_num = desc._slice_num;
        else if (_dimension == ETextureDimension::kCubeArray)
            slice_num = 6 * desc._slice_num;
        else if (_dimension == ETextureDimension::kCube)
            slice_num = 6;
        else if (_dimension == ETextureDimension::kTex3D)
            slice_num = desc._depth;
        else {};
        for (int i = 0; i < _mipmap_count; i++)
        {
            auto [w, h, d] = CalculateMipSize(_width, _height, _depth, i);
            u64 cur_mipmap_byte_size = w * h * d * _pixel_size;
            _state.SetSize(_state.GetSize() + cur_mipmap_byte_size);
            _total_byte_size += cur_mipmap_byte_size;
        }
        _state.SetSize(_state.GetSize() * slice_num);
        _total_byte_size *= slice_num;
        s_render_texture_gpu_mem_usage += _state.GetSize();
        s_gpu_mem_usage += _state.GetSize();
        g_pRenderTexturePool->Register(this);
    }

    void RenderTexture::CreateView()
    {
        u16 view_slice_count = std::max<u16>(1, _slice_num);
        if (_dimension == ETextureDimension::kTex2D || _dimension == ETextureDimension::kTex2DArray)
        {
            for (u16 i = 0; i < view_slice_count; i++)
            {
                for (u16 j = 0; j < _mipmap_count; j++)
                {
                    if (_depth_bit > 0)
                        CreateView(ETextureViewType::kDSV, j, i);
                    else
                        CreateView(ETextureViewType::kRTV, j, i);
                    CreateView(ETextureViewType::kSRV, j, i);
                    if (_is_random_access)
                        CreateView(ETextureViewType::kUAV, j, i);
                }
            }
        }
        else if (_dimension == ETextureDimension::kCube || _dimension == ETextureDimension::kCubeArray)
        {
            for (u16 i = 0; i < view_slice_count; i++)
            {
                for (u16 j = 1; j <= 6; j++)
                {
                    for (u16 k = 0; k < _mipmap_count; k++)
                    {
                        if (_depth_bit > 0)
                            CreateView(ETextureViewType::kDSV, (ECubemapFace::ECubemapFace) j, k, i);
                        else
                            CreateView(ETextureViewType::kRTV, (ECubemapFace::ECubemapFace) j, k, i);
                        CreateView(ETextureViewType::kSRV, (ECubemapFace::ECubemapFace) j, k, i);
                        if (_is_random_access)
                            CreateView(ETextureViewType::kUAV, (ECubemapFace::ECubemapFace) j, k, i);
                    }
                }
            }
        }
        else
        {
            AL_ASSERT(true);
        }
    }

    void RenderTexture::GenerateMipmap()
    {
        AL_ASSERT(true);
    }

    RenderTexture::~RenderTexture()
    {
        s_render_texture_gpu_mem_usage -= _state.GetSize();
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
        //先设置为当前+1，防止该纹理未使用从而其fence value一致保持较大的默认值而无法被复用。
        //info._rt->SetFenceValue(g_pGfxContext->GetFenceValueGPU() + 1);
        auto it = _pool.emplace(std::make_pair(hash, std::move(info)));
        _lut_pool.emplace(std::make_pair(id, it));
        g_pLogMgr->LogWarningFormat("Expand rt pool to {} with texture {} at frame {}", _pool.size(), _lut_pool[id]->second._rt->Name(), Application::s_frame_count);
        return id;
    }
    std::optional<u32> RenderTexturePool::GetByIDHash(RTHash hash)
    {
        auto range = _pool.equal_range(hash);
        for (auto it = range.first; it != range.second; ++it)
        {
            auto &info = it->second;
            if (info._is_available)
            {
                if (!info._rt->GetState().IsReferenceByGpu())
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
    void Ailu::RenderTexturePool::RelesaeUnusedRT()
    {
        for (auto it = _pool.begin(); it != _pool.end(); it++)
        {
            it->second._is_available = true;
        }
    }
    void RenderTexturePool::TryCleanUp()
    {
        constexpr u64 MaxInactiveFrames = 500;// 假设超过100帧未使用的 RenderTexture 将被释放
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
            _lut_pool.emplace(std::make_pair(it->second._id, it));
        }
        g_pLogMgr->LogFormat("RT pool release {} rt", released_rt_num);
    }
    void RenderTexturePool::Register(RenderTexture *rt)
    {
        _persistent_rts[rt->ID()] = rt;
    }
    void RenderTexturePool::UnRegister(u32 rt_id)
    {
        _persistent_rts.erase(rt_id);
    }
    //----------------------------------------------------------RenderTexturePool---------------------------------------------------------------------
}// namespace Ailu
