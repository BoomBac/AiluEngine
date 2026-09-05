#include "Inspector/ComponentEditors/AnimatorComponentEditor.h"

#include "Animation/AnimationControllerAsset.h"
#include "Animation/Clip.h"
#include "Inspector/ComponentEditorHelpers.h"
#include "Inspector/ComponentEditorRegistry.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <format>

namespace Ailu
{
    namespace Editor
    {
        void AnimatorComponentEditor::Build(ComponentEditorContext &context)
        {
            auto *component = context.GetComponent<ECS::AnimatorComponent>();
            if (component == nullptr || context._content == nullptr)
                return;

            auto *controller_dropdown = AddObjectAssetDropdownRow(context._content, "Controller",
                                                                  AnimationControllerAsset::StaticType(), component->_controller);
            controller_dropdown->_on_object_asset_selected += [component](Asset *, Object *, const Guid &guid)
            {
                component->_controller = guid;
                if (!guid.IsEmpty())
                    component->_clip = Guid::EmptyGuid();
                component->_instance = kInvalidAnimationInstanceHandle;
                SceneManagement::SceneMgr::Get().MarkCurSceneDirty();
            };

            auto *clip_dropdown = AddObjectAssetDropdownRow(context._content, "Clip", AnimationClip::StaticType(), component->_clip);
            clip_dropdown->_on_object_asset_selected += [component](Asset *, Object *, const Guid &guid)
            {
                component->_clip = guid;
                if (!guid.IsEmpty())
                    component->_controller = Guid::EmptyGuid();
                component->_instance = kInvalidAnimationInstanceHandle;
                SceneManagement::SceneMgr::Get().MarkCurSceneDirty();
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

            auto *root_motion_mode = AddDropdownRow(context._content, "Root Motion",
                                                    {"Disabled", "Extract Only", "Apply"});
            root_motion_mode->SetSelectedIndex(static_cast<i32>(component->_root_motion_mode), false);
            root_motion_mode->_on_selected_changed += [component](i32 index)
            {
                if (index >= 0 && index <= 2)
                    component->_root_motion_mode = static_cast<ERootMotionMode>(index);
                SceneManagement::SceneMgr::Get().MarkCurSceneDirty();
            };
        }
    }// namespace Editor
}// namespace Ailu
