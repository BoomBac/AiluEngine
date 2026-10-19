#include "Inspector/ComponentEditors/PostprocessVolumeComponentEditor.h"
#include "Inspector/ComponentEditorHelpers.h"
#include "Inspector/ComponentEditorRegistry.h"
#include "Scene/Component.h"
#include "UI/Container.h"

#include <algorithm>
#include <cmath>

using namespace Ailu;
using namespace Ailu::UI;

namespace Ailu
{
    namespace Editor
    {
        void PostprocessVolumeComponentEditor::Build(ComponentEditorContext &context)
        {
            auto *component = context.GetComponent<ECS::PostprocessVolumeComponent>();
            if (component == nullptr || context._content == nullptr)
                return;

            auto *settings = &component->_settings;
            AddCheckBoxRow(context._content, "Global", component->_is_global)->_on_click +=
                [component, context](bool value)
            {
                component->_is_global = value;
                context.MarkSceneDirty();
            };
            AddFloatInputRow(context._content, "Priority", std::to_string(component->_priority),
                             [component, context](f32 value)
            {
                component->_priority = static_cast<i32>(value);
                context.MarkSceneDirty();
            });

            AddCheckBoxRow(context._content, "Bloom", settings->_bloom._enabled)->_on_click +=
                [settings, context](bool value)
            {
                settings->_bloom._enabled = value;
                context.MarkSceneDirty();
            };
            AddFloatInputRow(context._content, "Bloom Intensity", std::format("{:.3f}", settings->_bloom._intensity),
                             [settings, context](f32 value)
                             {
                                 settings->_bloom._intensity = value;
                                 context.MarkSceneDirty();
                             });
            AddFloatInputRow(context._content, "Bloom Radius", std::format("{:.3f}", settings->_bloom._radius),
                             [settings, context](f32 value)
                             {
                                 settings->_bloom._radius = value;
                                 context.MarkSceneDirty();
                             });
            AddFloatInputRow(context._content, "Exposure", std::format("{:.3f}", settings->_tone_mapping._exposure),
                             [settings, context](f32 value)
                             {
                                 settings->_tone_mapping._exposure = value;
                                 context.MarkSceneDirty();
                             });

            AddFloatInputRow(context._content, "Gamma", std::format("{:.3f}", settings->_tone_mapping._gamma),
                             [settings, context](f32 value)
                             {
                                 settings->_tone_mapping._gamma = std::max(value, 0.01f);
                                 context.MarkSceneDirty();
                             });

            auto *tone_mapping = AddDropdownRow(context._content, "Tone Mapping", {"None", "ACES", "AgX", "Standard"});
            tone_mapping->SetSelectedIndex(static_cast<i32>(settings->_tone_mapping._mode), false);
            tone_mapping->_on_selected_changed += [settings, context](i32 index)
            {
                settings->_tone_mapping._mode = static_cast<Render::EToneMappingMode>(index);
                context.MarkSceneDirty();
            };

            auto *auto_exposure_panel = context._content->AddChild<UI::CollapsibleView>("Auto Exposure");
            auto_exposure_panel->GetSlotAs<UI::LinearSlot>()
                .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
            auto_exposure_panel->SetCollapsed(true);
            auto *auto_exposure_content = static_cast<UI::VerticalBox *>(
                auto_exposure_panel->GetContent()->AddChild<UI::VerticalBox>());
            auto_exposure_content->SlotPadding() = UI::Padding(2.0f, 2.0f, 2.0f, 2.0f);
            auto *auto_exposure = &settings->_auto_exposure;

            Editor::AddFloatInputRow(auto_exposure_content, "Scene Luminance to cd/m2",
                std::format("{:.3f}", auto_exposure->_scene_luminance_to_cd_m2),
                [auto_exposure, context](f32 value)
                {
                    if (std::isfinite(value) && value > 0.0f)
                    {
                        auto_exposure->_scene_luminance_to_cd_m2 = std::clamp(value, 1.0e-6f, 1.0e6f);
                        context.MarkSceneDirty();
                    }
                });
            Editor::AddFloatInputRow(auto_exposure_content, "Low Percentile (%)",
                std::format("{:.2f}", auto_exposure->_low_percentile), [auto_exposure, context](f32 value)
                {
                    if (std::isfinite(value))
                    {
                        auto_exposure->_low_percentile = std::clamp(value, 0.0f, 99.8f);
                        context.MarkSceneDirty();
                    }
                });
            Editor::AddFloatInputRow(auto_exposure_content, "High Percentile (%)",
                std::format("{:.2f}", auto_exposure->_high_percentile), [auto_exposure, context](f32 value)
                {
                    if (std::isfinite(value))
                    {
                        auto_exposure->_high_percentile = std::clamp(value, 0.1f, 100.0f);
                        context.MarkSceneDirty();
                    }
                });
            Editor::AddFloatInputRow(auto_exposure_content, "Minimum EV",
                std::format("{:.2f}", auto_exposure->_minimum_ev), [auto_exposure, context](f32 value)
                {
                    if (std::isfinite(value))
                    {
                        const f32 maximum_ev = std::isfinite(auto_exposure->_maximum_ev)
                            ? std::clamp(auto_exposure->_maximum_ev, -23.9f, 24.0f) : 16.0f;
                        auto_exposure->_minimum_ev = std::clamp(value, -24.0f, maximum_ev - 0.1f);
                        context.MarkSceneDirty();
                    }
                });
            Editor::AddFloatInputRow(auto_exposure_content, "Maximum EV",
                std::format("{:.2f}", auto_exposure->_maximum_ev), [auto_exposure, context](f32 value)
                {
                    if (std::isfinite(value))
                    {
                        const f32 minimum_ev = std::isfinite(auto_exposure->_minimum_ev)
                            ? std::clamp(auto_exposure->_minimum_ev, -24.0f, 23.9f) : -12.0f;
                        auto_exposure->_maximum_ev = std::clamp(value, minimum_ev + 0.1f, 24.0f);
                        context.MarkSceneDirty();
                    }
                });
            const f32 brighten_speed = auto_exposure->_adaptation_speed_brighten >= 0.0f
                ? auto_exposure->_adaptation_speed_brighten : auto_exposure->_adaptation_speed;
            Editor::AddFloatInputRow(auto_exposure_content, "Speed Brighten (EV/s)",
                std::format("{:.2f}", brighten_speed), [auto_exposure, context](f32 value)
                {
                    if (std::isfinite(value))
                    {
                        auto_exposure->_adaptation_speed_brighten = std::clamp(value, 0.0f, 100.0f);
                        context.MarkSceneDirty();
                    }
                });
            const f32 darken_speed = auto_exposure->_adaptation_speed_darken >= 0.0f
                ? auto_exposure->_adaptation_speed_darken : auto_exposure->_adaptation_speed;
            Editor::AddFloatInputRow(auto_exposure_content, "Speed Darken (EV/s)",
                std::format("{:.2f}", darken_speed), [auto_exposure, context](f32 value)
                {
                    if (std::isfinite(value))
                    {
                        auto_exposure->_adaptation_speed_darken = std::clamp(value, 0.0f, 100.0f);
                        context.MarkSceneDirty();
                    }
                });
            auto *metering_mode = Editor::AddDropdownRow(
                auto_exposure_content, "Metering", {"Average", "Center Weighted"});
            const i32 metering_index = static_cast<u8>(auto_exposure->_metering_mode) <=
                static_cast<u8>(Render::EExposureMeteringMode::kCenterWeighted)
                ? static_cast<i32>(auto_exposure->_metering_mode) : 0;
            metering_mode->SetSelectedIndex(metering_index, false);
            metering_mode->_on_selected_changed += [auto_exposure, context](i32 index)
            {
                if (index < 0 || index > 1)
                    return;
                auto_exposure->_metering_mode = static_cast<Render::EExposureMeteringMode>(index);
                context.MarkSceneDirty();
            };

            auto *depth_of_field_panel = context._content->AddChild<UI::CollapsibleView>("Depth of Field");
            depth_of_field_panel->GetSlotAs<UI::LinearSlot>()
                .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
            depth_of_field_panel->SetCollapsed(true);
            auto *depth_of_field_content = static_cast<UI::VerticalBox *>(
                depth_of_field_panel->GetContent()->AddChild<UI::VerticalBox>());
            depth_of_field_content->SlotPadding() = UI::Padding(2.0f, 2.0f, 2.0f, 2.0f);
            auto *depth_of_field = &settings->_depth_of_field;
            AddCheckBoxRow(depth_of_field_content, "Enabled", depth_of_field->_enabled)->_on_click +=
                [depth_of_field, context](bool value)
            {
                depth_of_field->_enabled = value;
                context.MarkSceneDirty();
            };
            Editor::AddFloatInputRow(depth_of_field_content, "Maximum CoC Diameter (px)",
                std::format("{:.1f}", depth_of_field->_maximum_coc_diameter_px), [depth_of_field, context](f32 value)
                {
                    if (std::isfinite(value))
                    {
                        depth_of_field->_maximum_coc_diameter_px = std::clamp(value, 0.0f, 64.0f);
                        context.MarkSceneDirty();
                    }
                });
            Editor::AddFloatInputRow(depth_of_field_content, "Aperture Blade Count",
                std::to_string(depth_of_field->_aperture_blade_count), [depth_of_field, context](f32 value)
                {
                    if (std::isfinite(value))
                    {
                        depth_of_field->_aperture_blade_count = std::clamp(static_cast<i32>(std::round(value)),
                                                                          3, 9);
                        context.MarkSceneDirty();
                    }
                });
        }
    }// namespace Editor
}// namespace Ailu
