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
            void ShowPopupListView(UIElement *anchor, float viewport_height, const std::function<void(const Ref<ListView> &)> &fill_fn)
            {
                auto list_view = MakeRef<ListView>();
                list_view->Name("PopupListView");
                UIBrush transparent_brush;
                transparent_brush._type = EUIBrushType::kColor;
                transparent_brush._tint = Colors::kTransparent;
                list_view->SetBackgroundBrush(transparent_brush);

                const auto abs_rect = anchor->GetArrangeRect();
                list_view->GetSlot()->Size({abs_rect.z, list_view->GetSlot()->_size.y});

                fill_fn(list_view);

                list_view->SetViewportHeight(viewport_height);

                Vector2f show_pos = abs_rect.xy;
                show_pos.y += abs_rect.w;

                UIManager::Get()->ShowPopupAt(show_pos.x, show_pos.y, list_view);
            }

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
                    auto btn = value_box->AddChild<Button>();
                    btn->GetSlotAs<LinearSlot>().Margin(kPropInnerMargin).CrossAlignment(EAlignment::kRight)
                            .SizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto).FillRate(1.0f);
                    btn->OnMouseClick() += [value_name, obj](UIEvent &e)
                    {
                        ShowPopupListView(e._current_target, 200.0f, [value_name, obj](const Ref<ListView> &list_view)
                                          {
                            auto none_item = MakeRef<Text>("None");
                            none_item->GetSlot()->Size({64.0f, 64.0f});
                            none_item->OnMouseClick() += [value_name, obj](UIEvent &e)
                            {
                                obj->SetTexture(value_name, nullptr);
                                UIManager::Get()->HidePopup();
                            };
                            list_view->AddItem(none_item);
                            none_item->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFixed, ESizePolicy::kFixed).Size({64.0f, 64.0f});
                            for (auto it = ResourceMgr::Get().ResourceBegin<Render::Texture2D>(); it != ResourceMgr::Get().ResourceEnd<Render::Texture2D>(); it++)
                            {
                                auto tex = ResourceMgr::Get().IterToRefPtr<Render::Texture2D>(it).get();
                                auto item_hb = MakeRef<HorizontalBox>();
                                auto img = item_hb->AddChild<Image>(tex);
                                img->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFixed, ESizePolicy::kFixed).Size({64.0f, 64.0f});
                                img->OnMouseClick() += [value_name, tex, obj](UIEvent &e)
                                {
                                    obj->SetTexture(value_name, tex);
                                    UIManager::Get()->HidePopup();
                                };
                                auto item = item_hb->AddChild<Text>(tex->Name());
                                item->SlotPadding() = Padding(4.0f);
                                item->InvalidateLayout();
                                list_view->AddItem(item_hb);
                            } });
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
                auto btn = Editor::AddButtonRow(context._content, "Mesh", comp->_p_mesh != nullptr ? comp->_p_mesh->Name() : "None");
                btn->OnMouseClick() += [comp, btn](UIEvent &e)
                {
                    ShowPopupListView(e._current_target, 200.0f, [comp, btn](const Ref<ListView> &list_view)
                                      {
                        for (auto it = ResourceMgr::Get().ResourceBegin<Render::Mesh>(); it != ResourceMgr::Get().ResourceEnd<Render::Mesh>(); it++)
                        {
                            const auto &mesh = ResourceMgr::Get().IterToRefPtr<Render::Mesh>(it);
                            auto text = MakeRef<Text>(mesh->Name());
                            text->OnMouseClick() += [mesh, comp, btn](UIEvent &e)
                            {
                                if (comp->_p_mesh == mesh)
                                    return;
                                comp->_p_mesh = mesh;
                                btn->SetText(comp->_p_mesh->Name());
                                UIManager::Get()->HidePopup();
                                SceneMgr::Get().MarkCurSceneDirty();
                            };
                            list_view->AddItem(text);
                        } });
                };
            }

            {
                auto subindex = Selection::GetSelectedSubIndex(context._entity);
                auto btn = Editor::AddButtonRow(context._content, std::format("Material[{}]", subindex),
                    (comp->_p_mats[subindex] != nullptr) ? comp->_p_mats[subindex]->Name() : "None");
                btn->OnMouseClick() += [comp, btn, subindex](UIEvent &e)
                {
                    ShowPopupListView(e._current_target, 200.0f, [comp, btn, subindex](const Ref<ListView> &list_view)
                                      {
                        for (auto it = ResourceMgr::Get().ResourceBegin<Render::Material>(); it != ResourceMgr::Get().ResourceEnd<Render::Material>(); it++)
                        {
                            const auto &mat = ResourceMgr::Get().IterToRefPtr<Render::Material>(it);
                            auto text = MakeRef<Text>(mat->Name());
                            text->OnMouseClick() += [mat, comp, btn, subindex](UIEvent &e)
                            {
                                if (comp->_p_mats[subindex] == mat)
                                    return;
                                comp->_p_mats[subindex] = mat;
                                btn->SetText(comp->_p_mats[subindex]->Name());
                                UIManager::Get()->HidePopup();
                            };
                            list_view->AddItem(text);
                        } });
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
