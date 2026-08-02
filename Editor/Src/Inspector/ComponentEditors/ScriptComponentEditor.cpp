#include "Inspector/ComponentEditors/ScriptComponentEditor.h"
#include "Inspector/ComponentEditorRegistry.h"
#include "Inspector/ComponentEditorHelpers.h"
#include "Scene/Scene.h"

using namespace Ailu;
using namespace Ailu::UI;
using SceneManagement::SceneMgr;

namespace Ailu
{
    namespace Editor
    {
        void ScriptComponentEditor::Build(ComponentEditorContext &context)
        {
            auto *comp = context.GetComponent<ECS::ScriptComponent>();
            if (comp == nullptr || context._content == nullptr)
                return;

            auto entity = context._entity;

            Editor::AddTextInputRow(context._content, "Path", comp->_script_path, [entity](const String &content)
            {
                auto &r = SceneMgr::Get().ActiveScene()->GetRegister();
                auto *script_comp = r.GetComponent<ECS::ScriptComponent>(entity);
                if (script_comp == nullptr)
                    return;
                if (script_comp->_script_path == content)
                    return;
                script_comp->_script_path = content;
                script_comp->ResetRuntime();
                SceneMgr::Get().MarkCurSceneDirty();
            });

            auto hint = context._content->AddChild<Text>("Example: Scripts/tick_logger.lua");
            hint->GetSlotAs<LinearSlot>().Margin({2.0f, 0.0f, 2.0f, 2.0f});
        }

        void ScriptComponentEditor::Refresh(ComponentEditorContext &context)
        {
        }
    }// namespace Editor
}// namespace Ailu
