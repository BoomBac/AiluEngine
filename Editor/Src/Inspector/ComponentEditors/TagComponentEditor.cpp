#include "Inspector/ComponentEditors/TagComponentEditor.h"
#include "Inspector/ComponentEditorHelpers.h"
#include "Inspector/ComponentEditorRegistry.h"
#include "Scene/Scene.h"
#include "Scene/Component.h"

namespace Ailu::Editor
{
    void TagComponentEditor::Build(ComponentEditorContext &context)
    {
        auto *component = context.GetComponent<ECS::TagComponent>();
        if (component == nullptr || context._content == nullptr)
            return;

        const Vector<String> tags{"Untagged", "MainCamera"};
        auto *tag_selector = AddDropdownRow(context._content, "Tag", tags);
        tag_selector->SetSelectedIndex(component->_tag == "MainCamera" ? 1 : 0);
        auto *scene = context._scene;
        tag_selector->_on_selected_changed += [component, scene](i32 index)
        {
            component->_tag = index == 1 ? "MainCamera" : "Untagged";
            scene->MarkEdited();
        };

        const Vector<String> layers{"Default", "SkyBox"};
        auto *layer_selector = AddDropdownRow(context._content, "Layer", layers);
        layer_selector->SetSelectedIndex(component->_layer_mask == static_cast<u32>(Render::ERenderLayer::kSkyBox) ? 1 : 0);
        layer_selector->_on_selected_changed += [component, scene](i32 index)
        {
            component->_layer_mask = index == 1 ? static_cast<u32>(Render::ERenderLayer::kSkyBox) :
                                                  static_cast<u32>(Render::ERenderLayer::kDefault);
            scene->MarkEdited();
        };
    }
}
