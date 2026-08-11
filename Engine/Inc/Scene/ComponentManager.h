#pragma once
#ifndef __COMPONENT_MANAGER_H__
#define __COMPONENT_MANAGER_H__

#include "Entity.h"
#include <unordered_map>

namespace Ailu
{
    namespace ECS
    {
        // sparse set
        template<typename T>
        class ComponentManager : public IComponentManager
        {
        public:
            Ref<IComponentManager> Clone() final
            {
                auto copy = std::make_shared<ComponentManager<T>>();
                copy->_comps = _comps;
                copy->_entities = _entities;
                copy->_lut = _lut;
                return copy;
            }

            bool CopyComponent(Entity source, Entity target) final
            {
                T *source_component = GetComponent(source);
                if (source_component == nullptr)
                    return false;

                if (T *target_component = GetComponent(target))
                {
                    *target_component = *source_component;
                    return false;
                }

                Create(target, *source_component);
                return true;
            }

            template<typename... Args>
            T &Create(Entity entity, Args &&...args)
            {
                AL_ASSERT(entity != kInvalidEntity);
                AL_ASSERT(!_lut.contains(entity));
                AL_ASSERT(_comps.size() == _entities.size());
                _comps.emplace_back(std::forward<Args>(args)...);
                u64 index = _comps.size() - 1;
                _lut[entity] = index;
                _entities.emplace_back(entity);
                return _comps.back();
            }
            void EntityDestroyed(Entity entity)
            {
                Remove(entity);
            }
            void *GetComponentPtr(Entity entity) override
            {
                auto it = _lut.find(entity);
                return it != _lut.end() ? static_cast<void *>(&_comps[it->second]) : nullptr;
            }
            void Remove(Entity entity)
            {
                auto it = _lut.find(entity);
                if (it != _lut.end())
                {
                    // Directly index into components and entities array:
                    const size_t index = it->second;
                    const Entity entity = _entities[index];
                    if (index < _comps.size() - 1)
                    {
                        _comps[index] = std::move(_comps.back());
                        _entities[index] = _entities.back();
                        _lut[_entities[index]] = index;
                    }
                    _comps.pop_back();
                    _entities.pop_back();
                    _lut.erase(entity);
                }
            }
            /// @brief Move component at src index toward dst, shifting intervening elements
            /// @param src original index
            /// @param dst target index
            void Move(u64 src, u64 dst)
            {
                AL_ASSERT(src < _comps.size() && dst < _comps.size());
                if (src == dst) return;
                T comp = std::move(_comps[src]);
                Entity ent = _entities[src];

                const i64 step = (dst > src) ? +1 : -1;

                for (u64 i = src; i != dst; i += step)
                {
                    u64 next = i + step;
                    _comps[i] = std::move(_comps[next]);
                    _entities[i] = _entities[next];
                    _lut[_entities[i]] = i;
                }
                _comps[dst] = std::move(comp);
                _entities[dst] = ent;
                _lut[ent] = dst;
            }

            void MoveToFirst(u64 src)
            {
                Move(src, 0);
            }

            void MoveToLast(u64 src)
            {
                Move(src, _comps.size() - 1);
            }

            T *GetComponent(Entity entity)
            {
                return _lut.contains(entity) ? &_comps[_lut[entity]] : nullptr;
            }

            T &operator[](u64 index) { return _comps[index]; }
            u64 Count() const { return _comps.size(); }
            Entity GetEntity(u64 index) const
            {
                AL_ASSERT(index < _entities.size());
                return _entities[index];
            }
            i64 GetIndex(Entity entity) const
            {
                return _lut.contains(entity) ? _lut.at(entity) : -1;
            }
            auto &begin() { return _comps.begin(); }
            auto &end() { return _comps.end(); }
            auto &View() { return _comps; }
            const auto &View() const { return _comps; }
            const auto &ViewEntity() const { return _entities; }

        private:
            Vector<T> _comps;
            Vector<Entity> _entities;
            std::unordered_map<Entity, u64> _lut;
        };

    }// namespace ECS
}// namespace Ailu

#endif// __COMPONENT_MANAGER_H__
