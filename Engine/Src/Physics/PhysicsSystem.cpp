#include "pch.h"
#include "Physics/PhysicsSystem.h"
#include "Scene/Component.h"
#include "Framework/Common/Profiler.h"
#include "Framework/Common/Application.h"
#include "Render/Gizmo.h"

namespace Ailu
{
    namespace ECS
    {
        PhysicsSystem::PhysicsSystem()
        {
            _collision_matrix[EColliderType::kSphere][EColliderType::kSphere] = [&](const CCollider &a, const Matrix4x4f &ma, const CCollider &b, const Matrix4x4f &mb) -> ContactData {
                return CollisionDetection::Intersect(CCollider::AsShpere(a) * ma, CCollider::AsShpere(b) * mb);};
            _collision_matrix[EColliderType::kSphere][EColliderType::kBox] = [&](const CCollider &a, const Matrix4x4f &ma, const CCollider &b, const Matrix4x4f &mb) -> ContactData{
                return CollisionDetection::Intersect(CCollider::AsShpere(a) * ma, CCollider::AsBox(b) * mb);};
            _collision_matrix[EColliderType::kSphere][EColliderType::kCapsule] = [&](const CCollider &a, const Matrix4x4f &ma, const CCollider &b, const Matrix4x4f &mb) -> ContactData{
                return CollisionDetection::Intersect(CCollider::AsShpere(a) * ma, CCollider::AsCapsule(b) * mb);};
            _collision_matrix[EColliderType::kCapsule][EColliderType::kSphere] = [&](const CCollider &a, const Matrix4x4f &ma, const CCollider &b, const Matrix4x4f &mb) -> ContactData
            { return CollisionDetection::Intersect(CCollider::AsCapsule(a) * ma, CCollider::AsShpere(b) * mb); };
            _collision_matrix[EColliderType::kCapsule][EColliderType::kBox] = [&](const CCollider &a, const Matrix4x4f &ma, const CCollider &b, const Matrix4x4f &mb) -> ContactData
            { return CollisionDetection::Intersect(CCollider::AsCapsule(a) * ma, CCollider::AsBox(b) * mb); };
            _collision_matrix[EColliderType::kCapsule][EColliderType::kCapsule] = [&](const CCollider &a, const Matrix4x4f &ma, const CCollider &b, const Matrix4x4f &mb) -> ContactData
            { return CollisionDetection::Intersect(CCollider::AsCapsule(a) * ma, CCollider::AsCapsule(b) * mb); };
            _collision_matrix[EColliderType::kBox][EColliderType::kSphere] = [&](const CCollider &a, const Matrix4x4f &ma, const CCollider &b, const Matrix4x4f &mb) -> ContactData
            { return CollisionDetection::Intersect(CCollider::AsBox(a) * ma, CCollider::AsShpere(b) * mb); };
            _collision_matrix[EColliderType::kBox][EColliderType::kBox] = [&](const CCollider &a, const Matrix4x4f &ma, const CCollider &b, const Matrix4x4f &mb) -> ContactData
            { return CollisionDetection::Intersect(CCollider::AsBox(a) * ma, CCollider::AsBox(b) * mb); };
            _collision_matrix[EColliderType::kBox][EColliderType::kCapsule] = [&](const CCollider &a, const Matrix4x4f &ma, const CCollider &b, const Matrix4x4f &mb) -> ContactData
            { return CollisionDetection::Intersect(CCollider::AsBox(a) * ma, CCollider::AsCapsule(b) * mb); };
        }
        void PhysicsSystem::Update(Register &r, f32 delta_time)
        {
            if (Application::Get()._is_playing_mode || Application::Get()._is_simulate_mode)
            {
                PROFILE_BLOCK_CPU(PhysicsSystem_Update)
                _collisions.clear();
                delta_time *= 0.001f;
                delta_time *= TimeMgr::s_time_scale;
                for (auto &e: _entities)
                {
                    Intergate(r, e, delta_time);
                    ResolveCollision(r, e);
                }
            }
        }
        void PhysicsSystem::AddForce(Register &r, ECS::Entity entity, const Vector3f &force)
        {
            r.GetComponent<CRigidBody>(entity)->_force += force;
        }
        void PhysicsSystem::AddForce(Register &r, ECS::Entity entity, const Vector3f &force, const Vector3f &apply_pos)
        {
            auto rigid = r.GetComponent<CRigidBody>(entity);
            rigid->_force += force;

            // 计算力矩：r 是从质心到作用点的向量
            Vector3f r_vec = apply_pos - r.GetComponent<TransformComponent>(entity)->_transform._position;
            rigid->_torque += CrossProduct(r_vec, force);
        }
        void PhysicsSystem::Intergate(Register &r, ECS::Entity entity, f32 delta_time)
        {
            auto rigid = r.GetComponent<CRigidBody>(entity);
            auto &transf = r.GetComponent<TransformComponent>(entity)->_transform;
            //线性移动
            Vector3f acceleration = rigid->_force / rigid->_mass + kGravity;
            rigid->_velocity += acceleration * delta_time;
            transf._position += rigid->_velocity * delta_time;
            if (transf._position.y < 0.0f)
            {
                rigid->_velocity.y = 0.0f;
            }
            //旋转
            Vector3f angular_acceleration = rigid->_torque / rigid->_inertia;
            rigid->_angular_velocity += angular_acceleration * delta_time;
            Quaternion delta_rotation = Quaternion::AngleAxis(Magnitude(rigid->_angular_velocity) * delta_time, Normalize(rigid->_angular_velocity));
            transf._rotation = delta_rotation * transf._rotation;// 累积旋转
            //clear accum
            rigid->_force = Vector3f::kZero;
            rigid->_torque = Vector3f::kZero;
            Transform::ToMatrix(transf, transf._world_matrix);
        }
        void PhysicsSystem::ResolveCollision(Register &r, ECS::Entity entity)
        {
            auto &transf = r.GetComponent<TransformComponent>(entity)->_transform;
            if (auto c = r.GetComponent<CCollider>(entity))
            {
                u32 index = 0u;
                f32 restitution = 0.6f;// 恢复系数
                if (_collisions.contains(entity))
                {
                    DebugDrawer::DebugWireframe(*c, transf, _collisions.contains(entity) ? Colors::kRed : Colors::kGreen);
                    return;
                }
                for (auto &other_c: r.View<CCollider>())
                {
                    auto other_enrity = r.GetEntity<CCollider>(index++);
                    if (other_enrity == entity)
                        return;
                    else
                    {
                        const CCollider &cur_c = *c;
                        const Matrix4x4f &cur_m = transf._world_matrix, other_m = r.GetComponent<TransformComponent>(other_enrity)->_transform._world_matrix;
                        auto rigid = r.GetComponent<CRigidBody>(entity);
                        if (SqrMagnitude(rigid->_velocity) < 1.f)
                            continue;
                        if (auto hit_result = _collision_matrix[c->_type][other_c._type](cur_c, cur_m, other_c, other_m); hit_result._is_collision)
                        {
                            LOG_INFO("pos: {}",transf._position.ToString());
                            _collisions.insert(entity);
                            _collisions.insert(other_enrity);
                            DebugDrawer::DebugWireframe(hit_result);
                            f32 v_before = rigid->_velocity.y;
                            f32 impulse = -(1.0f + restitution) * rigid->_mass * v_before;
                            f32 impact_force = impulse / 0.016f;
                            AddForce(r, entity, Vector3f(0.0f, impact_force, 0.0f), hit_result._point);
                            //AddForce(r, entity, -kGravity * 0.1, hit_result._point);
                            // --- 位置修正部分 ---
                            // 获取法线方向和穿透深度
                            Vector3f collision_normal = hit_result._normal;
                            f32 penetration_depth = hit_result._penetration;

                            // 设置修正比例（可以根据具体情况调整）
                            f32 correction_ratio = 0.5f;// 将修正分摊到两个物体上
                            Vector3f correction = penetration_depth * correction_ratio * collision_normal;

                            // 将物体沿法线方向修正
                            transf._position += correction;

                            // 更新世界矩阵
                            Transform::ToMatrix(transf, transf._world_matrix);
                        }
                    }
                }
                DebugDrawer::DebugWireframe(*c, transf, _collisions.contains(entity) ? Colors::kRed : Colors::kGreen);
            }
        }
    }
}