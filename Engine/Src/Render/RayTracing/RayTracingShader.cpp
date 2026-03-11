#include "Render/RayTracing/RayTracingShader.h"
#include "RHI/DX12/RayTracing/D3DRayTracingShader.h"
#include "pch.h"

namespace Ailu::Render
{
    Ref<RayTracingShader> RayTracingShader::Create(const WString &sys_path)
    {
        AL_ASSERT_MSG(false, "Unsupported render api!");
        switch (RendererAPI::GetAPI())
        {
            case RendererAPI::ERenderAPI::kNone:
                AL_ASSERT_MSG(false, "None render api used!");
                return nullptr;
            case RendererAPI::ERenderAPI::kDirectX12:
            {
                return MakeRef<RHI::DX12::D3DRayTracingShader>(sys_path);
            }
        }
        AL_ASSERT_MSG(false, "Unsupported render api!");
        return nullptr;
    }

    bool RayTracingShader::RHICompileImpl(ShaderVariantHash variant_hash, bool is_load_cache)
    {
        return true;
    }
}