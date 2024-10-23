#pragma once
#ifndef __ENTITY__
#define __ENTITY__

#include "FrameWork/Math/Random.h"
#include "GlobalMarco.h"
#include <bitset>
#include <ranges>
#include <set>
#include <unordered_map>

namespace Ailu
{
    //https://austinmorlan.com/posts/entity_component_system/
    //https://wickedengine.net/2019/09/entity-component-system/
    //https://skypjack.github.io/2019-06-25-ecs-baf-part-4/
    namespace ECS
    {
        using Entity = u32;
        static const Entity kInvalidEntity = 0u;
        static const u32 kMaxEntityNum = 200;

        class IComponentManager
        {
        public:
            virtual ~IComponentManager() = default;
            virtual void EntityDestroyed(Entity entity) = 0;
            virtual Ref<IComponentManager> Clone() = 0;
        };
        //sparse set
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
            T *GetComponent(Entity entity)
            {
                return _lut.contains(entity) ? &_comps[_lut[entity]] : nullptr;
            }

            T &operator[](u64 index) { return _comps[index]; }
            u64 Count() const { return _comps.size(); }
            Entity GetEntity(u64 index) const { return _entities[index]; }
            auto &begin() { return _comps.begin(); }
            auto &end() { return _comps.end(); }
            //auto View() { return std::views::all(_comps); }
            //auto View() const { return std::views::all(_comps); }
            auto &View() { return _comps; }
            const auto &View() const { return _comps; }
            //auto &ViewEntity() { return _comps; }
            const auto &ViewEntity() const { return _entities; }

        private:
            Vector<T> _comps;
            Vector<Entity> _entities;
            std::unordered_map<Entity, u64> _lut;
        };
        const static u16 kSignatureNum = 32u;
        using Signature = std::bitset<kSignatureNum>;
        class System
        {
            friend class Register;

        public:
            virtual void Update(Register &r, f32 delta_time) {};
            virtual void OnPushEntity(Entity entity) {};
            virtual void WaitFor() const {};
            virtual Ref<System> Clone() 
            { 
                AL_ASSERT(true);
                return nullptr; 
            };

        protected:
            std::set<Entity> _entities;
        };

        class Register
        {
        public:
            Register() = default;
            Register(const Register &other)
                : _comp_types(other._comp_types),
                  _sys_signatures(other._sys_signatures),
                  _entity_num(other._entity_num),
                  _entities(other._entities),
                  _available_entities(other._available_entities),
                  _is_init(other._is_init)
            {
                for (const auto &pair: other._mgrs)
                {
                    const auto &type_name = pair.first;
                    const auto &manager = pair.second;
                    _mgrs[type_name] = pair.second->Clone();
                }
                for (const auto &pair: other._systems)
                {
                    const auto &type_name = pair.first;
                    const auto &manager = pair.second;
                    _systems[type_name] = pair.second->Clone();
                }
            }
            Register(Register &&other) noexcept
                : _mgrs(std::move(other._mgrs)),
                  _comp_types(std::move(other._comp_types)),
                  _systems(std::move(other._systems)),
                  _sys_signatures(std::move(other._sys_signatures)),
                  _entity_num(other._entity_num),
                  _entities(std::move(other._entities)),
                  _available_entities(std::move(other._available_entities)),
                  _is_init(other._is_init)
            {
                other._entity_num = -1;
                other._is_init = false;
            }
            Register &operator=(const Register &other)
            {
                if (this != &other)
                {
                    for (const auto &pair: other._mgrs)
                    {
                        const auto &type_name = pair.first;
                        const auto &manager = pair.second;
                        _mgrs[type_name] = pair.second->Clone();
                    }
                    for (const auto &pair: other._systems)
                    {
                        const auto &type_name = pair.first;
                        const auto &manager = pair.second;
                        _systems[type_name] = pair.second->Clone();
                    }
                    _comp_types = other._comp_types;
                    _sys_signatures = other._sys_signatures;
                    _entity_num = other._entity_num;
                    _entities = other._entities;
                    _available_entities = other._available_entities;
                    _is_init = other._is_init;
                }
                return *this;
            }
            Register &operator=(Register &&other) noexcept
            {
                if (this != &other)
                {
                    _mgrs = std::move(other._mgrs);
                    _comp_types = std::move(other._comp_types);
                    _systems = std::move(other._systems);
                    _sys_signatures = std::move(other._sys_signatures);
                    _entity_num = other._entity_num;
                    _entities = std::move(other._entities);
                    _available_entities = std::move(other._available_entities);
                    _is_init = other._is_init;

                    other._entity_num = -1;
                    other._is_init = false;
                }
                return *this;
            }
            bool operator==(const Register &other) const
            {
                return _comp_types == other._comp_types &&
                       _sys_signatures == other._sys_signatures &&
                       _entity_num == other._entity_num &&
                       _entities == other._entities &&
                       _available_entities == other._available_entities &&
                       _is_init == other._is_init;
            }
            Entity Create()
            {
                if (!_is_init)
                {
                    for (u32 i = 1; i <= kMaxEntityNum; i++)
                    {
                        _entities[i - 1].reset();
                        _available_entities.push(i);
                    }
                    _is_init = true;
                }
                Entity e = _available_entities.front();
                _available_entities.pop();
                ++_entity_num;
                return e;
                //return (u32) Random::RandomInt();
            }
            void Destory(Entity entity)
            {
                for (auto &it: _mgrs)
                {
                    auto &[type_name, mgr] = it;
                    mgr->EntityDestroyed(entity);
                }
                for (auto &it: _systems)
                {
                    auto &[type_name, sys] = it;
                    sys->_entities.erase(entity);
                }
                _available_entities.push(entity);
            }
            void EntitySignatureChanged(Entity entity)
            {
                AL_ASSERT(entity < kMaxEntityNum);
                for (auto &it: _systems)
                {
                    auto &[type_name, sys] = it;
                    Signature acquired_sig = _sys_signatures[type_name];
                    if ((acquired_sig & _entities[entity]) == acquired_sig)
                    {
                        sys->_entities.insert(entity);
                        sys->OnPushEntity(entity);
                    }
                    else
                        sys->_entities.erase(entity);
                }
            }

            template<typename T>
            bool HasComponent(Entity entity) const
            {
                AL_ASSERT(entity < kMaxEntityNum);
                const auto &type_name = T::TypeName();
                AL_ASSERT(_comp_types.contains(type_name));
                return _entities[entity].test(_comp_types.at(type_name));
            };
            template<typename T>
            u16 GetComponentTypeID() const
            {
                const auto &type_name = T::TypeName();
                AL_ASSERT(_comp_types.contains(type_name));
                return _comp_types.at(type_name);
            }
            template<typename T>
            void RegisterComponent()
            {
                const auto &type_name = T::TypeName();
                AL_ASSERT(!_mgrs.contains(type_name));
                _mgrs[type_name] = MakeRef<ComponentManager<T>>();
                _comp_types[type_name] = _comp_types.size();
            }
            template<typename T>
            T *RegisterSystem(Signature sig)
            {
                const auto &type_name = T::TypeName();
                AL_ASSERT(!_systems.contains(type_name));
                _systems[type_name] = MakeRef<T>();
                _sys_signatures[type_name] = sig;
                return std::static_pointer_cast<T>(_systems[type_name]).get();
            }
            auto SystemView()
            {
                return std::views::all(_systems);
            }
            auto SystemView() const
            {
                return std::views::all(_systems);
            }
            template<typename T, typename... Args>
            T &AddComponent(Entity entity, Args &&...args)
            {
                AL_ASSERT(entity < kMaxEntityNum);
                const auto &type_name = T::TypeName();
                AL_ASSERT(_mgrs.contains(type_name));
                _entities[entity].set(_comp_types[type_name], true);
                EntitySignatureChanged(entity);
                return static_cast<ComponentManager<T> *>(_mgrs[type_name].get())->Create(entity, std::forward<Args>(args)...);
            }

            template<typename T>
            void RemoveComponent(Entity entity)
            {
                AL_ASSERT(entity < kMaxEntityNum);
                const auto &type_name = T::TypeName();
                AL_ASSERT(_mgrs.contains(type_name));
                static_cast<ComponentManager<T> *>(_mgrs[type_name].get())->Remove(entity);
                _entities[entity].set(_comp_types[type_name], false);
                EntitySignatureChanged(entity);
            }
            template<typename T>
            T *GetComponent(u64 entity)
            {
                const auto &type_name = T::TypeName();
                return static_cast<ComponentManager<T> *>(_mgrs[type_name].get())->GetComponent(entity);
            }
            template<typename T>
            const T *GetComponent(u64 entity) const
            {
                const auto &type_name = T::TypeName();
                return static_cast<ComponentManager<T> *>(_mgrs.at(type_name).get())->GetComponent(entity);
            }

            template<typename SrcT, typename DstT>
            const DstT *GetComponent(u64 index) const
            {
                Entity e = GetEntity<SrcT>(index);
                return GetComponent<DstT>(e);
            }

            template<typename T>
            Entity GetEntity(u64 index) const
            {
                const auto &type_name = T::TypeName();
                return static_cast<ComponentManager<T> *>(_mgrs.at(type_name).get())->GetEntity(index);
            }

            template<typename T>
            auto &View()
            {
                const auto &type_name = T::TypeName();
                AL_ASSERT(_mgrs.contains(type_name));
                return static_cast<ComponentManager<T> *>(_mgrs[type_name].get())->View();
            }
            template<typename T>
            const auto &View() const
            {
                const auto &type_name = T::TypeName();
                AL_ASSERT(_mgrs.contains(type_name));
                return static_cast<const ComponentManager<T> *>(_mgrs.at(type_name).get())->View();
            }

            template<typename T>
            const auto &EntityView() const
            {
                const auto &type_name = T::TypeName();
                AL_ASSERT(_mgrs.contains(type_name));
                return static_cast<const ComponentManager<T> *>(_mgrs.at(type_name).get())->ViewEntity();
            }
            u32 EntityNum() const { return _entity_num; }

        private:
            std::unordered_map<String, Ref<IComponentManager>> _mgrs;
            std::unordered_map<String, u16> _comp_types;
            std::unordered_map<String, Ref<System>> _systems;
            std::unordered_map<String, Signature> _sys_signatures;
            u32 _entity_num = 0u;
            Array<Signature, kMaxEntityNum> _entities;
            Queue<Entity> _available_entities;
            bool _is_init = false;
        };
    }// namespace ECS
}// namespace Ailu
#endif// __ENTITY__