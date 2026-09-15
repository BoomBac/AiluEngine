#include "PlaceActor/PlaceActorRegistration.h"

#include "Framework/Math/Color.h"
#include "PlaceActor/PlaceActorRegistry.h"
#include "Scene/Component.h"
#include "Scene/Scene.h"

#include <utility>

namespace Ailu
{
    namespace Editor
    {
        namespace
        {
            void SetSpawnPosition(const PlaceActorSpawnContext &context, ECS::Entity entity)
            {
                if (context._scene == nullptr)
                    return;

                if (auto *transform = context._scene->GetRegister().GetComponent<ECS::TransformComponent>(entity))
                    transform->SetLocalPosition(context._position);
            }

            void RegisterLightActor(PlaceActorRegistry &registry, String id, String display_name, i32 order,
                                    ECS::ELightType type, const Vector4f &light_param)
            {
                PlaceActorDesc desc;
                desc._id = std::move(id);
                desc._display_name = std::move(display_name);
                desc._category = "Lighting";
                desc._order = order;
                const String actor_name = desc._display_name;
                desc._spawn = [type, light_param, actor_name](const PlaceActorSpawnContext &context)
                {
                    if (context._scene == nullptr)
                        return ECS::kInvalidEntity;

                    const ECS::Entity entity = context._scene->AddObject(actor_name);
                    auto &light = context._scene->GetRegister().AddComponent<ECS::LightComponent>(entity);
                    light._type = type;
                    light._light._light_color = Color(1.0f, 1.0f, 1.0f, 1.0f);
                    light._light._light_pos = {context._position.x, context._position.y, context._position.z, 1.0f};
                    light._light._light_dir = Vector4f(0.0f, -1.0f, 0.0f, 0.0f);
                    light._light._light_param = light_param;
                    light._light._is_two_side = false;
                    light._shadow._is_cast_shadow = true;
                    light._shadow._constant_bias = 0.01f;
                    light._shadow._slope_bias = 0.01f;
                    light._shadow._padding = 0.0f;
                    SetSpawnPosition(context, entity);
                    return entity;
                };
                registry.Register(std::move(desc));
            }
        }

        void RegisterPlaceActors()
        {
            auto &registry = PlaceActorRegistry::Get();
            RegisterLightActor(registry, "light.directional", "Directional Light", 0, ECS::ELightType::kDirectional,
                               Vector4f(0.0f, 0.0f, 0.0f, 0.265f));
            RegisterLightActor(registry, "light.point", "Point Light", 1, ECS::ELightType::kPoint,
                               Vector4f(10.0f, 0.1f, 0.0f, 0.0f));
            RegisterLightActor(registry, "light.spot", "Spot Light", 2, ECS::ELightType::kSpot,
                               Vector4f(10.0f, 20.0f, 30.0f, 0.0f));
            RegisterLightActor(registry, "light.area", "Area Light", 3, ECS::ELightType::kArea,
                               Vector4f(10.0f, 4.0f, 5.0f, 0.0f));

            registry.Register({
                "lighting.light_probe",
                "Light Probe",
                "Lighting",
                4,
                [](const PlaceActorSpawnContext &context)
                {
                    if (context._scene == nullptr)
                        return ECS::kInvalidEntity;

                    const ECS::Entity entity = context._scene->AddObject("Light Probe");
                    auto &probe = context._scene->GetRegister().AddComponent<ECS::CLightProbe>(entity);
                    probe._size = 10.0f;
                    probe._is_update_every_tick = false;
                    probe._is_dirty = true;
                    SetSpawnPosition(context, entity);
                    return entity;
                }
            });

            registry.Register({
                "lighting.light_probe_group",
                "Light Probe Group",
                "Lighting",
                5,
                [](const PlaceActorSpawnContext &context)
                {
                    if (context._scene == nullptr)
                        return ECS::kInvalidEntity;

                    const ECS::Entity entity = context._scene->AddObject("Light Probe Group");
                    context._scene->GetRegister().AddComponent<ECS::CLightProbeGroup>(entity);
                    SetSpawnPosition(context, entity);
                    return entity;
                }
            });
        }
    }// namespace Editor
}// namespace Ailu
