#ifndef __RENDER_GRAPH_FWD_h__
#define __RENDER_GRAPH_FWD_h__
#include "GlobalMarco.h"

namespace Ailu::Render::RDG
{
    struct RGHandle
    {
    public:
        bool operator==(const RGHandle &other) const
        {
            return _id == other._id && _version == other._version;
        }
        bool IsValid() const { return _id != 0u; }
    public:
        u32 _id;
        u32 _version;
    };

    enum class EPassType : u8
    {
        kGraphics,
        kCompute,
        kAsyncCompute,
        kCopy
    };
    struct AILU_API PassDesc
    {
        EPassType _type = EPassType::kGraphics;
    };

    class RenderGraph;
}
#endif