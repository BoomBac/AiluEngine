#include "Inspector/ComponentEditors/StaticMeshComponentEditor.h"
#include "Inspector/ComponentEditorRegistry.h"
#include "Inspector/ComponentEditorHelpers.h"
#include "Common/Selection.h"
#include "Framework/Common/ResourceMgr.h"
#include "Scene/Scene.h"
#include "Render/Material.h"
#include "Render/Mesh.h"
#include "Render/2D/Sprite.h"
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
            void CreateMaterialPropWidget(UIElement *root, Render::ShaderPropertyInfo &prop, Material *obj)
            {
                HorizontalBox *value_box = nullptr;
                Editor::AddPropertyRow(root, prop._prop_name, &value_box);
                String value_name = prop._value_name;
                if (prop._type == Render::EShaderPropertyType::kRange)
                {
                    auto slider = value_box->AddChild<Slider>();
                    slider->GetSlotAs<LinearSlot>().Margin(kPropInnerMargin).CrossAlignment(EAlignment::kRight)
                            .SizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto).FillRate(3.0f);
                    f32 current_value = prop.GetValue<f32>();
                    current_value = std::max(std::min(current_value, prop._default_value[1]), prop._default_value[0]);
                    slider->SetValue((current_value - prop._default_value[0]) / (prop._default_value[1] - prop._default_value[0]));
                    slider->_range = {prop._default_value[0], prop._default_value[1]};
                    auto input = value_box->AddChild<InputBlock>();
                    input->GetSlotAs<LinearSlot>().Margin(kPropInnerMargin).CrossAlignment(EAlignment::kRight)
                            .SizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto).FillRate(1.0f);
                    input->SetContent(std::format("{:.2f}", prop.GetValue<f32>()));
                    input->_on_content_changed += [value_name, obj, slider](String content)
                    {
                        if (auto opt = StringUtils::ParseFloat(content); opt.has_value())
                        {
                            auto p = obj->GetShaderProperty(value_name);
                            if (p)
                                p->SetValue<f32>(opt.value());
                            slider->SetValue((opt.value() - p->_default_value[0]) / (p->_default_value[1] - p->_default_value[0]), false);
                        }
                    };
                    slider->_on_value_change += [value_name, obj, input](f32 v) {
                        auto p = obj->GetShaderProperty(value_name);
                        if (p)
                            p->SetValue<f32>(v);
                        input->SetContent(std::format("{:.2f}", v), false);
                    };
                }
                else if (prop._type == Render::EShaderPropertyType::kFloat)
                {
                    auto input = value_box->AddChild<InputBlock>();
                    input->GetSlotAs<LinearSlot>().Margin(kPropInnerMargin).CrossAlignment(EAlignment::kRight)
                            .SizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto).FillRate(1.0f);
                    input->SetContent(std::format("{:.2f}", prop.GetValue<f32>()));
                    input->_on_content_changed += [value_name, obj](String content)
                    {
                        if (auto opt = StringUtils::ParseFloat(content); opt.has_value())
                        {
                            auto p = obj->GetShaderProperty(value_name);
                            if (p)
                                p->SetValue<f32>(opt.value());
                        }
                    };
                }
                else if (prop._type == Render::EShaderPropertyType::kColor)
                {
                    auto btn = value_box->AddChild<Button>();
                    btn->GetSlotAs<LinearSlot>().Margin(kPropInnerMargin).CrossAlignment(EAlignment::kRight)
                            .SizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto).FillRate(1.0f);
                    btn->SetText(FormatColorButtonText(prop.GetValue<Vector4f>(), true));
                    btn->OnMouseClick() += [value_name, obj, btn](UIEvent &e)
                    {
                        auto color_picker = MakeRef<ColorPicker>(obj->GetShaderProperty(value_name)->GetValue<Vector4f>());
                        color_picker->Name(std::format("ColorPicker_{}", value_name));
                        color_picker->GetSlot()->Size({320.0f, 240.0f});
                        color_picker->OnValueChanged() += [value_name, obj, btn](Vector4f color)
                        {
                            auto p = obj->GetShaderProperty(value_name);
                            if (p)
                                p->SetValue<Vector4f>(color);
                            btn->SetText(FormatColorButtonText(color, true));
                        };
                        auto abs_rect = e._current_target->GetArrangeRect();
                        Vector2f show_pos = abs_rect.xy;
                        show_pos.y += abs_rect.w;
                        UIManager::Get()->ShowPopupAt(show_pos.x, show_pos.y, color_picker);
                    };
                }
                else if (prop._type == Render::EShaderPropertyType::kTexture2D)
                {
                    auto *dropdown = value_box->AddChild<ObjectAssetDropdown>(Render::Texture2D::StaticType());
                    dropdown->GetSlotAs<LinearSlot>().Margin(kPropInnerMargin).CrossAlignment(EAlignment::kRight)
                        .SizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto).FillRate(1.0f);
                    dropdown->_on_object_asset_selected += [value_name, obj](Asset *, Object *selected, const Guid &)
                    {
                        obj->SetTexture(value_name, dynamic_cast<Render::Texture2D *>(selected));
                    };
                }
            }
        }// namespace

        void StaticMeshComponentEditor::Build(ComponentEditorContext &context)
        {
            auto *comp = context.GetComponent<ECS::StaticMeshComponent>();
            if (comp == nullptr || context._content == nullptr)
                return;

            {
                auto *dropdown = Editor::AddObjectAssetDropdownRow(context._content, "Mesh", Render::Mesh::StaticType(),
                    ResourceMgr::Get().GetAssetGuid(comp->_p_mesh.get()));
                dropdown->_on_object_asset_selected += [comp](Asset *, Object *selected, const Guid &)
                {
                    comp->_p_mesh = selected == nullptr ? nullptr : std::dynamic_pointer_cast<Render::Mesh>(selected->SharedFromThis());
                    SceneMgr::Get().MarkCurSceneDirty();
                };
            }

            {
                auto subindex = Selection::GetSelectedSubIndex(context._entity);
                auto *material_dropdown = Editor::AddObjectAssetDropdownRow(context._content, std::format("Material[{}]", subindex),
                    Render::Material::StaticType(), ResourceMgr::Get().GetAssetGuid(comp->_p_mats[subindex].get()));
                material_dropdown->_on_object_asset_selected += [comp, subindex](Asset *, Object *selected, const Guid &)
                {
                    comp->_p_mats[subindex] = selected == nullptr ? nullptr : std::dynamic_pointer_cast<Render::Material>(selected->SharedFromThis());
                    SceneMgr::Get().MarkCurSceneDirty();
                };
                if (auto mat = comp->_p_mats[subindex]; mat != nullptr)
                {
                    auto dropdown = Editor::AddDropdownRow(context._content, "CullMode", Vector<String>{"Off", "Front", "Back"});
                    dropdown->SetSelectedIndex(static_cast<i32>(mat->GetCullMode()));
                    dropdown->_on_selected_changed += [mat](i32 idx)
                    {
                        mat->SetCullMode(static_cast<Render::ECullMode>(idx));
                    };
                    for (auto &prop : mat->GetShaderProperty())
                    {
                        CreateMaterialPropWidget(context._content, *prop, mat.get());
                    }
                }
            }
        }

        void StaticMeshComponentEditor::Refresh(ComponentEditorContext &context)
        {
        }
    }// namespace Editor
}// namespace Ailu
