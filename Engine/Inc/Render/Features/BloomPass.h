#pragma once
#ifndef __BLOOM_PASS_H__
#define __BLOOM_PASS_H__

#include "Render/PostprocessSettings.h"
#include "Render/RenderGraph/RenderGraphFwd.h"

namespace Ailu::Render
{
    struct RenderingData;
    class Material;

    class BloomPass
    {
    public:
        BloomPass();

        RDG::RGHandle Record(RDG::RenderGraph &graph, RDG::RGHandle source, const RenderingData &rendering_data,
                             const BloomSettings &settings);

    private:
        Ref<Material> _bloom_compose_mat;
        Vector<Ref<Material>> _bloom_mats;
        u16 _mip_count = 6u;
    };
}

#endif // __BLOOM_PASS_H__
