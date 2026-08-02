#ifndef INSPECTOR_COMPONENTEDITORREGISTRY_H
#define INSPECTOR_COMPONENTEDITORREGISTRY_H
#include "Framework/Common/NonCopyable.h"
#include "Framework/Core/SmartPtr.h"
#include "Framework/Core/String.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/Containers/Map.h"
#include "IComponentEditor.h"
#include "Objects/Type.h"
#include "Scene/Entity.h"
#include <functional>

namespace Ailu
{
    namespace UI
    {
        class CollapsibleView;
        class VerticalBox;
    }
    namespace SceneManagement
    {
        class Scene;
    }
    namespace Editor
    {
        struct ComponentEditorContext
        {
            SceneManagement::Scene *_scene = nullptr;
            ECS::Entity _entity = ECS::kInvalidEntity;
            const struct ComponentEditorInfo *_component_info = nullptr;
            UI::VerticalBox *_content = nullptr;
            std::function<void()> _request_rebuild;

            ECS::Register &Registry() const;
            void *ComponentInstance() const;

            template<typename TComponent>
            TComponent *GetComponent() const
            {
                return Registry().GetComponent<TComponent>(_entity);
            }

            void MarkSceneDirty() const;
            void RequestRebuild() const;
        };

        struct ComponentEditorInfo
        {
            ECS::ComponentTypeId _component_type = 0u;
            const Type *_reflection_type = nullptr;
            String _display_name;
            String _category;
            i32 _order = 0;
            bool _allow_add = true;
            bool _allow_remove = true;

            std::function<bool(ECS::Register &, ECS::Entity)> _has_component;
            std::function<void *(ECS::Register &, ECS::Entity)> _get_component;
            std::function<void(ECS::Register &, ECS::Entity)> _add_component;
            std::function<void(ECS::Register &, ECS::Entity)> _remove_component;
            std::function<Scope<IComponentEditor>()> _create_editor;
        };

        class ComponentEditorRegistry final : public NonCopyable
        {
        public:
            static ComponentEditorRegistry &Get();

            template<typename TComponent>
            void Register(String display_name, String category = "General", i32 order = 0, bool allow_add = true, bool allow_remove = true);

            template<typename TComponent, typename TEditor>
            void RegisterCustom(String display_name, String category = "General", i32 order = 0, bool allow_add = true, bool allow_remove = true);

            const Vector<ComponentEditorInfo> &Components() const { return _components; }
            const ComponentEditorInfo *Find(ECS::ComponentTypeId component_type) const;

        private:
            void RegisterInternal(ComponentEditorInfo &&info);

            Vector<ComponentEditorInfo> _components;
            HashMap<ECS::ComponentTypeId, u64> _component_indices;
        };

        template<typename TComponent>
        void ComponentEditorRegistry::Register(String display_name, String category, i32 order, bool allow_add, bool allow_remove)
        {
            ComponentEditorInfo info;
            info._component_type = TComponent::StaticComponentTypeId();
            info._reflection_type = StaticClass<TComponent>();
            info._display_name = std::move(display_name);
            info._category = std::move(category);
            info._order = order;
            info._allow_add = allow_add;
            info._allow_remove = allow_remove;

            info._has_component = [](ECS::Register &registry, ECS::Entity entity)
            {
                return registry.HasComponent<TComponent>(entity);
            };

            info._get_component = [](ECS::Register &registry, ECS::Entity entity) -> void *
            {
                return static_cast<void *>(registry.GetComponent<TComponent>(entity));
            };

            info._add_component = [](ECS::Register &registry, ECS::Entity entity)
            {
                registry.AddComponent<TComponent>(entity);
            };

            info._remove_component = [](ECS::Register &registry, ECS::Entity entity)
            {
                registry.RemoveComponent<TComponent>(entity);
            };

            RegisterInternal(std::move(info));
        }

        template<typename TComponent, typename TEditor>
        void ComponentEditorRegistry::RegisterCustom(String display_name, String category, i32 order, bool allow_add, bool allow_remove)
        {
            ComponentEditorInfo info;
            info._component_type = TComponent::StaticComponentTypeId();
            info._reflection_type = StaticClass<TComponent>();
            info._display_name = std::move(display_name);
            info._category = std::move(category);
            info._order = order;
            info._allow_add = allow_add;
            info._allow_remove = allow_remove;

            info._has_component = [](ECS::Register &registry, ECS::Entity entity)
            {
                return registry.HasComponent<TComponent>(entity);
            };

            info._get_component = [](ECS::Register &registry, ECS::Entity entity) -> void *
            {
                return static_cast<void *>(registry.GetComponent<TComponent>(entity));
            };

            info._add_component = [](ECS::Register &registry, ECS::Entity entity)
            {
                registry.AddComponent<TComponent>(entity);
            };

            info._remove_component = [](ECS::Register &registry, ECS::Entity entity)
            {
                registry.RemoveComponent<TComponent>(entity);
            };

            info._create_editor = []() -> Scope<IComponentEditor>
            {
                return MakeScope<TEditor>();
            };

            RegisterInternal(std::move(info));
        }
    }// namespace Editor
}// namespace Ailu
#endif// INSPECTOR_COMPONENTEDITORREGISTRY_H
