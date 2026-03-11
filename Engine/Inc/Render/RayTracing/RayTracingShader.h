#pragma once
#ifndef __RAY_TRACING_SHADER_H__
#define __RAY_TRACING_SHADER_H__
#include "../Shader.h"
#include "generated/RayTracingShader.gen.h"

namespace Ailu::Render
{
    ACLASS()
    class RayTracingShader : public Object
    {
        GENERATED_BODY()
    public:
        static Ref<RayTracingShader> Create(const WString &sys_path);
        RayTracingShader() = default;
        ~RayTracingShader() = default;
    protected:
        virtual bool RHICompileImpl(ShaderVariantHash variant_hash, bool is_load_cache);
    };
}
#endif// !__RAY_TRACING_SHADER_H__