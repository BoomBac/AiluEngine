#include "Render/PostprocessSettings.h"

#include "Scene/Component.h"
#include "Scene/Scene.h"

namespace Ailu::Render
{
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
