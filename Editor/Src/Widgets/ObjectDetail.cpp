#include "Widgets/ObjectDetail.h"
#include "Common/Selection.h"
#include "Framework/Common/ResourceMgr.h"
#include "Scene/Scene.h"
#include "UI/Basic.h"
#include "UI/ColorPicker.h"
#include "UI/Container.h"
#include "UI/UIFramework.h"
#include "Render/2D/Sprite.h"

#include "Common/Undo.h"
#include "Objects/JsonArchive.h"
#include <cctype>

namespace Ailu
{
    using namespace UI;
    using SceneManagement::SceneMgr;
    namespace Editor
    {
        namespace
        {
            inline const Vector4f kPropLabelMargin = {2.0f, 0.0f, 2.0f, 2.0f};
            inline const Vector4f kPropValueMargin = {10.0f, 0.0f, 2.0f, 2.0f};
            inline const Vector4f kPropInnerMargin = {2.0f, 0.0f, 2.0f, 2.0f};
            inline const UI::Padding kComponentBlockMargin = {2.0f, 4.0f, 2.0f, 4.0f};
            inline const UI::Padding kComponentBlockPadding = {4.0f, 4.0f, 4.0f, 4.0f};
            inline const Color kComponentBlockBg = Color(0.105f, 0.115f, 0.145f, 1.0f);
            inline const Color kComponentBlockBorder = Color(0.22f, 0.245f, 0.30f, 1.0f);
            inline constexpr f32 kPropLabelFill = 1.0f;
            inline constexpr f32 kPropValueFill = 3.0f;

            struct ComponentMenuItem
            {
                String _name;
                std::function<bool(ECS::Register &, ECS::Entity)> _is_added;
                std::function<void(ECS::Register &, ECS::Entity)> _add;
            };

            inline String ToLower(String value)
            {
                for (char &ch : value)
                    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                return value;
            }

            inline Vector<ComponentMenuItem> GetComponentMenuItems()
            {
                return {
                    {"Tag", [](auto &r, auto e) { return r.HasComponent<ECS::TagComponent>(e); },
                     [](auto &r, auto e) { r.AddComponent<ECS::TagComponent>(e); }},
                    {"Persistent ID", [](auto &r, auto e) { return r.HasComponent<ECS::PersistentIdComponent>(e); },
                     [](auto &r, auto e) { r.AddComponent<ECS::PersistentIdComponent>(e); }},
                    {"Transform", [](auto &r, auto e) { return r.HasComponent<ECS::TransformComponent>(e); },
                     [](auto &r, auto e) { r.AddComponent<ECS::TransformComponent>(e); }},
                    {"Script", [](auto &r, auto e) { return r.HasComponent<ECS::ScriptComponent>(e); },
                     [](auto &r, auto e) { r.AddComponent<ECS::ScriptComponent>(e); }},
                    {"Static Mesh", [](auto &r, auto e) { return r.HasComponent<ECS::StaticMeshComponent>(e); },
                     [](auto &r, auto e) { r.AddComponent<ECS::StaticMeshComponent>(e); }},
                    {"Light", [](auto &r, auto e) { return r.HasComponent<ECS::LightComponent>(e); },
                     [](auto &r, auto e) { r.AddComponent<ECS::LightComponent>(e); }},
                    {"Camera", [](auto &r, auto e) { return r.HasComponent<ECS::CCamera>(e); },
                     [](auto &r, auto e) { r.AddComponent<ECS::CCamera>(e); }},
                    {"Hierarchy", [](auto &r, auto e) { return r.HasComponent<ECS::CHierarchy>(e); },
                     [](auto &r, auto e) { r.AddComponent<ECS::CHierarchy>(e); }},
                    {"Light Probe", [](auto &r, auto e) { return r.HasComponent<ECS::CLightProbe>(e); },
                     [](auto &r, auto e) { r.AddComponent<ECS::CLightProbe>(e); }},
                    {"Rigid Body", [](auto &r, auto e) { return r.HasComponent<ECS::CRigidBody>(e); },
                     [](auto &r, auto e) { r.AddComponent<ECS::CRigidBody>(e); }},
                    {"Collider", [](auto &r, auto e) { return r.HasComponent<ECS::CCollider>(e); },
                     [](auto &r, auto e) { r.AddComponent<ECS::CCollider>(e); }},
                    {"Skeleton Mesh", [](auto &r, auto e) { return r.HasComponent<ECS::CSkeletonMesh>(e); },
                     [](auto &r, auto e) { r.AddComponent<ECS::CSkeletonMesh>(e); }},
                    {"VXGI", [](auto &r, auto e) { return r.HasComponent<ECS::CVXGI>(e); },
                     [](auto &r, auto e) { r.AddComponent<ECS::CVXGI>(e); }},
                    {"Sprite Renderer", [](auto &r, auto e) { return r.HasComponent<ECS::SpriteRendererComponent>(e); },
                     [](auto &r, auto e) { r.AddComponent<ECS::SpriteRendererComponent>(e); }},
                    {"Audio Source", [](auto &r, auto e) { return r.HasComponent<ECS::AudioSourceComponent>(e); },
                     [](auto &r, auto e) { r.AddComponent<ECS::AudioSourceComponent>(e); }},
                    {"Audio Listener", [](auto &r, auto e) { return r.HasComponent<ECS::AudioListenerComponent>(e); },
                     [](auto &r, auto e) { r.AddComponent<ECS::AudioListenerComponent>(e); }},
                };
            }

            inline String FormatColorButtonText(const Vector4f &color, bool include_alpha = false)
            {
                if (include_alpha)
                    return std::format("R:{:.2f} G:{:.2f} B:{:.2f} A:{:.2f}", color.x, color.y, color.z, color.w);
                return std::format("R:{:.2f} G:{:.2f} B:{:.2f}", color.x, color.y, color.z);
            }

            inline UI::HorizontalBox *AddPropertyRow(UI::UIElement *parent, const String &label, UI::HorizontalBox **out_value_box = nullptr)
            {
                auto row = parent->AddChild<UI::HorizontalBox>();
                row->AddChild<UI::Text>(label)->GetSlotAs<UI::LinearSlot>().Margin(kPropLabelMargin)
                        .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto).FillRate(kPropLabelFill);

                auto value_box = row->AddChild<UI::HorizontalBox>();
                value_box->GetSlotAs<UI::LinearSlot>().Margin(kPropValueMargin).CrossAlignment(UI::EAlignment::kRight)
                        .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto).FillRate(kPropValueFill);

                if (out_value_box != nullptr)
                    *out_value_box = value_box;
                return row;
            }

            inline UI::InputBlock *AddFloatInputRow(UI::UIElement *parent, const String &label, const String &initial_text, const std::function<void(f32)> &on_value_changed)
            {
                UI::HorizontalBox *value_box = nullptr;
                AddPropertyRow(parent, label, &value_box);

                auto input = value_box->AddChild<UI::InputBlock>(initial_text);
                input->GetSlotAs<UI::LinearSlot>().Margin(kPropInnerMargin).CrossAlignment(UI::EAlignment::kRight)
                        .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto).FillRate(1.0f);
                if (on_value_changed)
                {
                    input->_on_content_changed += [on_value_changed](String content)
                    {
                        if (auto opt = StringUtils::ParseFloat(content); opt.has_value())
                            on_value_changed(opt.value());
                    };
                }
                return input;
            }

            inline UI::InputBlock *AddTextInputRow(UI::UIElement *parent, const String &label, const String &initial_text, const std::function<void(const String &)> &on_value_changed)
            {
                UI::HorizontalBox *value_box = nullptr;
                AddPropertyRow(parent, label, &value_box);

                auto input = value_box->AddChild<UI::InputBlock>(initial_text);
                input->GetSlotAs<UI::LinearSlot>().Margin(kPropInnerMargin).CrossAlignment(UI::EAlignment::kRight)
                        .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto).FillRate(1.0f);
                if (on_value_changed)
                {
                    input->_on_content_changed += [on_value_changed](String content)
                    {
                        on_value_changed(content);
                    };
                }
                return input;
            }

            inline UI::Slider *AddFloatSliderRow(UI::UIElement *parent, const String &label, f32 min_value, f32 max_value, f32 value, const std::function<void(f32)> &on_value_changed)
            {
                UI::HorizontalBox *value_box = nullptr;
                AddPropertyRow(parent, label, &value_box);

                auto slider = value_box->AddChild<UI::Slider>(min_value, max_value, value);
                slider->GetSlotAs<UI::LinearSlot>().Margin(kPropInnerMargin).CrossAlignment(UI::EAlignment::kRight)
                        .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto).FillRate(3.0f);
                auto input = value_box->AddChild<UI::InputBlock>(std::format("{:.2f}", value));
                input->GetSlotAs<UI::LinearSlot>().Margin(kPropInnerMargin).CrossAlignment(UI::EAlignment::kRight)
                        .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto).FillRate(1.0f);
                slider->_on_value_change += [input, on_value_changed](f32 v)
                {
                    input->SetContent(std::format("{:.2f}", v), false);
                    if (on_value_changed)
                        on_value_changed(v);
                };
                input->_on_content_changed += [slider](String content)
                {
                    if (auto opt = StringUtils::ParseFloat(content); opt.has_value())
                        slider->SetValue(opt.value());
                };
                return slider;
            }

            inline Array<UI::InputBlock *, 3> AddVec3InputRow(UI::UIElement *parent, const String &label,
                                                              const String &x_text = String{}, const String &y_text = String{}, const String &z_text = String{},
                                                              const std::function<void(int, f32)> &on_axis_value_changed = {})
            {
                UI::HorizontalBox *value_box = nullptr;
                AddPropertyRow(parent, label, &value_box);

                Array<UI::InputBlock *, 3> blocks{};
                const Array<String, 3> texts = {x_text, y_text, z_text};
                for (int axis = 0; axis < 3; ++axis)
                {
                    blocks[axis] = value_box->AddChild<UI::InputBlock>(texts[axis]);
                    blocks[axis]->GetSlotAs<UI::LinearSlot>().Margin(kPropInnerMargin).CrossAlignment(UI::EAlignment::kRight)
                            .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto).FillRate(1.0f);
                    if (on_axis_value_changed)
                    {
                        blocks[axis]->_on_content_changed += [axis, on_axis_value_changed](String content)
                        {
                            if (auto opt = StringUtils::ParseFloat(content); opt.has_value())
                                on_axis_value_changed(axis, opt.value());
                        };
                    }
                }
                return blocks;
            }

            inline UI::Button *AddButtonRow(UI::UIElement *parent, const String &label, const String &button_text)
            {
                UI::HorizontalBox *value_box = nullptr;
                AddPropertyRow(parent, label, &value_box);
                auto btn = value_box->AddChild<UI::Button>();
                btn->GetSlotAs<UI::LinearSlot>().Margin(kPropInnerMargin).CrossAlignment(UI::EAlignment::kRight);
                btn->SetText(button_text);
                return btn;
            }

            inline UI::Dropdown *AddDropdownRow(UI::UIElement *parent, const String &label, const Vector<String> &items)
            {
                UI::HorizontalBox *value_box = nullptr;
                AddPropertyRow(parent, label, &value_box);
                auto dropdown = value_box->AddChild<UI::Dropdown>(items);
                dropdown->GetSlotAs<UI::LinearSlot>().Margin(kPropInnerMargin).CrossAlignment(UI::EAlignment::kRight)
                        .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto).FillRate(1.0f);
                return dropdown;
            }

            inline UI::CheckBox* AddCheckBoxRow(UI::UIElement *parent, const String &label, bool initial_state)
            {
                UI::HorizontalBox *value_box = nullptr;
                AddPropertyRow(parent, label, &value_box);
                auto checkbox = value_box->AddChild<UI::CheckBox>();
                checkbox->GetSlotAs<UI::LinearSlot>().Margin(kPropInnerMargin).CrossAlignment(UI::EAlignment::kRight)
                        .SizePolicy(UI::ESizePolicy::kAuto, UI::ESizePolicy::kAuto);
                checkbox->SetChecked(initial_state);
                return checkbox;
            }

            template<typename T>
            inline UI::CollapsibleView *AddComponentBlock(UI::UIElement *parent, const String &title,
                                                        UI::CollapsibleView **block_ptr,
                                                        ECS::Entity entity)
            {
                auto frame = parent->AddChild<UI::Border>();
                frame->_bg_color = kComponentBlockBg;
                frame->_border_color = kComponentBlockBorder;
                frame->Thickness(1.0f);
                frame->SlotPadding() = kComponentBlockPadding;
                auto &style_override = frame->GetStyleOverride();
                style_override._corner_radius = Vector4f{6.0f};
                style_override._override_mask |= static_cast<u32>(UI::EUIControlVisualOverride::kCornerRadius);
                frame->GetSlotAs<UI::LinearSlot>().Margin(kComponentBlockMargin)
                        .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);

                auto block = frame->AddChild<UI::CollapsibleView>(title);
                block->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);

                // Darken header background to distinguish from content area
                {
                    auto &header_style = static_cast<UI::LinearBox *>(block->GetHeader())->GetStyleOverride();
                    UI::UIBrush header_bg;
                    header_bg._type = UI::EUIBrushType::kColor;
                    header_bg._tint = Color(0.055f, 0.06f, 0.075f, 1.0f);
                    header_style.SetBackground(header_bg);
                }

                // Make title fill horizontal space so the remove button stays on the right
                if (auto *header_box = static_cast<UI::LinearBox *>(block->GetHeader()))
                {
                    if (auto *title_widget = header_box->ChildAt(0))
                        title_widget->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
                }

                // Add remove button on the right side of header
                {
                    auto *header_box = static_cast<UI::LinearBox *>(block->GetHeader());
                    auto *remove_btn = header_box->AddChild<UI::Button>();
                    remove_btn->SetText("X");
                    remove_btn->GetSlotAs<UI::LinearSlot>()
                            .SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed)
                            .Size({UI::CollapsibleView::s_header_height, UI::CollapsibleView::s_header_height});

                    remove_btn->OnMouseClick() += [parent, frame, block_ptr, entity](UI::UIEvent &e)
                    {
                        if (entity != ECS::kInvalidEntity)
                        {
                            // 通过 CommandManager 统一管理，支持 undo/redo
                            auto *scene = SceneMgr::Get().ActiveScene();
                            g_pCommandMgr->ExecuteCommand(MakeScope<SceneQueuedCommand>(
                                scene, MakeScope<SceneManagement::RemoveComponentCommand<T>>(entity)));
                            // 不立即移除 UI，下一帧 Update 检测到组件消失后会自动清理
                        }
                        else
                        {
                            // 无组件需要移除，直接清理 UI
                            parent->RemoveChild(frame);
                            if (block_ptr)
                                *block_ptr = nullptr;
                        }
                        e._is_handled = true;
                    };
                }

                return block;
            }

            inline void RemoveComponentBlock(UI::UIElement *parent, UI::CollapsibleView *&block)
            {
                if (block == nullptr)
                    return;

                UI::UIElement *frame = block->GetParent();
                parent->RemoveChild(frame != nullptr ? frame : block);
                block = nullptr;
            }
        }// namespace

        inline void ShowPopupListView(UI::UIElement *anchor, float viewport_height, const std::function<void(const Ref<UI::ListView> &)> &fill_fn)
        {
            auto list_view = MakeRef<UI::ListView>();
            list_view->Name("PopupListView");

            const auto abs_rect = anchor->GetArrangeRect();
            list_view->GetSlot()->Size({abs_rect.z, list_view->GetSlot()->_size.y});

            // 由调用方负责填充内容
            fill_fn(list_view);

            list_view->SetViewportHeight(viewport_height);

            Vector2f show_pos = abs_rect.xy;
            show_pos.y += abs_rect.w;

            UI::UIManager::Get()->ShowPopupAt(show_pos.x, show_pos.y, list_view);
        }

        static void CreateMaterialPropWidget(UI::UIElement *root, Render::ShaderPropertyInfo &prop, Material *obj)
        {
            UI::HorizontalBox *value_box = nullptr;
            AddPropertyRow(root, prop._prop_name, &value_box);
            String value_name = prop._value_name;//保存名称(HLSL中实际使用的属性名称)，这里传入的prop引用可能会失效当shader重载时
            if (prop._type == Render::EShaderPropertyType::kRange)
            {
                auto slider = value_box->AddChild<UI::Slider>();
                slider->GetSlotAs<UI::LinearSlot>().Margin(kPropInnerMargin).CrossAlignment(UI::EAlignment::kRight)
                        .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto).FillRate(3.0f);
                f32 current_value = prop.GetValue<f32>();
                current_value = std::max(std::min(current_value, prop._default_value[1]), prop._default_value[0]);
                slider->SetValue((current_value - prop._default_value[0]) / (prop._default_value[1] - prop._default_value[0]));
                slider->_range = {prop._default_value[0], prop._default_value[1]};
                auto input = value_box->AddChild<UI::InputBlock>();
                input->GetSlotAs<UI::LinearSlot>().Margin(kPropInnerMargin).CrossAlignment(UI::EAlignment::kRight)
                        .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto).FillRate(1.0f);
                input->SetContent(std::format("{:.2f}", prop.GetValue<f32>()));
                input->_on_content_changed += [value_name,obj,slider](String content)
                {
                    if (auto opt = StringUtils::ParseFloat(content); opt.has_value())
                    {
                        auto p = obj->GetShaderProperty(value_name);
                        if (p)
                            p->SetValue<f32>(opt.value());
                        slider->SetValue((opt.value() - p->_default_value[0]) / (p->_default_value[1] - p->_default_value[0]), false);
                    }
                };
                slider->_on_value_change += [value_name,obj,input](f32 v) {
                    auto p = obj->GetShaderProperty(value_name);
                    if (p)
                        p->SetValue<f32>(v);
                    input->SetContent(std::format("{:.2f}", v), false);
                };
            }
            else if (prop._type == Render::EShaderPropertyType::kFloat)
            {
                auto input = value_box->AddChild<UI::InputBlock>();
                input->GetSlotAs<UI::LinearSlot>().Margin(kPropInnerMargin).CrossAlignment(UI::EAlignment::kRight)
                        .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto).FillRate(1.0f);
                input->SetContent(std::format("{:.2f}", prop.GetValue<f32>()));
                input->_on_content_changed += [value_name,obj](String content)
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
                auto btn = value_box->AddChild<UI::Button>();
                btn->GetSlotAs<UI::LinearSlot>().Margin(kPropInnerMargin).CrossAlignment(UI::EAlignment::kRight)
                        .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto).FillRate(1.0f);
                btn->SetText(FormatColorButtonText(prop.GetValue<Vector4f>(), true));
                btn->OnMouseClick() += [value_name,obj,btn](UI::UIEvent &e)
                {
                    auto color_picker = MakeRef<UI::ColorPicker>(obj->GetShaderProperty(value_name)->GetValue<Vector4f>());
                    color_picker->Name(std::format("ColorPicker_{}", value_name));
                    color_picker->GetSlot()->Size({320.0f, 240.0f});
                    color_picker->OnValueChanged() += [value_name,obj,btn](Vector4f color)
                    {
                        auto p = obj->GetShaderProperty(value_name);
                        if (p)
                            p->SetValue<Vector4f>(color);
                        btn->SetText(FormatColorButtonText(color, true));
                    };
                    auto abs_rect = e._current_target->GetArrangeRect();
                    Vector2f show_pos = abs_rect.xy;
                    show_pos.y += abs_rect.w;
                    UI::UIManager::Get()->ShowPopupAt(show_pos.x, show_pos.y, color_picker);
                };
            }
            else if (prop._type == Render::EShaderPropertyType::kTexture2D)
            {
                auto btn = value_box->AddChild<UI::Button>();
                btn->GetSlotAs<UI::LinearSlot>().Margin(kPropInnerMargin).CrossAlignment(UI::EAlignment::kRight)
                        .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto).FillRate(1.0f);
                btn->OnMouseClick() += [value_name,obj](UI::UIEvent &e)
                {
                    ShowPopupListView(e._current_target, 200.0f, [value_name,obj](const Ref<UI::ListView> &list_view)
                                      {
                        auto none_item = MakeRef<UI::Text>("None");
                        none_item->GetSlot()->Size({64.0f, 64.0f});
                        none_item->OnMouseClick() += [value_name,obj](UI::UIEvent &e)
                        {
                            obj->SetTexture(value_name,nullptr);
                            UIManager::Get()->HidePopup();
                        };
                        list_view->AddItem(none_item);
                        none_item->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFixed, ESizePolicy::kFixed).Size({64.0f, 64.0f});
                        for (auto it = ResourceMgr::Get().ResourceBegin<Render::Texture2D>(); it != ResourceMgr::Get().ResourceEnd<Render::Texture2D>(); it++)
                        {
                            auto tex = ResourceMgr::Get().IterToRefPtr<Render::Texture2D>(it).get();
                            auto item_hb = MakeRef<UI::HorizontalBox>();
                            auto img = item_hb->AddChild<UI::Image>(tex);
                            img->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFixed, ESizePolicy::kFixed).Size({64.0f, 64.0f});
                            img->OnMouseClick() += [value_name, tex, obj](UI::UIEvent &e)
                            {
                                obj->SetTexture(value_name,tex);
                                //prop.SetValue<Texture2D*>(tex);
                                UIManager::Get()->HidePopup();
                            };
                            auto item = item_hb->AddChild<UI::Text>(tex->Name());
                            item->SlotPadding() = UI::Padding(4.0f);
                            item->InvalidateLayout();
                            list_view->AddItem(item_hb);
                        } });
                };
            }
        }

        void CreateTransformBlock(ECS::Entity entity,VerticalBox* vb,CollapsibleView*& transform_block)
        {
            if (transform_block != nullptr)
                return;

            transform_block = AddComponentBlock<ECS::TransformComponent>(vb, "Transform", &transform_block, entity);
            auto transf_block = transform_block->GetContent()->AddChild<UI::VerticalBox>();
            auto pos_block = AddVec3InputRow(transf_block, "Position", "0000");
            auto rot_block = AddVec3InputRow(transf_block, "Rotation");
            auto scale_block = AddVec3InputRow(transf_block, "Scale");
            for (auto i = 0; i < 3; i++)
            {
                pos_block[i]->_on_content_changed += [entity,i](String content)
                {
                    auto &r = SceneMgr::Get().ActiveScene()->GetRegister();
                    if (auto comp = r.GetComponent<ECS::TransformComponent>(entity); comp != nullptr)
                    {
                        if (auto opt = StringUtils::ParseFloat(content); opt.has_value())
                            comp->_local_transform._position[i] = opt.value();
                    }
                };
                rot_block[i]->_on_content_changed += [entity,i](String content)
                {
                    auto &r = SceneMgr::Get().ActiveScene()->GetRegister();
                    if (auto comp = r.GetComponent<ECS::TransformComponent>(entity); comp != nullptr)
                    {
                        if (auto opt = StringUtils::ParseFloat(content); opt.has_value())
                        {
                            Vector3f euler = Quaternion::EulerAngles(comp->_local_transform._rotation);
                            euler[i] = opt.value();
                            comp->_local_transform._rotation = Quaternion::EulerAngles(euler);
                        }
                    }
                };
                scale_block[1]->_on_content_changed += [entity,i](String content)
                {
                    auto &r = SceneMgr::Get().ActiveScene()->GetRegister();
                    if (auto comp = r.GetComponent<ECS::TransformComponent>(entity); comp != nullptr)
                    {
                        if (auto opt = StringUtils::ParseFloat(content); opt.has_value())
                            comp->_local_transform._scale[i] = opt.value();
                    }
                };
            }
            auto set_block = [&](UI::InputBlock *block, f32 value)
            {
                if (!block->IsEditing())
                    block->SetContent(std::format("{:.2f}", value), false);
            };
             auto &r = SceneMgr::Get().ActiveScene()->GetRegister();
            auto comp = r.GetComponent<ECS::TransformComponent>(entity);
            set_block(pos_block[0], comp->_local_transform._position.x);
            set_block(pos_block[1], comp->_local_transform._position.y);
            set_block(pos_block[2], comp->_local_transform._position.z);
            Vector3f euler = Quaternion::EulerAngles(comp->_local_transform._rotation);
            set_block(rot_block[0], euler.x);
            set_block(rot_block[1], euler.y);
            set_block(rot_block[2], euler.z);
            set_block(scale_block[0], comp->_local_transform._scale.x);
            set_block(scale_block[1], comp->_local_transform._scale.y);
            set_block(scale_block[2], comp->_local_transform._scale.z);
        }

        ObjectDetail::ObjectDetail() : DockWindow("Object Detail")
        {
            _root = _content_root->AddChild<UI::ScrollView>();
            _root->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            _vb = _root->AddChild<UI::VerticalBox>();
            _vb->SlotPadding() = UI::Padding(4.0f, 6.0f, 0.0f, 0.0f);
            _vb->InvalidateLayout();
            auto name_row = _vb->AddChild<UI::HorizontalBox>();
            _name_text = name_row->AddChild<UI::Text>("Name");
            _name_text->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
            _add_component_button = name_row->AddChild<UI::Button>();
            _add_component_button->SetText("+");
            _add_component_button->GetSlotAs<UI::LinearSlot>()
                    .SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed)
                    .Size({UI::CollapsibleView::s_header_height, UI::CollapsibleView::s_header_height});
            _add_component_button->OnMouseClick() += [this](UI::UIEvent &e)
            {
                ShowAddComponentPopup(e._current_target);
                e._is_handled = true;
            };
            _vb->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);

            Selection::on_selection_changed += [this]()
            {
                _needs_rebuild = true;
            };
        }
        ObjectDetail::~ObjectDetail()
        {
            JsonArchive ar;
            ar << *_content_widget;
            ar.Save("ObjectDetailLayout.json");
        }

        void ObjectDetail::ShowAddComponentPopup(UI::UIElement *anchor)
        {
            const ECS::Entity selected = Selection::FirstEntity();
            auto *scene = SceneMgr::Get().ActiveScene();
            if (selected == ECS::kInvalidEntity || scene == nullptr || anchor == nullptr)
                return;

            auto popup = MakeRef<UI::VerticalBox>();
            popup->Name("AddComponentPopup");
            popup->SlotPadding() = UI::Padding(4.0f);

            auto search = popup->AddChild<UI::InputBlock>("Search components...");
            search->GetSlotAs<UI::LinearSlot>()
                    .SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed)
                    .Size({300.0f, 28.0f});

            auto list_view = popup->AddChild<UI::ListView>();
            list_view->GetSlotAs<UI::LinearSlot>()
                    .SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed)
                    .Size({300.0f, 240.0f});
            list_view->SetViewportHeight(240.0f);

            const auto items = GetComponentMenuItems();
            auto populate = std::make_shared<std::function<void(const String &)>>();
            *populate = [list_view, items, selected, scene](const String &query)
            {
                list_view->ClearItems();
                const String lowered_query = ToLower(query);
                auto &scene_register = scene->GetRegister();
                for (const auto &item : items)
                {
                    if (item._is_added(scene_register, selected) ||
                        ToLower(item._name).find(lowered_query) == String::npos)
                        continue;

                    auto item_text = MakeRef<UI::Text>(item._name);
                    item_text->OnMouseClick() += [item, selected, scene](UI::UIEvent &e)
                    {
                        auto &scene_register = scene->GetRegister();
                        if (!item._is_added(scene_register, selected))
                        {
                            item._add(scene_register, selected);
                            SceneMgr::Get().MarkCurSceneDirty();
                        }
                        UI::UIManager::Get()->HidePopup();
                        e._is_handled = true;
                    };
                    list_view->AddItem(item_text);
                }
            };
            search->_on_content_changed += [populate](String query)
            {
                (*populate)(query);
            };
            (*populate)(String{});

            const auto abs_rect = anchor->GetArrangeRect();
            popup->GetSlot()->Size({308.0f, 280.0f});
            Vector2f show_pos = abs_rect.xy;
            show_pos.y += abs_rect.w;
            UI::UIManager::Get()->ShowPopupAt(show_pos.x, show_pos.y, popup);
        }

        void ObjectDetail::Update(f32 dt)
        {
            DockWindow::Update(dt);

            auto remove_block = [&](UI::CollapsibleView *&block)
            {
                RemoveComponentBlock(_vb, block);
            };
            auto clear_dynamic_blocks = [&]()
            {
                remove_block(_transform_block);
                remove_block(_script_block);
                remove_block(_light_block);
                remove_block(_static_mesh_block);
                remove_block(_light_probe_block);
                remove_block(_cam_block);
                remove_block(_sprite_block);
                _script_path_block = nullptr;
                _prev_comp_block = nullptr;
            };

            if (auto selected = Selection::FirstEntity(); selected != ECS::kInvalidEntity)
            {
                if (_needs_rebuild)
                {
                    clear_dynamic_blocks();
                    _needs_rebuild = false;
                }
                auto &r = SceneMgr::Get().ActiveScene()->GetRegister();
                if (auto comp = r.GetComponent<ECS::TagComponent>(selected); comp != nullptr)
                {
                    _name_text->SetText(comp->_name);
                }
                if (auto comp = r.GetComponent<ECS::TransformComponent>(selected); comp != nullptr)
                {
                    if (_transform_block == nullptr)
                        CreateTransformBlock(selected,_vb,_transform_block);
                }
                else
                    remove_block(_transform_block);
                if (auto comp = r.GetComponent<ECS::ScriptComponent>(selected); comp != nullptr)
                {
                    if (_script_path_block == nullptr)
                    {
                        remove_block(_script_block);
                        _script_block = AddComponentBlock<ECS::ScriptComponent>(_vb, "Script", &_script_block, selected);
                        auto content = _script_block->GetContent()->AddChild<UI::VerticalBox>();
                        _script_path_block = AddTextInputRow(content, "Path", comp->_script_path, [comp](const String &content)
                        {
                            if (comp->_script_path == content)
                                return;
                            comp->_script_path = content;
                            comp->ResetRuntime();
                            SceneMgr::Get().MarkCurSceneDirty();
                        });
                        auto hint = content->AddChild<UI::Text>("Example: Scripts/tick_logger.lua");
                        hint->GetSlotAs<UI::LinearSlot>().Margin({2.0f, 0.0f, 2.0f, 2.0f});
                    }
                    if (_script_path_block != nullptr && !_script_path_block->IsEditing())
                        _script_path_block->SetContent(comp->_script_path, false);
                }
                else if (_script_block == nullptr || _script_path_block != nullptr)
                {
                    remove_block(_script_block);
                    _script_block = AddComponentBlock<ECS::ScriptComponent>(_vb, "Script", &_script_block, ECS::kInvalidEntity);
                    auto content = _script_block->GetContent()->AddChild<UI::VerticalBox>();
                    auto add_btn = AddButtonRow(content, "Component", "Add ScriptComponent");
                    add_btn->OnMouseClick() += [selected](UI::UIEvent &e)
                    {
                        auto &scene_register = SceneMgr::Get().ActiveScene()->GetRegister();
                        if (scene_register.GetComponent<ECS::ScriptComponent>(selected) != nullptr)
                            return;
                        auto &script_comp = scene_register.AddComponent<ECS::ScriptComponent>(selected);
                        script_comp._script_path = "Scripts/tick_logger.lua";
                        script_comp.ResetRuntime();
                        SceneMgr::Get().MarkCurSceneDirty();
                    };
                    auto hint = content->AddChild<UI::Text>("Click to attach the sample logger script");
                    hint->GetSlotAs<UI::LinearSlot>().Margin({2.0f, 0.0f, 2.0f, 2.0f});
                    _script_path_block = nullptr;
                }
                if (auto comp = r.GetComponent<ECS::LightComponent>(selected); comp != nullptr)
                {
                    if (_light_block == nullptr)
                    {
                        remove_block(_light_block);
                        _light_block = AddComponentBlock<ECS::LightComponent>(_vb, "LightComp", &_light_block, selected);
                        auto content = _light_block->GetContent()->AddChild<UI::VerticalBox>();
                        auto items = Vector<String>{"Directional", "Point", "Spot", "Area"};
                        auto light_type_dropdown = AddDropdownRow(content, "Type", items);
                        light_type_dropdown->SetSelectedIndex(static_cast<i32>(comp->_type));
                        light_type_dropdown->_on_selected_changed += [comp](i32 idx){
                            comp->_type = static_cast<ECS::ELightType>(idx);
                        };
                        {
                            AddFloatSliderRow(content, "Intensity", 0.0f, 100.0f, comp->_light._light_color.a, [=](f32 value)
                                              { comp->_light._light_color.a = value; });
                        }
                        {
                            auto btn = AddButtonRow(content, "Color", FormatColorButtonText(Vector4f(comp->_light._light_color.r, comp->_light._light_color.g, comp->_light._light_color.b, comp->_light._light_color.a)));
                            btn->OnMouseClick() += [comp, btn](UI::UIEvent &e)
                            {
                                auto color_picker = MakeRef<UI::ColorPicker>(Vector4f(comp->_light._light_color.r, comp->_light._light_color.g, comp->_light._light_color.b, 1.0f));
                                color_picker->Name("LightColor");
                                color_picker->GetSlot()->Size({320.0f, 240.0f});
                                color_picker->OnValueChanged() += [comp, btn](Vector4f color)
                                {
                                    comp->_light._light_color.r = color.r;
                                    comp->_light._light_color.g = color.g;
                                    comp->_light._light_color.b = color.b;
                                    btn->SetText(FormatColorButtonText(Vector4f(color.r, color.g, color.b, comp->_light._light_color.a)));
                                };
                                auto abs_rect = e._current_target->GetArrangeRect();
                                Vector2f show_pos = abs_rect.xy;
                                show_pos.y += abs_rect.w;
                                UI::UIManager::Get()->ShowPopupAt(show_pos.x, show_pos.y, color_picker);
                            };
                        }
                        if (comp->_type == ECS::ELightType::kDirectional)
                        {
                            auto &light_data = comp->_light;
                            if (light_data._light_param.w <= 0.0f)
                                light_data._light_param.w = 0.265f;
                            AddFloatSliderRow(content, "AngularRadius", 0.01f, 5.0f, light_data._light_param.w, [&light_data](f32 value)
                            {
                                light_data._light_param.w = value;
                            });
                        }
                        if (comp->_type == ECS::ELightType::kArea)
                        {
                            static const Vector<String> kShapes = {"Rectangle", "Disc"};
                            auto &light_data = comp->_light;
                            auto dropdown = AddDropdownRow(content, "Shape", kShapes);
                            dropdown->SetSelectedIndex(static_cast<i32>(light_data._light_param.w));
                            dropdown->_on_selected_changed += [&light_data](i32 idx){
                                light_data._light_param.w = static_cast<f32>(idx);
                            };
                            AddFloatSliderRow(content, "Range", 0.0f, 500.0f, light_data._light_param.x, [&light_data](f32 value)
                                      { light_data._light_param.x = value; });
                            AddCheckBoxRow(content, "TwoSide", light_data._is_two_side)->_on_click += [&light_data](bool checked)
                            {
                                light_data._is_two_side = checked;
                            };
                            if (light_data._light_param.w == 0)
                            {
                                AddFloatInputRow(content, "Width", std::to_string(light_data._light_param.y), [&light_data](f32 value)
                                {
                                    light_data._light_param.y = value;
                                });
                                AddFloatInputRow(content, "Height", std::to_string(light_data._light_param.z), [&light_data](f32 value)
                                {
                                    light_data._light_param.z = value;
                                });
                            }
                            else
                            {
                                AddFloatSliderRow(content, "Radius", 0.0f, 10.0f, light_data._light_param.x, [&light_data](f32 value)
                                {
                                    light_data._light_param.x = value;
                                });
                            }
                        }
                        else if (comp->_type == ECS::ELightType::kSpot)
                        {
                            auto &light_data = comp->_light;
                            AddFloatSliderRow(content, "Range", 0.0f, 500.0f, light_data._light_param.x, [&light_data](f32 value)
                                      { light_data._light_param.x = value; });
                            AddFloatSliderRow(content, "InnerAngle", 0.0f, 180.0f, light_data._light_param.y, [&light_data](f32 value)
                                      { light_data._light_param.y = value; });
                            AddFloatSliderRow(content, "OuterAngle", 0.0f, 180.0f, light_data._light_param.z, [&light_data](f32 value)
                                      { light_data._light_param.z = value; });
                        }
                        else if (comp->_type == ECS::ELightType::kPoint)
                        {
                            auto &light_data = comp->_light;
                            AddFloatSliderRow(content, "Range", 0.0f, 80.0f, light_data._light_param.x, [&light_data](f32 value)
                            {
                                light_data._light_param.x = value;
                            });
                            AddFloatSliderRow(content, "Radius", 0.0f, 1.0f, light_data._light_param.y, [&light_data](f32 value)
                            {
                                light_data._light_param.y = value;
                            });
                        }
                    }
                }
                else
                {
                    remove_block(_light_block);
                }
                if (auto comp = r.GetComponent<ECS::StaticMeshComponent>(selected); comp != nullptr)
                {
                    if (_static_mesh_block == nullptr || _needs_rebuild)
                    {
                        remove_block(_static_mesh_block);
                        _static_mesh_block = AddComponentBlock<ECS::StaticMeshComponent>(_vb, "StaticMesh", &_static_mesh_block, selected);
                        auto content = _static_mesh_block->GetContent()->AddChild<UI::VerticalBox>();
                        //mesh
                        {
                            auto btn = AddButtonRow(content, "Mesh", comp->_p_mesh != nullptr ? comp->_p_mesh->Name() : "None");
                            btn->OnMouseClick() += [comp, btn](UI::UIEvent &e)
                            {
                                ShowPopupListView(e._current_target, 200.0f, [comp, btn](const Ref<UI::ListView> &list_view)
                                                  {
                                    for (auto it = ResourceMgr::Get().ResourceBegin<Render::Mesh>(); it != ResourceMgr::Get().ResourceEnd<Render::Mesh>(); it++)
                                    {
                                        const auto &mesh = ResourceMgr::Get().IterToRefPtr<Render::Mesh>(it);
                                        auto text = MakeRef<Text>(mesh->Name());
                                        text->OnMouseClick() += [mesh, comp, btn](UIEvent &e)
                                        {
                                            LOG_INFO("StaticMesh item clicked: {}", mesh->Name());
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
                        //material
                        {
                            auto subindex = Selection::GetSelectedSubIndex(selected);
                            auto btn = AddButtonRow(content, std::format("Material[{}]", subindex), (comp->_p_mats[subindex] != nullptr) ? comp->_p_mats[subindex]->Name() : "None");
                            btn->OnMouseClick() += [comp, btn, subindex](UI::UIEvent &e)
                            {
                                ShowPopupListView(e._current_target, 200.0f, [comp, btn, subindex](const Ref<UI::ListView> &list_view)
                                                  {
                                        for (auto it = ResourceMgr::Get().ResourceBegin<Render::Material>(); it != ResourceMgr::Get().ResourceEnd<Render::Material>(); it++)
                                        {
                                            const auto &mat = ResourceMgr::Get().IterToRefPtr<Render::Material>(it);
                                            auto text = MakeRef<Text>(mat->Name());
                                            text->OnMouseClick() += [mat, comp, btn, subindex](UIEvent &e)
                                            {
                                                LOG_INFO("Material item clicked: {}", mat->Name());
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
                                auto dropdown = AddDropdownRow(content, "CullMode", Vector<String>{"Off", "Front", "Back"});
                                dropdown->SetSelectedIndex(static_cast<i32>(mat->GetCullMode()));
                                dropdown->_on_selected_changed += [mat](i32 idx)
                                {
                                    mat->SetCullMode(static_cast<Render::ECullMode>(idx));
                                };
                                for (auto &prop: mat->GetShaderProperty())
                                {
                                    CreateMaterialPropWidget(content, *prop, mat.get());
                                }
                            }
                        }
                    }
                }
                else
                {
                    remove_block(_static_mesh_block);
                }
                if (auto comp = r.GetComponent<ECS::CLightProbe>(selected); comp != nullptr)
                {
                    if (_light_probe_block == nullptr)
                    {
                        remove_block(_light_probe_block);
                        _light_probe_block = AddComponentBlock<ECS::CLightProbe>(_vb, "LightProbe", &_light_probe_block, selected);
                        auto content = _light_probe_block->GetContent()->AddChild<UI::VerticalBox>();
                        {
                            auto btn = content->AddChild<UI::Button>();
                            btn->SetText("Update Probe");
                            btn->OnMouseClick() += [comp](UI::UIEvent &e)
                            {
                                comp->_is_dirty = true;
                            };
                        }
                        auto toggle = AddCheckBoxRow(content,"UpdateTick",comp->_is_update_every_tick);
                        toggle->_on_click += [comp](bool is_on){
                            comp->_is_update_every_tick = is_on;
                        };
                    }
                }
                else
                {
                    remove_block(_light_probe_block);
                }
                if (auto comp = r.GetComponent<ECS::CCamera>(selected); comp != nullptr)
                {
                    if (_cam_block == nullptr)
                    {
                        remove_block(_cam_block);
                        _cam_block = AddComponentBlock<ECS::CCamera>(_vb, "Camera", &_cam_block, selected);
                        auto content = _cam_block->GetContent()->AddChild<UI::VerticalBox>();
                        //camera type
                        auto items = Vector<String>{"Perspective", "Orthographic"};
                        AddDropdownRow(content, "Type", items);
                        {
                            AddFloatInputRow(content, "Near", std::format("{}", comp->_camera.Near()), [=](f32 v)
                                             { comp->_camera.Near(v); });
                        }
                        {
                            AddFloatInputRow(content, "Far", std::format("{}", comp->_camera.Far()), [=](f32 v)
                                             { comp->_camera.Far(v); });
                        }
                        {
                            AddFloatInputRow(content, "Aspect", std::format("{}", comp->_camera.Aspect()), [=](f32 v)
                                             { comp->_camera.Aspect(v); });
                        }
                    }
                }
                else
                {
                    remove_block(_cam_block);
                }
                if (auto comp = r.GetComponent<ECS::SpriteRendererComponent>(selected); comp != nullptr)
                {
                    if (_sprite_block == nullptr)
                    {
                        remove_block(_sprite_block);
                        _sprite_block = AddComponentBlock<ECS::SpriteRendererComponent>(_vb, "Sprite Renderer", &_sprite_block, selected);
                        auto content = _sprite_block->GetContent()->AddChild<UI::VerticalBox>();

                        // Sprite picker
                        {
                            String sprite_name = (comp->_sprite != nullptr) ?
                                comp->_sprite->Name() : "None";
                            auto btn = AddButtonRow(content, "Sprite", sprite_name);
                            btn->OnMouseClick() += [comp, btn](UI::UIEvent &e)
                            {
                                ShowPopupListView(e._current_target, 200.0f, [comp, btn](const Ref<UI::ListView> &list_view)
                                                  {
                                    auto none_item = MakeRef<UI::Text>("None");
                                    none_item->OnMouseClick() += [comp, btn](UI::UIEvent &)
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
                                        auto text = MakeRef<UI::Text>(sprite_ref->Name());
                                        text->OnMouseClick() += [sprite_ref, comp, btn](UI::UIEvent &)
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

                        // Material picker
                        {
                            auto btn = AddButtonRow(content, "Material", comp->_material != nullptr ? comp->_material->Name() : "Default");
                            btn->OnMouseClick() += [comp, btn](UI::UIEvent &e)
                            {
                                ShowPopupListView(e._current_target, 200.0f, [comp, btn](const Ref<UI::ListView> &list_view)
                                                  {
                                    auto none_item = MakeRef<UI::Text>("Default");
                                    none_item->OnMouseClick() += [comp, btn](UI::UIEvent &)
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
                                        auto text = MakeRef<UI::Text>(mat->Name());
                                        text->OnMouseClick() += [mat, comp, btn](UI::UIEvent &)
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

                        // Color
                        {
                            auto btn = AddButtonRow(content, "Color", FormatColorButtonText(Vector4f(comp->_color.r, comp->_color.g, comp->_color.b, comp->_color.a), true));
                            btn->OnMouseClick() += [comp, btn](UI::UIEvent &e)
                            {
                                auto color_picker = MakeRef<UI::ColorPicker>(Vector4f(comp->_color.r, comp->_color.g, comp->_color.b, comp->_color.a));
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
                                UI::UIManager::Get()->ShowPopupAt(show_pos.x, show_pos.y, color_picker);
                            };
                        }

                        // Sorting Layer
                        {
                            AddFloatInputRow(content, "Sorting Layer", std::to_string(comp->_sorting_layer), [comp](f32 v)
                            {
                                comp->_sorting_layer = static_cast<i16>(v);
                                SceneMgr::Get().MarkCurSceneDirty();
                            });
                        }

                        // Order In Layer
                        {
                            AddFloatInputRow(content, "Order In Layer", std::to_string(comp->_order_in_layer), [comp](f32 v)
                            {
                                comp->_order_in_layer = static_cast<i32>(v);
                                SceneMgr::Get().MarkCurSceneDirty();
                            });
                        }

                        // Blend Mode
                        {
                            auto blend_items = Vector<String>{"Alpha", "Additive", "Multiply", "Opaque"};
                            auto dropdown = AddDropdownRow(content, "Blend Mode", blend_items);
                            dropdown->SetSelectedIndex(static_cast<i32>(comp->_blend_mode));
                            dropdown->_on_selected_changed += [comp](i32 idx)
                            {
                                comp->_blend_mode = static_cast<Render::ESpriteBlendMode>(idx);
                                SceneMgr::Get().MarkCurSceneDirty();
                            };
                        }

                        // Flip X
                        AddCheckBoxRow(content, "Flip X", comp->_flip_x)->_on_click += [comp](bool checked)
                        {
                            comp->_flip_x = checked;
                            SceneMgr::Get().MarkCurSceneDirty();
                        };

                        // Flip Y
                        AddCheckBoxRow(content, "Flip Y", comp->_flip_y)->_on_click += [comp](bool checked)
                        {
                            comp->_flip_y = checked;
                            SceneMgr::Get().MarkCurSceneDirty();
                        };

                        // Visible
                        AddCheckBoxRow(content, "Visible", comp->_visible)->_on_click += [comp](bool checked)
                        {
                            comp->_visible = checked;
                            SceneMgr::Get().MarkCurSceneDirty();
                        };
                    }
                }
                else
                {
                    remove_block(_sprite_block);
                }
            }
            else
            {
                _name_text->SetText("Name: (No Selection)");
                clear_dynamic_blocks();
                _needs_rebuild = true;
            }
        }
    }// namespace Editor
}// namespace Ailu
