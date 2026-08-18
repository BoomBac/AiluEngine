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

        auto collision_panel = context._content->AddChild<UI::CollapsibleView>("Collision");
        collision_panel->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
        collision_panel->SetCollapsed(true);
        auto collision_content = static_cast<UI::VerticalBox *>(
            collision_panel->GetContent()->AddChild<UI::VerticalBox>());
        collision_content->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);

        const Vector<String> preset_names{"Default", "Player", "Enemy", "WorldStatic", "WorldDynamic", "Projectile",
                                          "Trigger", "Pickup", "Custom"};
        auto preset_dropdown = AddDropdownRow(collision_content, "Collision Preset", preset_names);
        preset_dropdown->SetSelectedIndex(static_cast<i32>(component->_preset));

        const Vector<String> channel_names{"WorldStatic", "WorldDynamic", "Player", "Enemy", "Projectile", "Trigger",
                                           "Pickup"};
        auto object_type_dropdown = AddDropdownRow(collision_content, "Object Type", channel_names);
        object_type_dropdown->SetSelectedIndex(static_cast<i32>(component->_collision_profile._object_type));

        const Vector<String> response_names{"Ignore", "Overlap", "Block"};
        auto suppress_callbacks = std::make_shared<bool>(false);
        Vector<Array<UI::CheckBox *, 3>> response_checkboxes(channel_names.size());
        for (u32 channel_index = 0u; channel_index < channel_names.size(); ++channel_index)
        {
            UI::HorizontalBox *value_box = nullptr;
            AddPropertyRow(collision_content, channel_names[channel_index], &value_box);
            Array<UI::HorizontalBox *, 3> response_options{};
            for (u32 response_index = 0u; response_index < response_names.size(); ++response_index)
            {
                auto response_option = value_box->AddChild<UI::HorizontalBox>();
                response_options[response_index] = response_option;
                response_option->GetSlotAs<UI::LinearSlot>().Margin(kPropInnerMargin)
                    .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto).FillRate(1.0f);
                auto response_checkbox = response_option->AddChild<UI::CheckBox>();
                response_checkboxes[channel_index][response_index] = response_checkbox;
                response_checkbox->GetSlotAs<UI::LinearSlot>().Margin({0.0f, 0.0f, 4.0f, 0.0f})
                    .SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed).Size({16.0f, 16.0f});
                response_option->AddChild<UI::Text>(response_names[response_index])
                    ->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
                response_checkbox->SetChecked(static_cast<u32>(component->_collision_profile.GetResponse(
                    static_cast<ECS::ECollisionChannel2D>(channel_index))) == response_index);
            }
            for (u32 response_index = 0u; response_index < response_names.size(); ++response_index)
            {
                response_options[response_index]->OnMouseClick() +=
                    [response_checkboxes, component, context, preset_dropdown, suppress_callbacks, channel_index,
                     response_index](UI::UIEvent &e) {
                    for (u32 option_index = 0u; option_index < response_checkboxes[channel_index].size();
                         ++option_index)
                    {
                        response_checkboxes[channel_index][option_index]->SetChecked(option_index == response_index);
                    }
                    component->_collision_profile.SetResponse(
                        static_cast<ECS::ECollisionChannel2D>(channel_index),
                        static_cast<ECS::ECollisionResponse2D>(response_index));
                    component->_preset = ECS::ECollisionPreset2D::kCustom;
                    *suppress_callbacks = true;
                    preset_dropdown->SetSelectedIndex(static_cast<i32>(ECS::ECollisionPreset2D::kCustom));
                    *suppress_callbacks = false;
                    context.MarkSceneDirty();
                    e._is_handled = true;
                };
            }
        }

        preset_dropdown->_on_selected_changed +=
            [component, context, object_type_dropdown, response_checkboxes, suppress_callbacks](i32 index) {
            if (*suppress_callbacks)
                return;
            const auto preset = static_cast<ECS::ECollisionPreset2D>(index);
            component->_preset = preset;
            ECS::ApplyCollisionPreset2D(component->_collision_profile, preset);
            *suppress_callbacks = true;
            object_type_dropdown->SetSelectedIndex(static_cast<i32>(component->_collision_profile._object_type));
            for (u32 channel_index = 0u; channel_index < response_checkboxes.size(); ++channel_index)
            {
                const auto response = static_cast<u32>(component->_collision_profile.GetResponse(
                    static_cast<ECS::ECollisionChannel2D>(channel_index)));
                for (u32 response_index = 0u; response_index < response_checkboxes[channel_index].size();
                     ++response_index)
                {
                    response_checkboxes[channel_index][response_index]->SetChecked(response_index == response);
                }
            }
            *suppress_callbacks = false;
            context.MarkSceneDirty();
        };
        object_type_dropdown->_on_selected_changed +=
            [component, context, preset_dropdown, suppress_callbacks](i32 index) {
            if (*suppress_callbacks)
                return;
            component->_collision_profile._object_type = static_cast<ECS::ECollisionChannel2D>(index);
            component->_preset = ECS::ECollisionPreset2D::kCustom;
            *suppress_callbacks = true;
            preset_dropdown->SetSelectedIndex(static_cast<i32>(ECS::ECollisionPreset2D::kCustom));
            *suppress_callbacks = false;
            context.MarkSceneDirty();
        };

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
