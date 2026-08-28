#pragma once
#pragma warning(disable : 4251)
#pragma warning(disable : 4275)//dll interface warning
#ifndef __TEXTURE_H__
#define __TEXTURE_H__

#include "AlgFormat.h"
#include "Framework/Common/Hash.hpp"
#include "Framework/Common/Allocator.hpp"
#include "Framework/Common/Log.h"
#include "Framework/Common/Path.h"
#include "Framework/Common/ThreadPool.h"
#include "Framework/Math/ALMath.hpp"
#include "Framework/Core/CoreMinimal.h"
#include "Framework/Common/NonCopyable.h"
#include "Framework/Core/String.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/Containers/Map.h"
#include "GpuResource.h"
#include "CoreType.h"
#include <map>
#include <mutex>
#include <stdint.h>

#include "Assets/Asset.h"
#include "Framework/Common/Reflect.h"
#include "generated/Texture.gen.h"

namespace Ailu
{
    class Window;
    namespace Render
    {
        namespace TextureUtils
        {
            static u8 *ExpandImageDataToFourChannel(u8 *p_data, size_t size, u8 channel, u8 alpha = 255u)
            {
                u8 *new_data = AL_ALLOC_TAG(EMemoryTag::kTemporary, u8, size / channel * 4);
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
                u8 *new_image = AL_ALLOC_TAG(EMemoryTag::kTemporary, u8, new_img_size);
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
                                task_status.emplace_back(Core::ThreadPool::Get().Enqueue(downsample_sub_task, j * sub_task_block_size, (j + 1) * sub_task_block_size,
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

        AENUM()
        enum class ETextureDimension
        {
            kUnknown,
            kTex2D,
            kTex3D,
            kCube,
            kTex2DArray,
            kCubeArray
        };

        AENUM()
        enum class EFilterMode
        {
            kPoint,
            kBilinear,
            kTrilinear
        };

        AENUM()
        enum class EWrapMode
        {
            kClamp,
            kRepeat,
            kMirror
        };

        AENUM()
        enum class ETextureFormat
        {
            // 8-bit formats
            kR8UNorm,// R8_UNORM
            kR8UInt, // R8_UINT
            kR8SInt, // R8_SINT

            kRG8UNorm,// R8G8_UNORM
            kRG8UInt, // R8G8_UINT
            kRG8SInt, // R8G8_SINT

            kRGBA8UNorm,    // R8G8B8A8_UNORM
            kRGBA8UNormSRGB,// R8G8B8A8_UNORM_SRGB
            kRGBA8UInt,     // R8G8B8A8_UINT
            kRGBA8SInt,     // R8G8B8A8_SINT

            // 16-bit formats
            kR16Float,// R16_FLOAT
            kR16UNorm,// R16_UNORM
            kR16UInt, // R16_UINT
            kR16SInt, // R16_SINT

            kRG16Float,// R16G16_FLOAT
            kRG16UNorm,// R16G16_UNORM
            kRG16UInt, // R16G16_UINT
            kRG16SInt, // R16G16_SINT

            kRGBAHalf,   // R16G16B16A16_FLOAT
            kRGBA16UNorm,// R16G16B16A16_UNORM
            kRGBA16UInt, // R16G16B16A16_UINT

            // 32-bit float formats
            kR32Float,// R32_FLOAT
            kR32UInt, // R32_UINT
            kR32SInt, // R32_SINT

            kRGFloat,   // R32G32_FLOAT
            kRG32UInt,  // R32G32_UINT
            kRGBAFloat, // R32G32B32A32_FLOAT
            kRGBA32UInt,// R32G32B32A32_UINT
            kRGBFloat,  // R32G32B32_FLOAT

            // Packed formats
            kR11G11B10,   // R11G11B10_FLOAT
            kRGB10A2UNorm,// R10G10B10A2_UNORM
            kRGB10A2UInt, // R10G10B10A2_UINT

            // Depth-stencil formats
            kD16UNorm,      // D16_UNORM
            kD24UNormS8UInt,// D24_UNORM_S8_UINT
            kD32Float,      // D32_FLOAT
            kD32FloatS8X24, // D32_FLOAT_S8X24_UINT

            // Compressed formats
            kBC1_UNorm,     // DXT1
            kBC1_UNorm_SRGB,// DXT1 SRGB
            kBC3_UNorm,     // DXT5
            kBC3_UNorm_SRGB,// DXT5 SRGB
            kBC4_UNorm,     // Single-channel compressed
            kBC5_UNorm,     // Two-channel compressed
            kBC6H_UF16,     // HDR, unsigned float
            kBC6H_SF16,     // HDR, signed float
            kBC7_UNorm,     // High-quality RGBA compressed
            kBC7_UNorm_SRGB,// BC7 + SRGB

            // Legacy / platform formats
            kRGBA32,        // 32-bit UNORM (R8G8B8A8)
            kBGRA8UNorm,    // B8G8R8A8_UNORM
            kBGRA8UNormSRGB,// B8G8R8A8_UNORM_SRGB

            // Special/utility
            kUnknown// 未知格式
        };

        AENUM()
        enum class ECubemapFace
        {
            kUnknown,
            kPositiveX,
            kNegativeX,
            kPositiveY,
            kNegativeY,
            kPositiveZ,
            kNegativeZ
        };

        AENUM()
        enum class ERenderTargetFormat
        {
            kUnknown,
            kDefault,
            kDefaultHDR,
            kDepth,
            kShadowMap,
            kRGFloat,
            kRGHalf,
            kRFloat,
            kRGBAHalf,
            kRGBAFloat,
            kRUint,
            kRInt
        };

        static EALGFormat ConvertRenderTextureFormatToPixelFormat(ERenderTargetFormat format)
        {
            switch (format)
            {
                case ERenderTargetFormat::kUnknown:
                    return EALGFormat::kALGFormatUNKOWN;
                case ERenderTargetFormat::kDefault:
                    return EALGFormat::kALGFormatR8G8B8A8_UNORM;
                case ERenderTargetFormat::kDefaultHDR:
                    return EALGFormat::kALGFormatR11G11B10_FLOAT;
                case ERenderTargetFormat::kDepth:
                    return EALGFormat::kALGFormatD32_FLOAT_S8X24_UINT;
                case ERenderTargetFormat::kShadowMap:
                    return EALGFormat::kALGFormatD32_FLOAT;
                case ERenderTargetFormat::kRGFloat:
                    return EALGFormat::kALGFormatR32G32_FLOAT;
                case ERenderTargetFormat::kRGHalf:
                    return EALGFormat::kALGFormatR16G16_FLOAT;
                case ERenderTargetFormat::kRFloat:
                    return EALGFormat::kALGFormatR32_FLOAT;
                case ERenderTargetFormat::kRGBAHalf:
                    return EALGFormat::kALGFormatR16G16B16A16_FLOAT;
                case ERenderTargetFormat::kRGBAFloat:
                    return EALGFormat::kALGFormatR32G32B32A32_FLOAT;
                case ERenderTargetFormat::kRInt:
                    return EALGFormat::kALGFormatR32_SINT;
                case ERenderTargetFormat::kRUint:
                    return EALGFormat::kALGFormatR32_UINT;
                default:
                    break;
            }
            return EALGFormat::kALGFormatUNKOWN;
        }
        
        static ERenderTargetFormat ConvertPixelFormatFormatToRenderTexture(EALGFormat format)
        {
            switch (format)
            {
                case EALGFormat::kALGFormatR8G8B8A8_UNORM:
                    return ERenderTargetFormat::kDefault;
                case EALGFormat::kALGFormatR11G11B10_FLOAT:
                    return ERenderTargetFormat::kDefaultHDR;
                case EALGFormat::kALGFormatD32_FLOAT_S8X24_UINT:
                    return ERenderTargetFormat::kDepth;
                case EALGFormat::kALGFormatD32_FLOAT:
                    return ERenderTargetFormat::kShadowMap;
                case EALGFormat::kALGFormatR32G32_FLOAT:
                    return ERenderTargetFormat::kRGFloat;
                case EALGFormat::kALGFormatR16G16_FLOAT:
                    return ERenderTargetFormat::kRGHalf;
                case EALGFormat::kALGFormatR32_FLOAT:
                    return ERenderTargetFormat::kRFloat;
                case EALGFormat::kALGFormatR16G16B16A16_FLOAT:
                    return ERenderTargetFormat::kRGBAHalf;
                case EALGFormat::kALGFormatR32G32B32A32_FLOAT:
                    return ERenderTargetFormat::kRGBAFloat;
                case EALGFormat::kALGFormatR32_SINT:
                    return ERenderTargetFormat::kRInt;
                case EALGFormat::kALGFormatR32_UINT:
                    return ERenderTargetFormat::kRUint;
                default:
                    break;
            }
            return ERenderTargetFormat::kUnknown;
        }

        static EALGFormat ConvertTextureFormatToPixelFormat(ETextureFormat format)
        {
            if (format == ETextureFormat::kUnknown)
                return EALGFormat::kALGFormatUNKOWN;
            switch (format)
            {
                // 8-bit formats
                case ETextureFormat::kR8UNorm:
                    return EALGFormat::kALGFormatR8_UNORM;
                case ETextureFormat::kR8UInt:
                    return EALGFormat::kALGFormatR8_UINT;
                case ETextureFormat::kR8SInt:
                    return EALGFormat::kALGFormatR8_SINT;

                case ETextureFormat::kRG8UNorm:
                    return EALGFormat::kALGFormatR8G8_UNORM;
                case ETextureFormat::kRG8UInt:
                    return EALGFormat::kALGFormatR8G8_UINT;
                case ETextureFormat::kRG8SInt:
                    return EALGFormat::kALGFormatR8G8_SINT;

                case ETextureFormat::kRGBA8UNorm:
                    return EALGFormat::kALGFormatR8G8B8A8_UNORM;
                case ETextureFormat::kRGBA8UNormSRGB:
                    return EALGFormat::kALGFormatR8G8B8A8_UNORM_SRGB;
                case ETextureFormat::kRGBA8UInt:
                    return EALGFormat::kALGFormatR8G8B8A8_UINT;
                case ETextureFormat::kRGBA8SInt:
                    return EALGFormat::kALGFormatR8G8B8A8_SINT;

                // 16-bit formats
                case ETextureFormat::kR16Float:
                    return EALGFormat::kALGFormatR16_FLOAT;
                case ETextureFormat::kR16UNorm:
                    return EALGFormat::kALGFormatR16_UNORM;
                case ETextureFormat::kR16UInt:
                    return EALGFormat::kALGFormatR16_UINT;
                case ETextureFormat::kR16SInt:
                    return EALGFormat::kALGFormatR16_SINT;

                case ETextureFormat::kRG16Float:
                    return EALGFormat::kALGFormatR16G16_FLOAT;
                case ETextureFormat::kRG16UNorm:
                    return EALGFormat::kALGFormatR16G16_UNORM;
                case ETextureFormat::kRG16UInt:
                    return EALGFormat::kALGFormatR16G16_UINT;
                case ETextureFormat::kRG16SInt:
                    return EALGFormat::kALGFormatR16G16_SINT;

                case ETextureFormat::kRGBAHalf:
                    return EALGFormat::kALGFormatR16G16B16A16_FLOAT;
                case ETextureFormat::kRGBA16UNorm:
                    return EALGFormat::kALGFormatR16G16B16A16_UNORM;
                case ETextureFormat::kRGBA16UInt:
                    return EALGFormat::kALGFormatR16G16B16A16_UINT;

                // 32-bit float formats
                case ETextureFormat::kR32Float:
                    return EALGFormat::kALGFormatR32_FLOAT;
                case ETextureFormat::kR32UInt:
                    return EALGFormat::kALGFormatR32_UINT;
                case ETextureFormat::kR32SInt:
                    return EALGFormat::kALGFormatR32_SINT;

                case ETextureFormat::kRGFloat:
                    return EALGFormat::kALGFormatR32G32_FLOAT;
                case ETextureFormat::kRG32UInt:
                    return EALGFormat::kALGFormatR32G32_UINT;

                case ETextureFormat::kRGBAFloat:
                    return EALGFormat::kALGFormatR32G32B32A32_FLOAT;
                case ETextureFormat::kRGBA32UInt:
                    return EALGFormat::kALGFormatR32G32B32A32_UINT;

                case ETextureFormat::kRGBFloat:
                    return EALGFormat::kALGFormatR32G32B32_FLOAT;

                // Packed formats
                case ETextureFormat::kR11G11B10:
                    return EALGFormat::kALGFormatR11G11B10_FLOAT;
                case ETextureFormat::kRGB10A2UNorm:
                    return EALGFormat::kALGFormatR10G10B10A2_UNORM;
                case ETextureFormat::kRGB10A2UInt:
                    return EALGFormat::kALGFormatR10G10B10A2_UINT;

                // Depth-stencil formats
                case ETextureFormat::kD16UNorm:
                    return EALGFormat::kALGFormatD16_UNORM;
                case ETextureFormat::kD24UNormS8UInt:
                    return EALGFormat::kALGFormatD24S8_UINT;
                case ETextureFormat::kD32Float:
                    return EALGFormat::kALGFormatD32_FLOAT;
                case ETextureFormat::kD32FloatS8X24:
                    return EALGFormat::kALGFormatD32_FLOAT_S8X24_UINT;

                // Compressed formats
                case ETextureFormat::kBC1_UNorm:
                    return EALGFormat::kALGFormatBC1_UNORM;
                case ETextureFormat::kBC1_UNorm_SRGB:
                    return EALGFormat::kALGFormatBC1_UNORM_SRGB;
                case ETextureFormat::kBC3_UNorm:
                    return EALGFormat::kALGFormatBC3_UNORM;
                case ETextureFormat::kBC3_UNorm_SRGB:
                    return EALGFormat::kALGFormatBC3_UNORM_SRGB;
                case ETextureFormat::kBC4_UNorm:
                    return EALGFormat::kALGFormatBC4_UNORM;
                case ETextureFormat::kBC5_UNorm:
                    return EALGFormat::kALGFormatBC5_UNORM;
                case ETextureFormat::kBC6H_UF16:
                    return EALGFormat::kALGFormatBC6H_UF16;
                case ETextureFormat::kBC6H_SF16:
                    return EALGFormat::kALGFormatBC6H_SF16;
                case ETextureFormat::kBC7_UNorm:
                    return EALGFormat::kALGFormatBC7_UNORM;
                case ETextureFormat::kBC7_UNorm_SRGB:
                    return EALGFormat::kALGFormatBC7_UNORM_SRGB;

                // Legacy / platform
                case ETextureFormat::kRGBA32:
                    return EALGFormat::kALGFormatR8G8B8A8_UNORM;
                case ETextureFormat::kBGRA8UNorm:
                    return EALGFormat::kALGFormatB8G8R8A8_UNORM;
                case ETextureFormat::kBGRA8UNormSRGB:
                    return EALGFormat::kALGFormatB8G8R8A8_UNORM_SRGB;

                default:
                    return EALGFormat::kALGFormatUNKOWN;
            }
        }

        static bool IsDepthFormat(ERenderTargetFormat format)
        {
            return ERenderTargetFormat::kShadowMap == format || ERenderTargetFormat::kDepth == format;
        }
//sync with ShaderInterop.h
#if defined(_REVERSED_Z)
    #define kZFar  0.0f
    #define kZNear 1.0f
#else
    #define kZFar  1.0f
    #define kZNear 0.0f
#endif

        struct TextureDesc
        {
            union
            {
                struct
                {
                    u32 _width, _height;
                };
                struct
                {
                    f32 _scale_w,_scale_h;
                };
            };
            u16 _depth;
            EALGFormat _format;
            ETextureDimension _dimension;
            u16 _mip_num;
            u16 _array_size;
            union
            {
                struct
                {
                    u32 _is_color_target : 1;
                    u32 _is_depth_target : 1;
                    u32 _is_random_access : 1;
                    u32 _is_linear : 1;
                    u32 _is_readable : 1;
                    u32 _fixed_size : 1; //是否固定大小，否则的话就相对于当前视口
                    u32 _reserved : 26;
                };
                u32 _flags;
            };
            //for render buffer
            ELoadStoreAction _load;
            ELoadStoreAction _store;
            Vector4f _clear_color = Colors::kBlack;
            f32 _clear_depth = kZFar;
            TextureDesc() : _width(4u), _height(4u), _depth(1u), _format(EALGFormat::kALGFormatUNKOWN), _dimension(ETextureDimension::kTex2D),
                            _mip_num(1u), _array_size(0u), _flags(0u), _load(ELoadStoreAction::kClear), _store(ELoadStoreAction::kStore) {}
            TextureDesc(u16 w, u16 h, ERenderTargetFormat rt_format, ETextureDimension dimension = ETextureDimension::kTex2D) 
            : _width(w), _height(h), _depth(1u), _mip_num(1u), _array_size(0u), _flags(0u), _format(ConvertRenderTextureFormatToPixelFormat(rt_format)), _dimension(dimension), _load(ELoadStoreAction::kClear), _store(ELoadStoreAction::kStore)
             {
                 _is_depth_target = IsDepthFormat(rt_format);
                 _is_color_target = ~_is_depth_target;
                _fixed_size = true;
             }
        };

        //------------
        class CommandBuffer;
        using TextureHandle = size_t;

        ACLASS()
        class AILU_API Texture : public GpuResource
        {
            GENERATED_BODY()
            public:
                void MipmapLevel(const u16 &value) { _mipmap_count = value; }
                const u16 &MipmapLevel() const { return _mipmap_count; }

            protected:
                u16 _mipmap_count;
            public:
                void Readble(const bool &value) { _is_readble = value; }
                const bool &Readble() const { return _is_readble; }

            protected:
                bool _is_readble;
            public:
                void sRGB(const bool &value) { _is_srgb = value; }
                const bool &sRGB() const { return _is_srgb; }

            protected:
                bool _is_srgb;
            public:
                void Dimension(const ETextureDimension &value) { _dimension = value; }
                const ETextureDimension &Dimension() const { return _dimension; }

            protected:
                ETextureDimension _dimension;
            public:
                void FilterMode(const EFilterMode &value) { _filter_mode = value; }
                const EFilterMode &FilterMode() const { return _filter_mode; }

            protected:
                EFilterMode _filter_mode;
            public:
                void WrapMode(const EWrapMode &value) { _wrap_mode = value; }
                const EWrapMode &WrapMode() const { return _wrap_mode; }

            protected:
                EWrapMode _wrap_mode;
            public:
                const EALGFormat &PixelFormat() const { return _pixel_format; }

            protected:
                EALGFormat _pixel_format;
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

        public:
            Texture();
            virtual void Release();
            virtual ~Texture();
            virtual TextureHandle GetNativeTextureHandle() const { return 0; };
            //for texture2d/3d(s)
            virtual void CreateView(ETextureViewType view_type, u16 mipmap, u16 array_slice = 0) {};
            virtual TextureHandle GetView(ETextureViewType view_type, u16 mipmap, u16 array_slice = 0) const { return 0; };
            virtual void ReleaseView(ETextureViewType view_type, u16 mipmap, u16 array_slice = 0) {};
            //for cube_map(s)
            virtual void CreateView(ETextureViewType view_type, ECubemapFace face, u16 mipmap, u16 array_slice = 0) {};
            virtual TextureHandle GetView(ETextureViewType view_type, ECubemapFace face, u16 mipmap, u16 array_slice = 0) const { return 0; };
            virtual void ReleaseView(ETextureViewType view_type, ECubemapFace face, u16 mipmap, u16 array_slice = 0) {};
            //common
            virtual void CreateView() {};
            virtual void GenerateMipmap() {};
            virtual void GenerateMipmap(CommandBuffer *cmd) { GenerateMipmap(); };
            [[nodiscard]] u16 CalculateViewIndex(ETextureViewType view_type, u16 mipmap, u16 array_slice) const;
            [[nodiscard]] u16 CalculateViewIndex(ETextureViewType view_type, ECubemapFace face, u16 mipmap, u16 array_slice) const;
            //for 2d/2d array/3d
            [[nodiscard]] u16 CalculateSubResIndex(u16 mipmap, u16 depth_slice) const;
            //for cubemap / cubemap array
            [[nodiscard]] u16 CalculateSubResIndex(ECubemapFace face, u16 mipmap, u16 depth_slice) const;
            bool IsRenderTex() const { return _is_render_tex; }
            virtual bool IsValidMipmap(u16 mipmap) const { return mipmap < _pixel_data.size(); };
            u16 Width() const { return _width; }
            u16 Height() const { return _height; }
            i32 GetBindlessSRVIndex() const { return _bindless_srv_index; }
            i32 GetBindlessUAVIndex() const { return _bindless_uav_index; }
        protected:
        protected:
            inline static u64 s_gpu_mem_usage = 0u;
            u16 _pixel_size;
            bool _is_random_access;
            bool _is_have_total_view = false;
            Vector<bool> _is_data_filled;
            bool _is_render_tex = false;
            Vector<u8 *> _pixel_data;
            u16 _width, _height;
            i32 _bindless_srv_index = -1, _bindless_uav_index = -1;
        };

        ACLASS()
        class AILU_API Texture2D : public Texture
        {
            GENERATED_BODY()
        public:
            static Ref<Texture2D> Create(const TextureDesc &initializer);
            static Ref<Texture2D> Create(u16 w, u16 h, ETextureFormat format, bool is_mip = false, bool is_random_access = false);
            Texture2D() = default;
            Texture2D(const TextureDesc &initializer);
            virtual ~Texture2D();
            virtual void ReCreate(const TextureDesc &initializer);
            virtual void Release();
            TextureHandle GetNativeTextureHandle() const final { return GetView(ETextureViewType::kSRV, 0); };
            void CreateView() override;
            void CreateView(ETextureViewType view_type, u16 mipmap, u16 array_slice = 0) override {};
            virtual void GenerateMipmap() override;
            Color GetPixel32(u16 x, u16 y) const;
            Color GetPixel(u16 x, u16 y) const;
            Color GetPixelBilinear(float u, float v) const;
            bool TryGetPixel(u16 x, u16 y, Color &color) const;
            bool TryGetPixelBilinear(float u, float v, Color &color) const;
            Ptr GetPixelData(u16 mipmap);
            void SetPixel(u16 x, u16 y, Color color, u16 mipmap);
            void SetPixel32(u16 x, u16 y, Color32 color, u16 mipmap);
            void SetPixelData(u8 *data, u16 mipmap, u64 offset = 0u);
            void EncodeToPng(const Path &path);
            Vector4f TexelSize() const { return _texel_size; }

        private:
            Vector4f _texel_size;

        private:
            void Construct(const TextureDesc &initializer);
        };

        ACLASS()
        class AILU_API Texture3D : public Texture
        {
            GENERATED_BODY()
            public:
                const u16 &Depth() const { return _depth; }

            protected:
                u16 _depth;
        public:
            static Ref<Texture3D> Create(const TextureDesc &initializer);
            Texture3D() = default;
            Texture3D(const TextureDesc &initializer);
            virtual ~Texture3D();
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

        ACLASS()
        class CubeMap : public Texture
        {
            GENERATED_BODY()
        public:
            static Ref<CubeMap> Create(u16 width, bool mipmap_chain = true, ETextureFormat format = ETextureFormat::kRGBA32, bool linear = false, bool random_access = false);
            CubeMap() = default;
            CubeMap(u16 width, bool mipmap_chain = true, ETextureFormat format = ETextureFormat::kRGBA32, bool linear = false, bool random_access = false);
            virtual ~CubeMap();
            Color GetPixel32(ECubemapFace face, u16 x, u16 y);
            Color GetPixel(ECubemapFace face, u16 x, u16 y);
            Ptr GetPixelData(ECubemapFace face, u16 mipmap);
            void SetPixel(ECubemapFace face, u16 x, u16 y, Color color, u16 mipmap);
            void SetPixel32(ECubemapFace face, u16 x, u16 y, Color32 color, u16 mipmap);
            void SetPixelData(ECubemapFace face, u8 *data, u16 mipmap, u64 offset = 0u);

        protected:
            bool IsValidMipmap(u16 mipmap) const final { return mipmap < _pixel_data.size() / 2u; };

        protected:
            ETextureFormat _format;
        };

        enum class ETextureResState : u8
        {
            kDefault,
            kColorTagret,
            kShaderResource,
            kDepthTarget
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

        ACLASS()
        class AILU_API RenderTexture : public Texture
        {
            GENERATED_BODY()
            public:
                const u16 &Depth() const { return _depth; }

            protected:
                u16 _depth;
        public:
            void DepthBit(const u16 &value) { _depth_bit = value; }
            const u16 &DepthBit() const { return _depth_bit; }
        protected:
            u16 _depth_bit;

        public:
            inline static HashMap<u64, RenderTexture *> s_window_backbuffers;
            inline static std::mutex s_window_backbuffers_mtx;
            inline static RenderTexture *s_backbuffer;

            static RenderTexture *WindowBackBuffer(Window *w)
            {
                std::lock_guard<std::mutex> lock(s_window_backbuffers_mtx);
                u64 id = reinterpret_cast<u64>(w);
                if (s_window_backbuffers.contains(id))
                    return s_window_backbuffers[id];
                return nullptr;
            }
            static Vector<RenderTexture *> WindowBackBuffersSnapshot()
            {
                std::lock_guard<std::mutex> lock(s_window_backbuffers_mtx);
                Vector<RenderTexture *> backbuffers;
                backbuffers.reserve(s_window_backbuffers.size());
                for (auto &it: s_window_backbuffers)
                    backbuffers.push_back(it.second);
                return backbuffers;
            }
            static void RegisterWindowBackBuffer(Window *w, RenderTexture *rt)
            {
                std::lock_guard<std::mutex> lock(s_window_backbuffers_mtx);
                s_window_backbuffers[reinterpret_cast<u64>(w)] = rt;
            }
            static void UnregisterWindowBackBuffer(Window *w)
            {
                std::lock_guard<std::mutex> lock(s_window_backbuffers_mtx);
                s_window_backbuffers.erase(reinterpret_cast<u64>(w));
            }
            static u64 TotalGPUMemerySize() { return s_render_texture_gpu_mem_usage; }
            static RTHandle GetTempRT(u16 width, u16 height, String name = std::format("TempBuffer_{}", s_temp_rt_count++), ERenderTargetFormat format = ERenderTargetFormat::kDefault, bool mipmap_chain = false, bool linear = false, bool random_access = false);
            static RTHandle GetTempRT(const TextureDesc &desc, String name = std::format("TempBuffer_{}", s_temp_rt_count++));
            static RTHandle GetTempRT(u16 width, u16 height, String name, ERenderTargetFormat format, ELoadStoreAction load_action);
            static void ReleaseTempRT(RTHandle handle);
            //virtual TextureHandle GetView(ECubemapFace face, u16 mimmap) { return 0; };
            //当mipmap为0时，访问srv时，返回原图分辨率，也就是mip0，当访问uav时，实际访问的是mipmap1的uav（cubemap）
            //virtual TextureHandle GetView(u16 mimmap, bool random_access = false, ECubemapFace face = ECubemapFace::kUnknown, u16 array_slice = 0) override { return 0; };
            static Ref<RenderTexture> Create(u16 width, u16 height, String name = "", ERenderTargetFormat format = ERenderTargetFormat::kDefault, bool mipmap_chain = false, bool linear = false, bool random_access = false);
            static Ref<RenderTexture> Create(u16 width, u16 height, u16 array_slice, String name = "", ERenderTargetFormat format = ERenderTargetFormat::kDefault, bool mipmap_chain = false, bool linear = false, bool random_access = false);
            static Ref<RenderTexture> Create(u16 width, String name = "", ERenderTargetFormat format = ERenderTargetFormat::kDefault, bool mipmap_chain = false, bool linear = false, bool random_access = false);
            static Ref<RenderTexture> Create(const TextureDesc &desc, String name = "");
            //cubemap array not support mipmap
            static Ref<RenderTexture> Create(u16 width, String name = "", ERenderTargetFormat format = ERenderTargetFormat::kDefault, u16 array_slice = 1, bool linear = false, bool random_access = false);
        public:
            RenderTexture() = default;
            RenderTexture(const TextureDesc &desc);
            virtual ~RenderTexture();
            void CreateView(ETextureViewType view_type, u16 mipmap, u16 array_slice = 0) override {};
            TextureHandle GetView(ETextureViewType view_type, u16 mipmap, u16 array_slice = 0) const override { return 0; };
            void ReleaseView(ETextureViewType view_type, u16 mipmap, u16 array_slice = 0) override {};
            void CreateView(ETextureViewType view_type, ECubemapFace face, u16 mipmap, u16 array_slice = 0) override {};
            TextureHandle GetView(ETextureViewType view_type, ECubemapFace face, u16 mipmap, u16 array_slice = 0) const override { return 0; };
            void ReleaseView(ETextureViewType view_type, ECubemapFace face, u16 mipmap, u16 array_slice = 0) override {};
            void CreateView() override;
            u16 ArraySlice() const { return _slice_num; }
            Vector4f TexelSize() const { return _texel_size; }
            ELoadStoreAction _load_action = ELoadStoreAction::kClear;
            ELoadStoreAction _store_action = ELoadStoreAction::kStore;
            /// @brief 返回颜色srv，这里是给imgui使用的，需要找个地方资源状态
            /// @param view_index
            /// @return
            virtual TextureHandle ColorTexture(u16 view_index) { return 0; };
            /// @brief 返回深度srv，这里是给imgui使用的，需要找个地方资源状态
            /// @param view_index
            /// @return
            virtual TextureHandle DepthTexture(u16 view_index) { return 0; };
            virtual void GenerateMipmap() override;
            virtual void GenerateMipmap(CommandBuffer *cmd) { GenerateMipmap(); };
            virtual void GenerateMipmap(CommandBuffer *cmd, u16 source_mip, u16 output_mip_count)
            {
                GenerateMipmap(cmd);
            };
            // Returned data is allocated with AL_ALLOC and must be released with AL_FREE.
            virtual void *ReadBack(u16 mipmap, u16 array_slice = 0, ECubemapFace face = ECubemapFace::kUnknown) { return nullptr; };
            virtual void ReadBackAsync(std::function<void(void *)> callback, u16 mipmap, u16 array_slice = 0, ECubemapFace face = ECubemapFace::kUnknown) {};
            bool IsSwapChain() const { return _is_swapchain; }
        private:

        protected:
            inline static u64 s_render_texture_gpu_mem_usage = 0u;
            inline static u64 s_temp_rt_count = 0u;
            u16 _slice_num;
            Vector4f _texel_size;
            bool _is_swapchain = false;
            Color _clear_color = Colors::kBlack;
            f32 _clear_depth = kZFar;
        };

        class SwapchainTexture : public RenderTexture
        {
        public:
            SwapchainTexture(u16 width, u16 height,ERenderTargetFormat format)
                : RenderTexture(TextureDesc(width,height,format))
            {
                _name = "BackBuffer";
                _width = width;
                _height = height;
                _slice_num = 1;
                _texel_size = Vector4f(1.0f / width, 1.0f / height, width, height);
                _is_swapchain = true;
            }

            virtual ~SwapchainTexture() override {}

            virtual void Resize(u16 w, u16 h) 
            {
                _width = w;
                _height = h;
                _texel_size = Vector4f(1.0f / _width, 1.0f / _height, (f32)_width, (f32)_height);
            };
            virtual void Present() {};
            virtual void PreparePresent(RHICommandBuffer* cmd) {};
            virtual u8 GetCurrentBackBufferIndex() { return 0u; };

            void CreateView() override {};

            TextureHandle ColorTexture(u16 view_index) override
            {
                return 0u;
            }
            bool IsSwapChain() const { return _is_swapchain; }
        protected:
            u16 _buffer_num;
            u8 _cur_backbuf_index;
        };

        class AILU_API RenderTexturePool : public NonCopyable
        {
            struct RTInfo
            {
                bool _is_available;
                bool _is_temp;
                u32 _id;
                u64 _last_access_frame_count;
                Ref<RenderTexture> _rt;
                u64 _fence_value = 0;
            };
        public:
            RenderTexturePool() = default;
            ~RenderTexturePool();
            using RTPool = std::unordered_multimap<RTHash, RTInfo, RTHash::HashFunc>;
            u32 Add(RTHash hash, Ref<RenderTexture> rt);
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
    }// namespace Render

    namespace Math::ALHash
    {
        template<>
        struct Hasher<Render::TextureDesc>
        {
            u64 operator()(const Ailu::Render::TextureDesc &desc) const
            {
                u64 h = 0;
                h = CombineHashes(h, std::hash<u16>()(desc._width));
                h = CombineHashes(h, std::hash<u16>()(desc._height));
                h = CombineHashes(h, std::hash<u16>()(desc._depth));
                h = CombineHashes(h, std::hash<u16>()(desc._mip_num));
                h = CombineHashes(h, std::hash<u16>()(desc._array_size));
                h = CombineHashes(h, std::hash<u32>()(desc._flags));
                h = CombineHashes(h, std::hash<int>()(static_cast<int>(desc._format)));
                h = CombineHashes(h, std::hash<int>()(static_cast<int>(desc._dimension)));
                return h;
            }
        };
    }
}// namespace Ailu



#endif// !TEXTURE_H__
