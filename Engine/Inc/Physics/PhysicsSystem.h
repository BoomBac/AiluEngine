/*
 * @author    : BoomBac
 * @created   : 2024.9
*/
#pragma once
#ifndef __PHYSICS_SYSTEM__
#define __PHYSICS_SYSTEM__

#include "Scene/Entity.hpp"
#include "Render/Material.h"
#include "Scene/Component.h"
#include "Physics/Collision.h"
namespace Ailu
{
    namespace ECS
    {
        class PhysicsSystem : public System
        {
            DECLARE_CLASS(PhysicsSystem)
            using CollisionFunc = std::function<ContactData(const CCollider&,const Matrix4x4f&,const CCollider&,const Matrix4x4f&)>;
        public:
            inline static const Vector3f kGravity = Vector3f(0.f, -9.8f, 0.f);
            PhysicsSystem();
            void Update(Register &r, f32 delta_time) final;
            Ref<System> Clone() final
            {
                auto copy = MakeRef<PhysicsSystem>();
                copy->_entities = _entities;
                return copy;
            };
            void AddForce(Register &r,ECS::Entity entity, const Vector3f &force);
            void AddForce(Register &r,ECS::Entity entity, const Vector3f &force,const Vector3f &apply_pos);

        private:
            void Intergate(Register &r, ECS::Entity entity, f32 delta_time);
            void ResolveCollision(Register &r, ECS::Entity entity);
        private:
            CollisionFunc _collision_matrix[3][3];
            std::set<ECS::Entity> _collisions;
        };
    }// namespace ECS
}// namespace Ailu

#endif// !__PHYSICS_SYSTEM__