#ifndef  __ENGINE_CONFIG_H__
#define __ENGINE_CONFIG_H__
#include "Objects/Type.h"
#include "generated/EngineConfig.gen.h"
namespace Ailu
{
    ACLASS()
    class EngineConfig : public Object
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
    };
    static EngineConfig s_engine_config;
}
#endif  __ENGINE_CONFIG_H__