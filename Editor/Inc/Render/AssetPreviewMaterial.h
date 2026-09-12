#pragma once

#include "Framework/Core/SmartPtr.h"

namespace Ailu::Render
{
    class Material;
}

namespace Ailu::Editor
{
    //forwardlit 中 per-object cbuffer 预览路径的变体关键字
    inline constexpr const char *kPerObjectCBKeyword = "PER_OBJECT_CB";

    Ref<Render::Material> CreatePerObjectPreviewMaterial(Render::Material *source_material);
    Ref<Render::Material> CreatePerObjectPreviewWireframeMaterial();
}
