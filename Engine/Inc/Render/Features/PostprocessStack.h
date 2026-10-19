#pragma once
#ifndef __POSTPROCESS_STACK_H__
#define __POSTPROCESS_STACK_H__

#include "Render/Features/BloomPass.h"
#include "Render/Features/DepthOfFieldPass.h"
#include "Render/Features/AutoExposurePass.h"
#include "Render/Features/RenderFeature.h"
#include "Render/Features/ToneMappingPass.h"

namespace Ailu::Render
{
    class Camera;

    class AILU_API PostprocessStack final : public RenderPass
    {
    public:
        PostprocessStack();

        void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data) final;
        RenderTexture *GetPreExposureTexture(const Camera &camera) const;
        RenderTexture *GetPreviousPreExposureTexture(const Camera &camera) const;

    private:
        AutoExposurePass _auto_exposure;
        DepthOfFieldPass _depth_of_field;
        BloomPass _bloom;
        ToneMappingPass _tone_mapping;
        Ref<Shader> _overlay_depth_shader;
        Ref<Material> _overlay_depth_material;
    };
}

#endif // __POSTPROCESS_STACK_H__
