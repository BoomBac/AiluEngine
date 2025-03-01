#pragma once
#pragma warning(disable : 4251)
#pragma warning(disable : 4275)//dll interface warning
#ifndef __TEXTURE_H__
#define __TEXTURE_H__

#include "AlgFormat.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/Path.h"
#include "Framework/Common/ThreadPool.h"
#include "Framework/Math/ALMath.hpp"
#include "GlobalMarco.h"
#include "GpuResource.h"
#include <map>
#include <stdint.h>

#include "Framework/Common/Asset.h"
#include "Framework/Common/Reflect.h"

namespace Ailu
{
    namespace TextureUtils
    {
        static u8 *ExpandImageDataToFourChannel(u8 *p_data, size_t size, u8 channel, u8 alpha = 255u)
        {
            u8 *new_data = new u8[size / channel * 4];
            auto pixel_num = size / channel;
            for (size_t i = 0; i < pixel_num; i++)
            {
                memcpy(new_data + i * 4, p_data + i * channel, channel);
                new_data[i * 4 + channel] = alpha;
            }
            return new_data;
        }

        static u8 *DownSample(u8 *image, int width, int height, int channels)
        {
            constexpr static int sub_task_block_size = 128;
            constexpr static bool use_multithread = false;
            int row_size = channels * width;
            int new_width = width >> 1;
            int new_height = height >> 1;
            int new_img_size = new_width * new_height * channels;
            u8 *new_image = new u8[new_img_size];
            auto downsample_sub_task = [&](int xbegin, int xend, int ybegin, int yend, int new_width) -> bool
            {
                u8 blend_color[4]{};
                for (int i = ybegin; i < yend; ++i)
                {
                    for (int j = xbegin; j < xend; ++j)
                    {
                        // 计算混合颜色的坐标
                        int a_i = i * 2;
                        int a_j = j * 2;
                        // 获取当前像素及其右侧像素
                        u8 *a = &image[a_i * row_size + a_j * channels];
                        u8 *b = &image[a_i * row_size + (a_j + 1) * channels];
                        // 计算齐次加权平均值
                        for (int k = 0; k < channels; ++k)
                            blend_color[k] = static_cast<u8>((a[k] + b[k]) / 2);
                        int new_index = i * new_width * channels + j * channels;
                        memcpy(new_image + new_index, blend_color, sizeof(blend_color));
                    }
                }
                return true;
            };
            if (use_multithread)
            {
                int task_num = new_width / sub_task_block_size;
                task_num = task_num == 0 ? 1 : task_num;
                if (task_num == 1)
                    downsample_sub_task(0, new_width, 0, new_height, new_width);
                else
                {
                    Vector<std::future<bool>> task_status{};
                    for (int i = 0; i < task_num; i++)
                    {
                        for (int j = 0; j < task_num; j++)
                        {
                            task_status.emplace_back(g_pThreadTool->Enqueue(downsample_sub_task, j * sub_task_block_size, (j + 1) * sub_task_block_size,
                                                                            i * sub_task_block_size, (i + 1) * sub_task_block_size, new_width));
                            //downsample_sub_task(j * sub_task_block_size, (j + 1) * sub_task_block_size, i * sub_task_block_size, (i + 1) * sub_task_block_size, new_width);
                        }
                    }
                    for (auto &complete: task_status)
                    {
                        complete.get();
                    }
                }
            }
            else
                downsample_sub_task(0, new_width, 0, new_height, new_width);
            return new_image;
        }
    }// namespace TextureUtils

    struct TextureDesc
    {
        u16 _width, _height;
        u8 _mipmap;
        EALGFormat::EALGFormat _res_format;
        EALGFormat::EALGFormat _srv_format;
        EALGFormat::EALGFormat _uav_format;
        bool _read_only;
        TextureDesc() : _width(0), _height(0), _mipmap(1), _res_format(EALGFormat::EALGFormat::kALGFormatUnknown), _srv_format(EALGFormat::EALGFormat::kALGFormatUnknown), _uav_format(EALGFormat::EALGFormat::kALGFormatUnknown), _read_only(false) {}
        TextureDesc(u16 w, u16 h, u8 mipmap, EALGFormat::EALGFormat res_format, EALGFormat::EALGFormat srv_format, EALGFormat::EALGFormat uav_format, bool read_only) : _width(w), _height(h), _mipmap(mipmap),
                                                                                                                                                                        _res_format(res_format), _srv_format(srv_format), _uav_format(uav_format), _read_only(read_only)
        {
        }
    };


    DECLARE_ENUM(ETextureDimension, kUnknown, kTex2D, kTex3D, kCube, kTex2DArray, kCubeArray)
    DECLARE_ENUM(EFilterMode, kPoint, kBilinear, kTrilinear)
    DECLARE_ENUM(EWrapMode, kClamp, kRepeat, kMirror)
    DECLARE_ENUM(ETextureFormat, kRGBA32, kRGBAFloat, kRGBFloat, kRGBAHalf, kRGFloat, kR11G11B10)
    DECLARE_ENUM(ECubemapFace, kUnknown, kPositiveX, kNegativeX, kPositiveY, kNegativeY, kPositiveZ, kNegativeZ)
    static EALGFormat::EALGFormat ConvertTextureFormatToPixelFormat(ETextureFormat::ETextureFormat format,bool is_sRGB = true)
    {
        switch (format)
        {
            case ETextureFormat::kRGBA32:
                return is_sRGB? EALGFormat::kALGFormatR8G8B8A8_UNORM_SRGB : EALGFormat::EALGFormat::kALGFormatR8G8B8A8_UNORM;
            case ETextureFormat::kRGBAFloat:
                return EALGFormat::EALGFormat::kALGFormatR32G32B32A32_FLOAT;
            case ETextureFormat::kRGBFloat:
                return EALGFormat::EALGFormat::kALGFormatR32G32B32_FLOAT;
            case ETextureFormat::kRGBAHalf:
                return EALGFormat::EALGFormat::kALGFormatR16G16B16A16_FLOAT;
            case ETextureFormat::kRGFloat:
                return EALGFormat::EALGFormat::kALGFormatR32G32_FLOAT;
            case ETextureFormat::kR11G11B10:
                return EALGFormat::EALGFormat::kALGFormatR11G11B10_FLOAT;
            default:
                break;
        }
        return EALGFormat::EALGFormat::kALGFormatUnknown;
    }

    //------------
    class CommandBuffer;
    using TextureHandle = size_t;
    class AILU_API Texture : public GpuResource
    {
        DECLARE_PROTECTED_PROPERTY(mipmap_count, MipmapLevel, u16)
        DECLARE_PROTECTED_PROPERTY(is_readble, Readble, bool)
        DECLARE_PROTECTED_PROPERTY(is_srgb, sRGB, bool)
        DECLARE_PROTECTED_PROPERTY(dimension, Dimension, ETextureDimension::ETextureDimension)
        DECLARE_PROTECTED_PROPERTY(filter_mode, FilterMode, EFilterMode::EFilterMode)
        DECLARE_PROTECTED_PROPERTY(wrap_mode, WrapMode, EWrapMode::EWrapMode)
        DECLARE_PROTECTED_PROPERTY_RO(pixel_format, PixelFormat, EALGFormat::EALGFormat)
    public:
        enum ETextureViewType
        {
            kSRV,
            kUAV,
            kRTV,
            kDSV
        };

    public:
        inline static Texture *s_p_default_white = nullptr;
        inline static Texture *s_p_default_black = nullptr;
        inline static Texture *s_p_default_gray = nullptr;
        inline static Texture *s_p_default_normal = nullptr;
        inline static const u16 kMainRTVIndex = ((u16) ETextureViewType::kRTV) * 10000u;
        inline static const u16 kMainDSVIndex = ((u16) ETextureViewType::kDSV) * 10000u;
        /*
		常规绘制只需要最原始的view，这里固定使用9999，对于texture2d而言，其0和9999只是mipmaps的数量不一样，
		暂时不知其具体影响，所以不会为texture2d创建0号view
		*/
        inline static const u16 kMainSRVIndex = ((u16) ETextureViewType::kSRV + 1) * 10000u - 1u;

    public:
        static u16 MaxMipmapCount(u16 w, u16 h);
        static u16 MaxMipmapCount(u16 w, u16 h, u16 d);
        static std::tuple<u16, u16> CalculateMipSize(u16 w, u16 h, u16 mip);
        static std::tuple<u16, u16, u16> CalculateMipSize(u16 w, u16 h, u16 d, u16 mip);
        static bool IsValidSize(u16 w, u16 h, u16 mip, u16 in_w, u16 in_h);
        static bool IsValidSize(u16 w, u16 h, u16 d, u16 mip, u16 in_w, u16 in_h, u16 in_d);
        static u64 TotalGPUMemerySize() { return s_gpu_mem_usage; }
        Texture();
        virtual void Release();
        virtual ~Texture();
        virtual TextureHandle GetNativeTextureHandle() const { return 0; };
        //for texture2d/3d(s)
        virtual void CreateView(ETextureViewType view_type, u16 mipmap, u16 array_slice = 0) {};
        virtual TextureHandle GetView(ETextureViewType view_type, u16 mipmap, u16 array_slice = 0) const { return 0; };
        virtual void ReleaseView(ETextureViewType view_type, u16 mipmap, u16 array_slice = 0) {};
        //for cube_map(s)
        virtual void CreateView(ETextureViewType view_type, ECubemapFace::ECubemapFace face, u16 mipmap, u16 array_slice = 0) {};
        virtual TextureHandle GetView(ETextureViewType view_type, ECubemapFace::ECubemapFace face, u16 mipmap, u16 array_slice = 0) const { return 0; };
        virtual void ReleaseView(ETextureViewType view_type, ECubemapFace::ECubemapFace face, u16 mipmap, u16 array_slice = 0) {};
        //common
        virtual void CreateView() {};
        virtual void GenerateMipmap() {};
        u64 TotalByteSize() const { return _total_byte_size; }
        //slot传255的话，表示只设置一下描述符堆
        virtual void Bind(CommandBuffer *cmd, u16 view_index, u8 slot, u32 sub_res, bool is_target_compute_pipeline);
        [[nodiscard]] u16 CalculateViewIndex(ETextureViewType view_type, u16 mipmap, u16 array_slice) const;
        [[nodiscard]] u16 CalculateViewIndex(ETextureViewType view_type, ECubemapFace::ECubemapFace face, u16 mipmap, u16 array_slice) const;
        //for 2d/2d array/3d
        [[nodiscard]] u16 CalculateSubResIndex(u16 mipmap, u16 depth_slice) const;
        //for cubemap / cubemap array
        [[nodiscard]] u16 CalculateSubResIndex(ECubemapFace::ECubemapFace face, u16 mipmap, u16 depth_slice) const;
        bool IsRenderTex() const { return _is_render_tex; }
        virtual bool IsValidMipmap(u16 mipmap) const { return mipmap < _pixel_data.size(); };

    protected:

    protected:
        inline static u64 s_gpu_mem_usage = 0u;
        u16 _pixel_size;
        u64 _total_byte_size = 0u;
        ETextureFormat::ETextureFormat _format;
        bool _is_random_access;
        bool _is_have_total_view = false;
        bool _is_ready_for_rendering = false;
        Vector<bool> _is_data_filled;
        bool _is_render_tex = false;
        Vector<u8 *> _pixel_data;
    };
    struct Texture2DInitializer
    {
        u16 _width;
        u16 _height;
        ETextureFormat::ETextureFormat _format = ETextureFormat::kRGBA32;
        bool _is_linear = false;
        bool _is_mipmap_chain = false;
        bool _is_random_access = false;
        bool _is_readble = true;
        Texture2DInitializer() {}
        Texture2DInitializer(u16 w,u16 h,ETextureFormat::ETextureFormat format,bool is_linear = true,bool is_mipmap_chain = false,bool is_random_access = false,bool is_readble = true):
            _width(w),_height(h),_format(format),_is_mipmap_chain(is_mipmap_chain),_is_linear(is_linear),_is_random_access(is_random_access),_is_readble(is_readble) {}
    };
    class AILU_API Texture2D : public Texture
    {
        DECLARE_PROTECTED_PROPERTY_RO(width, Width, u16)
        DECLARE_PROTECTED_PROPERTY_RO(height, Height, u16)
    public:
        static Ref<Texture2D> Create(const Texture2DInitializer& initializer);
        Texture2D(const Texture2DInitializer& initializer);
        virtual ~Texture2D();
        virtual void ReCreate(const Texture2DInitializer& initializer);
        virtual void Apply() {};
        virtual void Release();
        TextureHandle GetNativeTextureHandle() const final { return GetView(ETextureViewType::kSRV, 0); };
        void CreateView() override;
        void CreateView(ETextureViewType view_type, u16 mipmap, u16 array_slice = 0) override {};
        virtual void GenerateMipmap() override;
        Color GetPixel32(u16 x, u16 y);
        Color GetPixel(u16 x, u16 y);
        Color GetPixelBilinear(float u, float v);
        Ptr GetPixelData(u16 mipmap);
        void SetPixel(u16 x, u16 y, Color color, u16 mipmap);
        void SetPixel32(u16 x, u16 y, Color32 color, u16 mipmap);
        void SetPixelData(u8 *data, u16 mipmap, u64 offset = 0u);
        void EncodeToPng(const Path &path);
        Vector4f TexelSize() const { return _texel_size; }
    private:
        Vector4f _texel_size;
    private:
        void Construct(const Texture2DInitializer& initializer);
    };

    struct Texture3DInitializer
    {
        u16 _width;
        u16 _height;
        u16 _depth;
        bool _is_mipmap_chain;
        ETextureFormat::ETextureFormat _format = ETextureFormat::kRGBA32;
        bool _is_linear = false;
        bool _is_random_access = false;
        bool _is_readble = true;
    };
    class AILU_API Texture3D : public Texture
    {
        DECLARE_PROTECTED_PROPERTY_RO(width, Width, u16)
        DECLARE_PROTECTED_PROPERTY_RO(height, Height, u16)
        DECLARE_PROTECTED_PROPERTY_RO(depth, Depth, u16)
    public:
        static Ref<Texture3D> Create(const Texture3DInitializer &initializer);
        Texture3D(const Texture3DInitializer &initializer);
        virtual ~Texture3D();
        virtual void Apply() {};
        virtual void Bind(CommandBuffer *cmd, u16 view_index, u8 slot, u32 sub_res, bool is_target_compute_pipeline) override {};
        virtual void GenerateMipmap() override;
        /// @brief 为3D纹理的每一级mipmap创建SRV和UAV（如果支持随机写入），只能在Apply之后调用
        void CreateView() override;
        virtual void CreateView(ETextureViewType view_type, u16 mipmap, u16 dpeth_slice) override {};
        virtual TextureHandle GetView(ETextureViewType view_type, u16 mipmap, u16 dpeth_slice) const override { return 0; };
        virtual void ReleaseView(ETextureViewType view_type, u16 mipmap, u16 dpeth_slice) override {};
        TextureHandle GetNativeTextureHandle() const final { return GetView(ETextureViewType::kSRV, 0, -1); };
        Color GetPixel32(u16 x, u16 y, u16 z);
        Color GetPixel(u16 x, u16 y, u16 z);
        Color GetPixelTrilinear(f32 u, f32 v, f32 w);
        Ptr GetPixelData(u16 mipmap);
        void SetPixel(u16 x, u16 y, u16 z, Color color, u16 mipmap);
        void SetPixel32(u16 x, u16 y, u16 z, Color32 color, u16 mipmap);
        void SetPixelData(u8 *data, u16 mipmap, u64 offset = 0u);

    protected:
    };

    class Texture2DArray
    {
    };

    class CubeMap : public Texture
    {
        DECLARE_PROTECTED_PROPERTY_RO(width, Width, u16)
    public:
        static Ref<CubeMap> Create(u16 width, bool mipmap_chain = true, ETextureFormat::ETextureFormat format = ETextureFormat::kRGBA32, bool linear = false, bool random_access = false);
        CubeMap(u16 width, bool mipmap_chain = true, ETextureFormat::ETextureFormat format = ETextureFormat::kRGBA32, bool linear = false, bool random_access = false);
        virtual ~CubeMap();
        virtual void Apply() {};
        Color GetPixel32(ECubemapFace::ECubemapFace face, u16 x, u16 y);
        Color GetPixel(ECubemapFace::ECubemapFace face, u16 x, u16 y);
        Ptr GetPixelData(ECubemapFace::ECubemapFace face, u16 mipmap);
        void SetPixel(ECubemapFace::ECubemapFace face, u16 x, u16 y, Color color, u16 mipmap);
        void SetPixel32(ECubemapFace::ECubemapFace face, u16 x, u16 y, Color32 color, u16 mipmap);
        void SetPixelData(ECubemapFace::ECubemapFace face, u8 *data, u16 mipmap, u64 offset = 0u);

    protected:
        bool IsValidMipmap(u16 mipmap) const final { return mipmap < _pixel_data.size() / 2u; };

    protected:
        ETextureFormat::ETextureFormat _format;
    };

    enum class ETextureResState : u8
    {
        kDefault,
        kColorTagret,
        kShaderResource,
        kDepthTarget
    };

    DECLARE_ENUM(ERenderTargetFormat, kUnknown, kDefault, kDefaultHDR, kDepth, kShadowMap, kRGFloat, kRGHalf, kRFloat, kRGBAHalf, kRGBAFloat, kRUint, kRInt)

    static EALGFormat::EALGFormat ConvertRenderTextureFormatToPixelFormat(ERenderTargetFormat::ERenderTargetFormat format)
    {
        switch (format)
        {
            case ERenderTargetFormat::kUnknown:
                return EALGFormat::EALGFormat::kALGFormatUnknown;
            case ERenderTargetFormat::kDefault:
                return EALGFormat::EALGFormat::kALGFormatR8G8B8A8_UNORM;
            case ERenderTargetFormat::kDefaultHDR:
                return EALGFormat::EALGFormat::kALGFormatR11G11B10_FLOAT;
            case ERenderTargetFormat::kDepth:
                return EALGFormat::EALGFormat::kALGFormatD32_FLOAT_S8X24_UINT;
            case ERenderTargetFormat::kShadowMap:
                return EALGFormat::EALGFormat::kALGFormatD32_FLOAT;
            case ERenderTargetFormat::kRGFloat:
                return EALGFormat::EALGFormat::kALGFormatR32G32_FLOAT;
            case ERenderTargetFormat::kRGHalf:
                return EALGFormat::EALGFormat::kALGFormatR16G16_FLOAT;
            case ERenderTargetFormat::kRFloat:
                return EALGFormat::EALGFormat::kALGFormatR32_FLOAT;
            case ERenderTargetFormat::kRGBAHalf:
                return EALGFormat::EALGFormat::kALGFormatR16G16B16A16_FLOAT;
            case ERenderTargetFormat::kRGBAFloat:
                return EALGFormat::EALGFormat::kALGFormatR32G32B32A32_FLOAT;
            case ERenderTargetFormat::kRInt:
                return EALGFormat::EALGFormat::kALGFormatR32_SINT;
            case ERenderTargetFormat::kRUint:
                return EALGFormat::EALGFormat::kALGFormatR32_UINT;
            default:
                break;
        }
        return EALGFormat::EALGFormat::kALGFormatUnknown;
    }

    struct RenderTextureDesc
    {
        u16 _width;
        u16 _height;
        u16 _depth;
        u16 _depth_bit;
        ERenderTargetFormat::ERenderTargetFormat _color_format;
        ERenderTargetFormat::ERenderTargetFormat _depth_format;
        ETextureDimension::ETextureDimension _dimension;
        u16 _mipmap_count;
        u16 _slice_num;
        bool _random_access;
        bool _is_srgb;
        RenderTextureDesc() : _width(0), _height(0), _depth_bit(0), _color_format(ERenderTargetFormat::kUnknown), _depth_format(ERenderTargetFormat::kUnknown), _mipmap_count(0), _random_access(false), _dimension(ETextureDimension::kUnknown), _slice_num(0), _is_srgb(false)
        {
        }
        RenderTextureDesc(u16 w, u16 h, ERenderTargetFormat::ERenderTargetFormat format = ERenderTargetFormat::kDefaultHDR)
            : _width(w), _height(h), _depth_bit(0), _color_format(format), _depth_format(ERenderTargetFormat::kUnknown), _mipmap_count(1), _random_access(false), _dimension(ETextureDimension::kTex2D), _slice_num(0)
        {
            _depth = 1;
            if (format == ERenderTargetFormat::kDepth || format == ERenderTargetFormat::kShadowMap)
            {
                _color_format = ERenderTargetFormat::kUnknown;
                _depth_format = format;
                _depth_bit = 24;
            }
        }
    };

    class RenderTexture;
    struct RTHandle
    {
        static bool Valid(const RTHandle &handle) { return handle._id != (u32) -1; }
        u32 _id;
        RTHandle() : _id((u32) -1) {}
        RTHandle(u32 id) : _id(id) {}
    };
    using RTHash = Math::ALHash::Hash<64>;
    class AILU_API RenderTexture : public Texture
    {
        DECLARE_PROTECTED_PROPERTY_RO(width, Width, u16)
        DECLARE_PROTECTED_PROPERTY_RO(height, Height, u16)
        DECLARE_PROTECTED_PROPERTY_RO(depth, Depth, u16)
    public:
        void DepthBit(const u16 &value) { _depth_bit = value; }
        const u16 &DepthBit() const { return _depth_bit; }

    protected:
        u16 _depth_bit;

    public:
        static u64 TotalGPUMemerySize() { return s_render_texture_gpu_mem_usage; }
        static RTHandle GetTempRT(u16 width, u16 height, String name = std::format("TempBuffer_{}", s_temp_rt_count++), ERenderTargetFormat::ERenderTargetFormat format = ERenderTargetFormat::kDefault, bool mipmap_chain = false, bool linear = false, bool random_access = false);
        static RTHandle GetTempRT(RenderTextureDesc desc, String name = std::format("TempBuffer_{}", s_temp_rt_count++));
        static void ReleaseTempRT(RTHandle handle);
        static void ResetRenderTarget(RenderTexture *rt = nullptr) { s_current_rt = rt; };
        RenderTexture(const RenderTextureDesc &desc);
        virtual ~RenderTexture();
        //virtual TextureHandle GetView(ECubemapFace::ECubemapFace face, u16 mimmap) { return 0; };
        //当mipmap为0时，访问srv时，返回原图分辨率，也就是mip0，当访问uav时，实际访问的是mipmap1的uav（cubemap）
        //virtual TextureHandle GetView(u16 mimmap, bool random_access = false, ECubemapFace::ECubemapFace face = ECubemapFace::kUnknown, u16 array_slice = 0) override { return 0; };
        static Scope<RenderTexture> Create(u16 width, u16 height, String name = "", ERenderTargetFormat::ERenderTargetFormat format = ERenderTargetFormat::kDefault, bool mipmap_chain = false, bool linear = false, bool random_access = false);
        static Scope<RenderTexture> Create(u16 width, u16 height, u16 array_slice, String name = "", ERenderTargetFormat::ERenderTargetFormat format = ERenderTargetFormat::kDefault, bool mipmap_chain = false, bool linear = false, bool random_access = false);
        static Scope<RenderTexture> Create(u16 width, String name = "", ERenderTargetFormat::ERenderTargetFormat format = ERenderTargetFormat::kDefault, bool mipmap_chain = false, bool linear = false, bool random_access = false);
        static Scope<RenderTexture> Create(RenderTextureDesc desc, String name);
        //cubemap array not support mipmap
        static Scope<RenderTexture> Create(u16 width, String name = "", ERenderTargetFormat::ERenderTargetFormat format = ERenderTargetFormat::kDefault, u16 array_slice = 1, bool linear = false, bool random_access = false);
        void CreateView(ETextureViewType view_type, u16 mipmap, u16 array_slice = 0) override {};
        TextureHandle GetView(ETextureViewType view_type, u16 mipmap, u16 array_slice = 0) const override { return 0; };
        void ReleaseView(ETextureViewType view_type, u16 mipmap, u16 array_slice = 0) override {};
        void CreateView(ETextureViewType view_type, ECubemapFace::ECubemapFace face, u16 mipmap, u16 array_slice = 0) override {};
        TextureHandle GetView(ETextureViewType view_type, ECubemapFace::ECubemapFace face, u16 mipmap, u16 array_slice = 0) const override { return 0; };
        void ReleaseView(ETextureViewType view_type, ECubemapFace::ECubemapFace face, u16 mipmap, u16 array_slice = 0) override {};
        void CreateView() override;
        u16 ArraySlice() const { return _slice_num; }
        Vector4f TexelSize() const { return _texel_size; }
        //return rtv handle
        virtual TextureHandle ColorRenderTargetHandle(u16 view_index, CommandBuffer *cmd = nullptr) { return 0; };
        virtual TextureHandle DepthRenderTargetHandle(u16 view_index, CommandBuffer *cmd = nullptr) { return 0; };
        virtual TextureHandle ColorTexture(u16 view_index = kMainSRVIndex, CommandBuffer *cmd = nullptr) { return 0; };
        virtual TextureHandle DepthTexture(u16 view_index, CommandBuffer *cmd = nullptr) { return 0; };
        virtual void GenerateMipmap() override;
        //ret data need to be delete[] by client
        virtual void *ReadBack(u16 mipmap, u16 array_slice = 0, ECubemapFace::ECubemapFace face = ECubemapFace::kUnknown) { return nullptr; };
        virtual void ReadBackAsync(std::function<void(void *)> callback, u16 mipmap, u16 array_slice = 0, ECubemapFace::ECubemapFace face = ECubemapFace::kUnknown) {};

    private:
        inline static RenderTexture *s_current_rt = nullptr;

    protected:
        static bool CanAsShaderResource(RenderTexture *rt)
        {
            bool ret = s_current_rt ? rt != s_current_rt : true;
            if (!ret)
            {
                LOG_WARNING("{} used as srv and rtv same time!", rt->Name());
            }
            return ret;
        }

    protected:
        inline static u64 s_render_texture_gpu_mem_usage = 0u;
        inline static u64 s_temp_rt_count = 0u;
        u16 _slice_num;
        Vector4f _texel_size;
    };

    class AILU_API RenderTexturePool
    {
        struct RTInfo
        {
            bool _is_available;
            bool _is_temp;
            u32 _id;
            u64 _last_access_frame_count;
            Scope<RenderTexture> _rt;
            u64 _fence_value = 0;
        };
        DISALLOW_COPY_AND_ASSIGN(RenderTexturePool)
    public:
        RenderTexturePool() = default;
        ~RenderTexturePool();
        using RTPool = std::unordered_multimap<RTHash, RTInfo, RTHash::HashFunc>;
        u32 Add(RTHash hash, Scope<RenderTexture> rt);
        std::optional<u32> GetByIDHash(RTHash hash);
        void ReleaseRT(RTHandle handle);
        /// @brief 释放所有未归还的RT，在当帧结束前调用
        void RelesaeUnusedRT();
        /// @brief 销毁长时间未使用的render texture
        void TryCleanUp();
        //存储一个render texture的指针，只做访问使用，不维护其生命周期
        void Register(RenderTexture *rt);
        void UnRegister(u32 rt_id);
        RenderTexture *Get(RTHandle handle)
        {
            if (_lut_pool.contains(handle._id))
                return _lut_pool[handle._id]->second._rt.get();
            else
                return nullptr;
        }
        u32 Size() const { return static_cast<u32>(_pool.size()); }
        RTPool::iterator begin() { return _pool.begin(); }
        RTPool::iterator end() { return _pool.end(); }
        auto PersistentRTBegin() { return _persistent_rts.begin(); }
        auto PersistentRTEnd() { return _persistent_rts.end(); }

    private:
        // 0->64 0~12=wdth,13~24=height,25~30= format
        RTPool _pool;
        Map<u32, RTPool::iterator> _lut_pool;
        Map<u32, RenderTexture *> _persistent_rts;
    };
    extern AILU_API RenderTexturePool *g_pRenderTexturePool;
}// namespace Ailu

#endif// !TEXTURE_H__
