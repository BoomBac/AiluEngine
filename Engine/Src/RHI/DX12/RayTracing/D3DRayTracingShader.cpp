#include "RHI/DX12/RayTracing/D3DRayTracingShader.h"
#include "pch.h"

namespace Ailu::RHI::DX12
{
    D3DRayTracingShader::D3DRayTracingShader(const WString &sys_path)
    {
    }

    D3DRayTracingShader::~D3DRayTracingShader()
    {
    }

    bool D3DRayTracingShader::RHICompileImpl(Render::ShaderVariantHash variant_hash, bool is_load_cache)
    {
        return true;
    }
}