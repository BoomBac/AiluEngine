#include "Inspector/ComponentEditors/LightProbeGroupComponentEditor.h"
#include "Inspector/ComponentEditorRegistry.h"
#include "Inspector/ComponentEditorHelpers.h"
#include "Scene/Component.h"
#include "Render/StaticDiffuseProbeData.h"

using namespace Ailu;
using namespace Ailu::UI;

namespace Ailu::Editor
{
    void LightProbeGroupComponentEditor::Build(ComponentEditorContext &context)
    {
        auto *component = context.GetComponent<ECS::CLightProbeGroup>();
        if (component == nullptr || context._content == nullptr)
            return;
        const ComponentEditorContext editor_context = context;

        AddVec3InputRow(context._content, "Probe Count", std::to_string(component->_probe_count.x),
                        std::to_string(component->_probe_count.y), std::to_string(component->_probe_count.z),
                        [component, editor_context](int axis, f32 value)
                        {
                            const i32 count = std::max(1, static_cast<i32>(std::round(value)));
                            component->_probe_count[axis] = count;
                            editor_context.MarkSceneDirty();
                        });
        AddVec3InputRow(context._content, "Size", std::format("{:.2f}", component->_size.x),
                        std::format("{:.2f}", component->_size.y), std::format("{:.2f}", component->_size.z),
                        [component, editor_context](int axis, f32 value)
                        {
                            component->_size[axis] = std::max(0.01f, value);
                            editor_context.MarkSceneDirty();
                        });
        const u32 probe_count = static_cast<u32>(std::max(1, component->_probe_count.x)) *
                                static_cast<u32>(std::max(1, component->_probe_count.y)) *
                                static_cast<u32>(std::max(1, component->_probe_count.z));
        UI::HorizontalBox *count_value_box = nullptr;
        AddPropertyRow(context._content, "Total Probes", &count_value_box);
        count_value_box->AddChild<UI::Text>(std::format("{}", probe_count));
        UI::HorizontalBox *memory_value_box = nullptr;
        AddPropertyRow(context._content, "SH Memory", &memory_value_box);
        memory_value_box->AddChild<UI::Text>(std::format("{} KB", (probe_count * 9u * sizeof(Vector4f)) / 1024u));
        UI::HorizontalBox *status_value_box = nullptr;
        AddPropertyRow(context._content, "Bake Status", &status_value_box);
        _bake_status = status_value_box->AddChild<UI::Text>(component->_baked_data.IsEmpty() ? "Not Baked" : "Baked");
        auto *asset = AddObjectAssetDropdownRow(context._content, "Baked SH",
                                                Render::StaticDiffuseProbeData::StaticType(), component->_baked_data);
        asset->_on_object_asset_selected += [component, editor_context](Asset *, Object *, const Guid &guid)
        {
            component->_baked_data = guid;
            editor_context.MarkSceneDirty();
        };
        auto *draw = AddCheckBoxRow(context._content, "Draw Probes", component->_draw_probes);
        draw->_on_click += [component, editor_context](bool enabled)
        {
            component->_draw_probes = enabled;
            editor_context.MarkSceneDirty();
        };
        auto *preview_mode = AddDropdownRow(context._content, "Preview", {"Spherical Harmonics", "Captured Cubemap"});
        preview_mode->SetSelectedIndex(component->_preview_cubemap ? 1 : 0, false);
        preview_mode->_on_selected_changed += [component](i32 index)
        {
            component->_preview_cubemap = index == 1;
        };
        auto *bake = AddButtonRow(context._content, "Bake", "Bake Static SH");
        bake->OnMouseClick() += [editor_context](UIEvent &)
        {
            if (auto *component = editor_context.GetComponent<ECS::CLightProbeGroup>(); component != nullptr)
            {
                component->_bake_requested = true;
                editor_context.MarkSceneDirty();
            }
            editor_context.RequestRebuild();
        };
        auto *bake_capture = AddButtonRow(context._content, "Bake + PIX Capture", "Bake and Capture Cubemap Frame with PIX");
        bake_capture->OnMouseClick() += [editor_context](UIEvent &)
        {
            if (auto *component = editor_context.GetComponent<ECS::CLightProbeGroup>(); component != nullptr)
            {
                component->_bake_requested = true;
                component->_capture_bake_requested = true;
                editor_context.MarkSceneDirty();
            }
            editor_context.RequestRebuild();
        };
        auto *clear = AddButtonRow(context._content, "Clear Bake", "Clear Static SH");
        clear->OnMouseClick() += [editor_context](UIEvent &)
        {
            if (auto *component = editor_context.GetComponent<ECS::CLightProbeGroup>(); component != nullptr)
            {
                component->_baked_data = Guid::EmptyGuid();
                component->_bake_requested = false;
                editor_context.MarkSceneDirty();
            }
            editor_context.RequestRebuild();
        };
    }

    void LightProbeGroupComponentEditor::Refresh(ComponentEditorContext &context)
    {
        auto *component = context.GetComponent<ECS::CLightProbeGroup>();
        if (component == nullptr || _bake_status == nullptr)
            return;
        _bake_status->SetText(component->_baked_data.IsEmpty() ? "Not Baked" : "Baked", false);
    }
}
