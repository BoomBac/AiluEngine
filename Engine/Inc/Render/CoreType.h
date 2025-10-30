#ifndef __CORE_TYPE_H__
#define __CORE_TYPE_H__
#include "GlobalMarco.h"


namespace Ailu
{
    namespace Render
    {
        enum class EResourceUsage : u32
        {
            kNone = 0,
            kReadSRV = 1 << 0,  // SRV - 着色器读取
            kWriteUAV = 1 << 1, // UAV - 无序访问
            kWriteRTV = 1 << 2,    // RTV - 渲染目标
            kDSV = 1 << 3,    // DSV - 深度模板
            kCopySrc = 1 << 4,      // 拷贝源
            kCopyDst = 1 << 5,        // 拷贝目标
            kIndirectArgument = 1 << 6,// 间接绘制参数
            kRaytracingAccel = 1 << 7  // 光追加速结构
        };

        inline EResourceUsage operator|(EResourceUsage a, EResourceUsage b)
        {
            return static_cast<EResourceUsage>(static_cast<u32>(a) | static_cast<u32>(b));
        }

        inline EResourceUsage operator&(EResourceUsage a, EResourceUsage b)
        {
            return static_cast<EResourceUsage>(static_cast<u32>(a) & static_cast<u32>(b));
        }

        enum class ELoadStoreAction
        {
            kLoad,
            kStore,
            kClear,
            kNotCare
        };

        const static u32 kTotalSubRes = 0XFFFFFFFF;

        enum class EResourceState
        {
            kCommon = 0,
            kVertexAndConstantBuffer = 0x1,
            kIndexBuffer = 0x2,
            kRenderTarget = 0x4,
            kUnorderedAccess = 0x8,
            kDepthWrite = 0x10,
            kDepthRead = 0x20,
            kNonPixelShaderResource = 0x40,
            kPixelShaderResource = 0x80,
            kStreamOut = 0x100,
            kIndirectArgument = 0x200,
            kCopyDest = 0x400,
            kCopySource = 0x800,
            kResolveDest = 0x1000,
            kResolveSource = 0x2000,
            kRaytracingAccelerationStructure = 0x400000,
            kShadingRateSource = 0x1000000,
            kGenericRead = ((((0x1 | 0x2) | 0x40) | 0x80) | 0x200) | 0x800,
            kAllShaderResource = (0x40 | 0x80),
            kPresent = 0,
            kPredication = 0x200,
            kVideoDecodeRead = 0x10000,
            kVideoDecodeWrite = 0x20000,
            kVideoProcessRead = 0x40000,
            kVideoProcessWrite = 0x80000,
            kVideoEncodeRead = 0x200000,
            kVideoEncodeWrite = 0x800000
        };
        enum class EGpuResType
        {
            kBuffer,
            KRWBuffer,
            kTexture,
            kRenderTexture,
            kVertexBuffer,
            kIndexBUffer,
            kConstBuffer,
            kGraphicsPSO,
        };

        struct ClearValue
        {
            enum class EType
            {
                kColor,
                kDepthStencil
            };
            EType type = EType::kColor;

            union
            {
                f32 color[4];
                struct
                {
                    f32 depth;
                    u32 stencil;
                } depthStencil;
            };

            static ClearValue Color(f32 r, f32 g, f32 b, f32 a = 1.0f)
            {
                ClearValue v;
                v.type = EType::kColor;
                v.color[0] = r;
                v.color[1] = g;
                v.color[2] = b;
                v.color[3] = a;
                return v;
            }

            static ClearValue DepthStencil(f32 depth, u32 stencil = 0)
            {
                ClearValue v;
                v.type = EType::kDepthStencil;
                v.depthStencil.depth = depth;
                v.depthStencil.stencil = stencil;
                return v;
            }
        };
    };
};

#endif