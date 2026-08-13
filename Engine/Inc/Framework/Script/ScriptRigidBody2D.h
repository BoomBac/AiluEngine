#pragma once

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Math/ALMath.hpp"
#include "Scene/Entity.h"

#include "generated/ScriptRigidBody2D.gen.h"

namespace Ailu
{
    namespace SceneManagement
    {
        class Scene;
    }

    ASTRUCT(Script)
    struct AILU_API ScriptRigidBody2D
    {
        GENERATED_BODY()
        SceneManagement::Scene *_scene = nullptr;
        ECS::Entity _entity = ECS::kInvalidEntity;

        AFUNCTION(ScriptProperty)
        bool IsValid() const;
        AFUNCTION(ScriptProperty)
        Vector2f GetVelocity() const;
        AFUNCTION(ScriptProperty)
        void SetVelocity(const Vector2f &velocity) const;
        AFUNCTION(ScriptProperty)
        f32 GetAngularVelocity() const;
        AFUNCTION(ScriptProperty)
        void SetAngularVelocity(f32 velocity) const;
        AFUNCTION(ScriptProperty)
        f32 GetGravityScale() const;
        AFUNCTION(ScriptProperty)
        void SetGravityScale(f32 scale) const;
        AFUNCTION(ScriptProperty)
        bool IsFixedRotation() const;
        AFUNCTION(ScriptProperty)
        void SetFixedRotation(bool fixed) const;

        AFUNCTION(Script)
        void AddForce(const Vector2f &force) const;
        AFUNCTION(Script)
        void AddImpulse(const Vector2f &impulse) const;
        AFUNCTION(Script)
        void AddTorque(f32 torque) const;
    };
}
