#include "Framework/Script/ScriptPhysics2D.h"

#include "Physics/2D/Physics2D.h"

namespace Ailu
{
    bool ScriptPhysics2D::IsValidBody(const ScriptEntity &entity)
    {
        return entity._scene != nullptr && entity.IsValid() && Physics2D::IsValidBody(*entity._scene, entity._entity);
    }
    void ScriptPhysics2D::SetPosition(const ScriptEntity &entity, const Vector2f &position)
    {
        if (entity._scene != nullptr && entity.IsValid()) Physics2D::SetPosition(*entity._scene, entity._entity, position);
    }
    Vector2f ScriptPhysics2D::GetPosition(const ScriptEntity &entity)
    {
        return entity._scene != nullptr && entity.IsValid() ? Physics2D::GetPosition(*entity._scene, entity._entity) : Vector2f::kZero;
    }
    void ScriptPhysics2D::SetLinearVelocity(const ScriptEntity &entity, const Vector2f &velocity)
    {
        if (entity._scene != nullptr && entity.IsValid()) Physics2D::SetLinearVelocity(*entity._scene, entity._entity, velocity);
    }
    Vector2f ScriptPhysics2D::GetLinearVelocity(const ScriptEntity &entity)
    {
        return entity._scene != nullptr && entity.IsValid() ? Physics2D::GetLinearVelocity(*entity._scene, entity._entity) : Vector2f::kZero;
    }
    void ScriptPhysics2D::SetAngularVelocity(const ScriptEntity &entity, f32 velocity)
    {
        if (entity._scene != nullptr && entity.IsValid()) Physics2D::SetAngularVelocity(*entity._scene, entity._entity, velocity);
    }
    void ScriptPhysics2D::AddForce(const ScriptEntity &entity, const Vector2f &force)
    {
        if (entity._scene != nullptr && entity.IsValid()) Physics2D::AddForce(*entity._scene, entity._entity, force);
    }
    void ScriptPhysics2D::AddImpulse(const ScriptEntity &entity, const Vector2f &impulse)
    {
        if (entity._scene != nullptr && entity.IsValid()) Physics2D::AddImpulse(*entity._scene, entity._entity, impulse);
    }
}
