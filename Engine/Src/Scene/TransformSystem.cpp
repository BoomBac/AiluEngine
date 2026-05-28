#include "Scene/TransformSystem.h"
#include "Framework/Common/Profiler.h"
#include "Scene/Component.h"
#include "pch.h"


namespace Ailu
{
    namespace ECS
    {
        TransformSystem::TransformSystem()
        {
        }

        void TransformSystem::OnPushEntity(Entity entity)
        {
            _order_dirty = true;
        }

        void TransformSystem::AppendSubtree(Register &r, Entity entity, std::unordered_set<Entity> &visited, std::unordered_set<Entity> &visiting)
        {
            if (!_entities.contains(entity) || !r.IsAlive(entity))
                return;
            if (visited.contains(entity))
                return;
            if (visiting.contains(entity))
            {
                LOG_ERROR("TransformSystem::RebuildOrder detected hierarchy cycle at entity {}", entity);
                return;
            }

            visiting.insert(entity);
            visited.insert(entity);
            _ordered_entities.push_back(entity);

            const auto *hier = r.GetComponent<CHierarchy>(entity);
            Entity child = hier != nullptr ? hier->_first_child : kInvalidEntity;
            u32 child_count = 0u;
            while (child != kInvalidEntity)
            {
                if (++child_count > ECS::kMaxEntityNum)
                {
                    LOG_ERROR("TransformSystem::RebuildOrder sibling chain exceeded max entity count at parent {}", entity);
                    break;
                }

                const auto *child_hier = r.GetComponent<CHierarchy>(child);
                const Entity next = child_hier != nullptr ? child_hier->_next_sibling : kInvalidEntity;
                AppendSubtree(r, child, visited, visiting);
                child = next;
            }

            visiting.erase(entity);
        }

        void TransformSystem::RebuildOrder(Register &r)
        {
            _ordered_entities.clear();
            _ordered_entities.reserve(_entities.size());

            std::unordered_set<Entity> visited;
            std::unordered_set<Entity> visiting;
            visited.reserve(_entities.size());
            visiting.reserve(_entities.size());

            for (Entity entity: _entities)
            {
                if (!r.IsAlive(entity))
                    continue;

                const auto *hier = r.GetComponent<CHierarchy>(entity);
                const bool is_root = hier == nullptr ||
                                     hier->_parent == kInvalidEntity ||
                                     !r.IsAlive(hier->_parent) ||
                                     !_entities.contains(hier->_parent);
                if (is_root)
                    AppendSubtree(r, entity, visited, visiting);
            }

            for (Entity entity: _entities)
            {
                if (r.IsAlive(entity) && !visited.contains(entity))
                {
                    LOG_ERROR("TransformSystem::RebuildOrder found unreachable or cyclic transform entity {}", entity);
                    _ordered_entities.push_back(entity);
                }
            }

            _observed_hierarchy_revision = r.HierarchyRevision();
            _order_dirty = false;
        }

        void TransformSystem::Update(Register &r, f32 delta_time)
        {
            PROFILE_BLOCK_CPU(TransformSystem_Update)
            if (_order_dirty || _observed_hierarchy_revision != r.HierarchyRevision() || _ordered_entities.size() != _entities.size())
                RebuildOrder(r);

            for (Entity entity: _ordered_entities)
            {
                auto *transform = r.GetComponent<TransformComponent>(entity);
                if (transform == nullptr)
                    continue;

                transform->_prev_world_matrix = transform->_world_matrix;
                Transform::ToMatrix(transform->_local_transform, transform->_local_matrix);

                const auto *hier = r.GetComponent<CHierarchy>(entity);
                TransformComponent *parent_transform = nullptr;
                if (hier != nullptr && hier->_parent != kInvalidEntity && r.IsAlive(hier->_parent))
                    parent_transform = r.GetComponent<TransformComponent>(hier->_parent);

                if (parent_transform != nullptr)
                {
                    transform->_world_matrix = transform->_local_matrix * parent_transform->_world_matrix;
                    transform->_cached_parent_world_version = parent_transform->_world_version;

                    Transform parent_world(parent_transform->_position,parent_transform->_rotation,parent_transform->_scale);
                    Transform world = Transform::Combine(parent_world, transform->_local_transform);
                    transform->_position = world._position;
                    transform->_rotation = world._rotation;
                    transform->_scale = world._scale;
                }
                else
                {
                    transform->_world_matrix = transform->_local_matrix;
                    transform->_cached_parent_world_version = TransformComponent::kInvalidVersion;
                    transform->_position = transform->_local_transform._position;
                    transform->_rotation = transform->_local_transform._rotation;
                    transform->_scale = transform->_local_transform._scale;
                }

                ++transform->_local_version;
                ++transform->_world_version;
                transform->_local_dirty = false;
                transform->_world_dirty = false;
                transform->_world_to_local_dirty = true;
            }
        }
    }// namespace ECS
}// namespace Ailu
