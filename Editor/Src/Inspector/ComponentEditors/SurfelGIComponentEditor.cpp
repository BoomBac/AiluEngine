#include "Inspector/ComponentEditors/SurfelGIComponentEditor.h"

#include "Inspector/ComponentEditorHelpers.h"
#include "Inspector/ComponentEditorRegistry.h"
#include "Scene/Component.h"
#include "Render/GraphicsContext.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Ailu::Editor
{
    void SurfelGIComponentEditor::Build(ComponentEditorContext &context)
    {
        auto *component = context.GetComponent<ECS::SurfelGIComponent>();
        if (component == nullptr || context._content == nullptr)
            return;
        const ComponentEditorContext editor_context = context;
        auto *enabled = AddCheckBoxRow(context._content, "Enabled", component->_enabled);
        enabled->_on_click += [component, editor_context](bool value)
        {
            component->_enabled = value;
            editor_context.MarkSceneDirty();
        };

        auto *draw_surfels = AddCheckBoxRow(context._content, "Debug Draw Surfels", component->_debug_draw_surfels);
        draw_surfels->_on_click += [component, editor_context](bool value)
        {
            component->_debug_draw_surfels = value;
            editor_context.MarkSceneDirty();
        };

        auto *opaque_disks = AddCheckBoxRow(context._content, "Opaque Debug Disks", component->_debug_opaque_disks);
        opaque_disks->_on_click += [component, editor_context](bool value)
        {
            component->_debug_opaque_disks = value;
            editor_context.MarkSceneDirty();
        };

        auto *show_irradiance = AddCheckBoxRow(context._content, "Debug Irradiance",
                                               component->_debug_show_irradiance);
        show_irradiance->_on_click += [component, editor_context](bool value)
        {
            component->_debug_show_irradiance = value;
            editor_context.MarkSceneDirty();
        };
        AddFloatInputRow(context._content, "Debug Irradiance Exposure",
                         std::format("{:.2f}", component->_debug_irradiance_exposure), [component, editor_context](f32 value)
        {
            if (!std::isfinite(value))
                return;
            component->_debug_irradiance_exposure = std::clamp(value, 0.0f, 100.0f);
            editor_context.MarkSceneDirty();
        });

        AddButtonRow(context._content, "Lighting Debug", "Irradiance")->OnMouseClick() +=
            [component](UI::UIEvent &)
        {
            component->_debug_lighting_mode = 1u;
        };
        AddButtonRow(context._content, "Lighting Debug", "Sample Count")->OnMouseClick() +=
            [component](UI::UIEvent &)
        {
            component->_debug_lighting_mode = 2u;
        };
        AddButtonRow(context._content, "Lighting Debug", "Update Age")->OnMouseClick() += [component](UI::UIEvent &)
        {
            component->_debug_lighting_mode = 3u;
        };
        AddButtonRow(context._content, "Lighting Debug", "Cascade / Status")->OnMouseClick() +=
            [component](UI::UIEvent &)
        {
            component->_debug_lighting_mode = 0u;
        };

        auto *debug_coverage = AddCheckBoxRow(context._content, "Debug Coverage", component->_debug_coverage);
        debug_coverage->_on_click += [component, editor_context](bool value)
        {
            component->_debug_coverage = value;
            editor_context.MarkSceneDirty();
        };

        auto *draw_tile_grid = AddCheckBoxRow(context._content, "Draw Screen Tile Grid",
                                              component->_debug_draw_tile_grid);
        draw_tile_grid->_on_click += [component](bool value)
        {
            component->_debug_draw_tile_grid = value;
        };

        auto *manual_spawn = AddCheckBoxRow(context._content, "Manual Spawn", component->_manual_spawn);
        manual_spawn->_on_click += [component](bool value)
        {
            component->_manual_spawn = value;
        };

        AddButtonRow(context._content, "Spawn Once", "Run One Spawn Pass")->OnMouseClick() +=
            [component, editor_context](UI::UIEvent &)
        {
            component->_manual_spawn = true;
            component->_spawn_once_requested = true;
            //Render::GraphicsContext::Get().TakePixCapture();
            editor_context.RequestRebuild();
        };

        AddFloatInputRow(context._content, "Base Cell Size", std::format("{:.3f}", component->_base_cell_size),
                         [component, editor_context](f32 value)
        {
            if (!std::isfinite(value))
                return;
            component->_base_cell_size = std::clamp(value, 0.001f, 10.0f);
            editor_context.MarkSceneDirty();
        });
        AddFloatInputRow(context._content, "Cascade Count", std::to_string(component->_cascade_count),
                         [component, editor_context](f32 value)
        {
            if (!std::isfinite(value))
                return;
            component->_cascade_count = static_cast<u32>(std::clamp(std::round(static_cast<double>(value)), 1.0, 5.0));
            editor_context.MarkSceneDirty();
        });
        AddFloatInputRow(context._content, "Grid Resolution", std::to_string(component->_grid_resolution),
                         [component, editor_context](f32 value)
        {
            if (!std::isfinite(value))
                return;
            component->_grid_resolution = static_cast<u32>(
                std::clamp(std::round(static_cast<double>(value)), 8.0, 128.0));
            editor_context.MarkSceneDirty();
        });
        AddFloatInputRow(context._content, "Target Pixel Radius",
                         std::format("{:.2f}", component->_target_pixel_radius), [component, editor_context](f32 value)
        {
            if (!std::isfinite(value))
                return;
            component->_target_pixel_radius = std::clamp(value, 1.0f, 64.0f);
            editor_context.MarkSceneDirty();
        });
        AddFloatSliderRow(context._content, "Spawn Coverage Threshold", 0.0f, 1.0f,
                          component->_spawn_coverage_threshold,
                          [component, editor_context](f32 value)
        {
            component->_spawn_coverage_threshold = std::clamp(value, 0.0f, 1.0f);
            editor_context.MarkSceneDirty();
        });
        AddFloatSliderRow(context._content, "Spawn Spacing Scale", 0.5f, 3.0f,
                          component->_spawn_spacing_scale,
                          [component, editor_context](f32 value)
        {
            component->_spawn_spacing_scale = std::clamp(value, 0.5f, 3.0f);
            editor_context.MarkSceneDirty();
        });
        AddFloatInputRow(context._content, "Max Spawn Per Frame", std::to_string(component->_max_spawn_per_frame),
                         [component, editor_context](f32 value)
        {
            if (!std::isfinite(value))
                return;
            component->_max_spawn_per_frame = static_cast<u32>(
                std::clamp(std::round(static_cast<double>(value)), 1.0, 4096.0));
            editor_context.MarkSceneDirty();
        });
        auto *lighting_enabled = AddCheckBoxRow(context._content, "Lighting Enabled", component->_lighting_enabled);
        lighting_enabled->_on_click += [component, editor_context](bool value)
        {
            component->_lighting_enabled = value;
            editor_context.MarkSceneDirty();
        };
        AddFloatInputRow(context._content, "Rays Per Surfel", std::to_string(component->_rays_per_surfel),
                         [component, editor_context](f32 value)
        {
            if (!std::isfinite(value))
                return;
            component->_rays_per_surfel = static_cast<u32>(
                std::clamp(std::round(static_cast<double>(value)), 1.0, 8.0));
            editor_context.MarkSceneDirty();
        });
        AddFloatInputRow(context._content, "Lighting Updates Per Frame",
                         std::to_string(component->_max_lighting_updates_per_frame),
                         [component, editor_context](f32 value)
        {
            if (!std::isfinite(value))
                return;
            component->_max_lighting_updates_per_frame = static_cast<u32>(
                std::clamp(std::round(static_cast<double>(value)), 1.0, 4096.0));
            editor_context.MarkSceneDirty();
        });
        AddFloatInputRow(context._content, "Lighting Max Ray Distance",
                         std::format("{:.2f}", component->_lighting_max_ray_distance),
                         [component, editor_context](f32 value)
        {
            if (!std::isfinite(value))
                return;
            component->_lighting_max_ray_distance = std::max(value, 0.01f);
            editor_context.MarkSceneDirty();
        });
        AddFloatSliderRow(context._content, "Spawn Normal Threshold", 0.0f, 1.0f,
                          component->_spawn_normal_threshold,
                          [component, editor_context](f32 value)
        {
            component->_spawn_normal_threshold = std::clamp(value, 0.0f, 1.0f);
            editor_context.MarkSceneDirty();
        });
        AddFloatSliderRow(context._content, "Blue Noise Strength", 0.0f, 1.0f,
                          component->_blue_noise_strength,
                          [component, editor_context](f32 value)
        {
            component->_blue_noise_strength = std::clamp(value, 0.0f, 1.0f);
            editor_context.MarkSceneDirty();
        });
        AddFloatInputRow(context._content, "Max Surfel Count", std::to_string(component->_max_surfel_count),
                         [component, editor_context](f32 value)
        {
            if (!std::isfinite(value))
                return;
            const double max_count = static_cast<double>(std::numeric_limits<u32>::max());
            component->_max_surfel_count = static_cast<u32>(
                std::clamp(std::round(static_cast<double>(value)), 1.0, max_count));
            editor_context.MarkSceneDirty();
        });
        AddFloatInputRow(context._content, "Debug Cascade", std::to_string(component->_debug_cascade),
                         [component, editor_context](f32 value)
        {
            if (!std::isfinite(value))
                return;
            component->_debug_cascade = static_cast<i32>(std::clamp(std::round(static_cast<double>(value)), -1.0, 4.0));
            editor_context.MarkSceneDirty();
        });
    }

    void SurfelGIComponentEditor::Refresh(ComponentEditorContext &)
    {
    }
}
