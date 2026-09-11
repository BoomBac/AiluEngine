#ifndef  __ENGINE_CONFIG_H__
#define __ENGINE_CONFIG_H__
#include "Objects/Type.h"
#include "generated/EngineConfig.gen.h"
namespace Ailu
{
    ACLASS()
    class AILU_API EngineConfig : public Object
    {
        GENERATED_BODY()
    public:
        APROPERTY(Category = "Layer")
        u32 None;
        APROPERTY(Category = "Layer")
        u32 Default;
        APROPERTY(Category = "Layer")
        u32 ShadowCaster;
        APROPERTY(Category = "Layer")
        u32 All;
        APROPERTY(Category = "Render")
        u32 MaxRenderObjectPerTask;
        APROPERTY(Category = "Render")
        bool isMultiThreadRender;
        APROPERTY(Category = "Render")
        bool enable_graphics_job;
        APROPERTY(Category = "Render")
        bool EnableCpuStateBatchedSubmission;
        APROPERTY(Category = "Render")
        bool EnableIncrementalGraphicsBinding;
        APROPERTY(Category = "Render")
        bool _enable_compute_skinning;
        APROPERTY(Category = "Debug")
        bool _enable_d3d12_debug_layer;
        APROPERTY(Category = "Debug")
        bool _enable_rdc;
        APROPERTY(Category = "Debug")
        bool _enable_pix;
    };
    extern AILU_API EngineConfig g_engine_config;
}
#endif  __ENGINE_CONFIG_H__
