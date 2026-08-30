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
            Render::Material *GetSelectedMaterial(const ComponentEditorContext &context)
            {
                if (context._scene == nullptr || context._component_info == nullptr)
                    return nullptr;

                const u32 subindex = Selection::GetSelectedSubIndex(context._entity);
                if (context._component_info->_component_type == ECS::CSkeletonMesh::StaticComponentTypeId())
                {
                    auto *component = context.GetComponent<ECS::CSkeletonMesh>();
                    return component != nullptr && subindex < component->_p_mats.size() ? component->_p_mats[subindex].get() : nullptr;
                }

                auto *component = context.GetComponent<ECS::StaticMeshComponent>();
                return component != nullptr && subindex < component->_p_mats.size() ? component->_p_mats[subindex].get() : nullptr;
            }
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
                visual._content_color = srgb.r * 0.299f + srgb.g * 0.587f + srgb.b * 0.114f > 0.5f ?
                    Colors::kBlack : Colors::kWhite;
                auto &style_override = button->GetStyleOverride();
                style_override.SetNormal(visual);
                style_override.SetHovered(visual);
                style_override.SetPressed(visual);
                style_override.SetFocused(visual);
                button->InvalidateStyle(EStyleInvalidation::kPaintOnly);
            }

            void RefreshMaterialProperty(Render::Material *material, Render::ShaderPropertyId property_id,
                                         InputBlock *input, Slider *slider, Button *color_button,
                                         ObjectAssetDropdown *texture_dropdown)
            {
                if (material == nullptr)
                    return;
                auto *property = material->GetShaderProperty(property_id);
                if (property == nullptr)
                    return;

                if (property->_type == Render::EShaderPropertyType::kFloat ||
                    property->_type == Render::EShaderPropertyType::kRange)
                {
                    const f32 value = property->GetValue<f32>();
                    if (slider != nullptr)
                    {
                        slider->_range = {property->_default_value.x, property->_default_value.y};
                        slider->SetValue(value, false);
                    }
                    if (input != nullptr)
                        input->SetContent(std::format("{:.2f}", value), false);
                }
                else if (property->_type == Render::EShaderPropertyType::kColor)
                {
                    const Vector4f color = property->GetValue<Vector4f>();
                    if (color_button != nullptr)
                    {
                        color_button->SetText(FormatColorButtonText(color, true), false);
                        UpdateColorButton(color_button, color);
                    }
                }
                else if (property->_type == Render::EShaderPropertyType::kTexture2D && texture_dropdown != nullptr)
                {
                    auto *texture = reinterpret_cast<Render::Texture *>(property->_value_ptr);
                    texture_dropdown->SetSelectedGuid(texture == nullptr ? Guid::EmptyGuid() :
                                                       ResourceMgr::Get().GetAssetGuid(texture), false);
                }
            }

            void CreateMaterialPropWidget(UIElement *root, Render::ShaderPropertyInfo &prop, Render::Material *obj,
                                          Vector<MaterialPropertyBinding> &bindings)
            {
                HorizontalBox *value_box = nullptr;
                Editor::AddPropertyRow(root, prop._prop_name, &value_box);
                const String value_name = prop._value_name;
                const Render::ShaderPropertyId property_id = prop._property_id == Render::kInvalidShaderPropertyId ?
                    Render::ShaderPropertyRegistry::Get().Intern(value_name) : prop._property_id;
                auto subscribe = [&bindings, obj, property_id](InputBlock *input, Slider *slider, Button *color_button,
                                                                ObjectAssetDropdown *texture_dropdown)
                {
                    auto &binding = bindings.emplace_back();
                    binding._material = obj;
                    binding._property_id = property_id;
                    binding._input = input;
                    binding._slider = slider;
                    binding._color_button = color_button;
                    binding._texture_dropdown = texture_dropdown;
                    binding._subscription = obj->_on_property_changed.Subscribe(property_id,
                        [obj, property_id, input, slider, color_button, texture_dropdown]
                    {
                        RefreshMaterialProperty(obj, property_id, input, slider, color_button, texture_dropdown);
                    });
                    RefreshMaterialProperty(obj, property_id, input, slider, color_button, texture_dropdown);
                };

            if (prop._type == Render::EShaderPropertyType::kRange)
                {
                    auto slider = value_box->AddChild<Slider>();
                    slider->GetSlotAs<LinearSlot>().Margin(kPropInnerMargin).CrossAlignment(EAlignment::kRight)
                            .SizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto).FillRate(3.0f);
                    slider->_range = {prop._default_value[0], prop._default_value[1]};
                    slider->SetValue(prop.GetValue<f32>());
                    auto input = value_box->AddChild<InputBlock>();
                    input->GetSlotAs<LinearSlot>().Margin(kPropInnerMargin).CrossAlignment(EAlignment::kRight)
                            .SizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto).FillRate(1.0f);
                    input->SetContent(std::format("{:.2f}", prop.GetValue<f32>()));
                    input->_on_content_changed += [property_id, obj, slider](String content)
                    {
                        if (auto opt = StringUtils::ParseFloat(content); opt.has_value())
                        {
                            obj->SetFloat(property_id, opt.value());
                            ResourceMgr::Get().MarkAssetDirty(obj);
                            slider->SetValue(opt.value(), false);
                        }
                    };
                    slider->_on_value_change += [property_id, obj, input](f32 v) {
                        obj->SetFloat(property_id, v);
                        ResourceMgr::Get().MarkAssetDirty(obj);
                        input->SetContent(std::format("{:.2f}", v), false);
                    };
                    subscribe(input, slider, nullptr, nullptr);
                }
                else if (prop._type == Render::EShaderPropertyType::kFloat)
                {
                    auto input = value_box->AddChild<InputBlock>();
                    input->GetSlotAs<LinearSlot>().Margin(kPropInnerMargin).CrossAlignment(EAlignment::kRight)
                            .SizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto).FillRate(1.0f);
                    input->SetContent(std::format("{:.2f}", prop.GetValue<f32>()));
                    input->_on_content_changed += [property_id, obj](String content)
                    {
                        if (auto opt = StringUtils::ParseFloat(content); opt.has_value())
                        {
                            obj->SetFloat(property_id, opt.value());
                            ResourceMgr::Get().MarkAssetDirty(obj);
                        }
                    };
                    subscribe(input, nullptr, nullptr, nullptr);
                }
                else if (prop._type == Render::EShaderPropertyType::kColor)
                {
                    auto btn = value_box->AddChild<Button>();
                    btn->GetSlotAs<LinearSlot>().Margin(kPropInnerMargin).CrossAlignment(EAlignment::kRight)
                            .SizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto).FillRate(1.0f);
                    UpdateColorButton(btn, prop.GetValue<Vector4f>());
                    btn->SetText(FormatColorButtonText(prop.GetValue<Vector4f>(), true));
                    btn->OnMouseClick() += [property_id, obj, btn](UIEvent &e)
                    {
                        auto *property = obj->GetShaderProperty(property_id);
                        if (property == nullptr)
                            return;
                        auto color_picker = MakeRef<ColorPicker>(property->GetValue<Vector4f>());
                        color_picker->Name(std::format("ColorPicker_{}", property_id));
                        color_picker->GetSlot()->Size({320.0f, 240.0f});
                        color_picker->OnValueChanged() += [property_id, obj, btn](Vector4f color)
                        {
                            obj->SetVector(property_id, color);
                            ResourceMgr::Get().MarkAssetDirty(obj);
                        };
                        auto abs_rect = e._current_target->GetArrangeRect();
                        Vector2f show_pos = abs_rect.xy;
                        show_pos.y += abs_rect.w;
                        UIManager::Get()->ShowPopupAt(show_pos.x, show_pos.y, color_picker);
                    };
                    subscribe(nullptr, nullptr, btn, nullptr);
                }
                else if (prop._type == Render::EShaderPropertyType::kTexture2D)
                {
                    auto *dropdown = value_box->AddChild<ObjectAssetDropdown>(Render::Texture2D::StaticType());
                    dropdown->GetSlotAs<LinearSlot>().Margin(kPropInnerMargin).CrossAlignment(EAlignment::kRight)
                        .SizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto).FillRate(1.0f);
                    auto *texture = reinterpret_cast<Render::Texture *>(prop._value_ptr);
                    if (texture != nullptr)
                        dropdown->SetSelectedGuid(ResourceMgr::Get().GetAssetGuid(texture));
                    dropdown->_on_object_asset_selected += [property_id, obj](Asset *, Object *selected, const Guid &)
                    {
                        obj->SetTexture(property_id, dynamic_cast<Render::Texture2D *>(selected));
                        ResourceMgr::Get().MarkAssetDirty(obj);
                    };
                    subscribe(nullptr, nullptr, nullptr, dropdown);
                }
            }
        }// namespace

        template<typename TComponent, typename TMesh>
        void BuildMeshComponentEditor(ComponentEditorContext &context, Vector<MaterialPropertyBinding> &bindings)
        {
            auto *comp = context.GetComponent<TComponent>();
            if (comp == nullptr || context._content == nullptr)
                return;

            {
                auto *dropdown = Editor::AddObjectAssetDropdownRow(context._content, "Mesh", TMesh::StaticType(),
                                                                    comp->_mesh_guid.IsEmpty() ?
                                                                        ResourceMgr::Get().GetAssetGuid(comp->_p_mesh.get()) : comp->_mesh_guid);
                dropdown->_on_object_asset_selected += [comp](Asset *, Object *selected, const Guid &)
                {
                    comp->_p_mesh = selected == nullptr ? nullptr :
                        std::dynamic_pointer_cast<TMesh>(selected->SharedFromThis());
                    comp->_mesh_guid = selected == nullptr ? Guid::EmptyGuid() : ResourceMgr::Get().GetAssetGuid(selected);
                    SceneMgr::Get().MarkCurSceneDirty();
                };
            }

            const u32 subindex = Selection::GetSelectedSubIndex(context._entity);
            if (subindex >= comp->_p_mats.size())
                return;

            auto *material_dropdown = Editor::AddObjectAssetDropdownRow(context._content,
                std::format("Material[{}]", subindex), Render::Material::StaticType(),
                subindex < comp->_material_guids.size() && !comp->_material_guids[subindex].IsEmpty()
                    ? comp->_material_guids[subindex] : ResourceMgr::Get().GetAssetGuid(comp->_p_mats[subindex].get()));
            material_dropdown->_on_object_asset_selected += [comp, subindex](Asset *, Object *selected, const Guid &)
            {
                comp->_p_mats[subindex] = selected == nullptr ? nullptr :
                    std::dynamic_pointer_cast<Render::Material>(selected->SharedFromThis());
                if (comp->_material_guids.size() <= subindex)
                    comp->_material_guids.resize(subindex + 1u, Guid::EmptyGuid());
                comp->_material_guids[subindex] = selected == nullptr ? Guid::EmptyGuid() :
                    ResourceMgr::Get().GetAssetGuid(selected);
                SceneMgr::Get().MarkCurSceneDirty();
            };
            if (auto mat = comp->_p_mats[subindex]; mat != nullptr)
            {
                auto surface_dropdown = Editor::AddDropdownRow(context._content, "Surface Type",
                    Vector<String>{"Opaque", "Transparent", "Alpha Test"});
                surface_dropdown->SetSelectedIndex(static_cast<i32>(mat->SurfaceType()));
                surface_dropdown->_on_selected_changed += [mat](i32 idx)
                {
                    mat->SurfaceType(static_cast<Render::ESurfaceType>(idx));
                    ResourceMgr::Get().MarkAssetDirty(mat.get());
                };
                auto &surface_binding = bindings.emplace_back();
                surface_binding._material = mat.get();
                surface_binding._property_id = Render::Material::SurfacePropertyId();
                surface_binding._dropdown = surface_dropdown;
                surface_binding._subscription = mat->_on_property_changed.Subscribe(
                    surface_binding._property_id, [mat, surface_dropdown]
                {
                    const i32 surface_index = static_cast<i32>(mat->SurfaceType());
                    if (surface_dropdown->GetSelectedIndex() != surface_index)
                        surface_dropdown->SetSelectedIndex(surface_index, false);
                });

                auto dropdown = Editor::AddDropdownRow(context._content, "CullMode", Vector<String>{"Off", "Front", "Back"});
                dropdown->SetSelectedIndex(static_cast<i32>(mat->GetCullMode()));
                dropdown->_on_selected_changed += [mat](i32 idx)
                {
                    mat->SetCullMode(static_cast<Render::ECullMode>(idx));
                    ResourceMgr::Get().MarkAssetDirty(mat.get());
                };
                for (auto &prop : mat->GetShaderProperty())
                    CreateMaterialPropWidget(context._content, *prop, mat.get(), bindings);
            }
        }

        StaticMeshComponentEditor::~StaticMeshComponentEditor()
        {
            ClearMaterialBindings();
        }

        void StaticMeshComponentEditor::ClearMaterialBindings()
        {
            for (const auto &binding : _material_bindings)
            {
                if (binding._material != nullptr && binding._subscription != 0u)
                    binding._material->_on_property_changed.Unsubscribe(binding._subscription);
            }
            _material_bindings.clear();
        }

        void StaticMeshComponentEditor::Build(ComponentEditorContext &context)
        {
            ClearMaterialBindings();
            if (context._component_info != nullptr &&
                context._component_info->_component_type == ECS::CSkeletonMesh::StaticComponentTypeId())
                BuildMeshComponentEditor<ECS::CSkeletonMesh, Render::SkeletonMesh>(context, _material_bindings);
            else
                BuildMeshComponentEditor<ECS::StaticMeshComponent, Render::Mesh>(context, _material_bindings);

            _cached_material = GetSelectedMaterial(context);
            _cached_shader = _cached_material != nullptr ? _cached_material->GetShader() : nullptr;
        }

        void StaticMeshComponentEditor::Refresh(ComponentEditorContext &context)
        {
        }

        bool StaticMeshComponentEditor::NeedsRebuild(const ComponentEditorContext &context) const
        {
            Render::Material *material = GetSelectedMaterial(context);
            Render::Shader *shader = material != nullptr ? material->GetShader() : nullptr;
            return material != _cached_material || shader != _cached_shader;
        }
    }// namespace Editor
}// namespace Ailu
