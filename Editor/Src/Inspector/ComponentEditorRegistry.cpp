#include "Inspector/ComponentEditorRegistry.h"
#include "Scene/Scene.h"

namespace Ailu
{
    namespace Editor
    {
        ECS::Register &ComponentEditorContext::Registry() const
        {
            return _scene->GetRegister();
        }

        void *ComponentEditorContext::ComponentInstance() const
        {
            if (_component_info == nullptr || _scene == nullptr)
                return nullptr;
            return _component_info->_get_component(_scene->GetRegister(), _entity);
        }

        void ComponentEditorContext::MarkSceneDirty() const
        {
            if (_scene != nullptr)
                _scene->MarkEdited();
        }

        void ComponentEditorContext::RequestRebuild() const
        {
            if (_request_rebuild)
                _request_rebuild();
        }

        ComponentEditorRegistry &ComponentEditorRegistry::Get()
        {
            static ComponentEditorRegistry s_instance;
            return s_instance;
        }

        const ComponentEditorInfo *ComponentEditorRegistry::Find(ECS::ComponentTypeId component_type) const
        {
            auto it = _component_indices.find(component_type);
            if (it != _component_indices.end())
                return &_components[it->second];
            return nullptr;
        }

        void ComponentEditorRegistry::RegisterInternal(ComponentEditorInfo &&info)
        {
            if (_component_indices.find(info._component_type) != _component_indices.end())
            {
                LOG_WARNING("ComponentEditorRegistry: Duplicate registration for component type {}, overwriting.", info._component_type);
                u64 idx = _component_indices[info._component_type];
                _components[idx] = std::move(info);
                return;
            }
            _component_indices[info._component_type] = _components.size();
            _components.emplace_back(std::move(info));
        }
    }// namespace Editor
}// namespace Ailu
