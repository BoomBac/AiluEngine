#include "Inspector/ComponentEditors/AnimatorComponentEditor.h"

#include "Animation/AnimationControllerAsset.h"
#include "Assets/Asset.h"
#include "Inspector/ComponentEditorHelpers.h"
#include "Inspector/ComponentEditorRegistry.h"
#include "Framework/Common/ResourceMgr.h"
#include "Scene/Scene.h"
#include "UI/Basic.h"
#include "UI/Container.h"
#include "UI/UIFramework.h"

#include <algorithm>
#include <format>

namespace Ailu
{
    namespace Editor
    {
        namespace
        {
            void ShowControllerPicker(UI::UIElement *anchor, ECS::AnimatorComponent *component)
            {
                auto list = MakeRef<UI::ListView>();
                list->SetViewportHeight(220.0f);
                auto none = MakeRef<UI::Text>("None");
                none->OnMouseClick() += [component](UI::UIEvent &)
                {
                    component->_controller = Guid::EmptyGuid();
                    component->_instance = kInvalidAnimationInstanceHandle;
                    SceneManagement::SceneMgr::Get().MarkCurSceneDirty();
                    UI::UIManager::Get()->HidePopup();
                };
                list->AddItem(none);
                for (auto it = ResourceMgr::Get().Begin(); it != ResourceMgr::Get().End(); ++it)
                {
                    Asset *asset = it->second.get();
                    if (asset == nullptr || asset->_asset_type != AnimationControllerAsset::StaticType())
                        continue;
                    auto controller = ResourceMgr::Get().Load<AnimationControllerAsset>(asset->_asset_path);
                    if (!controller)
                        continue;
                    auto item = MakeRef<UI::Text>(controller->Name());
                    const Guid guid = asset->GetGuid();
                    item->OnMouseClick() += [component, guid](UI::UIEvent &)
                    {
                        component->_controller = guid;
                        component->_instance = kInvalidAnimationInstanceHandle;
                        SceneManagement::SceneMgr::Get().MarkCurSceneDirty();
                        UI::UIManager::Get()->HidePopup();
                    };
                    list->AddItem(item);
                }
                const auto rect = anchor->GetArrangeRect();
                UI::UIManager::Get()->ShowPopupAt(rect.x, rect.y + rect.w, list);
            }
        }

        void AnimatorComponentEditor::Build(ComponentEditorContext &context)
        {
            auto *component = context.GetComponent<ECS::AnimatorComponent>();
            if (component == nullptr || context._content == nullptr)
                return;

            auto *controller_button = AddButtonRow(context._content, "Controller", "None");
            if (!component->_controller.IsEmpty())
            {
                if (auto *controller = ResourceMgr::Get().Get<AnimationControllerAsset>(component->_controller); controller != nullptr)
                    controller_button->SetText(controller->Name());
                else
                    controller_button->SetText(component->_controller.ToString());
            }
            controller_button->OnMouseClick() += [component](UI::UIEvent &event)
            {
                ShowControllerPicker(event._current_target, component);
                event._is_handled = true;
            };

            AddFloatInputRow(context._content, "Speed", std::format("{:.3f}", component->_speed), [component](f32 value)
            {
                component->_speed = std::max(value, 0.0f);
                SceneManagement::SceneMgr::Get().MarkCurSceneDirty();
            });

            auto *play_on_awake = AddCheckBoxRow(context._content, "Play On Awake", component->_play_on_awake);
            play_on_awake->_on_click += [component](bool value)
            {
                component->_play_on_awake = value;
                SceneManagement::SceneMgr::Get().MarkCurSceneDirty();
            };
        }
    }// namespace Editor
}// namespace Ailu
