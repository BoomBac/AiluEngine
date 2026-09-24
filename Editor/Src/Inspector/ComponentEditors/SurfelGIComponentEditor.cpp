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
        component->_manual_spawn = true;
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
            Render::GraphicsContext::Get().TakePixCapture();
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
