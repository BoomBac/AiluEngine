#include "Inspector/ComponentEditors/EnvironmentComponentEditor.h"

#include "Inspector/ComponentEditorHelpers.h"
#include "Inspector/ComponentEditorRegistry.h"
#include "Scene/Component.h"
#include "UI/Basic.h"
#include "UI/ColorPicker.h"
#include "UI/Container.h"
#include "UI/ObjectAssetDropdown.h"
#include "UI/UIFramework.h"
#include "Render/Texture.h"

#include <algorithm>

namespace Ailu::Editor
{
    namespace
    {
        constexpr f32 kPreviewMaxMip = 12.0f;

        void AddColorRow(UI::UIElement *parent, const String &label, Color &color, const ComponentEditorContext &context)
        {
            auto *button = AddButtonRow(parent, label, FormatColorButtonText(Vector4f(color)));
            button->OnMouseClick() += [&color, context, button](UI::UIEvent &event)
            {
                auto picker = MakeRef<UI::ColorPicker>(Vector4f(color));
                picker->Name("EnvironmentColorPicker");
                picker->GetSlot()->Size({320.0f, 240.0f});
                picker->OnValueChanged() += [&color, context, button](Vector4f value)
                {
                    color = Color(value);
                    button->SetText(FormatColorButtonText(value));
                    context.MarkSceneDirty();
                };
                const Vector4f rect = event._current_target->GetArrangeRect();
                UI::UIManager::Get()->ShowPopupAt(rect.x, rect.y + rect.w, picker);
                event._is_handled = true;
            };
        }
    }

    void EnvironmentComponentEditor::Build(ComponentEditorContext &context)
    {
        auto *component = context.GetComponent<ECS::EnvironmentComponent>();
        if (component == nullptr || context._content == nullptr)
            return;

        _cached_source = component->_source;

        auto *source = AddDropdownRow(context._content, "Source", {"Procedural Sky", "Texture"});
        source->SetSelectedIndex(static_cast<i32>(component->_source), false);
        source->_on_selected_changed += [component, context](i32 index)
        {
            component->_source = static_cast<ECS::EEnvironmentSource>(std::clamp(index, 0, 1));
            context.MarkSceneDirty();
            context.RequestRebuild();
        };

        if (component->_source == ECS::EEnvironmentSource::kTexture)
        {
            auto *texture = AddObjectAssetDropdownRow(context._content, "Texture", Render::Texture::StaticType(),
                                                      component->_texture, true);
            texture->_on_object_asset_selected += [component, context](Asset *, Object *, const Guid &guid)
            {
                component->_texture = guid;
                context.MarkSceneDirty();
            };
        }
        else
        {
            AddColorRow(context._content, "Sky Color", component->_procedural_sky._sky_color, context);
            AddColorRow(context._content, "Ground Color", component->_procedural_sky._ground_color, context);
        }

        AddFloatSliderRow(context._content, "Intensity", 0.0f, 8.0f, component->_intensity,
                          [component, context](f32 value)
        {
            component->_intensity = value;
            context.MarkSceneDirty();
        });
        AddFloatSliderRow(context._content, "Rotation", -6.283185f, 6.283185f, component->_rotation,
                          [component, context](f32 value)
        {
            component->_rotation = value;
            context.MarkSceneDirty();
        });
        AddCheckBoxRow(context._content, "Render Sky", component->_render_sky)->_on_click += [component, context](bool value)
        {
            component->_render_sky = value;
            context.MarkSceneDirty();
        };
        AddCheckBoxRow(context._content, "Enable IBL", component->_enable_ibl)->_on_click += [component, context](bool value)
        {
            component->_enable_ibl = value;
            context.MarkSceneDirty();
        };

        auto *preview_panel = context._content->AddChild<UI::CollapsibleView>("Scene Preview");
        preview_panel->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
        auto *preview_content = static_cast<UI::VerticalBox *>(preview_panel->GetContent()->AddChild<UI::VerticalBox>());
        preview_content->SlotPadding() = UI::Padding(2.0f, 2.0f, 2.0f, 2.0f);

        auto *preview_source = AddDropdownRow(preview_content, "Texture", {"Original", "IBL Diffuse", "IBL Specular"});
        preview_source->SetSelectedIndex(std::clamp(component->_src_type, 0, 2), false);
        preview_source->_on_selected_changed += [component, context](i32 index)
        {
            component->_src_type = std::clamp(index, 0, 2);
            context.MarkSceneDirty();
        };
        AddFloatSliderRow(preview_content, "Mip", 0.0f, kPreviewMaxMip,
                          std::clamp(component->_mipmap, 0.0f, kPreviewMaxMip), [component, context](f32 value)
        {
            component->_mipmap = std::clamp(value, 0.0f, kPreviewMaxMip);
            context.MarkSceneDirty();
        });
    }

    bool EnvironmentComponentEditor::NeedsRebuild(const ComponentEditorContext &context) const
    {
        auto *component = context.GetComponent<ECS::EnvironmentComponent>();
        return component != nullptr && component->_source != _cached_source;
    }
}
