#include "Inspector/ComponentEditors/PostprocessVolumeComponentEditor.h"
#include "Inspector/ComponentEditorHelpers.h"
#include "Inspector/ComponentEditorRegistry.h"
#include "Scene/Component.h"
#include "UI/Container.h"

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
            AddCheckBoxRow(context._content, "Global", component->_is_global)->_on_click += [component, context](bool value)
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

            AddCheckBoxRow(context._content, "Bloom", settings->_bloom._enabled)->_on_click += [settings, context](bool value)
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

            auto *tone_mapping = AddDropdownRow(context._content, "Tone Mapping", {"None", "ACES"});
            tone_mapping->SetSelectedIndex(static_cast<i32>(settings->_tone_mapping._mode), false);
            tone_mapping->_on_selected_changed += [settings, context](i32 index)
            {
                settings->_tone_mapping._mode = static_cast<Render::EToneMappingMode>(index);
                context.MarkSceneDirty();
            };
        }
    }// namespace Editor
}// namespace Ailu
