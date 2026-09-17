#include "Inspector/ComponentEditors/DDGIVolumeComponentEditor.h"

#include "Inspector/ComponentEditorHelpers.h"
#include "Inspector/ComponentEditorRegistry.h"
#include "Scene/Component.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Ailu::Editor
{
    void DDGIVolumeComponentEditor::Build(ComponentEditorContext &context)
    {
        auto *component = context.GetComponent<ECS::DDGIVolumeComponent>();
        if (component == nullptr || context._content == nullptr)
            return;

        const ComponentEditorContext editor_context = context;
        AddVec3InputRow(context._content, "Probe Count", std::to_string(component->_probe_count.x),
                        std::to_string(component->_probe_count.y), std::to_string(component->_probe_count.z),
                        [component, editor_context](int axis, f32 value)
                        {
                            const double rounded = std::round(static_cast<double>(value));
                            const double max_count = static_cast<double>(std::numeric_limits<u32>::max());
                            component->_probe_count[axis] = static_cast<u32>(std::clamp(rounded, 1.0, max_count));
                            editor_context.MarkSceneDirty();
                        });
        AddVec3InputRow(context._content, "Probe Spacing", std::format("{:.2f}", component->_probe_spacing.x),
                        std::format("{:.2f}", component->_probe_spacing.y),
                        std::format("{:.2f}", component->_probe_spacing.z),
                        [component, editor_context](int axis, f32 value)
                        {
                            if (!std::isfinite(value))
                                return;
                            component->_probe_spacing[axis] = std::max(0.001f, value);
                            editor_context.MarkSceneDirty();
                        });
        auto *draw = AddCheckBoxRow(context._content, "Draw Probes", component->_draw_probes);
        draw->_on_click +=
            [component, editor_context](bool enabled)
        {
            component->_draw_probes = enabled;
            editor_context.MarkSceneDirty();
        };

        auto *use_relocation = AddCheckBoxRow(context._content, "Probe Relocation", component->_use_probe_relocation);
        use_relocation->_on_click += [component, editor_context](bool enabled)
        {
            component->_use_probe_relocation = enabled;
            editor_context.MarkSceneDirty();
        };

        auto *relocation_debug = AddCheckBoxRow(context._content, "Relocation Debug",
                                                component->_draw_relocation_debug);
        relocation_debug->_on_click += [component, editor_context](bool enabled)
        {
            component->_draw_relocation_debug = enabled;
            editor_context.MarkSceneDirty();
        };

        AddFloatSliderRow(context._content, "Backface Threshold", 0.0f, 1.0f,
                          component->_relocation_backface_threshold,
                          [component, editor_context](f32 value)
        {
            component->_relocation_backface_threshold = std::clamp(value, 0.0f, 1.0f);
            editor_context.MarkSceneDirty();
        });
        AddFloatSliderRow(context._content, "Random Ray Backface Threshold", 0.0f, 1.0f,
                          component->_random_ray_backface_threshold,
                          [component, editor_context](f32 value)
        {
            component->_random_ray_backface_threshold = std::clamp(value, 0.0f, 1.0f);
            editor_context.MarkSceneDirty();
        });
        AddFloatSliderRow(context._content, "Min Frontface Ratio", 0.0f, 0.5f,
                          component->_relocation_min_frontface_distance_ratio,
                          [component, editor_context](f32 value)
        {
            component->_relocation_min_frontface_distance_ratio = std::clamp(value, 0.0f, 0.5f);
            editor_context.MarkSceneDirty();
        });

        auto *freeze_ray_rotation = AddCheckBoxRow(context._content, "Freeze Ray Rotation",
                                                    component->_freeze_ray_rotation);
        freeze_ray_rotation->_on_click += [component, editor_context](bool enabled)
        {
            component->_freeze_ray_rotation = enabled;
            editor_context.MarkSceneDirty();
        };

        const Vector<String> debug_mode_names{"Radiance", "Distance", "Hit Type", "Ray Direction", "Irradiance",
                                              "Distance Mean", "Distance Variance", "Final Weight"};
        auto *debug_mode = AddDropdownRow(context._content, "Debug Mode", debug_mode_names);
        const i32 debug_mode_index = std::clamp(static_cast<i32>(component->_debug_mode), 0,
                                                static_cast<i32>(debug_mode_names.size()) - 1);
        debug_mode->SetSelectedIndex(debug_mode_index, false);
        debug_mode->_on_selected_changed += [component, editor_context](i32 index)
        {
            component->_debug_mode = static_cast<ECS::EDDGIProbeDebugMode>(index);
            editor_context.MarkSceneDirty();
        };
    }

    void DDGIVolumeComponentEditor::Refresh(ComponentEditorContext &)
    {
    }
}
