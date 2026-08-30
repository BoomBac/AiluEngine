#include "Scene/Entity.h"
#include "Scene/Component.h"
#include "pch.h"
#include <algorithm>

namespace Ailu
{
    namespace ECS
    {
        // ---------------------------------------------------------------------------
        // Vector-size helpers
        // ---------------------------------------------------------------------------

        void Register::EnsureMgrVector(u32 type_id)
        {
            if (type_id >= static_cast<u32>(_mgrs.size()))
                _mgrs.resize(type_id + 1);
        }

        void Register::EnsureCallbackVectors(u32 type_id)
        {
            if (type_id >= static_cast<u32>(_on_comp_add_callback.size()))
            {
                _on_comp_add_callback.resize(type_id + 1);
                _on_comp_remove_callback.resize(type_id + 1);
            }
        }

        void Register::EnsureSystemVectors(u32 sys_id)
        {
            if (sys_id >= static_cast<u32>(_systems.size()))
            {
                _systems.resize(sys_id + 1);
                _sys_signatures.resize(sys_id + 1);
            }
        }

        // ---------------------------------------------------------------------------
        // Register — special member functions
        // ---------------------------------------------------------------------------

        Register::Register(const Register &other)
            : _entity_num(other._entity_num),
              _hierarchy_revision(other._hierarchy_revision),
              _entity_signatures(other._entity_signatures),
              _entity_generations(other._entity_generations),
              _free_indices(other._free_indices),
              _system_schedule_dirty(true),
              _is_init(other._is_init)
        {
            _mgrs.resize(other._mgrs.size());
            for (u32 i = 0; i < static_cast<u32>(other._mgrs.size()); ++i)
            {
                if (other._mgrs[i])
                    _mgrs[i] = other._mgrs[i]->Clone();
            }
            _systems.resize(other._systems.size());
            for (u32 i = 0; i < static_cast<u32>(other._systems.size()); ++i)
            {
                if (other._systems[i])
                    _systems[i] = other._systems[i]->Clone();
            }
            _sys_signatures = other._sys_signatures;
            _on_comp_add_callback = other._on_comp_add_callback;
            _on_comp_remove_callback = other._on_comp_remove_callback;
        }

        Register::Register(Register &&other) noexcept
            : _mgrs(std::move(other._mgrs)),
              _systems(std::move(other._systems)),
              _sys_signatures(std::move(other._sys_signatures)),
              _entity_num(other._entity_num),
              _hierarchy_revision(other._hierarchy_revision),
              _entity_signatures(std::move(other._entity_signatures)),
              _entity_generations(std::move(other._entity_generations)),
              _free_indices(std::move(other._free_indices)),
              _system_schedule_dirty(true),
              _is_init(other._is_init),
              _on_comp_remove_callback(std::move(other._on_comp_remove_callback)),
              _on_comp_add_callback(std::move(other._on_comp_add_callback))
        {
            other._entity_num = static_cast<u32>(-1);
            other._is_init = false;
        }

        Register &Register::operator=(const Register &other)
        {
            if (this != &other)
            {
                _mgrs.resize(other._mgrs.size());
                for (u32 i = 0; i < static_cast<u32>(other._mgrs.size()); ++i)
                {
                    if (other._mgrs[i])
                        _mgrs[i] = other._mgrs[i]->Clone();
                    else
                        _mgrs[i] = nullptr;
                }
                _systems.resize(other._systems.size());
                for (u32 i = 0; i < static_cast<u32>(other._systems.size()); ++i)
                {
                    if (other._systems[i])
                        _systems[i] = other._systems[i]->Clone();
                    else
                        _systems[i] = nullptr;
                }
                _sys_signatures = other._sys_signatures;
                _entity_num = other._entity_num;
                _hierarchy_revision = other._hierarchy_revision;
                _entity_signatures = other._entity_signatures;
                _entity_generations = other._entity_generations;
                _free_indices = other._free_indices;
                _system_schedule_dirty = true;
                _is_init = other._is_init;
                _on_comp_add_callback = other._on_comp_add_callback;
                _on_comp_remove_callback = other._on_comp_remove_callback;
            }
            return *this;
        }

        Register &Register::operator=(Register &&other) noexcept
        {
            if (this != &other)
            {
                _mgrs = std::move(other._mgrs);
                _systems = std::move(other._systems);
                _sys_signatures = std::move(other._sys_signatures);
                _entity_num = other._entity_num;
                _hierarchy_revision = other._hierarchy_revision;
                _entity_signatures = std::move(other._entity_signatures);
                _entity_generations = std::move(other._entity_generations);
                _free_indices = std::move(other._free_indices);
                _system_schedule_dirty = true;
                _is_init = other._is_init;
                _on_comp_add_callback = std::move(other._on_comp_add_callback);
                _on_comp_remove_callback = std::move(other._on_comp_remove_callback);
                other._entity_num = static_cast<u32>(-1);
                other._is_init = false;
            }
            return *this;
        }

        bool Register::operator==(const Register &other) const
        {
            return _mgrs == other._mgrs &&
                   _systems == other._systems &&
                   _sys_signatures == other._sys_signatures &&
                   _entity_num == other._entity_num &&
                   _hierarchy_revision == other._hierarchy_revision &&
                   _entity_signatures == other._entity_signatures &&
                   _entity_generations == other._entity_generations &&
                   _free_indices == other._free_indices &&
                   _system_schedule_dirty == other._system_schedule_dirty &&
                   _is_init == other._is_init;
        }

        // ---------------------------------------------------------------------------
        // Entity lifecycle
        // ---------------------------------------------------------------------------

        Entity Register::Create()
        {
            if (!_is_init)
            {
                GrowPool(kDefaultEntityCapacity);
                _is_init = true;
            }
            if (_free_indices.empty())
            {
                u32 new_capacity = static_cast<u32>(_entity_generations.size()) * kEntityCapacityGrowFactor;
                if (new_capacity > kMaxEntityCapacity)
                    new_capacity = kMaxEntityCapacity;
                if (new_capacity <= static_cast<u32>(_entity_generations.size()))
                {
                    AL_ASSERT_MSG(false, "Entity pool exhausted! Max capacity reached.");
                    return kInvalidEntity;
                }
                GrowPool(new_capacity);
            }
            u32 idx = _free_indices.front();
            _free_indices.pop();
            u32 gen = _entity_generations[idx];
            _entity_signatures[idx].reset();
            ++_entity_num;
            return MakeEntity(idx, gen);
        }

        bool Register::IsAlive(Entity entity) const
        {
            u32 idx = EntityIndex(entity);
            return idx > 0
                && idx < static_cast<u32>(_entity_generations.size())
                && _entity_generations[idx] == EntityGeneration(entity)
                && _entity_generations[idx] != 0u;
        }

        Entity Register::GetAliveEntityByIndex(u32 index) const
        {
            if (index == 0u || index >= static_cast<u32>(_entity_generations.size()) || _entity_generations[index] == 0u ||
                !_entity_signatures[index].any())
                return kInvalidEntity;

            Entity entity = MakeEntity(index, _entity_generations[index]);
            return IsAlive(entity) ? entity : kInvalidEntity;
        }

        void Register::Destory(Entity entity)
        {
            if (!IsAlive(entity))
                return;
            u32 idx = EntityIndex(entity);

            // Touch hierarchy if entity has CHierarchy component
            ComponentTypeId hier_id = GetComponentTypeId<CHierarchy>();
            if (hier_id < static_cast<ComponentTypeId>(_mgrs.size()) && _mgrs[hier_id])
            {
                if (_entity_signatures[idx].test(hier_id))
                    TouchHierarchy();
            }

            for (u32 i = 0; i < static_cast<u32>(_mgrs.size()); ++i)
            {
                if (_mgrs[i])
                    _mgrs[i]->EntityDestroyed(entity);
            }

            for (u32 i = 0; i < static_cast<u32>(_systems.size()); ++i)
            {
                if (_systems[i])
                    _systems[i]->_entities.erase(entity);
            }
            _disabled_components.erase(entity);
            MarkSystemScheduleDirty();

            // Bump generation so old handles become stale
            ++_entity_generations[idx];
            if (_entity_generations[idx] == 0u)
                _entity_generations[idx] = 1u;  // generation 0 = dead/invalid
            _entity_signatures[idx].reset();
            --_entity_num;
            _free_indices.push(idx);
        }

        void Register::Destroy(Entity entity)
        {
            Destory(entity);
        }

        void Register::EntitySignatureChanged(Entity entity)
        {
            AL_ASSERT(IsAlive(entity));
            u32 idx = EntityIndex(entity);
            for (u32 i = 0; i < static_cast<u32>(_systems.size()); ++i)
            {
                if (!_systems[i])
                    continue;
                Signature acquired_sig = _sys_signatures[i];
                if ((acquired_sig & _entity_signatures[idx]) == acquired_sig)
                {
                    const auto [insert_it, inserted] = _systems[i]->_entities.insert(entity);
                    if (inserted)
                        _systems[i]->OnPushEntity(entity);
                }
                else
                    _systems[i]->_entities.erase(entity);
            }
        }

        // ---------------------------------------------------------------------------
        // Accessors
        // ---------------------------------------------------------------------------

        u32 Register::EntityNum() const { return _entity_num; }

        bool Register::HasComponentType(Entity entity, ComponentTypeId type_id) const
        {
            return GetComponentInstance(entity, type_id) != nullptr;
        }

        void *Register::GetComponentInstance(Entity entity, ComponentTypeId type_id) const
        {
            if (!IsAlive(entity) || type_id >= static_cast<u32>(_mgrs.size()))
                return nullptr;
            const auto &mgr = _mgrs[type_id];
            return mgr ? mgr->GetComponentPtr(entity) : nullptr;
        }

        Vector<ComponentTypeId> Register::GetEntityComponentTypes(Entity entity) const
        {
            Vector<ComponentTypeId> result;
            if (!IsAlive(entity))
                return result;
            for (u32 type_id = 0u; type_id < static_cast<u32>(_mgrs.size()); ++type_id)
            {
                if (_mgrs[type_id] && _mgrs[type_id]->GetComponentPtr(entity) != nullptr)
                    result.push_back(type_id);
            }
            return result;
        }

        bool Register::CopyComponent(Entity source, Entity target, ComponentTypeId type_id)
        {
            if (!IsAlive(source) || !IsAlive(target) || type_id >= static_cast<ComponentTypeId>(_mgrs.size()) ||
                !_mgrs[type_id] || !_mgrs[type_id]->GetComponentPtr(source))
                return false;

            const bool added = _mgrs[type_id]->CopyComponent(source, target);
            if (!added)
                return true;

            const u32 target_index = EntityIndex(target);
            _entity_signatures[target_index].set(type_id, true);
            if (type_id < static_cast<ComponentTypeId>(_on_comp_add_callback.size()))
            {
                for (auto &callback : _on_comp_add_callback[type_id])
                    callback(target);
            }
            EntitySignatureChanged(target);
            return true;
        }

        bool Register::RemoveComponentType(Entity entity, ComponentTypeId type_id)
        {
            if (!IsAlive(entity) || type_id >= static_cast<ComponentTypeId>(_mgrs.size()) || !_mgrs[type_id] ||
                !_mgrs[type_id]->GetComponentPtr(entity))
                return false;
            const u32 entity_index = EntityIndex(entity);
            if (type_id == CHierarchy::StaticComponentTypeId())
                TouchHierarchy();
            if (!_mgrs[type_id]->RemoveComponent(entity))
                return false;
            _entity_signatures[entity_index].set(type_id, false);
            if (const auto disabled_it = _disabled_components.find(entity); disabled_it != _disabled_components.end())
            {
                disabled_it->second.set(type_id, false);
                if (disabled_it->second.none())
                    _disabled_components.erase(disabled_it);
            }
            if (type_id < static_cast<ComponentTypeId>(_on_comp_remove_callback.size()))
            {
                for (auto &callback : _on_comp_remove_callback[type_id])
                    callback(entity);
            }
            EntitySignatureChanged(entity);
            return true;
        }

        u64 Register::HierarchyRevision() const { return _hierarchy_revision; }
        void Register::TouchHierarchy() { ++_hierarchy_revision; }
        bool Register::IsEntityEnabled(Entity entity) const
        {
            const auto *hierarchy = GetComponent<CHierarchy>(entity);
            return hierarchy != nullptr && hierarchy->_enabled_in_hierarchy;
        }
        void Register::MarkSystemScheduleDirty() { _system_schedule_dirty = true; }

        // ---------------------------------------------------------------------------
        // Deferred destruction
        // ---------------------------------------------------------------------------

        void Register::DeferredDestroy(Entity entity)
        {
            if (IsAlive(entity))
                _pending_destroy_queue.push_back(entity);
        }

        void Register::FlushDestroy()
        {
            if (_pending_destroy_queue.empty())
                return;
            for (Entity e: _pending_destroy_queue)
                Destory(e);
            _pending_destroy_queue.clear();
        }

        void Register::RebuildSystemSchedule()
        {
            for (auto &entries: _system_schedule)
                entries.clear();

            for (auto &system: _systems)
            {
                if (system == nullptr)
                    continue;

                SystemEntry entry;
                entry._system = system.get();
                entry._phase = system->GetPhase();
                entry._order = system->GetOrder();
                const u32 phase_index = static_cast<u32>(entry._phase);
                AL_ASSERT(phase_index < kSystemPhaseCount);
                _system_schedule[phase_index].emplace_back(entry);
            }

            for (auto &entries: _system_schedule)
            {
                std::stable_sort(entries.begin(), entries.end(), [](const SystemEntry &lhs, const SystemEntry &rhs)
                {
                    return lhs._order < rhs._order;
                });
            }

            _system_schedule_dirty = false;
        }

        void Register::ExecutePhase(ESystemPhase phase, f32 delta_time)
        {
            if (_system_schedule_dirty)
                RebuildSystemSchedule();

            const u32 phase_index = static_cast<u32>(phase);
            AL_ASSERT(phase_index < kSystemPhaseCount);
            auto &entries = _system_schedule[phase_index];
            for (const SystemEntry &entry: entries)
            {
                if (entry._system == nullptr || !entry._system->IsEnabled())
                    continue;
                entry._system->Update(*this, delta_time);
            }
        }

        void Register::WaitForSystems() const
        {
            for (const auto &system : _systems)
            {
                if (system)
                    system->WaitFor();
            }
        }

        // ---------------------------------------------------------------------------
        // Batch operations
        // ---------------------------------------------------------------------------

        Vector<Entity> Register::CreateBatch(u32 count)
        {
            Vector<Entity> result;
            result.reserve(count);
            if (!_is_init)
            {
                GrowPool(kDefaultEntityCapacity);
                _is_init = true;
            }
            u32 needed = count;
            u32 available = static_cast<u32>(_free_indices.size());
            if (available < needed)
            {
                u32 cur_size = static_cast<u32>(_entity_generations.size());
                u32 target = cur_size;
                while (target - cur_size + available < needed)
                {
                    target *= kEntityCapacityGrowFactor;
                    if (target > kMaxEntityCapacity)
                    {
                        target = kMaxEntityCapacity;
                        break;
                    }
                }
                if (target > cur_size)
                    GrowPool(target);
            }
            for (u32 i = 0; i < count; ++i)
            {
                Entity e = Create();
                if (e == kInvalidEntity)
                    break;
                result.push_back(e);
            }
            return result;
        }

        void Register::DestroyBatch(const Vector<Entity> &entities)
        {
            for (Entity e: entities)
                DeferredDestroy(e);
        }

        // ---------------------------------------------------------------------------
        // Pool management
        // ---------------------------------------------------------------------------

        void Register::GrowPool(u32 new_capacity)
        {
            u32 old_size = static_cast<u32>(_entity_generations.size());
            _entity_generations.resize(new_capacity, 0u);
            _entity_signatures.resize(new_capacity);
            u32 first_new_index = old_size;
            if (old_size == 0u)
            {
                _entity_generations[0] = 0u;
                first_new_index = 1u;
            }
            for (u32 i = first_new_index; i < new_capacity; ++i)
            {
                _entity_generations[i] = 1u;
                _free_indices.push(i);
            }
        }

    }// namespace ECS
}// namespace Ailu
