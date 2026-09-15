#pragma once
#ifndef __TONE_MAPPING_PASS_H__
#define __TONE_MAPPING_PASS_H__

#include "Assets/AssetRef.h"
#include "Render/PostprocessSettings.h"
#include "Render/RenderGraph/RenderGraphFwd.h"

namespace Ailu::Render
{
    struct RenderingData;
    class Shader;
    class Material;

    class ToneMappingPass
    {
    public:
        ToneMappingPass();

        RDG::RGHandle Record(RDG::RenderGraph &graph, RDG::RGHandle source, const RenderingData &rendering_data,
                             const ToneMappingSettings &settings);

    private:
        AssetRef<Shader> _shader;
        Ref<Material> _material;
    };
}

#endif // __TONE_MAPPING_PASS_H__
