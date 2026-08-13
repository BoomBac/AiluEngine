#include "Physics/2D/Physics2D.h"
#include "Physics/2D/Physics2DComponents.h"
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

        Physics2DWorld *GetWorldWithBody(SceneManagement::Scene &scene, ECS::Entity entity)
        {
            auto *physics_system = scene.GetRegister().GetSystem<ECS::Physics2DSystem>();
            if (physics_system == nullptr)
                return nullptr;

            Physics2DWorld &world = physics_system->World();
            if (!world.IsValidBody(entity) && scene.GetRegister().HasComponent<ECS::Collider2DComponent>(entity))
                world.CreateBody(scene.GetRegister(), entity);
            return &world;
        }
    }

    bool IsValidBody(SceneManagement::Scene &scene, ECS::Entity entity)
    {
        Physics2DWorld *world = GetWorld(scene);
        return world != nullptr && world->IsValidBody(entity);
    }
    void SetPosition(SceneManagement::Scene &scene, ECS::Entity entity, const Vector2f &position)
    {
        if (auto *world = GetWorldWithBody(scene, entity))
            world->SetPosition(entity, position);
    }
    Vector2f GetPosition(SceneManagement::Scene &scene, ECS::Entity entity)
    {
        if (auto *world = GetWorldWithBody(scene, entity))
            return world->GetPosition(entity);
        return Vector2f::kZero;
    }
    void SetLinearVelocity(SceneManagement::Scene &scene, ECS::Entity entity, const Vector2f &velocity)
    {
        if (auto *world = GetWorldWithBody(scene, entity))
            world->SetLinearVelocity(entity, velocity);
    }
    Vector2f GetLinearVelocity(SceneManagement::Scene &scene, ECS::Entity entity)
    {
        if (auto *world = GetWorldWithBody(scene, entity))
            return world->GetLinearVelocity(entity);
        return Vector2f::kZero;
    }
    f32 GetAngularVelocity(SceneManagement::Scene &scene, ECS::Entity entity)
    {
        if (auto *world = GetWorldWithBody(scene, entity))
            return world->GetAngularVelocity(entity);
        return 0.0f;
    }
    void SetAngularVelocity(SceneManagement::Scene &scene, ECS::Entity entity, f32 velocity)
    {
        if (auto *world = GetWorldWithBody(scene, entity))
            world->SetAngularVelocity(entity, velocity);
    }
    f32 GetGravityScale(SceneManagement::Scene &scene, ECS::Entity entity)
    {
        if (auto *world = GetWorldWithBody(scene, entity))
            return world->GetGravityScale(entity);
        return 0.0f;
    }
    void SetGravityScale(SceneManagement::Scene &scene, ECS::Entity entity, f32 scale)
    {
        if (auto *world = GetWorldWithBody(scene, entity))
            world->SetGravityScale(entity, scale);
    }
    bool IsFixedRotation(SceneManagement::Scene &scene, ECS::Entity entity)
    {
        if (auto *world = GetWorldWithBody(scene, entity))
            return world->IsFixedRotation(entity);
        return false;
    }
    void SetFixedRotation(SceneManagement::Scene &scene, ECS::Entity entity, bool fixed)
    {
        if (auto *world = GetWorldWithBody(scene, entity))
            world->SetFixedRotation(entity, fixed);
    }
    void AddForce(SceneManagement::Scene &scene, ECS::Entity entity, const Vector2f &force)
    {
        if (auto *world = GetWorldWithBody(scene, entity))
            world->AddForce(entity, force);
    }
    void AddImpulse(SceneManagement::Scene &scene, ECS::Entity entity, const Vector2f &impulse)
    {
        if (auto *world = GetWorldWithBody(scene, entity))
            world->AddImpulse(entity, impulse);
    }
    void AddTorque(SceneManagement::Scene &scene, ECS::Entity entity, f32 torque)
    {
        if (auto *world = GetWorldWithBody(scene, entity))
            world->AddTorque(entity, torque);
    }
    bool Raycast(SceneManagement::Scene &scene, const Raycast2DDesc &desc, RaycastHit2D &hit)
    {
        auto *world = GetWorld(scene);
        return world != nullptr && world->Raycast(desc, hit);
    }
    void OverlapCircle(SceneManagement::Scene &scene, const OverlapCircle2DDesc &desc, Vector<OverlapHit2D> &hits)
    {
        if (auto *world = GetWorld(scene)) world->OverlapCircle(desc, hits);
        else hits.clear();
    }
    void OverlapBox(SceneManagement::Scene &scene, const OverlapBox2DDesc &desc, Vector<OverlapHit2D> &hits)
    {
        if (auto *world = GetWorld(scene)) world->OverlapBox(desc, hits);
        else hits.clear();
    }
}
