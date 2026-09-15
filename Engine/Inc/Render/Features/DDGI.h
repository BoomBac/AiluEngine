#pragma once
#ifndef __DDGI_FEATURE_H__
#define __DDGI_FEATURE_H__

#include "Framework/Core/Containers/Map.h"
#include "Framework/Core/Containers/Vector.h"
#include "Render/DDGI/DDGITypes.h"
#include "Render/Features/RenderFeature.h"
#include <limits>

namespace Ailu::SceneManagement
{
    class Scene;
}

namespace Ailu::Render
{
    class AILU_API DDGIFeature final : public RenderFeature
    {
    public:
        DDGIFeature();

        void AddRenderPasses(Renderer &renderer, const RenderingData &rendering_data) final;

    private:
        void SyncSceneVolumes(const SceneManagement::Scene &scene);
        void SyncVolumeRuntimes();

        Vector<DDGIVolumeRenderData> _volumes;
        HashMap<ECS::Entity, DDGIVolumeRuntime> _volume_runtimes;
        const SceneManagement::Scene *_last_scene = nullptr;
        u64 _last_sync_frame = std::numeric_limits<u64>::max();
    };
}

#endif // __DDGI_FEATURE_H__
