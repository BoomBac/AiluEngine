#pragma once
#ifndef __D3D_RAY_TRACING_SHADER_H__
#define __D3D_RAY_TRACING_SHADER_H__
#include "Render/RayTracing/RayTracingShader.h"

namespace Ailu::RHI::DX12
{
    class D3DRayTracingShader : public Render::RayTracingShader
    {
    public:
        D3DRayTracingShader(const WString &sys_path);
        ~D3DRayTracingShader();

        bool RHICompileImpl(Render::ShaderVariantHash variant_hash, bool is_load_cache) override;
    };
}

#endif // !__D3D_RAY_TRACING_SHADER_H__