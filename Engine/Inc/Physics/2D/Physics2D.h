#pragma once
#ifndef __PHYSICS_2D_H__
#define __PHYSICS_2D_H__

#include "Physics/2D/Physics2DTypes.h"
#include "Framework/Core/Containers/Vector.h"

namespace Ailu::SceneManagement { class Scene; }

namespace Ailu::Physics2D
{
    bool IsValidBody(SceneManagement::Scene &scene, ECS::Entity entity);
    void SetPosition(SceneManagement::Scene &scene, ECS::Entity entity, const Vector2f &position);
    Vector2f GetPosition(SceneManagement::Scene &scene, ECS::Entity entity);
    void SetLinearVelocity(SceneManagement::Scene &scene, ECS::Entity entity, const Vector2f &velocity);
    Vector2f GetLinearVelocity(SceneManagement::Scene &scene, ECS::Entity entity);
    f32 GetAngularVelocity(SceneManagement::Scene &scene, ECS::Entity entity);
    void SetAngularVelocity(SceneManagement::Scene &scene, ECS::Entity entity, f32 velocity);
    f32 GetGravityScale(SceneManagement::Scene &scene, ECS::Entity entity);
    void SetGravityScale(SceneManagement::Scene &scene, ECS::Entity entity, f32 scale);
    bool IsFixedRotation(SceneManagement::Scene &scene, ECS::Entity entity);
    void SetFixedRotation(SceneManagement::Scene &scene, ECS::Entity entity, bool fixed);
    void AddForce(SceneManagement::Scene &scene, ECS::Entity entity, const Vector2f &force);
    void AddImpulse(SceneManagement::Scene &scene, ECS::Entity entity, const Vector2f &impulse);
    void AddTorque(SceneManagement::Scene &scene, ECS::Entity entity, f32 torque);
    bool Raycast(SceneManagement::Scene &scene, const Raycast2DDesc &desc, RaycastHit2D &hit);
    void OverlapCircle(SceneManagement::Scene &scene, const OverlapCircle2DDesc &desc, Vector<OverlapHit2D> &hits);
    void OverlapBox(SceneManagement::Scene &scene, const OverlapBox2DDesc &desc, Vector<OverlapHit2D> &hits);
}

#endif
