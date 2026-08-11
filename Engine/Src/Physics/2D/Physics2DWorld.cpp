#include "pch.h"
#include "Framework/Common/Profiler.h"
#include "Physics/2D/Physics2DComponents.h"
#include "Physics/2D/Physics2DWorld.h"
#include "Render/Gizmo.h"
#include "Scene/Component.h"

#include <box2d/box2d.h>

namespace Ailu
{
    namespace
    {
        b2BodyType ToBox2DBodyType(ECS::EBody2DType type)
        {
            switch (type)
            {
            case ECS::EBody2DType::kKinematic: return b2_kinematicBody;
            case ECS::EBody2DType::kDynamic: return b2_dynamicBody;
            default: return b2_staticBody;
            }
        }

        f32 GetPhysicsAngle(const Quaternion &rotation)
        {
            return Quaternion::EulerAngles(rotation).z * Math::k2Radius;
        }

        b2Vec2 Rotate(const b2Vec2 &value, f32 angle)
        {
            const f32 sin_angle = std::sin(angle);
            const f32 cos_angle = std::cos(angle);
            return b2Vec2{cos_angle * value.x - sin_angle * value.y, sin_angle * value.x + cos_angle * value.y};
        }
    }

    struct Physics2DWorld::Impl
    {
        struct ShapeRuntime
        {
            ECS::Entity _entity = ECS::kInvalidEntity;
            u16 _shape_index = 0u;
            u8 _layer = 0u;
            bool _is_trigger = false;
        };

        struct BodyRuntime
        {
            b2BodyId _body_id = b2_nullBodyId;
            Vector<b2ShapeId> _shape_ids;
            Vector<Scope<ShapeRuntime>> _shape_data;
        };

        b2WorldId _world_id = b2_nullWorldId;
        std::unordered_map<ECS::Entity, BodyRuntime> _bodies;
        Vector<PhysicsContact2D> _pending_events;
        Array<u32, 32> _layer_collision_masks = [] {
            Array<u32, 32> masks{};
            masks.fill(0xffffffffu);
            return masks;
        }();
    };

    Physics2DWorld::Physics2DWorld() : _impl(MakeScope<Impl>())
    {
        Initialize();
    }

    Physics2DWorld::~Physics2DWorld()
    {
        Shutdown();
    }

    void Physics2DWorld::Initialize()
    {
        if (IsInitialized())
            return;

        b2WorldDef world_def = b2DefaultWorldDef();
        world_def.gravity = b2Vec2{0.0f, -9.8f};
        _impl->_world_id = b2CreateWorld(&world_def);
    }

    void Physics2DWorld::Shutdown()
    {
        if (!IsInitialized())
            return;

        _impl->_bodies.clear();
        b2DestroyWorld(_impl->_world_id);
        _impl->_world_id = b2_nullWorldId;
    }

    void Physics2DWorld::Sync(ECS::Register &r, const std::set<ECS::Entity> &entities)
    {
        PROFILE_BLOCK_CPU("Physics2DWorld::Sync")
        if (!IsInitialized())
            return;

        for (ECS::Entity entity : entities)
        {
            const auto *rigid_body = r.GetComponent<ECS::RigidBody2DComponent>(entity);
            const b2BodyType desired_type = rigid_body != nullptr ? ToBox2DBodyType(rigid_body->_type) : b2_staticBody;
            if (!IsValidBody(entity))
            {
                CreateBody(r, entity);
                continue;
            }

            const b2BodyId body_id = _impl->_bodies.at(entity)._body_id;
            if (b2Body_GetType(body_id) != desired_type)
            {
                DestroyBody(entity);
                CreateBody(r, entity);
                continue;
            }

            if (desired_type == b2_staticBody)
            {
                const auto *transform = r.GetComponent<ECS::TransformComponent>(entity);
                if (transform != nullptr)
                {
                    const Vector3f &position = transform->_position;
                    b2Body_SetTransform(body_id, b2Vec2{position.x, position.y},
                                        b2MakeRot(GetPhysicsAngle(transform->_rotation)));
                }
            }
        }

        Vector<ECS::Entity> entities_to_destroy;
        for (const auto &[entity, body_runtime] : _impl->_bodies)
        {
            if (!r.IsAlive(entity) || !entities.contains(entity))
                entities_to_destroy.push_back(entity);
        }
        for (ECS::Entity entity : entities_to_destroy)
            DestroyBody(entity);
    }

    void Physics2DWorld::CreateBody(ECS::Register &r, ECS::Entity entity)
    {
        if (!IsInitialized() || IsValidBody(entity))
            return;

        const auto *transform = r.GetComponent<ECS::TransformComponent>(entity);
        const auto *collider = r.GetComponent<ECS::Collider2DComponent>(entity);
        if (transform == nullptr || collider == nullptr)
            return;

        const auto *rigid_body = r.GetComponent<ECS::RigidBody2DComponent>(entity);
        b2BodyDef body_def = b2DefaultBodyDef();
        body_def.type = rigid_body != nullptr ? ToBox2DBodyType(rigid_body->_type) : b2_staticBody;
        const Vector3f &body_position = body_def.type == b2_staticBody ? transform->_position
                                                                         : transform->_local_transform._position;
        const Quaternion &body_rotation = body_def.type == b2_staticBody ? transform->_rotation
                                                                          : transform->_local_transform._rotation;
        body_def.position = b2Vec2{body_position.x, body_position.y};
        body_def.rotation = b2MakeRot(GetPhysicsAngle(body_rotation));
        if (rigid_body != nullptr)
        {
            body_def.gravityScale = rigid_body->_gravity_scale;
            body_def.linearDamping = rigid_body->_linear_damping;
            body_def.angularDamping = rigid_body->_angular_damping;
            body_def.motionLocks.angularZ = rigid_body->_fixed_rotation;
            body_def.isBullet = rigid_body->_continuous;
            body_def.enableSleep = rigid_body->_allow_sleep;
        }

        Impl::BodyRuntime body_runtime;
        body_runtime._body_id = b2CreateBody(_impl->_world_id, &body_def);
        for (u16 shape_index = 0u; shape_index < collider->_shapes.size(); ++shape_index)
        {
            const ECS::ColliderShape2D &shape = collider->_shapes[shape_index];
            if (shape._type == ECS::ECollider2DShape::kPolygon || shape._type == ECS::ECollider2DShape::kChain)
            {
                LOG_WARNING("Physics2DWorld::CreateBody: entity {} shape {} is not supported in phase 1", entity, shape_index);
                continue;
            }
            if ((shape._type == ECS::ECollider2DShape::kBox &&
                 (shape._size.x <= 0.0f || shape._size.y <= 0.0f)) ||
                ((shape._type == ECS::ECollider2DShape::kCircle || shape._type == ECS::ECollider2DShape::kCapsule) &&
                 shape._radius <= 0.0f))
            {
                LOG_WARNING("Physics2DWorld::CreateBody: entity {} shape {} has invalid dimensions", entity, shape_index);
                continue;
            }
            if (shape._layer >= 32u)
            {
                LOG_WARNING("Physics2DWorld::CreateBody: entity {} shape {} has invalid layer {}", entity, shape_index,
                            shape._layer);
                continue;
            }

            b2ShapeDef shape_def = b2DefaultShapeDef();
            shape_def.density = std::max(shape._density, 0.0f);
            shape_def.material.friction = std::max(shape._friction, 0.0f);
            shape_def.material.restitution = std::max(shape._restitution, 0.0f);
            shape_def.isSensor = shape._is_trigger;
            shape_def.enableSensorEvents = shape._is_trigger;
            shape_def.enableContactEvents = !shape._is_trigger;
            shape_def.filter.categoryBits = 1ull << shape._layer;
            shape_def.filter.maskBits = _impl->_layer_collision_masks[shape._layer];
            auto shape_data = MakeScope<Impl::ShapeRuntime>();
            shape_data->_entity = entity;
            shape_data->_shape_index = shape_index;
            shape_data->_layer = shape._layer;
            shape_data->_is_trigger = shape._is_trigger;
            shape_def.userData = shape_data.get();

            b2ShapeId shape_id = b2_nullShapeId;
            const b2Vec2 center{shape._center.x, shape._center.y};
            switch (shape._type)
            {
            case ECS::ECollider2DShape::kBox:
            {
                const b2Polygon box = b2MakeOffsetBox(std::max(shape._size.x, 0.0f) * 0.5f,
                                                       std::max(shape._size.y, 0.0f) * 0.5f, center,
                                                       b2MakeRot(shape._rotation));
                shape_id = b2CreatePolygonShape(body_runtime._body_id, &shape_def,
                                                 &box);
                break;
            }
            case ECS::ECollider2DShape::kCircle:
            {
                const b2Circle circle{center, std::max(shape._radius, 0.0f)};
                shape_id = b2CreateCircleShape(body_runtime._body_id, &shape_def, &circle);
                break;
            }
            case ECS::ECollider2DShape::kCapsule:
            {
                const f32 radius = std::max(shape._radius, 0.0f);
                const f32 half_segment = std::max(shape._height * 0.5f - radius, 0.0f);
                const b2Vec2 offset = Rotate(b2Vec2{0.0f, half_segment}, shape._rotation);
                const b2Capsule capsule{b2Vec2{center.x - offset.x, center.y - offset.y},
                                        b2Vec2{center.x + offset.x, center.y + offset.y}, radius};
                shape_id = b2CreateCapsuleShape(body_runtime._body_id, &shape_def, &capsule);
                break;
            }
            default: break;
            }

            if (b2Shape_IsValid(shape_id))
            {
                body_runtime._shape_ids.push_back(shape_id);
                body_runtime._shape_data.push_back(std::move(shape_data));
            }
        }
        _impl->_bodies.emplace(entity, std::move(body_runtime));
    }

    void Physics2DWorld::DestroyBody(ECS::Entity entity)
    {
        const auto it = _impl->_bodies.find(entity);
        if (it == _impl->_bodies.end())
            return;

        if (b2Body_IsValid(it->second._body_id))
            b2DestroyBody(it->second._body_id);
        _impl->_bodies.erase(it);
    }

    void Physics2DWorld::SetPosition(ECS::Entity entity, const Vector2f &position)
    {
        const auto it = _impl->_bodies.find(entity);
        if (it == _impl->_bodies.end() || !b2Body_IsValid(it->second._body_id))
            return;
        b2Body_SetTransform(it->second._body_id, b2Vec2{position.x, position.y}, b2Body_GetRotation(it->second._body_id));
    }

    Vector2f Physics2DWorld::GetPosition(ECS::Entity entity) const
    {
        const auto it = _impl->_bodies.find(entity);
        if (it == _impl->_bodies.end() || !b2Body_IsValid(it->second._body_id))
            return Vector2f::kZero;
        const b2Vec2 position = b2Body_GetPosition(it->second._body_id);
        return Vector2f(position.x, position.y);
    }

    void Physics2DWorld::SetLinearVelocity(ECS::Entity entity, const Vector2f &velocity)
    {
        const auto it = _impl->_bodies.find(entity);
        if (it != _impl->_bodies.end() && b2Body_IsValid(it->second._body_id))
            b2Body_SetLinearVelocity(it->second._body_id, b2Vec2{velocity.x, velocity.y});
    }

    Vector2f Physics2DWorld::GetLinearVelocity(ECS::Entity entity) const
    {
        const auto it = _impl->_bodies.find(entity);
        if (it == _impl->_bodies.end() || !b2Body_IsValid(it->second._body_id))
            return Vector2f::kZero;
        const b2Vec2 velocity = b2Body_GetLinearVelocity(it->second._body_id);
        return Vector2f(velocity.x, velocity.y);
    }

    void Physics2DWorld::SetAngularVelocity(ECS::Entity entity, f32 velocity)
    {
        const auto it = _impl->_bodies.find(entity);
        if (it != _impl->_bodies.end() && b2Body_IsValid(it->second._body_id))
            b2Body_SetAngularVelocity(it->second._body_id, velocity);
    }

    void Physics2DWorld::AddForce(ECS::Entity entity, const Vector2f &force)
    {
        const auto it = _impl->_bodies.find(entity);
        if (it != _impl->_bodies.end() && b2Body_IsValid(it->second._body_id))
            b2Body_ApplyForceToCenter(it->second._body_id, b2Vec2{force.x, force.y}, true);
    }

    void Physics2DWorld::AddImpulse(ECS::Entity entity, const Vector2f &impulse)
    {
        const auto it = _impl->_bodies.find(entity);
        if (it != _impl->_bodies.end() && b2Body_IsValid(it->second._body_id))
            b2Body_ApplyLinearImpulseToCenter(it->second._body_id, b2Vec2{impulse.x, impulse.y}, true);
    }

    void Physics2DWorld::SetLayerCollision(u8 layer_a, u8 layer_b, bool enabled)
    {
        if (layer_a >= 32u || layer_b >= 32u)
        {
            LOG_WARNING("Physics2DWorld::SetLayerCollision: invalid layers {} and {}", layer_a, layer_b);
            return;
        }

        const u32 bit_a = 1u << layer_a;
        const u32 bit_b = 1u << layer_b;
        if (enabled)
        {
            _impl->_layer_collision_masks[layer_a] |= bit_b;
            _impl->_layer_collision_masks[layer_b] |= bit_a;
        }
        else
        {
            _impl->_layer_collision_masks[layer_a] &= ~bit_b;
            _impl->_layer_collision_masks[layer_b] &= ~bit_a;
        }

        for (const auto &[entity, body_runtime] : _impl->_bodies)
        {
            for (b2ShapeId shape_id : body_runtime._shape_ids)
            {
                const auto *shape = static_cast<const Impl::ShapeRuntime *>(b2Shape_GetUserData(shape_id));
                if (shape == nullptr || shape->_layer != layer_a && shape->_layer != layer_b)
                    continue;
                b2Filter filter = b2Shape_GetFilter(shape_id);
                filter.maskBits = _impl->_layer_collision_masks[shape->_layer];
                b2Shape_SetFilter(shape_id, filter);
            }
        }
    }

    bool Physics2DWorld::CanLayersCollide(u8 layer_a, u8 layer_b) const
    {
        return layer_a < 32u && layer_b < 32u && (_impl->_layer_collision_masks[layer_a] & (1u << layer_b)) != 0u;
    }

    void Physics2DWorld::SetLayerCollisionMasks(const Vector<u32> &layer_collision_masks)
    {
        if (layer_collision_masks.size() != _impl->_layer_collision_masks.size())
        {
            LOG_WARNING("Physics2DWorld::SetLayerCollisionMasks: expected {} layers, got {}",
                        _impl->_layer_collision_masks.size(), layer_collision_masks.size());
            return;
        }

        for (u8 layer = 0u; layer < _impl->_layer_collision_masks.size(); ++layer)
        {
            if (_impl->_layer_collision_masks[layer] == layer_collision_masks[layer])
                continue;
            for (const auto &[entity, body_runtime] : _impl->_bodies)
            {
                for (b2ShapeId shape_id : body_runtime._shape_ids)
                {
                    const auto *shape = static_cast<const Impl::ShapeRuntime *>(b2Shape_GetUserData(shape_id));
                    if (shape == nullptr || shape->_layer != layer)
                        continue;
                    b2Filter filter = b2Shape_GetFilter(shape_id);
                    filter.maskBits = layer_collision_masks[layer];
                    b2Shape_SetFilter(shape_id, filter);
                }
            }
            _impl->_layer_collision_masks[layer] = layer_collision_masks[layer];
        }
    }

    bool Physics2DWorld::Raycast(const Raycast2DDesc &desc, RaycastHit2D &hit) const
    {
        Vector<RaycastHit2D> hits;
        RaycastAll(desc, hits);
        if (hits.empty())
            return false;
        hit = hits.front();
        return true;
    }

    void Physics2DWorld::RaycastAll(const Raycast2DDesc &desc, Vector<RaycastHit2D> &hits) const
    {
        hits.clear();
        const f32 direction_length = Magnitude(desc._direction);
        if (!IsInitialized() || direction_length <= std::numeric_limits<f32>::epsilon() || desc._distance <= 0.0f)
            return;

        struct QueryContext
        {
            const PhysicsQueryFilter *_filter = nullptr;
            f32 _distance = 0.0f;
            Vector<RaycastHit2D> *_hits = nullptr;
        } context{&desc._filter, desc._distance, &hits};
        const auto callback = [](b2ShapeId shape_id, b2Pos point, b2Vec2 normal, float fraction, void *user_data) {
            auto &query = *static_cast<QueryContext *>(user_data);
            const auto *shape = static_cast<const Impl::ShapeRuntime *>(b2Shape_GetUserData(shape_id));
            if (shape == nullptr || (!query._filter->_hit_trigger && shape->_is_trigger) ||
                (query._filter->_layer_mask & (1u << shape->_layer)) == 0u)
                return -1.0f;
            query._hits->push_back(RaycastHit2D{shape->_entity, shape->_shape_index, Vector2f(point.x, point.y),
                                                Vector2f(normal.x, normal.y), query._distance * fraction, fraction});
            return 1.0f;
        };

        const Vector2f translation = Normalize(desc._direction) * desc._distance;
        const b2QueryFilter filter = b2DefaultQueryFilter();
        b2World_CastRay(_impl->_world_id, b2Pos{desc._origin.x, desc._origin.y},
                         b2Vec2{translation.x, translation.y}, filter, callback, &context);
        std::ranges::sort(hits, {}, &RaycastHit2D::_fraction);
    }

    bool Physics2DWorld::CircleCast(const CircleCast2DDesc &desc, ShapeCastHit2D &hit) const
    {
        const f32 direction_length = Magnitude(desc._direction);
        if (!IsInitialized() || direction_length <= std::numeric_limits<f32>::epsilon() || desc._distance <= 0.0f ||
            desc._radius <= 0.0f)
            return false;

        struct QueryContext
        {
            const PhysicsQueryFilter *_filter = nullptr;
            f32 _distance = 0.0f;
            ShapeCastHit2D *_hit = nullptr;
            bool _has_hit = false;
        } context{&desc._filter, desc._distance, &hit};
        const auto callback = [](b2ShapeId shape_id, b2Pos point, b2Vec2 normal, float fraction, void *user_data) {
            auto &query = *static_cast<QueryContext *>(user_data);
            const auto *shape = static_cast<const Impl::ShapeRuntime *>(b2Shape_GetUserData(shape_id));
            if (shape == nullptr || (!query._filter->_hit_trigger && shape->_is_trigger) ||
                (query._filter->_layer_mask & (1u << shape->_layer)) == 0u)
                return -1.0f;
            *query._hit = ShapeCastHit2D{shape->_entity, shape->_shape_index, Vector2f(point.x, point.y),
                                         Vector2f(normal.x, normal.y), query._distance * fraction, fraction};
            query._has_hit = true;
            return fraction;
        };

        b2ShapeProxy proxy{};
        proxy.points[0] = b2Vec2{0.0f, 0.0f};
        proxy.count = 1;
        proxy.radius = desc._radius;
        const Vector2f translation = Normalize(desc._direction) * desc._distance;
        const b2QueryFilter filter = b2DefaultQueryFilter();
        b2World_CastShape(_impl->_world_id, b2Pos{desc._origin.x, desc._origin.y}, &proxy,
                          b2Vec2{translation.x, translation.y}, filter, callback, &context);
        return context._has_hit;
    }

    void Physics2DWorld::OverlapCircle(const OverlapCircle2DDesc &desc, Vector<OverlapHit2D> &hits) const
    {
        hits.clear();
        if (!IsInitialized() || desc._radius <= 0.0f)
            return;

        struct QueryContext
        {
            const PhysicsQueryFilter *_filter = nullptr;
            Vector<OverlapHit2D> *_hits = nullptr;
        } context{&desc._filter, &hits};
        const auto callback = [](b2ShapeId shape_id, void *user_data) {
            auto &query = *static_cast<QueryContext *>(user_data);
            const auto *shape = static_cast<const Impl::ShapeRuntime *>(b2Shape_GetUserData(shape_id));
            if (shape == nullptr || (!query._filter->_hit_trigger && shape->_is_trigger) ||
                (query._filter->_layer_mask & (1u << shape->_layer)) == 0u)
                return true;
            query._hits->push_back(OverlapHit2D{shape->_entity, shape->_shape_index});
            return true;
        };

        b2ShapeProxy proxy{};
        proxy.points[0] = b2Vec2{0.0f, 0.0f};
        proxy.count = 1;
        proxy.radius = desc._radius;
        const b2QueryFilter filter = b2DefaultQueryFilter();
        b2World_OverlapShape(_impl->_world_id, b2Pos{desc._center.x, desc._center.y}, &proxy, filter, callback, &context);
    }

    void Physics2DWorld::OverlapBox(const OverlapBox2DDesc &desc, Vector<OverlapHit2D> &hits) const
    {
        hits.clear();
        if (!IsInitialized() || desc._size.x <= 0.0f || desc._size.y <= 0.0f)
            return;

        struct QueryContext
        {
            const PhysicsQueryFilter *_filter = nullptr;
            Vector<OverlapHit2D> *_hits = nullptr;
        } context{&desc._filter, &hits};
        const auto callback = [](b2ShapeId shape_id, void *user_data) {
            auto &query = *static_cast<QueryContext *>(user_data);
            const auto *shape = static_cast<const Impl::ShapeRuntime *>(b2Shape_GetUserData(shape_id));
            if (shape == nullptr || (!query._filter->_hit_trigger && shape->_is_trigger) ||
                (query._filter->_layer_mask & (1u << shape->_layer)) == 0u)
                return true;
            query._hits->push_back(OverlapHit2D{shape->_entity, shape->_shape_index});
            return true;
        };

        const f32 half_width = desc._size.x * 0.5f;
        const f32 half_height = desc._size.y * 0.5f;
        b2ShapeProxy proxy{};
        const b2Vec2 points[] = {{-half_width, -half_height}, {half_width, -half_height},
                                 {half_width, half_height}, {-half_width, half_height}};
        for (int index = 0; index < 4; ++index)
            proxy.points[index] = Rotate(points[index], desc._rotation);
        proxy.count = 4;
        proxy.radius = 0.0f;
        const b2QueryFilter filter = b2DefaultQueryFilter();
        b2World_OverlapShape(_impl->_world_id, b2Pos{desc._center.x, desc._center.y}, &proxy, filter, callback, &context);
    }

    void Physics2DWorld::Step(f32 fixed_delta_time)
    {
        PROFILE_BLOCK_CPU("Physics2DWorld::Step")
        if (!IsInitialized() || fixed_delta_time <= 0.0f)
            return;

        b2World_Step(_impl->_world_id, fixed_delta_time, 4);

        auto append_event = [this](b2ShapeId shape_a, b2ShapeId shape_b, EPhysicsContact2DType type) {
            if (!b2Shape_IsValid(shape_a) || !b2Shape_IsValid(shape_b))
                return;
            const auto *data_a = static_cast<const Impl::ShapeRuntime *>(b2Shape_GetUserData(shape_a));
            const auto *data_b = static_cast<const Impl::ShapeRuntime *>(b2Shape_GetUserData(shape_b));
            if (data_a == nullptr || data_b == nullptr)
                return;

            _impl->_pending_events.push_back(PhysicsContact2D{type, data_a->_entity, data_b->_entity,
                                                               data_a->_shape_index, data_b->_shape_index});
        };

        const b2SensorEvents sensor_events = b2World_GetSensorEvents(_impl->_world_id);
        for (int index = 0; index < sensor_events.beginCount; ++index)
        {
            const b2SensorBeginTouchEvent &event = sensor_events.beginEvents[index];
            append_event(event.sensorShapeId, event.visitorShapeId, EPhysicsContact2DType::kTriggerBegin);
        }
        for (int index = 0; index < sensor_events.endCount; ++index)
        {
            const b2SensorEndTouchEvent &event = sensor_events.endEvents[index];
            append_event(event.sensorShapeId, event.visitorShapeId, EPhysicsContact2DType::kTriggerEnd);
        }

        const b2ContactEvents contact_events = b2World_GetContactEvents(_impl->_world_id);
        for (int index = 0; index < contact_events.beginCount; ++index)
        {
            const b2ContactBeginTouchEvent &event = contact_events.beginEvents[index];
            append_event(event.shapeIdA, event.shapeIdB, EPhysicsContact2DType::kCollisionBegin);
        }
        for (int index = 0; index < contact_events.endCount; ++index)
        {
            const b2ContactEndTouchEvent &event = contact_events.endEvents[index];
            append_event(event.shapeIdA, event.shapeIdB, EPhysicsContact2DType::kCollisionEnd);
        }
    }

    void Physics2DWorld::FlushEvents()
    {
        PROFILE_BLOCK_CPU("Physics2DWorld::FlushEvents")
        for (const PhysicsContact2D &event : _impl->_pending_events)
            _OnContact_delegate.Invoke(event);
        _impl->_pending_events.clear();
    }

    void Physics2DWorld::DebugDraw(ECS::Register &r) const
    {
        if (!_debug_draw_enabled)
            return;

        for (const auto &[entity, body_runtime] : _impl->_bodies)
        {
            const b2BodyType body_type = b2Body_GetType(body_runtime._body_id);
            const Color body_color = body_type == b2_staticBody ? Math::Colors::kGreen
                                     : body_type == b2_kinematicBody ? Math::Colors::kCyan
                                                                    : Math::Colors::kBlue;
            DebugDrawCollider(r, entity, body_color);
        }
    }

    void Physics2DWorld::DebugDrawCollider(ECS::Register &r, ECS::Entity entity, Color color) const
    {
        const auto body_it = _impl->_bodies.find(entity);
        if (body_it == _impl->_bodies.end() || !b2Body_IsValid(body_it->second._body_id))
            return;

        const auto *transform = r.GetComponent<ECS::TransformComponent>(entity);
        const auto *collider = r.GetComponent<ECS::Collider2DComponent>(entity);
        if (transform == nullptr || collider == nullptr)
            return;

        const b2BodyId body_id = body_it->second._body_id;
        const b2Vec2 body_position = b2Body_GetPosition(body_id);
        const f32 body_angle = b2Rot_GetAngle(b2Body_GetRotation(body_id));
        const f32 z = transform->_position.z;
        for (const ECS::ColliderShape2D &shape : collider->_shapes)
        {
            const Color shape_color = shape._is_trigger ? Math::Colors::kYellow : color;
            const b2Vec2 local_center{shape._center.x, shape._center.y};
            const b2Vec2 center_offset = Rotate(local_center, body_angle);
            const Vector3f center(body_position.x + center_offset.x, body_position.y + center_offset.y, z);
            if (shape._type == ECS::ECollider2DShape::kCircle)
            {
                Render::Gizmo::DrawCircle(center, shape._radius, Render::Gizmo::kSegments, shape_color,
                                           MatrixRotationX(Math::kHalfPi));
                continue;
            }

            if (shape._type == ECS::ECollider2DShape::kCapsule)
            {
                const f32 half_segment = std::max(shape._height * 0.5f - shape._radius, 0.0f);
                const b2Vec2 offset = Rotate(b2Vec2{0.0f, half_segment}, body_angle + shape._rotation);
                const Vector3f point_a(center.x - offset.x, center.y - offset.y, z);
                const Vector3f point_b(center.x + offset.x, center.y + offset.y, z);
                Render::Gizmo::DrawCircle(point_a, shape._radius, Render::Gizmo::kSegments, shape_color,
                                           MatrixRotationX(Math::kHalfPi));
                Render::Gizmo::DrawCircle(point_b, shape._radius, Render::Gizmo::kSegments, shape_color,
                                           MatrixRotationX(Math::kHalfPi));
                const b2Vec2 side = Rotate(b2Vec2{shape._radius, 0.0f}, body_angle + shape._rotation);
                Render::Gizmo::DrawLine(point_a + Vector3f(side.x, side.y, 0.0f),
                                         point_b + Vector3f(side.x, side.y, 0.0f), shape_color);
                Render::Gizmo::DrawLine(point_a - Vector3f(side.x, side.y, 0.0f),
                                         point_b - Vector3f(side.x, side.y, 0.0f), shape_color);
                continue;
            }

            if (shape._type == ECS::ECollider2DShape::kBox)
            {
                const f32 half_width = shape._size.x * 0.5f;
                const f32 half_height = shape._size.y * 0.5f;
                const b2Vec2 corners[] = {{-half_width, -half_height}, {half_width, -half_height},
                                           {half_width, half_height}, {-half_width, half_height}};
                Vector3f points[4];
                for (u32 index = 0u; index < 4u; ++index)
                {
                    const b2Vec2 point = Rotate(corners[index], body_angle + shape._rotation);
                    points[index] = center + Vector3f(point.x, point.y, 0.0f);
                }
                for (u32 index = 0u; index < 4u; ++index)
                    Render::Gizmo::DrawLine(points[index], points[(index + 1u) % 4u], shape_color);
            }
        }
    }

    void Physics2DWorld::SyncTransforms(ECS::Register &r)
    {
        for (const auto &[entity, body_runtime] : _impl->_bodies)
        {
            if (!r.IsAlive(entity) || !b2Body_IsValid(body_runtime._body_id) ||
                b2Body_GetType(body_runtime._body_id) == b2_staticBody)
                continue;

            auto *transform = r.GetComponent<ECS::TransformComponent>(entity);
            if (transform == nullptr)
                continue;

            const b2Vec2 position = b2Body_GetPosition(body_runtime._body_id);
            const f32 angle = b2Rot_GetAngle(b2Body_GetRotation(body_runtime._body_id));
            Vector3f local_position = transform->_local_transform._position;
            local_position.x = position.x;
            local_position.y = position.y;
            transform->SetLocalPosition(local_position);
            transform->SetLocalRotation(Quaternion::RadiusAxis(angle, Vector3f::kForward));
        }
    }

    bool Physics2DWorld::IsInitialized() const
    {
        return _impl != nullptr && b2World_IsValid(_impl->_world_id);
    }

    bool Physics2DWorld::IsValidBody(ECS::Entity entity) const
    {
        const auto it = _impl->_bodies.find(entity);
        return it != _impl->_bodies.end() && b2Body_IsValid(it->second._body_id);
    }
}
