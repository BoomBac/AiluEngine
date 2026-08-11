#include "Inspector/ComponentEditors/Collider2DComponentEditor.h"

#include "Inspector/ComponentEditorRegistry.h"
#include "Inspector/ComponentEditorHelpers.h"
#include "Physics/2D/Physics2DComponents.h"

namespace Ailu::Editor
{
    void Collider2DComponentEditor::Build(ComponentEditorContext &context)
    {
        auto *component = context.GetComponent<ECS::Collider2DComponent>();
        if (component == nullptr || context._content == nullptr)
            return;

        AddButtonRow(context._content, "Shapes", "Add Shape")->OnMouseClick() += [component, context](UI::UIEvent &) {
            component->_shapes.push_back(ECS::ColliderShape2D{});
            context.MarkSceneDirty();
            context.RequestRebuild();
        };

        const Vector<String> shape_types{"Box", "Circle", "Capsule", "Polygon", "Chain"};
        for (u32 shape_index = 0u; shape_index < component->_shapes.size(); ++shape_index)
        {
            ECS::ColliderShape2D &shape = component->_shapes[shape_index];
            const String prefix = std::format("Shape {}", shape_index);
            auto type_dropdown = AddDropdownRow(context._content, prefix + " Type", shape_types);
            type_dropdown->SetSelectedIndex(static_cast<i32>(shape._type));
            type_dropdown->_on_selected_changed += [component, shape_index, context](i32 index) {
                component->_shapes[shape_index]._type = static_cast<ECS::ECollider2DShape>(index);
                context.MarkSceneDirty();
                context.RequestRebuild();
            };
            AddFloatInputRow(context._content, prefix + " Center X", std::format("{:.2f}", shape._center.x),
                             [component, shape_index, context](f32 value) { component->_shapes[shape_index]._center.x = value; context.MarkSceneDirty(); });
            AddFloatInputRow(context._content, prefix + " Center Y", std::format("{:.2f}", shape._center.y),
                             [component, shape_index, context](f32 value) { component->_shapes[shape_index]._center.y = value; context.MarkSceneDirty(); });
            AddFloatInputRow(context._content, prefix + " Rotation", std::format("{:.2f}", shape._rotation),
                             [component, shape_index, context](f32 value) { component->_shapes[shape_index]._rotation = value; context.MarkSceneDirty(); });
            if (shape._type == ECS::ECollider2DShape::kBox)
            {
                AddFloatInputRow(context._content, prefix + " Width", std::format("{:.2f}", shape._size.x),
                                 [component, shape_index, context](f32 value) { component->_shapes[shape_index]._size.x = value; context.MarkSceneDirty(); });
                AddFloatInputRow(context._content, prefix + " Height", std::format("{:.2f}", shape._size.y),
                                 [component, shape_index, context](f32 value) { component->_shapes[shape_index]._size.y = value; context.MarkSceneDirty(); });
            }
            else if (shape._type == ECS::ECollider2DShape::kCircle || shape._type == ECS::ECollider2DShape::kCapsule)
            {
                AddFloatInputRow(context._content, prefix + " Radius", std::format("{:.2f}", shape._radius),
                                 [component, shape_index, context](f32 value) { component->_shapes[shape_index]._radius = value; context.MarkSceneDirty(); });
                if (shape._type == ECS::ECollider2DShape::kCapsule)
                    AddFloatInputRow(context._content, prefix + " Height", std::format("{:.2f}", shape._height),
                                     [component, shape_index, context](f32 value) { component->_shapes[shape_index]._height = value; context.MarkSceneDirty(); });
            }
            AddCheckBoxRow(context._content, prefix + " Trigger", shape._is_trigger)->_on_click += [component, shape_index, context](bool value) { component->_shapes[shape_index]._is_trigger = value; context.MarkSceneDirty(); };
            AddFloatInputRow(context._content, prefix + " Layer", std::to_string(shape._layer),
                             [component, shape_index, context](f32 value) { component->_shapes[shape_index]._layer = static_cast<u8>(std::clamp(value, 0.0f, 31.0f)); context.MarkSceneDirty(); });
            AddFloatInputRow(context._content, prefix + " Density", std::format("{:.2f}", shape._density),
                             [component, shape_index, context](f32 value) { component->_shapes[shape_index]._density = value; context.MarkSceneDirty(); });
            AddFloatInputRow(context._content, prefix + " Friction", std::format("{:.2f}", shape._friction),
                             [component, shape_index, context](f32 value) { component->_shapes[shape_index]._friction = value; context.MarkSceneDirty(); });
            AddFloatInputRow(context._content, prefix + " Restitution", std::format("{:.2f}", shape._restitution),
                             [component, shape_index, context](f32 value) { component->_shapes[shape_index]._restitution = value; context.MarkSceneDirty(); });
            AddButtonRow(context._content, prefix, "Remove")->OnMouseClick() += [component, shape_index, context](UI::UIEvent &) {
                component->_shapes.erase(component->_shapes.begin() + shape_index);
                context.MarkSceneDirty();
                context.RequestRebuild();
            };
        }
    }
}
