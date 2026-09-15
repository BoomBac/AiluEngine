#include "Inspector/ComponentEditors/CameraComponentEditor.h"
#include "Inspector/ComponentEditorRegistry.h"
#include "Inspector/ComponentEditorHelpers.h"
#include "Scene/Scene.h"

using namespace Ailu;
using namespace Ailu::UI;

namespace Ailu
{
    namespace Editor
    {
        void CameraComponentEditor::Build(ComponentEditorContext &context)
        {
            auto *comp = context.GetComponent<ECS::CCamera>();
            if (comp == nullptr || context._content == nullptr)
                return;

            auto items = Vector<String>{"Orthographic", "Perspective"};
            auto type_dropdown = Editor::AddDropdownRow(context._content, "Type", items);
            type_dropdown->SetSelectedIndex(static_cast<i32>(comp->_camera.Type()));
            type_dropdown->_on_selected_changed += [comp, context](i32 idx)
            {
                comp->_camera.Type(static_cast<Render::ECameraType>(idx));
                context.MarkSceneDirty();
            };

            Editor::AddFloatInputRow(context._content, "Near", std::format("{}", comp->_camera.Near()), [comp, context](f32 v)
            {
                comp->_camera.Near(v);
                context.MarkSceneDirty();
            });

            Editor::AddFloatInputRow(context._content, "Far", std::format("{}", comp->_camera.Far()), [comp, context](f32 v)
            {
                comp->_camera.Far(v);
                context.MarkSceneDirty();
            });

            Editor::AddFloatInputRow(context._content, "Aspect", std::format("{}", comp->_camera.Aspect()), [comp, context](f32 v)
            {
                comp->_camera.Aspect(v);
                context.MarkSceneDirty();
            });

            if (comp->_camera.Type() == Render::ECameraType::kOrthographic)
            {
                Editor::AddFloatInputRow(context._content, "Size", std::format("{}", comp->_camera.Size()), [comp, context](f32 v)
                {
                    comp->_camera.Size(v);
                    context.MarkSceneDirty();
                });
            }
            else
            {
                Editor::AddFloatInputRow(context._content, "Fov", std::format("{}", comp->_camera.FovH()), [comp, context](f32 v)
                {
                    comp->_camera.FovH(v);
                    context.MarkSceneDirty();
                });
            }
            _cached_type = static_cast<i32>(comp->_camera.Type());
        }

        bool CameraComponentEditor::NeedsRebuild(const ComponentEditorContext &context) const
        {
            const auto *comp = context.GetComponent<ECS::CCamera>();
            return comp == nullptr || _cached_type != static_cast<i32>(comp->_camera.Type());
        }
    }// namespace Editor
}// namespace Ailu
