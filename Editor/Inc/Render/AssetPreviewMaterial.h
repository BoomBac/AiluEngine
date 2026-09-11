#pragma once

#include "Framework/Core/SmartPtr.h"

namespace Ailu::Render
{
    class Material;
}

namespace Ailu::Editor
{
    Ref<Render::Material> CreatePerObjectPreviewMaterial(Render::Material *source_material);
    Ref<Render::Material> CreatePerObjectPreviewWireframeMaterial();
}
