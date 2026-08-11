#include "Physics/2D/Physics2D.h"
#include "Physics/2D/Physics2DSystem.h"
#include "Scene/Scene.h"

namespace Ailu::Physics2D
{
    namespace
    {
        Physics2DWorld *GetWorld(SceneManagement::Scene &scene)
        {
            return scene.GetRegister().GetSystem<ECS::Physics2DSystem>() ?
                   &scene.GetRegister().GetSystem<ECS::Physics2DSystem>()->World() : nullptr;
        }
    }

    bool IsValidBody(SceneManagement::Scene &scene, ECS::Entity entity)
    {
        Physics2DWorld *world = GetWorld(scene);
        return world != nullptr && world->IsValidBody(entity);
    }
    void SetPosition(SceneManagement::Scene &scene, ECS::Entity entity, const Vector2f &position) { if (auto *world = GetWorld(scene)) world->SetPosition(entity, position); }
    Vector2f GetPosition(SceneManagement::Scene &scene, ECS::Entity entity) { if (auto *world = GetWorld(scene)) return world->GetPosition(entity); return Vector2f::kZero; }
    void SetLinearVelocity(SceneManagement::Scene &scene, ECS::Entity entity, const Vector2f &velocity) { if (auto *world = GetWorld(scene)) world->SetLinearVelocity(entity, velocity); }
    Vector2f GetLinearVelocity(SceneManagement::Scene &scene, ECS::Entity entity) { if (auto *world = GetWorld(scene)) return world->GetLinearVelocity(entity); return Vector2f::kZero; }
    void SetAngularVelocity(SceneManagement::Scene &scene, ECS::Entity entity, f32 velocity) { if (auto *world = GetWorld(scene)) world->SetAngularVelocity(entity, velocity); }
    void AddForce(SceneManagement::Scene &scene, ECS::Entity entity, const Vector2f &force) { if (auto *world = GetWorld(scene)) world->AddForce(entity, force); }
    void AddImpulse(SceneManagement::Scene &scene, ECS::Entity entity, const Vector2f &impulse) { if (auto *world = GetWorld(scene)) world->AddImpulse(entity, impulse); }
}
