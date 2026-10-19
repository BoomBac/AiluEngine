#include "Render/PostprocessSettings.h"

#include "Scene/Component.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <cmath>

namespace Ailu::Render
{
    f32 ResolveSceneLuminanceToCdM2(const AutoExposureSettings &settings)
    {
        constexpr f32 kDefaultScale = 683.0f;
        constexpr f32 kMinimumScale = 1.0e-6f;
        constexpr f32 kMaximumScale = 1.0e6f;
        if (!std::isfinite(settings._scene_luminance_to_cd_m2) || settings._scene_luminance_to_cd_m2 <= 0.0f)
            return kDefaultScale;
        return std::clamp(settings._scene_luminance_to_cd_m2, kMinimumScale, kMaximumScale);
    }

    PostprocessSettings ResolvePostprocessSettings(const SceneManagement::Scene &scene)
    {
        const auto &registry = scene.GetRegister();
        const ECS::PostprocessVolumeComponent *selected = nullptr;
        ECS::Entity selected_entity = ECS::kInvalidEntity;
        u32 index = 0u;
        for (const auto &volume : registry.View<ECS::PostprocessVolumeComponent>())
        {
            const ECS::Entity entity = registry.GetEntity<ECS::PostprocessVolumeComponent>(index++);
            if (!scene.IsEntityEnabled(entity) ||
                !registry.IsComponentEnabled<ECS::PostprocessVolumeComponent>(entity) || !volume._is_global)
                continue;

            if (selected == nullptr || volume._priority > selected->_priority ||
                (volume._priority == selected->_priority && entity < selected_entity))
            {
                selected = &volume;
                selected_entity = entity;
            }
        }
        if (selected != nullptr)
            return selected->_settings;

        PostprocessSettings settings;
        settings._bloom._enabled = false;
        settings._tone_mapping._mode = EToneMappingMode::kNone;
        return settings;
    }
}
