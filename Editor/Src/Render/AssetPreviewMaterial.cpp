#include "Render/AssetPreviewMaterial.h"

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
        if (preview_material == nullptr || !source_material->IsStandardLit())
            return preview_material;

        auto preview_shader = ResourceMgr::Get().Get<Render::Shader>(L"Shaders/hlsl/forwardlit_preview.alasset");
        if (preview_shader == nullptr)
            return nullptr;

        preview_material->SetActiveShader(preview_shader);
        preview_material->SetCullMode(source_material->GetCullMode());
        return preview_material;
    }

    Ref<Render::Material> CreatePerObjectPreviewWireframeMaterial()
    {
        auto source_material = ResourceMgr::Get().Get<Render::Material>(L"Runtime/Material/Wireframe");
        auto preview_shader = ResourceMgr::Get().Get<Render::Shader>(L"Shaders/hlsl/wireframe_preview.alasset");
        if (source_material == nullptr || preview_shader == nullptr)
            return nullptr;

        auto preview_material = source_material->CreateInstance();
        if (preview_material == nullptr)
            return nullptr;

        preview_material->SetActiveShader(preview_shader);
        preview_material->SetCullMode(source_material->GetCullMode());
        return preview_material;
    }
}
