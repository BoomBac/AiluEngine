#include "Inspector/ComponentEditors/ColliderComponentEditor.h"
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
        void ColliderComponentEditor::Build(ComponentEditorContext &context)
        {
            auto *comp = context.GetComponent<ECS::CCollider>();
            if (comp == nullptr || context._content == nullptr)
                return;

            _cached_type = comp->_type;

            {
                auto items = Vector<String>{"Box", "Sphere", "Capsule"};
                auto dropdown = Editor::AddDropdownRow(context._content, "Type", items);
                dropdown->SetSelectedIndex(static_cast<i32>(comp->_type));
                dropdown->_on_selected_changed += [comp](i32 idx)
                {
                    comp->_type = static_cast<ECS::EColliderType>(idx);
                    SceneMgr::Get().MarkCurSceneDirty();
                };
            }

            Editor::AddCheckBoxRow(context._content, "Is Trigger", comp->_is_trigger)->_on_click += [comp](bool checked)
            {
                comp->_is_trigger = checked;
                SceneMgr::Get().MarkCurSceneDirty();
            };

            {
                auto blocks = Editor::AddVec3InputRow(context._content, "Center",
                    std::format("{:.2f}", comp->_center.x),
                    std::format("{:.2f}", comp->_center.y),
                    std::format("{:.2f}", comp->_center.z),
                    [comp](int axis, f32 v)
                    {
                        comp->_center[axis] = v;
                        SceneMgr::Get().MarkCurSceneDirty();
                    });
            }

            if (comp->_type == ECS::EColliderType::kBox)
            {
                auto blocks = Editor::AddVec3InputRow(context._content, "Size",
                    std::format("{:.2f}", comp->_param.x),
                    std::format("{:.2f}", comp->_param.y),
                    std::format("{:.2f}", comp->_param.z),
                    [comp](int axis, f32 v)
                    {
                        comp->_param[axis] = v;
                        SceneMgr::Get().MarkCurSceneDirty();
                    });
            }
            else if (comp->_type == ECS::EColliderType::kSphere)
            {
                Editor::AddFloatInputRow(context._content, "Radius", std::format("{:.2f}", comp->_param.x), [comp](f32 v)
                {
                    comp->_param.x = v;
                    SceneMgr::Get().MarkCurSceneDirty();
                });
            }
            else if (comp->_type == ECS::EColliderType::kCapsule)
            {
                Editor::AddFloatInputRow(context._content, "Radius", std::format("{:.2f}", comp->_param.x), [comp](f32 v)
                {
                    comp->_param.x = v;
                    SceneMgr::Get().MarkCurSceneDirty();
                });
                Editor::AddFloatInputRow(context._content, "Height", std::format("{:.2f}", comp->_param.y), [comp](f32 v)
                {
                    comp->_param.y = v;
                    SceneMgr::Get().MarkCurSceneDirty();
                });
            }
        }

        bool ColliderComponentEditor::NeedsRebuild(const ComponentEditorContext &context) const
        {
            auto *comp = context.GetComponent<ECS::CCollider>();
            if (comp == nullptr)
                return false;
            return comp->_type != _cached_type;
        }
    }// namespace Editor
}// namespace Ailu
