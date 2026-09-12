#include "Render/AssetPreviewMaterial.h"

#include "Framework/Common/Log.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/Material.h"
#include "Render/Shader.h"

namespace Ailu::Editor
{
    Ref<Render::Material> CreatePerObjectPreviewMaterial(Render::Material *source_material)
    {
        if (source_material == nullptr)
            return nullptr;

        auto preview_material = source_material->CreateInstance();
        if (preview_material == nullptr)
            return nullptr;

        //只有标准 Lit 材质才切到 forwardlit 的预览变体，自定义 shader 材质保持原样
        auto forward_lit_shader = ResourceMgr::Get().Get<Render::Shader>(L"Shaders/hlsl/forwardlit.alasset");
        if (!source_material->IsStandardLit() && source_material->GetShader() != forward_lit_shader)
            return preview_material;
        if (forward_lit_shader == nullptr)
        {
            LOG_ERROR("ForwardLit shader is unavailable for preview material!");
            return nullptr;
        }

        //场景绘制走 scene primitive 缓冲，预览只提供 per-object cbuffer，需切到对应变体
        preview_material->SetActiveShader(forward_lit_shader);
        preview_material->EnableKeyword(kPerObjectCBKeyword);
        preview_material->SetCullMode(source_material->GetCullMode());
        return preview_material;
    }

    Ref<Render::Material> CreatePerObjectPreviewWireframeMaterial()
    {
        auto source_material = ResourceMgr::Get().Get<Render::Material>(L"Runtime/Material/Wireframe");
        if (source_material == nullptr)
        {
            LOG_ERROR("Wireframe material is unavailable for preview material!");
            return nullptr;
        }

        //场景绘制走 scene primitive 缓冲，预览只提供 per-object cbuffer，需切到对应变体
        auto preview_material = source_material->CreateInstance();
        if (preview_material == nullptr)
            return nullptr;

        preview_material->EnableKeyword(kPerObjectCBKeyword);
        return preview_material;
    }
}
