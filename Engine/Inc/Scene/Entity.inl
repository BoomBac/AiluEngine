#pragma once
#ifndef __ENTITY_INL__
#define __ENTITY_INL__

// ---------------------------------------------------------------------------
// Register template method implementations
//   ComponentTypeId from DECLARE_COMPONENT replaces String-keyed hash maps
//   with O(1) Vector indexing.
// ---------------------------------------------------------------------------

namespace Ailu
{
    namespace ECS
    {
        template<typename T>
        bool Register::HasComponent(Entity entity) const
        {
            if (!IsAlive(entity))
                return false;
            ComponentTypeId type_id = T::StaticComponentTypeId();
            return _entity_signatures[EntityIndex(entity)].test(type_id);
        }

        template<typename T>
        u16 Register::GetComponentTypeID() const
        {
            return static_cast<u16>(T::StaticComponentTypeId());
        }

        template<typename T>
        void Register::RegisterComponent()
        {
            ComponentTypeId type_id = T::StaticComponentTypeId();
            EnsureMgrVector(static_cast<u32>(type_id));
            AL_ASSERT(!_mgrs[type_id]);
            _mgrs[type_id] = MakeRef<ComponentManager<T>>();
            EnsureCallbackVectors(static_cast<u32>(type_id));
        }

        template<typename T>
        T *Register::RegisterSystem(Signature sig)
        {
            SystemTypeId sys_id = T::StaticSystemTypeId();
            EnsureSystemVectors(static_cast<u32>(sys_id));
            _systems[sys_id] = MakeRef<T>();
            _sys_signatures[sys_id] = sig;
            MarkSystemScheduleDirty();
            return static_cast<T *>(_systems[sys_id].get());
        }

        template<typename T>
        T *Register::GetSystem()
        {
            SystemTypeId sys_id = T::StaticSystemTypeId();
            if (sys_id < static_cast<SystemTypeId>(_systems.size()) && _systems[sys_id])
                return static_cast<T *>(_systems[sys_id].get());
            return nullptr;
        }

        template<typename T, typename... Args>
        T &Register::AddComponent(Entity entity, Args &&...args)
        {
            AL_ASSERT(IsAlive(entity));
            ComponentTypeId type_id = T::StaticComponentTypeId();
            AL_ASSERT(type_id < static_cast<ComponentTypeId>(_mgrs.size()) && _mgrs[type_id]);
            auto &component = static_cast<ComponentManager<T> *>(_mgrs[type_id].get())->Create(entity, std::forward<Args>(args)...);
            _entity_signatures[EntityIndex(entity)].set(type_id, true);
            if constexpr (std::is_same_v<T, CHierarchy>)
                TouchHierarchy();
            if (static_cast<u32>(type_id) < static_cast<u32>(_on_comp_add_callback.size()))
            {
                for (auto &f: _on_comp_add_callback[type_id])
                    f(entity);
            }
            EntitySignatureChanged(entity);
            return component;
        }

        template<typename T>
        void Register::RemoveComponent(Entity entity)
        {
            AL_ASSERT(IsAlive(entity));
            ComponentTypeId type_id = T::StaticComponentTypeId();
            AL_ASSERT(type_id < static_cast<ComponentTypeId>(_mgrs.size()) && _mgrs[type_id]);
            u32 idx = EntityIndex(entity);
            if constexpr (std::is_same_v<T, CHierarchy>)
            {
                if (_entity_signatures[idx].test(type_id))
                    TouchHierarchy();
            }
            static_cast<ComponentManager<T> *>(_mgrs[type_id].get())->Remove(entity);
            _entity_signatures[idx].set(type_id, false);
            if (const auto disabled_iter = _disabled_components.find(entity); disabled_iter != _disabled_components.end())
            {
                disabled_iter->second.set(type_id, false);
                if (disabled_iter->second.none())
                    _disabled_components.erase(disabled_iter);
            }
            if (static_cast<u32>(type_id) < static_cast<u32>(_on_comp_remove_callback.size()))
            {
                for (auto &f: _on_comp_remove_callback[type_id])
                    f(entity);
            }
            EntitySignatureChanged(entity);
        }

        template<typename T>
        bool Register::IsComponentEnabled(Entity entity) const
        {
            if (!HasComponent<T>(entity))
                return false;
            const auto disabled_iter = _disabled_components.find(entity);
            return disabled_iter == _disabled_components.end() || !disabled_iter->second.test(T::StaticComponentTypeId());
        }

        template<typename T>
        void Register::SetComponentEnabled(Entity entity, bool enabled)
        {
            if (!HasComponent<T>(entity))
                return;
            Signature &disabled = _disabled_components[entity];
            disabled.set(T::StaticComponentTypeId(), !enabled);
            if (disabled.none())
                _disabled_components.erase(entity);
        }

        template<typename T>
        T *Register::GetComponent(u64 entity)
        {
            ComponentTypeId type_id = T::StaticComponentTypeId();
            if (type_id >= static_cast<ComponentTypeId>(_mgrs.size()) || !_mgrs[type_id])
                return nullptr;
            return static_cast<ComponentManager<T> *>(_mgrs[type_id].get())->GetComponent(entity);
        }

        template<typename T>
        const T *Register::GetComponent(u64 entity) const
        {
            ComponentTypeId type_id = T::StaticComponentTypeId();
            if (type_id >= static_cast<ComponentTypeId>(_mgrs.size()) || !_mgrs[type_id])
                return nullptr;
            return static_cast<ComponentManager<T> *>(_mgrs[type_id].get())->GetComponent(entity);
        }

        template<typename SrcT, typename DstT>
        DstT *Register::GetComponent(u64 index)
        {
            Entity e = GetEntity<SrcT>(index);
            return GetComponent<DstT>(e);
        }

        template<typename SrcT, typename DstT>
        const DstT *Register::GetComponent(u64 index) const
        {
            Entity e = GetEntity<SrcT>(index);
            return GetComponent<DstT>(e);
        }

        template<typename T>
        ComponentManager<T> *Register::GetComponentMgr()
        {
            ComponentTypeId type_id = T::StaticComponentTypeId();
            if (type_id >= static_cast<ComponentTypeId>(_mgrs.size()) || !_mgrs[type_id])
                return nullptr;
            return static_cast<ComponentManager<T> *>(_mgrs[type_id].get());
        }

        template<typename T>
        Entity Register::GetEntity(u64 index) const
        {
            ComponentTypeId type_id = T::StaticComponentTypeId();
            return static_cast<ComponentManager<T> *>(_mgrs[type_id].get())->GetEntity(index);
        }

        template<typename T>
        auto &Register::View()
        {
            ComponentTypeId type_id = T::StaticComponentTypeId();
            return static_cast<ComponentManager<T> *>(_mgrs[type_id].get())->View();
        }

        template<typename T>
        const auto &Register::View() const
        {
            ComponentTypeId type_id = T::StaticComponentTypeId();
            return static_cast<const ComponentManager<T> *>(_mgrs[type_id].get())->View();
        }

        template<typename T>
        const auto &Register::EntityView() const
        {
            ComponentTypeId type_id = T::StaticComponentTypeId();
            return static_cast<const ComponentManager<T> *>(_mgrs[type_id].get())->ViewEntity();
        }

        template<typename T>
        void Register::RegisterOnComponentAdd(CompAddCallback callback)
        {
            ComponentTypeId type_id = T::StaticComponentTypeId();
            EnsureCallbackVectors(static_cast<u32>(type_id));
            _on_comp_add_callback[type_id].emplace_back(callback);
        }

        template<typename T>
        void Register::RegisterOnComponentRemove(CompRemoveCallback callback)
        {
            ComponentTypeId type_id = T::StaticComponentTypeId();
            EnsureCallbackVectors(static_cast<u32>(type_id));
            _on_comp_remove_callback[type_id].emplace_back(callback);
        }

    }// namespace ECS
}// namespace Ailu

#endif// __ENTITY_INL__
