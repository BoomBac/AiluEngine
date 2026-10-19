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
        namespace
        {
            void UpdateColorButton(Button *button, const Vector4f &color)
            {
                if (button == nullptr)
                    return;

                const Color srgb = Color(color).ToSrgb();
                UIControlVisual visual;
                visual._background._type = EUIBrushType::kColor;
                visual._background._tint = Color(color);
                visual._border_color = Colors::kWhite;
                visual._border_width = Vector4f(1.0f);
                visual._corner_radius = Vector4f(3.0f);
                visual._content_color = srgb.r * 0.299f + srgb.g * 0.587f + srgb.b * 0.114f > 0.5f
                    ? Colors::kBlack : Colors::kWhite;

                auto &style_override = button->GetStyleOverride();
                style_override.SetNormal(visual);
                style_override.SetHovered(visual);
                style_override.SetPressed(visual);
                style_override.SetFocused(visual);
                button->InvalidateStyle(EStyleInvalidation::kPaintOnly);
            }
        }

        void LightComponentEditor::Build(ComponentEditorContext &context)
        {
            auto *comp = context.GetComponent<ECS::LightComponent>();
            if (comp == nullptr || context._content == nullptr)
                return;

            _cached_light_type = comp->_type;
            _cached_use_color_temperature = comp->_light._use_color_temperature;

            auto items = Vector<String>{"Directional", "Point", "Spot", "Area"};
            auto light_type_dropdown = Editor::AddDropdownRow(context._content, "Type", items);
            light_type_dropdown->SetSelectedIndex(static_cast<i32>(comp->_type));
            light_type_dropdown->_on_selected_changed += [comp, context](i32 idx)
            {
                comp->_type = static_cast<ECS::ELightType>(idx);
                const auto unit = comp->_light._intensity_unit;
                if (comp->_type == ECS::ELightType::kDirectional)
                {
                    if (unit != ECS::ELightIntensityUnit::kLux &&
                        unit != ECS::ELightIntensityUnit::kWattPerSquareMeter &&
                        unit != ECS::ELightIntensityUnit::kUnitlessLegacy)
                        comp->_light._intensity_unit = ECS::ELightIntensityUnit::kLux;
                }
                else if (unit == ECS::ELightIntensityUnit::kLux ||
                         unit == ECS::ELightIntensityUnit::kWattPerSquareMeter)
                    comp->_light._intensity_unit = ECS::ELightIntensityUnit::kCandela;
                context.MarkSceneDirty();
            };

            auto &light_data = comp->_light;
            Vector<String> intensity_units = comp->_type == ECS::ELightType::kDirectional
                ? Vector<String>{"Lux (lx)", "Radiant W/m2", "Legacy Unitless"}
                : Vector<String>{"Candela (cd)", "Lumen (lm)", "Radiant Watt", "Legacy Unitless"};
            auto intensity_unit = Editor::AddDropdownRow(context._content, "Intensity Unit", intensity_units);
            i32 selected_unit = 0;
            if (comp->_type == ECS::ELightType::kDirectional)
            {
                if (light_data._intensity_unit == ECS::ELightIntensityUnit::kWattPerSquareMeter)
                    selected_unit = 1;
                else if (light_data._intensity_unit == ECS::ELightIntensityUnit::kUnitlessLegacy)
                    selected_unit = 2;
            }
            else if (light_data._intensity_unit == ECS::ELightIntensityUnit::kLumen)
                selected_unit = 1;
            else if (light_data._intensity_unit == ECS::ELightIntensityUnit::kWatt)
                selected_unit = 2;
            else if (light_data._intensity_unit == ECS::ELightIntensityUnit::kUnitlessLegacy)
                selected_unit = 3;
            intensity_unit->SetSelectedIndex(selected_unit);
            intensity_unit->_on_selected_changed += [comp, context](i32 index)
            {
                if (comp->_type == ECS::ELightType::kDirectional)
                {
                    constexpr ECS::ELightIntensityUnit kUnits[] = {
                        ECS::ELightIntensityUnit::kLux, ECS::ELightIntensityUnit::kWattPerSquareMeter,
                        ECS::ELightIntensityUnit::kUnitlessLegacy};
                    if (index >= 0 && index < static_cast<i32>(std::size(kUnits)))
                        comp->_light._intensity_unit = kUnits[index];
                }
                else
                {
                    constexpr ECS::ELightIntensityUnit kUnits[] = {
                        ECS::ELightIntensityUnit::kCandela, ECS::ELightIntensityUnit::kLumen,
                        ECS::ELightIntensityUnit::kWatt, ECS::ELightIntensityUnit::kUnitlessLegacy};
                    if (index >= 0 && index < static_cast<i32>(std::size(kUnits)))
                        comp->_light._intensity_unit = kUnits[index];
                }
                context.MarkSceneDirty();
            };

            Editor::AddFloatSliderRow(context._content, "Intensity", 0.0f, 100000.0f, light_data._intensity,
                                      [comp, context](f32 value)
            {
                comp->_light._intensity = value;
                context.MarkSceneDirty();
            });

            {
                const Vector4f initial_color(comp->_light._light_color.r, comp->_light._light_color.g,
                                             comp->_light._light_color.b, 1.0f);
                auto btn = Editor::AddButtonRow(context._content, "Color", FormatColorButtonText(initial_color));
                UpdateColorButton(btn, initial_color);
                btn->OnMouseClick() += [comp, btn, context](UIEvent &e)
                {
                    auto color_picker = MakeRef<ColorPicker>(
                        Vector4f(comp->_light._light_color.r, comp->_light._light_color.g, comp->_light._light_color.b, 1.0f));
                    color_picker->Name("LightColor");
                    color_picker->GetSlot()->Size({320.0f, 240.0f});
                    color_picker->OnValueChanged() += [comp, btn, context](Vector4f color)
                    {
                        comp->_light._light_color.r = color.r;
                        comp->_light._light_color.g = color.g;
                        comp->_light._light_color.b = color.b;
                        const Vector4f light_color(color.r, color.g, color.b, 1.0f);
                        btn->SetText(FormatColorButtonText(light_color));
                        UpdateColorButton(btn, light_color);
                        context.MarkSceneDirty();
                    };
                    auto abs_rect = e._current_target->GetArrangeRect();
                    Vector2f show_pos = abs_rect.xy;
                    show_pos.y += abs_rect.w;
                    UIManager::Get()->ShowPopupAt(show_pos.x, show_pos.y, color_picker);
                };
            }

            auto *use_temperature = Editor::AddCheckBoxRow(context._content, "Use Color Temperature",
                                                            light_data._use_color_temperature);
            use_temperature->_on_click += [comp, context](bool checked)
            {
                comp->_light._use_color_temperature = checked;
                context.MarkSceneDirty();
            };
            if (light_data._use_color_temperature)
            {
                Editor::AddFloatSliderRow(context._content, "Temperature (K)", 1000.0f, 40000.0f,
                                          light_data._color_temperature_kelvin, [comp, context](f32 value)
                {
                    comp->_light._color_temperature_kelvin = std::clamp(value, 1000.0f, 40000.0f);
                    context.MarkSceneDirty();
                });
            }

            auto *shadow_panel = context._content->AddChild<CollapsibleView>("Shadow");
            shadow_panel->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto);
            shadow_panel->SetCollapsed(true);
            auto *shadow_content = static_cast<VerticalBox *>(shadow_panel->GetContent()->AddChild<VerticalBox>());
            shadow_content->SlotPadding() = Padding(2.0f, 2.0f, 2.0f, 2.0f);

            auto *cast_shadow = Editor::AddCheckBoxRow(shadow_content, "Cast Shadow", comp->_shadow._is_cast_shadow);
            cast_shadow->_on_click += [comp, context](bool checked)
            {
                comp->_shadow._is_cast_shadow = checked;
                context.MarkSceneDirty();
            };

            auto &shadow_data = comp->_shadow;
            Editor::AddFloatSliderRow(shadow_content, "Constant Bias", 0.0f, 1.0f, shadow_data._constant_bias,
                                      [&shadow_data, context](f32 value)
            {
                shadow_data._constant_bias = value;
                context.MarkSceneDirty();
            });
            Editor::AddFloatSliderRow(shadow_content, "Slope Bias", 0.0f, 1.0f, shadow_data._slope_bias,
                                      [&shadow_data, context](f32 value)
            {
                shadow_data._slope_bias = value;
                context.MarkSceneDirty();
            });

            if (comp->_type == ECS::ELightType::kDirectional)
            {
                Editor::AddFloatSliderRow(context._content, "Angular Radius", 0.01f, 5.0f, light_data._angular_radius,
                                          [&light_data, context](f32 value)
                {
                    light_data._angular_radius = value;
                    context.MarkSceneDirty();
                });
            }
            else if (comp->_type == ECS::ELightType::kArea)
            {
                static const Vector<String> kShapes = {"Rectangle", "Disc"};
                auto dropdown = Editor::AddDropdownRow(context._content, "Shape", kShapes);
                dropdown->SetSelectedIndex(static_cast<i32>(light_data._area_shape));
                dropdown->_on_selected_changed += [&light_data, context](i32 idx)
                {
                    light_data._area_shape = static_cast<ECS::EAreaLightShape>(std::clamp(idx, 0, 1));
                    context.MarkSceneDirty();
                };
                Editor::AddFloatSliderRow(context._content, "Range (0 = infinite)", 0.0f, 500.0f, light_data._range,
                                          [&light_data, context](f32 value)
                {
                    light_data._range = value;
                    context.MarkSceneDirty();
                });
                Editor::AddCheckBoxRow(context._content, "TwoSide", light_data._is_two_side)->_on_click +=
                    [&light_data, context](bool checked)
                {
                    light_data._is_two_side = checked;
                    context.MarkSceneDirty();
                };
                if (light_data._area_shape == ECS::EAreaLightShape::kRectangle)
                {
                    Editor::AddFloatInputRow(context._content, "Width", std::to_string(light_data._area_width),
                                             [&light_data, context](f32 value)
                    {
                        light_data._area_width = value;
                        context.MarkSceneDirty();
                    });
                    Editor::AddFloatInputRow(context._content, "Height", std::to_string(light_data._area_height),
                                             [&light_data, context](f32 value)
                    {
                        light_data._area_height = value;
                        context.MarkSceneDirty();
                    });
                }
                else
                {
                    Editor::AddFloatSliderRow(context._content, "Radius", 0.0f, 10.0f, light_data._area_disc_radius,
                                              [&light_data, context](f32 value)
                    {
                        light_data._area_disc_radius = value;
                        context.MarkSceneDirty();
                    });
                }
            }
            else if (comp->_type == ECS::ELightType::kSpot)
            {
                Editor::AddFloatSliderRow(context._content, "Range (0 = infinite)", 0.0f, 500.0f, light_data._range,
                                          [&light_data, context](f32 value)
                {
                    light_data._range = value;
                    context.MarkSceneDirty();
                });
                Editor::AddFloatSliderRow(context._content, "Inner Angle", 0.0f, 180.0f, light_data._spot_inner_angle,
                                          [&light_data, context](f32 value)
                {
                    light_data._spot_inner_angle = std::min(value, light_data._spot_outer_angle);
                    context.MarkSceneDirty();
                });
                Editor::AddFloatSliderRow(context._content, "Outer Angle", 0.0f, 180.0f, light_data._spot_outer_angle,
                                          [&light_data, context](f32 value)
                {
                    light_data._spot_outer_angle = std::max(value, light_data._spot_inner_angle);
                    context.MarkSceneDirty();
                });
            }
            else if (comp->_type == ECS::ELightType::kPoint)
            {
                Editor::AddFloatSliderRow(context._content, "Range (0 = infinite)", 0.0f, 500.0f, light_data._range,
                                          [&light_data, context](f32 value)
                {
                    light_data._range = value;
                    context.MarkSceneDirty();
                });
                Editor::AddFloatSliderRow(context._content, "Source Radius", 0.0f, 10.0f, light_data._source_radius,
                                          [&light_data, context](f32 value)
                {
                    light_data._source_radius = value;
                    context.MarkSceneDirty();
                });
            }
        }

        void LightComponentEditor::Refresh(ComponentEditorContext &context)
        {
            auto *comp = context.GetComponent<ECS::LightComponent>();
            if (comp == nullptr)
                return;
            _cached_light_type = comp->_type;
            _cached_use_color_temperature = comp->_light._use_color_temperature;
        }

        bool LightComponentEditor::NeedsRebuild(const ComponentEditorContext &context) const
        {
            auto *comp = context.GetComponent<ECS::LightComponent>();
            if (comp == nullptr)
                return false;
            return comp->_type != _cached_light_type ||
                   comp->_light._use_color_temperature != _cached_use_color_temperature;
        }
    }// namespace Editor
}// namespace Ailu
