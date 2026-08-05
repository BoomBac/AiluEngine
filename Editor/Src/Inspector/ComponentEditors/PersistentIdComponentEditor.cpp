#include "Inspector/ComponentEditors/PersistentIdComponentEditor.h"
#include "Inspector/ComponentEditorRegistry.h"
#include "Inspector/ComponentEditorHelpers.h"
#include "Scene/Component.h"

#include "Ext/imgui/imgui.h"

using namespace Ailu;
using namespace Ailu::UI;

namespace Ailu
{
    namespace Editor
    {
        void PersistentIdComponentEditor::Build(ComponentEditorContext &context)
        {
            auto *comp = context.GetComponent<ECS::PersistentIdComponent>();
            if (comp == nullptr || context._content == nullptr)
                return;

            // 只读展示 GUID，不提供编辑入口。
            UI::HorizontalBox *value_box = nullptr;
            AddPropertyRow(context._content, "GUID", &value_box);
            value_box->AddChild<UI::Text>(comp->_guid.ToString())
                ->GetSlotAs<UI::LinearSlot>()
                .Margin(kPropInnerMargin)
                .CrossAlignment(UI::EAlignment::kRight)
                .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto)
                .FillRate(3.0f);

            auto copy_btn = value_box->AddChild<UI::Button>("Copy");
            copy_btn->GetSlotAs<UI::LinearSlot>()
                .Margin(kPropInnerMargin)
                .CrossAlignment(UI::EAlignment::kRight);
            const String guid_str = comp->_guid.ToString();
            copy_btn->OnMouseClick() += [guid_str](UI::UIEvent &e)
            {
                ImGui::SetClipboardText(guid_str.c_str());
                e._is_handled = true;
            };
        }

        void PersistentIdComponentEditor::Refresh(ComponentEditorContext &context)
        {
        }
    }// namespace Editor
}// namespace Ailu
