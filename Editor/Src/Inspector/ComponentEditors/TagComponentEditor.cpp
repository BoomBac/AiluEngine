#include "Inspector/ComponentEditors/TagComponentEditor.h"
#include "Inspector/ComponentEditorHelpers.h"
#include "Inspector/ComponentEditorRegistry.h"
#include "Project/ProjectManager.h"
#include "Scene/Scene.h"
#include "Scene/Component.h"

namespace Ailu::Editor
{
    void TagComponentEditor::Build(ComponentEditorContext &context)
    {
        auto *component = context.GetComponent<ECS::TagComponent>();
        if (component == nullptr || context._content == nullptr)
            return;

        if (!ProjectManager::Get().HasOpenedProject())
            return;
        const auto &settings = ProjectManager::Get().CurrentProject().Settings();
        const Vector<String> tags = settings._tags.empty() ? Vector<String>{"Untagged"} : settings._tags;
        auto *tag_selector = AddDropdownRow(context._content, "Tag", tags);
        i32 selected_tag = 0;
        for (u32 index = 0u; index < tags.size(); ++index)
        {
            if (tags[index] == component->_tag)
            {
                selected_tag = static_cast<i32>(index);
                break;
            }
        }
        tag_selector->SetSelectedIndex(selected_tag);
        auto *scene = context._scene;
        tag_selector->_on_selected_changed += [component, scene, tags](i32 index)
        {
            if (index < 0 || static_cast<u32>(index) >= tags.size())
                return;
            component->_tag = tags[index];
            scene->MarkEdited();
        };

        const Vector<String> layers = settings._layers;
        if (layers.empty())
            return;
        auto *layer_selector = AddDropdownRow(context._content, "Layer", layers);
        i32 selected_layer = 0;
        for (u32 index = 0u; index < layers.size(); ++index)
        {
            if (component->_layer_mask == (1u << index))
            {
                selected_layer = static_cast<i32>(index);
                break;
            }
        }
        layer_selector->SetSelectedIndex(selected_layer);
        layer_selector->_on_selected_changed += [component, scene](i32 index)
        {
            if (index < 0 || index >= static_cast<i32>(ProjectSettings::kLayerCount))
                return;
            component->_layer_mask = 1u << static_cast<u32>(index);
            scene->MarkEdited();
        };
    }
}
