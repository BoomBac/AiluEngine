#include "Framework/Script/ScriptRigidBody2D.h"

#include "Physics/2D/Physics2D.h"
#include "Physics/2D/Physics2DComponents.h"
#include "Scene/Scene.h"

namespace Ailu
{
    bool ScriptRigidBody2D::IsValid() const
    {
        return _scene != nullptr && _scene->IsValidEntity(_entity) &&
               _scene->GetRegister().HasComponent<ECS::RigidBody2DComponent>(_entity);
    }
    Vector2f ScriptRigidBody2D::GetVelocity() const
    {
        return IsValid() ? Physics2D::GetLinearVelocity(*_scene, _entity) : Vector2f::kZero;
    }
    void ScriptRigidBody2D::SetVelocity(const Vector2f &velocity) const
    {
        if (IsValid()) Physics2D::SetLinearVelocity(*_scene, _entity, velocity);
    }
    f32 ScriptRigidBody2D::GetAngularVelocity() const
    {
        return IsValid() ? Physics2D::GetAngularVelocity(*_scene, _entity) : 0.0f;
    }
    void ScriptRigidBody2D::SetAngularVelocity(f32 velocity) const
    {
        if (IsValid()) Physics2D::SetAngularVelocity(*_scene, _entity, velocity);
    }
    f32 ScriptRigidBody2D::GetGravityScale() const
    {
        return IsValid() ? Physics2D::GetGravityScale(*_scene, _entity) : 0.0f;
    }
    void ScriptRigidBody2D::SetGravityScale(f32 scale) const
    {
        if (IsValid()) Physics2D::SetGravityScale(*_scene, _entity, scale);
    }
    bool ScriptRigidBody2D::IsFixedRotation() const
    {
        return IsValid() && Physics2D::IsFixedRotation(*_scene, _entity);
    }
    void ScriptRigidBody2D::SetFixedRotation(bool fixed) const
    {
        if (IsValid()) Physics2D::SetFixedRotation(*_scene, _entity, fixed);
    }
    void ScriptRigidBody2D::AddForce(const Vector2f &force) const
    {
        if (IsValid()) Physics2D::AddForce(*_scene, _entity, force);
    }
    void ScriptRigidBody2D::AddImpulse(const Vector2f &impulse) const
    {
        if (IsValid()) Physics2D::AddImpulse(*_scene, _entity, impulse);
    }
    void ScriptRigidBody2D::AddTorque(f32 torque) const
    {
        if (IsValid()) Physics2D::AddTorque(*_scene, _entity, torque);
    }
}
