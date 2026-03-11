#pragma once
#ifndef __SELECTION_H__
#define __SELECTION_H__
#include "Objects/Object.h"
#include "Scene/Entity.hpp"
namespace Ailu
{
    namespace Editor
    {
        class Selection
        {
        public:
            static void AddSelection(Object *obj)
            {
                if (!s_can_select)
                    return;
                if (std::find(s_selections.begin(), s_selections.end(), obj) == s_selections.end())
                {
                    s_selections.emplace_back(obj);
                }
            }
            static void AddAndRemovePreSelection(Object *obj)
            {
                if (!s_can_select)
                    return;
                if (!s_selections.empty())
                    s_selections.pop_back();
                AddSelection(obj);
            }
            static void RemoveSlection(Object *obj)
            {
                std::erase_if(s_selections, [&](auto o) -> bool
                              { return o == obj; });
            }
            static void AddSelection(ECS::Entity entity,u32 sub_index = 0)
            {
                if (!s_can_select)
                    return;
                if (std::find(s_selected_entities.begin(), s_selected_entities.end(), entity) == s_selected_entities.end())
                {
                    s_selected_entities.emplace_back(entity);
                    s_selected_entities_subindex[entity] = sub_index;
                }
            }
            static void AddAndRemovePreSelection(ECS::Entity entity, u32 sub_index = 0)
            {
                if (!s_can_select)
                    return;
                if (!s_selected_entities.empty())
                {
                    s_selected_entities_subindex.erase(s_selected_entities.back());
                    s_selected_entities.pop_back();
                }
                if (entity != ECS::kInvalidEntity)
                    AddSelection(entity, sub_index);
            }
            static void RemoveSlection(ECS::Entity entity)
            {
                std::erase_if(s_selected_entities, [&](auto o) -> bool
                              { return o == entity; });
                s_selected_entities_subindex.erase(entity);
            }
            static void RemoveSlection()
            {
                if (!s_can_select)
                    return;
                s_selected_entities.clear();
                s_selections.clear();
                s_selected_entities_subindex.clear();
            }


            static List<Object *> &Selections() { return s_selections; }
            static List<ECS::Entity> &SelectedEntities() { return s_selected_entities; }
            static u32 GetSelectedSubIndex(ECS::Entity entity)
            {
                if (s_selected_entities_subindex.contains(entity))
                    return s_selected_entities_subindex[entity];
                return 0;
            }
            
            template<typename T>
            static T *FirstObject();
            static ECS::Entity FirstEntity()
            {
                if (s_selected_entities.empty())
                    return ECS::kInvalidEntity;
                return s_selected_entities.front();
            }
            static void Active(bool can_select) { s_can_select = can_select; }
            static bool IsActive() { return s_can_select; }

        private:
            inline static List<ECS::Entity> s_selected_entities{};
            inline static HashMap<ECS::Entity,u32> s_selected_entities_subindex{};//for submesh selection, only used in editor, no need to save in scene
            inline static List<Object *> s_selections{};
            inline static bool s_can_select = true;
        };
        template<typename T>
        inline T *Selection::FirstObject()
        {
            for (auto obj: s_selections)
            {
                auto ptr = dynamic_cast<T *>(obj);
                if (ptr)
                {
                    return ptr;
                }
            }
            return nullptr;
        }
    }// namespace Editor
}// namespace Ailu

#endif// !SELECTION_H__
