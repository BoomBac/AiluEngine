#pragma once
#ifndef __DEPTH_OF_FIELD_PASS_H__
#define __DEPTH_OF_FIELD_PASS_H__

#include "Assets/AssetRef.h"
#include "Render/PostprocessSettings.h"
#include "Render/RenderGraph/RenderGraphFwd.h"

namespace Ailu::Render
{
    struct RenderingData;
    class Camera;
    class Material;
    class Shader;

    class DepthOfFieldPass
    {
    public:
        DepthOfFieldPass();

        RDG::RGHandle Record(RDG::RenderGraph &graph, RDG::RGHandle source, RDG::RGHandle depth,
                             const Camera &camera, const DepthOfFieldSettings &settings);

    private:
        AssetRef<Shader> _shader;
        Ref<Material> _materials[6];
    };
}

#endif // __DEPTH_OF_FIELD_PASS_H__
