#include "Inspector/ComponentEditors/LightProbeComponentEditor.h"
#include "Inspector/ComponentEditorRegistry.h"
#include "Inspector/ComponentEditorHelpers.h"
#include "Scene/Component.h"

using namespace Ailu;
using namespace Ailu::UI;

namespace Ailu
{
    namespace Editor
    {
        void LightProbeComponentEditor::Build(ComponentEditorContext &context)
        {
            auto *comp = context.GetComponent<ECS::CLightProbe>();
            if (comp == nullptr || context._content == nullptr)
                return;

            auto btn = context._content->AddChild<Button>();
            btn->SetText("Update Probe");
            btn->OnMouseClick() += [comp](UIEvent &e)
            {
                comp->_is_dirty = true;
            };

            Editor::AddCheckBoxRow(context._content, "UpdateTick", comp->_is_update_every_tick)->_on_click += [comp](bool is_on)
            {
                comp->_is_update_every_tick = is_on;
            };
        }
    }// namespace Editor
}// namespace Ailu
