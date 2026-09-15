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
    }

    void DDGIVolumeComponentEditor::Refresh(ComponentEditorContext &)
    {
    }
}
