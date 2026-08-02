#include "Inspector/ComponentEditors/SpriteRendererComponentEditor.h"
#include "Inspector/ComponentEditorRegistry.h"
#include "Inspector/ComponentEditorHelpers.h"
#include "Framework/Common/ResourceMgr.h"
#include "Scene/Scene.h"
#include "Render/Material.h"
#include "Render/2D/Sprite.h"
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
        }// namespace

        void SpriteRendererComponentEditor::Build(ComponentEditorContext &context)
        {
            auto *comp = context.GetComponent<ECS::SpriteRendererComponent>();
            if (comp == nullptr || context._content == nullptr)
                return;

            {
                String sprite_name = (comp->_sprite != nullptr) ? comp->_sprite->Name() : "None";
                auto btn = Editor::AddButtonRow(context._content, "Sprite", sprite_name);
                btn->OnMouseClick() += [comp, btn](UIEvent &e)
                {
                    ShowPopupListView(e._current_target, 200.0f, [comp, btn](const Ref<ListView> &list_view)
                                      {
                        auto none_item = MakeRef<Text>("None");
                        none_item->OnMouseClick() += [comp, btn](UIEvent &)
                        {
                            comp->_sprite = nullptr;
                            btn->SetText("None");
                            UIManager::Get()->HidePopup();
                            SceneMgr::Get().MarkCurSceneDirty();
                        };
                        list_view->AddItem(none_item);
                        for (auto it = ResourceMgr::Get().ResourceBegin<Render::Sprite>(); it != ResourceMgr::Get().ResourceEnd<Render::Sprite>(); it++)
                        {
                            const auto &sprite_ref = ResourceMgr::Get().IterToRefPtr<Render::Sprite>(it);
                            auto text = MakeRef<Text>(sprite_ref->Name());
                            text->OnMouseClick() += [sprite_ref, comp, btn](UIEvent &)
                            {
                                comp->_sprite = sprite_ref.get();
                                btn->SetText(sprite_ref->Name());
                                UIManager::Get()->HidePopup();
                                SceneMgr::Get().MarkCurSceneDirty();
                            };
                            list_view->AddItem(text);
                        } });
                };
            }

            {
                auto btn = Editor::AddButtonRow(context._content, "Material", comp->_material != nullptr ? comp->_material->Name() : "Default");
                btn->OnMouseClick() += [comp, btn](UIEvent &e)
                {
                    ShowPopupListView(e._current_target, 200.0f, [comp, btn](const Ref<ListView> &list_view)
                                      {
                        auto none_item = MakeRef<Text>("Default");
                        none_item->OnMouseClick() += [comp, btn](UIEvent &)
                        {
                            comp->_material = nullptr;
                            btn->SetText("Default");
                            UIManager::Get()->HidePopup();
                            SceneMgr::Get().MarkCurSceneDirty();
                        };
                        list_view->AddItem(none_item);
                        for (auto it = ResourceMgr::Get().ResourceBegin<Render::Material>(); it != ResourceMgr::Get().ResourceEnd<Render::Material>(); it++)
                        {
                            const auto &mat = ResourceMgr::Get().IterToRefPtr<Render::Material>(it);
                            auto text = MakeRef<Text>(mat->Name());
                            text->OnMouseClick() += [mat, comp, btn](UIEvent &)
                            {
                                comp->_material = mat;
                                btn->SetText(mat->Name());
                                UIManager::Get()->HidePopup();
                                SceneMgr::Get().MarkCurSceneDirty();
                            };
                            list_view->AddItem(text);
                        } });
                };
            }

            {
                auto btn = Editor::AddButtonRow(context._content, "Color", FormatColorButtonText(Vector4f(comp->_color.r, comp->_color.g, comp->_color.b, comp->_color.a), true));
                btn->OnMouseClick() += [comp, btn](UIEvent &e)
                {
                    auto color_picker = MakeRef<ColorPicker>(Vector4f(comp->_color.r, comp->_color.g, comp->_color.b, comp->_color.a));
                    color_picker->Name("SpriteColor");
                    color_picker->GetSlot()->Size({320.0f, 240.0f});
                    color_picker->OnValueChanged() += [comp, btn](Vector4f color)
                    {
                        comp->_color = Color(color.x, color.y, color.z, color.w);
                        btn->SetText(FormatColorButtonText(color, true));
                        SceneMgr::Get().MarkCurSceneDirty();
                    };
                    auto abs_rect = e._current_target->GetArrangeRect();
                    Vector2f show_pos = abs_rect.xy;
                    show_pos.y += abs_rect.w;
                    UIManager::Get()->ShowPopupAt(show_pos.x, show_pos.y, color_picker);
                };
            }

            Editor::AddFloatInputRow(context._content, "Sorting Layer", std::to_string(comp->_sorting_layer), [comp](f32 v)
            {
                comp->_sorting_layer = static_cast<i16>(v);
                SceneMgr::Get().MarkCurSceneDirty();
            });

            Editor::AddFloatInputRow(context._content, "Order In Layer", std::to_string(comp->_order_in_layer), [comp](f32 v)
            {
                comp->_order_in_layer = static_cast<i32>(v);
                SceneMgr::Get().MarkCurSceneDirty();
            });

            {
                auto blend_items = Vector<String>{"Alpha", "Additive", "Multiply", "Opaque"};
                auto dropdown = Editor::AddDropdownRow(context._content, "Blend Mode", blend_items);
                dropdown->SetSelectedIndex(static_cast<i32>(comp->_blend_mode));
                dropdown->_on_selected_changed += [comp](i32 idx)
                {
                    comp->_blend_mode = static_cast<Render::ESpriteBlendMode>(idx);
                    SceneMgr::Get().MarkCurSceneDirty();
                };
            }

            Editor::AddCheckBoxRow(context._content, "Flip X", comp->_flip_x)->_on_click += [comp](bool checked)
            {
                comp->_flip_x = checked;
                SceneMgr::Get().MarkCurSceneDirty();
            };

            Editor::AddCheckBoxRow(context._content, "Flip Y", comp->_flip_y)->_on_click += [comp](bool checked)
            {
                comp->_flip_y = checked;
                SceneMgr::Get().MarkCurSceneDirty();
            };

            Editor::AddCheckBoxRow(context._content, "Visible", comp->_visible)->_on_click += [comp](bool checked)
            {
                comp->_visible = checked;
                SceneMgr::Get().MarkCurSceneDirty();
            };
        }
    }// namespace Editor
}// namespace Ailu
