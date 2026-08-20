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
        }// namespace

        void SpriteRendererComponentEditor::Build(ComponentEditorContext &context)
        {
            auto *comp = context.GetComponent<ECS::SpriteRendererComponent>();
            if (comp == nullptr || context._content == nullptr)
                return;

            {
                auto *dropdown = Editor::AddObjectAssetDropdownRow(context._content, "Sprite", Render::Sprite::StaticType(),
                    ResourceMgr::Get().GetAssetGuid(comp->_sprite));
                dropdown->_on_object_asset_selected += [comp](Asset *, Object *selected, const Guid &)
                {
                    comp->_sprite = dynamic_cast<Render::Sprite *>(selected);
                    SceneMgr::Get().MarkCurSceneDirty();
                };
            }

            {
                auto *dropdown = Editor::AddObjectAssetDropdownRow(context._content, "Material", Render::Material::StaticType(),
                    ResourceMgr::Get().GetAssetGuid(comp->_material.get()));
                dropdown->_on_object_asset_selected += [comp](Asset *, Object *selected, const Guid &)
                {
                    comp->_material = selected == nullptr ? nullptr : std::dynamic_pointer_cast<Render::Material>(selected->SharedFromThis());
                    SceneMgr::Get().MarkCurSceneDirty();
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
