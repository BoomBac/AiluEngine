#ifndef __CORE_TYPE_H__
#define __CORE_TYPE_H__
#include "GlobalMarco.h"


namespace Ailu
{
    namespace Render
    {
        enum class EResourceAccess : u8
        {
            kUnknown = 0,          // 未定义状态
            kReadSRV = 1 << 0,     // Shader Resource View (只读)
            kWriteUAV = 1 << 1,    // Unordered Access View (可写)
            kRenderTarget = 1 << 2,// Render Target 读/写
            kDepthStencil = 1 << 3,// Depth/Stencil 读/写
            kCopySrc = 1 << 4,     // Copy 源
            kCopyDst = 1 << 5      // Copy 目标
        };

        inline EResourceAccess operator|(EResourceAccess a, EResourceAccess b)
        {
            return static_cast<EResourceAccess>(static_cast<u8>(a) | static_cast<u8>(b));
        }

        inline EResourceAccess &operator|=(EResourceAccess &a, EResourceAccess b)
        {
            a = a | b;
            return a;
        }

        inline bool HasFlag(EResourceAccess flags, EResourceAccess test)
        {
            return (static_cast<u8>(flags) & static_cast<u8>(test)) != 0;
        }

        enum class ELoadStoreAction
        {
            kLoad,
            kStore,
            kClear,
            kNotCare
        };
    };
};

#endif