#pragma once
#ifndef __PHYSICS_2D_WORLD_H__
#define __PHYSICS_2D_WORLD_H__

#include "Framework/Core/SmartPtr.h"
#include "Framework/Core/Types.h"
#include "Framework/Core/Delegate.h"
#include "Framework/Math/Color.h"
#include "Framework/Platform/Api.h"
#include "Physics/2D/Physics2DTypes.h"
#include "Scene/Entity.h"

namespace Ailu
{
    class AILU_API Physics2DWorld
    {
    public:
        Physics2DWorld();
        ~Physics2DWorld();

        Physics2DWorld(const Physics2DWorld &) = delete;
        Physics2DWorld &operator=(const Physics2DWorld &) = delete;

        void Initialize();
        void Shutdown();
        void Sync(ECS::Register &r, const std::set<ECS::Entity> &entities);
        void Step(f32 fixed_delta_time);
        void SyncTransforms(ECS::Register &r);
        void FlushEvents();
        void DebugDraw(ECS::Register &r) const;
        void DebugDrawCollider(ECS::Register &r, ECS::Entity entity, Math::Color color) const;
        void CreateBody(ECS::Register &r, ECS::Entity entity);
        void DestroyBody(ECS::Entity entity);
        void SetPosition(ECS::Entity entity, const Vector2f &position);
        Vector2f GetPosition(ECS::Entity entity) const;
        void SetLinearVelocity(ECS::Entity entity, const Vector2f &velocity);
        Vector2f GetLinearVelocity(ECS::Entity entity) const;
        f32 GetAngularVelocity(ECS::Entity entity) const;
        void SetAngularVelocity(ECS::Entity entity, f32 velocity);
        f32 GetGravityScale(ECS::Entity entity) const;
        void SetGravityScale(ECS::Entity entity, f32 scale);
        bool IsFixedRotation(ECS::Entity entity) const;
        void SetFixedRotation(ECS::Entity entity, bool fixed);
        void AddForce(ECS::Entity entity, const Vector2f &force);
        void AddImpulse(ECS::Entity entity, const Vector2f &impulse);
        void AddTorque(ECS::Entity entity, f32 torque);

        void SetLayerCollision(u8 layer_a, u8 layer_b, bool enabled);
        bool CanLayersCollide(u8 layer_a, u8 layer_b) const;
        void SetLayerCollisionMasks(const Vector<u32> &layer_collision_masks);

        bool Raycast(const Raycast2DDesc &desc, RaycastHit2D &hit) const;
        void RaycastAll(const Raycast2DDesc &desc, Vector<RaycastHit2D> &hits) const;
        bool CircleCast(const CircleCast2DDesc &desc, ShapeCastHit2D &hit) const;
        void OverlapCircle(const OverlapCircle2DDesc &desc, Vector<OverlapHit2D> &hits) const;
        void OverlapBox(const OverlapBox2DDesc &desc, Vector<OverlapHit2D> &hits) const;

        bool IsInitialized() const;
        bool IsValidBody(ECS::Entity entity) const;
        void SetDebugDrawEnabled(bool enabled) { _debug_draw_enabled = enabled; }
        bool IsDebugDrawEnabled() const { return _debug_draw_enabled; }

        DECLARE_DELEGATE(OnContact, const PhysicsContact2D &);

    private:
        struct Impl;
        Scope<Impl> _impl;
        bool _debug_draw_enabled = false;
    };
}

#endif
