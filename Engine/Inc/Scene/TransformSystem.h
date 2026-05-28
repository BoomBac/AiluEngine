#pragma once
#ifndef __TRANSFORM_SYSTEM_H__
#define __TRANSFORM_SYSTEM_H__
#include "Entity.hpp"
#include <unordered_set>

namespace Ailu
{
    namespace ECS
    {
        class TransformSystem : public System
        {
            DECLARE_CLASS(TransformSystem)
        public:
            TransformSystem();
            void Update(Register &r, f32 delta_time) final;
            virtual Ref<System> Clone() final
            {
                auto copy = MakeRef<TransformSystem>();
                copy->_entities = _entities;
                copy->_ordered_entities = _ordered_entities;
                copy->_observed_hierarchy_revision = _observed_hierarchy_revision;
                copy->_order_dirty = _order_dirty;
                return copy;
            };
            void OnPushEntity(Entity entity) final;
        private:
            void RebuildOrder(Register &r);
            void AppendSubtree(Register &r, Entity entity, std::unordered_set<Entity> &visited, std::unordered_set<Entity> &visiting);

        private:
            Vector<Entity> _ordered_entities;
            u64 _observed_hierarchy_revision = 0u;
            bool _order_dirty = true;
        };
    }// namespace ECS
}// namespace Ailu


#endif// !RENDER_SYSTEM_H__
