#pragma once
#ifndef __PHYSICS_2D_H__
#define __PHYSICS_2D_H__

#include "Physics/2D/Physics2DTypes.h"

namespace Ailu::SceneManagement { class Scene; }

namespace Ailu::Physics2D
{
    bool IsValidBody(SceneManagement::Scene &scene, ECS::Entity entity);
    void SetPosition(SceneManagement::Scene &scene, ECS::Entity entity, const Vector2f &position);
    Vector2f GetPosition(SceneManagement::Scene &scene, ECS::Entity entity);
    void SetLinearVelocity(SceneManagement::Scene &scene, ECS::Entity entity, const Vector2f &velocity);
    Vector2f GetLinearVelocity(SceneManagement::Scene &scene, ECS::Entity entity);
    void SetAngularVelocity(SceneManagement::Scene &scene, ECS::Entity entity, f32 velocity);
    void AddForce(SceneManagement::Scene &scene, ECS::Entity entity, const Vector2f &force);
    void AddImpulse(SceneManagement::Scene &scene, ECS::Entity entity, const Vector2f &impulse);
}

#endif
