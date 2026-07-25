#pragma once
#ifndef __ENTITY_H__
#define __ENTITY_H__

#include "FrameWork/Math/Random.hpp"
#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/String.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/Containers/Array.h"
#include "Framework/Core/Containers/Queue.h"
#include "Framework/Core/Containers/List.h"
#include "Framework/Core/Containers/Map.h"
#include "Framework/Common/Assert.h"
#include <bitset>
#include <ranges>
#include <set>
#include <unordered_map>
#include <functional>

namespace Ailu
{
    //https://austinmorlan.com/posts/entity_component_system/
    //https://wickedengine.net/2019/09/entity-component-system/
    //https://skypjack.github.io/2019-06-25-ecs-baf-part-4/
    namespace ECS
    {
        using Entity = u64;
        using ComponentTypeId = u32;
        struct CHierarchy;
        static const Entity kInvalidEntity = 0u;
        static const u32 kMaxEntityCapacity = 1'000'000u;

        // --- Entity Handle helpers (index + generation packed in u64) ---
        // Bits 0–31 = index, Bits 32–63 = generation
        inline u32 EntityIndex(Entity e) { return static_cast<u32>(e & 0xFFFFFFFFull); }
        inline u32 EntityGeneration(Entity e) { return static_cast<u32>((e >> 32) & 0xFFFFFFFFull); }
        inline Entity MakeEntity(u32 index, u32 generation) { return (static_cast<u64>(generation) << 32) | index; }

        // --- Type ID for systems (separate from ComponentTypeId in Component.h) ---
        using SystemTypeId = u32;

        inline SystemTypeId AllocateSystemTypeId()
        {
            static std::atomic<SystemTypeId> s_next_id = 0;
            return s_next_id.fetch_add(1, std::memory_order_relaxed);
        }

        template<typename T>
        SystemTypeId GetSystemTypeId()
        {
            static const SystemTypeId kTypeId = AllocateSystemTypeId();
            return kTypeId;
        }

        class IComponentManager
        {
        public:
            virtual ~IComponentManager() = default;
            virtual void EntityDestroyed(Entity entity) = 0;
            virtual Ref<IComponentManager> Clone() = 0;
        };

        const static u16 kSignatureNum = 128u;
        using Signature = std::bitset<kSignatureNum>;

#define DECLARE_SYSTEM(name)                                              \
public:                                                                   \
    static SystemTypeId StaticSystemTypeId()                              \
    {                                                                     \
        return Ailu::ECS::GetSystemTypeId<name>();                        \
    }                                                                     \
                                                                          \
    SystemTypeId GetSystemTypeId() const override                         \
    {                                                                     \
        return StaticSystemTypeId();                                      \
    }

        class System
        {
            friend class Register;

        public:
            virtual ~System() = default;
            virtual void Update(Register &r, f32 delta_time) {};
            virtual void OnPushEntity(Entity entity) {};
            virtual void WaitFor() const {};
            virtual Ref<System> Clone()
            {
                AL_ASSERT(true);
                return nullptr;
            };
            virtual SystemTypeId GetSystemTypeId() const = 0;
        protected:
            std::set<Entity> _entities;
        };

        using CompAddCallback = std::function<void(ECS::Entity)>;
        using CompRemoveCallback = std::function<void(ECS::Entity)>;

        // Forward declaration
        template<typename T>
        class ComponentManager;

        class AILU_API Register
        {
        public:
            Register() = default;
            Register(const Register &other);
            Register(Register &&other) noexcept;
            Register &operator=(const Register &other);
            Register &operator=(Register &&other) noexcept;
            bool operator==(const Register &other) const;

            Entity Create();
            bool IsAlive(Entity entity) const;
            void Destory(Entity entity);
            void Destroy(Entity entity);
            void EntitySignatureChanged(Entity entity);

            template<typename T>
            bool HasComponent(Entity entity) const;

            template<typename T>
            u16 GetComponentTypeID() const;

            template<typename T>
            void RegisterComponent();

            template<typename T>
            T *RegisterSystem(Signature sig);

            template<typename T>
            T *GetSystem();

            auto SystemView() { return std::views::all(_systems); }
            auto SystemView() const { return std::views::all(_systems); }

            template<typename T, typename... Args>
            T &AddComponent(Entity entity, Args &&...args);

            template<typename T>
            void RemoveComponent(Entity entity);

            template<typename T>
            T *GetComponent(u64 entity);

            template<typename T>
            const T *GetComponent(u64 entity) const;

            template<typename SrcT, typename DstT>
            DstT *GetComponent(u64 index);

            template<typename SrcT, typename DstT>
            const DstT *GetComponent(u64 index) const;

            template<typename T>
            ComponentManager<T> *GetComponentMgr();

            template<typename T>
            Entity GetEntity(u64 index) const;

            template<typename T>
            auto &View();

            template<typename T>
            const auto &View() const;

            template<typename T>
            const auto &EntityView() const;

            template<typename T>
            void RegisterOnComponentAdd(CompAddCallback callback);

            template<typename T>
            void RegisterOnComponentRemove(CompRemoveCallback callback);

            u32 EntityNum() const;
            u64 HierarchyRevision() const;
            void TouchHierarchy();

            // --- Deferred destruction ---
            void DeferredDestroy(Entity entity);
            void FlushDestroy();

            // --- Batch operations ---
            Vector<Entity> CreateBatch(u32 count);
            void DestroyBatch(const Vector<Entity> &entities);

        private:
            void GrowPool(u32 new_capacity);
            void EnsureMgrVector(u32 type_id);
            void EnsureCallbackVectors(u32 type_id);
            void EnsureSystemVectors(u32 sys_id);

        private:
            static constexpr u32 kDefaultEntityCapacity = 256u;
            static constexpr u32 kEntityCapacityGrowFactor = 2u;

            // --- Component storage indexed by ComponentTypeId ---
            Vector<Ref<IComponentManager>> _mgrs;
            // --- System storage indexed by SystemTypeId ---
            Vector<Ref<System>> _systems;
            Vector<Signature> _sys_signatures;
            // --- Callback storage indexed by ComponentTypeId ---
            Vector<List<CompAddCallback>> _on_comp_add_callback;
            Vector<List<CompRemoveCallback>> _on_comp_remove_callback;
            u32 _entity_num = 0u;
            u64 _hierarchy_revision = 1u;
            Vector<Signature> _entity_signatures;
            Vector<u32> _entity_generations;
            Queue<u32> _free_indices;
            Vector<Entity> _pending_destroy_queue;
            bool _is_init = false;
        };

    }// namespace ECS
}// namespace Ailu

#include "ComponentManager.h"
#include "Entity.inl"

#endif// __ENTITY_H__
