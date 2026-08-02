#include "Inspector/ComponentEditors/LightComponentEditor.h"
#include "Inspector/ComponentEditorRegistry.h"
#include "Inspector/ComponentEditorHelpers.h"
#include "Scene/Scene.h"
#include "UI/Basic.h"
#include "UI/ColorPicker.h"
#include "UI/UIFramework.h"

using namespace Ailu;
using namespace Ailu::UI;
using SceneManagement::SceneMgr;

namespace Ailu
{
    namespace Editor
    {
        void LightComponentEditor::Build(ComponentEditorContext &context)
        {
            auto *comp = context.GetComponent<ECS::LightComponent>();
            if (comp == nullptr || context._content == nullptr)
                return;

            _cached_light_type = comp->_type;

            auto items = Vector<String>{"Directional", "Point", "Spot", "Area"};
            auto light_type_dropdown = Editor::AddDropdownRow(context._content, "Type", items);
            light_type_dropdown->SetSelectedIndex(static_cast<i32>(comp->_type));
            light_type_dropdown->_on_selected_changed += [comp](i32 idx)
            {
                comp->_type = static_cast<ECS::ELightType>(idx);
            };

            Editor::AddFloatSliderRow(context._content, "Intensity", 0.0f, 100.0f, comp->_light._light_color.a, [comp](f32 value)
            {
                comp->_light._light_color.a = value;
            });

            {
                auto btn = Editor::AddButtonRow(context._content, "Color",
                    FormatColorButtonText(Vector4f(comp->_light._light_color.r, comp->_light._light_color.g, comp->_light._light_color.b, comp->_light._light_color.a)));
                btn->OnMouseClick() += [comp, btn](UIEvent &e)
                {
                    auto color_picker = MakeRef<ColorPicker>(
                        Vector4f(comp->_light._light_color.r, comp->_light._light_color.g, comp->_light._light_color.b, 1.0f));
                    color_picker->Name("LightColor");
                    color_picker->GetSlot()->Size({320.0f, 240.0f});
                    color_picker->OnValueChanged() += [comp, btn](Vector4f color)
                    {
                        comp->_light._light_color.r = color.r;
                        comp->_light._light_color.g = color.g;
                        comp->_light._light_color.b = color.b;
                        btn->SetText(FormatColorButtonText(Vector4f(color.r, color.g, color.b, comp->_light._light_color.a)));
                    };
                    auto abs_rect = e._current_target->GetArrangeRect();
                    Vector2f show_pos = abs_rect.xy;
                    show_pos.y += abs_rect.w;
                    UIManager::Get()->ShowPopupAt(show_pos.x, show_pos.y, color_picker);
                };
            }

            if (comp->_type == ECS::ELightType::kDirectional)
            {
                auto &light_data = comp->_light;
                if (light_data._light_param.w <= 0.0f)
                    light_data._light_param.w = 0.265f;
                Editor::AddFloatSliderRow(context._content, "AngularRadius", 0.01f, 5.0f, light_data._light_param.w, [&light_data](f32 value)
                {
                    light_data._light_param.w = value;
                });
            }
            else if (comp->_type == ECS::ELightType::kArea)
            {
                static const Vector<String> kShapes = {"Rectangle", "Disc"};
                auto &light_data = comp->_light;
                auto dropdown = Editor::AddDropdownRow(context._content, "Shape", kShapes);
                dropdown->SetSelectedIndex(static_cast<i32>(light_data._light_param.w));
                dropdown->_on_selected_changed += [&light_data](i32 idx)
                {
                    light_data._light_param.w = static_cast<f32>(idx);
                };
                Editor::AddFloatSliderRow(context._content, "Range", 0.0f, 500.0f, light_data._light_param.x, [&light_data](f32 value)
                {
                    light_data._light_param.x = value;
                });
                Editor::AddCheckBoxRow(context._content, "TwoSide", light_data._is_two_side)->_on_click += [&light_data](bool checked)
                {
                    light_data._is_two_side = checked;
                };
                if (light_data._light_param.w == 0)
                {
                    Editor::AddFloatInputRow(context._content, "Width", std::to_string(light_data._light_param.y), [&light_data](f32 value)
                    {
                        light_data._light_param.y = value;
                    });
                    Editor::AddFloatInputRow(context._content, "Height", std::to_string(light_data._light_param.z), [&light_data](f32 value)
                    {
                        light_data._light_param.z = value;
                    });
                }
                else
                {
                    Editor::AddFloatSliderRow(context._content, "Radius", 0.0f, 10.0f, light_data._light_param.x, [&light_data](f32 value)
                    {
                        light_data._light_param.x = value;
                    });
                }
            }
            else if (comp->_type == ECS::ELightType::kSpot)
            {
                auto &light_data = comp->_light;
                Editor::AddFloatSliderRow(context._content, "Range", 0.0f, 500.0f, light_data._light_param.x, [&light_data](f32 value)
                {
                    light_data._light_param.x = value;
                });
                Editor::AddFloatSliderRow(context._content, "InnerAngle", 0.0f, 180.0f, light_data._light_param.y, [&light_data](f32 value)
                {
                    light_data._light_param.y = value;
                });
                Editor::AddFloatSliderRow(context._content, "OuterAngle", 0.0f, 180.0f, light_data._light_param.z, [&light_data](f32 value)
                {
                    light_data._light_param.z = value;
                });
            }
            else if (comp->_type == ECS::ELightType::kPoint)
            {
                auto &light_data = comp->_light;
                Editor::AddFloatSliderRow(context._content, "Range", 0.0f, 80.0f, light_data._light_param.x, [&light_data](f32 value)
                {
                    light_data._light_param.x = value;
                });
                Editor::AddFloatSliderRow(context._content, "Radius", 0.0f, 1.0f, light_data._light_param.y, [&light_data](f32 value)
                {
                    light_data._light_param.y = value;
                });
            }
        }

        void LightComponentEditor::Refresh(ComponentEditorContext &context)
        {
            auto *comp = context.GetComponent<ECS::LightComponent>();
            if (comp == nullptr)
                return;
            _cached_light_type = comp->_type;
        }

        bool LightComponentEditor::NeedsRebuild(const ComponentEditorContext &context) const
        {
            auto *comp = context.GetComponent<ECS::LightComponent>();
            if (comp == nullptr)
                return false;
            return comp->_type != _cached_light_type;
        }
    }// namespace Editor
}// namespace Ailu
