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
            type_dropdown->_on_selected_changed += [comp](i32 idx)
            {
                comp->_camera.Type(static_cast<Render::ECameraType>(idx));
            };

            Editor::AddFloatInputRow(context._content, "Near", std::format("{}", comp->_camera.Near()), [comp](f32 v)
            {
                comp->_camera.Near(v);
            });

            Editor::AddFloatInputRow(context._content, "Far", std::format("{}", comp->_camera.Far()), [comp](f32 v)
            {
                comp->_camera.Far(v);
            });

            Editor::AddFloatInputRow(context._content, "Aspect", std::format("{}", comp->_camera.Aspect()), [comp](f32 v)
            {
                comp->_camera.Aspect(v);
            });
        }
    }// namespace Editor
}// namespace Ailu
