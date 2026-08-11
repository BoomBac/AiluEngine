#include "Inspector/ComponentEditors/RigidBody2DComponentEditor.h"

#include "Inspector/ComponentEditorRegistry.h"
#include "Inspector/ComponentEditorHelpers.h"
#include "Physics/2D/Physics2DComponents.h"

namespace Ailu::Editor
{
    void RigidBody2DComponentEditor::Build(ComponentEditorContext &context)
    {
        auto *component = context.GetComponent<ECS::RigidBody2DComponent>();
        if (component == nullptr || context._content == nullptr)
            return;

        const Vector<String> body_types{"Static", "Kinematic", "Dynamic"};
        auto type_dropdown = AddDropdownRow(context._content, "Body Type", body_types);
        type_dropdown->SetSelectedIndex(static_cast<i32>(component->_type));
        type_dropdown->_on_selected_changed += [component, context](i32 index) {
            component->_type = static_cast<ECS::EBody2DType>(index);
            context.MarkSceneDirty();
        };
        AddFloatInputRow(context._content, "Gravity Scale", std::format("{:.2f}", component->_gravity_scale),
                         [component, context](f32 value) { component->_gravity_scale = value; context.MarkSceneDirty(); });
        AddFloatInputRow(context._content, "Linear Damping", std::format("{:.2f}", component->_linear_damping),
                         [component, context](f32 value) { component->_linear_damping = value; context.MarkSceneDirty(); });
        AddFloatInputRow(context._content, "Angular Damping", std::format("{:.2f}", component->_angular_damping),
                         [component, context](f32 value) { component->_angular_damping = value; context.MarkSceneDirty(); });
        AddCheckBoxRow(context._content, "Fixed Rotation", component->_fixed_rotation)->_on_click += [component, context](bool value) { component->_fixed_rotation = value; context.MarkSceneDirty(); };
        AddCheckBoxRow(context._content, "Continuous", component->_continuous)->_on_click += [component, context](bool value) { component->_continuous = value; context.MarkSceneDirty(); };
        AddCheckBoxRow(context._content, "Allow Sleep", component->_allow_sleep)->_on_click += [component, context](bool value) { component->_allow_sleep = value; context.MarkSceneDirty(); };
    }
}
