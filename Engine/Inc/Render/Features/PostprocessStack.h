#pragma once
#ifndef __POSTPROCESS_STACK_H__
#define __POSTPROCESS_STACK_H__

#include "Render/Features/BloomPass.h"
#include "Render/Features/RenderFeature.h"
#include "Render/Features/ToneMappingPass.h"

namespace Ailu::Render
{
    class AILU_API PostprocessStack final : public RenderPass
    {
    public:
        PostprocessStack();

        void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data) final;

    private:
        BloomPass _bloom;
        ToneMappingPass _tone_mapping;
    };
}

#endif // __POSTPROCESS_STACK_H__
