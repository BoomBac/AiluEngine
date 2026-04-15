#include "Widgets/ObjectDetail.h"
#include "Common/Selection.h"
#include "Framework/Common/ResourceMgr.h"
#include "Scene/Scene.h"
#include "UI/Basic.h"
#include "UI/ColorPicker.h"
#include "UI/Container.h"
#include "UI/UIFramework.h"

#include "Objects/JsonArchive.h"

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
            inline constexpr f32 kPropLabelFill = 1.0f;
            inline constexpr f32 kPropValueFill = 3.0f;

            inline String FormatColorButtonText(const Vector4f &color, bool include_alpha = false)
            {
                if (include_alpha)
                    return std::format("R:{:.2f} G:{:.2f} B:{:.2f} A:{:.2f}", color.x, color.y, color.z, color.w);
                return std::format("R:{:.2f} G:{:.2f} B:{:.2f}", color.x, color.y, color.z);
            }

            inline UI::HorizontalBox *AddPropertyRow(UI::UIElement *parent, const String &label, UI::HorizontalBox **out_value_box = nullptr)
            {
                auto row = parent->AddChild<UI::HorizontalBox>();
                row->AddChild<UI::Text>(label)
                        ->SlotMargin(kPropLabelMargin)
                        .SlotSizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto)
                        .SlotFillRate(kPropLabelFill);

                auto value_box = row->AddChild<UI::HorizontalBox>()
                                         ->SlotMargin(kPropValueMargin)
                                         .SlotAlignmentH(UI::EAlignment::kRight)
                                         .SlotSizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto)
                                         .SlotFillRate(kPropValueFill)
                                         .As<UI::HorizontalBox>();

                if (out_value_box != nullptr)
                    *out_value_box = value_box;
                return row;
            }

            inline UI::InputBlock *AddFloatInputRow(UI::UIElement *parent, const String &label, const String &initial_text, const std::function<void(f32)> &on_value_changed)
            {
                UI::HorizontalBox *value_box = nullptr;
                AddPropertyRow(parent, label, &value_box);

                auto input = value_box->AddChild<UI::InputBlock>(initial_text)
                                     ->SlotMargin(kPropInnerMargin)
                                     .SlotAlignmentH(UI::EAlignment::kRight)
                                     .SlotSizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto)
                                     .SlotFillRate(1.0f)
                                     .As<UI::InputBlock>();
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

            inline UI::Slider *AddFloatSliderRow(UI::UIElement *parent, const String &label, f32 min_value, f32 max_value, f32 value, const std::function<void(f32)> &on_value_changed)
            {
                UI::HorizontalBox *value_box = nullptr;
                AddPropertyRow(parent, label, &value_box);

                auto slider = value_box->AddChild<UI::Slider>(min_value, max_value, value)
                                      ->SlotMargin(kPropInnerMargin)
                                      .SlotAlignmentH(UI::EAlignment::kRight)
                                      .SlotSizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto)
                                      .SlotFillRate(3.0f)
                                      .As<UI::Slider>();
                auto input = value_box->AddChild<UI::InputBlock>(std::format("{:.2f}", value))
                                     ->SlotMargin(kPropInnerMargin)
                                     .SlotAlignmentH(UI::EAlignment::kRight)
                                     .SlotSizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto)
                                     .SlotFillRate(1.0f)
                                     .As<UI::InputBlock>();
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
                    blocks[axis] = value_box->AddChild<UI::InputBlock>(texts[axis])
                                           ->SlotMargin(kPropInnerMargin)
                                           .SlotAlignmentH(UI::EAlignment::kRight)
                                           .SlotSizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto)
                                           .SlotFillRate(1.0f)
                                           .As<UI::InputBlock>();
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
                auto btn = value_box->AddChild<UI::Button>()
                                   ->SlotMargin(kPropInnerMargin)
                                   .SlotAlignmentH(UI::EAlignment::kRight)
                                   .As<UI::Button>();
                btn->SetText(button_text);
                return btn;
            }

            inline UI::Dropdown *AddDropdownRow(UI::UIElement *parent, const String &label, const Vector<String> &items)
            {
                UI::HorizontalBox *value_box = nullptr;
                AddPropertyRow(parent, label, &value_box);
                return value_box->AddChild<UI::Dropdown>(items)
                        ->SlotMargin(kPropInnerMargin)
                        .SlotAlignmentH(UI::EAlignment::kRight)
                        .SlotSizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto)
                        .SlotFillRate(1.0f)
                        .As<UI::Dropdown>();
            }

            inline UI::CheckBox* AddCheckBoxRow(UI::UIElement *parent, const String &label, bool initial_state)
            {
                UI::HorizontalBox *value_box = nullptr;
                AddPropertyRow(parent, label, &value_box);
                auto checkbox = value_box->AddChild<UI::CheckBox>()
                                         ->SlotMargin(kPropInnerMargin)
                                         .SlotAlignmentH(UI::EAlignment::kRight)
                                         .SlotSizePolicy(UI::ESizePolicy::kAuto, UI::ESizePolicy::kAuto)
                                         .As<UI::CheckBox>();
                checkbox->SetChecked(initial_state);
                return checkbox;
            }
        }// namespace

        inline void ShowPopupListView(UI::UIElement *anchor, float viewport_height, const std::function<void(const Ref<UI::ListView> &)> &fill_fn)
        {
            auto list_view = MakeRef<UI::ListView>();
            list_view->Name("PopupListView");
            list_view->SlotSizePolicy(ESizePolicy::kFixed, ESizePolicy::kAuto);

            const auto abs_rect = anchor->GetArrangeRect();
            list_view->SlotSize(abs_rect.z, list_view->SlotSize().y);

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
                auto slider = value_box->AddChild<UI::Slider>()
                                      ->SlotMargin(kPropInnerMargin)
                                      .SlotAlignmentH(UI::EAlignment::kRight)
                                      .SlotSizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto)
                                      .SlotFillRate(3.0f)
                                      .As<UI::Slider>();
                f32 current_value = prop.GetValue<f32>();
                current_value = std::max(std::min(current_value, prop._default_value[1]), prop._default_value[0]);
                slider->SetValue((current_value - prop._default_value[0]) / (prop._default_value[1] - prop._default_value[0]));
                slider->_range = {prop._default_value[0], prop._default_value[1]};
                auto input = value_box->AddChild<UI::InputBlock>()
                                     ->SlotMargin(kPropInnerMargin)
                                     .SlotAlignmentH(UI::EAlignment::kRight)
                                     .SlotSizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto)
                                     .SlotFillRate(1.0f)
                                     .As<UI::InputBlock>();
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
                auto input = value_box->AddChild<UI::InputBlock>()
                                     ->SlotMargin(kPropInnerMargin)
                                     .SlotAlignmentH(UI::EAlignment::kRight)
                                     .SlotSizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto)
                                     .SlotFillRate(1.0f)
                                     .As<UI::InputBlock>();
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
                auto btn = value_box->AddChild<UI::Button>()
                                   ->SlotMargin(kPropInnerMargin)
                                   .SlotAlignmentH(UI::EAlignment::kRight)
                                   .SlotSizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto)
                                   .SlotFillRate(1.0f)
                                   .As<UI::Button>();
                btn->SetText(FormatColorButtonText(prop.GetValue<Vector4f>(), true));
                btn->OnMouseClick() += [value_name,obj,btn](UI::UIEvent &e)
                {
                    auto color_picker = MakeRef<UI::ColorPicker>(obj->GetShaderProperty(value_name)->GetValue<Vector4f>());
                    color_picker->Name(std::format("ColorPicker_{}", value_name));
                    color_picker->SlotSizePolicy(ESizePolicy::kFixed, ESizePolicy::kFixed);
                    color_picker->SlotSize(320.0f, 240.0f);
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
                auto btn = value_box->AddChild<UI::Button>()
                                   ->SlotMargin(kPropInnerMargin)
                                   .SlotAlignmentH(UI::EAlignment::kRight)
                                   .SlotSizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto)
                                   .SlotFillRate(1.0f)
                                   .As<UI::Button>();
                btn->OnMouseClick() += [value_name,obj](UI::UIEvent &e)
                {
                    ShowPopupListView(e._current_target, 200.0f, [value_name,obj](const Ref<UI::ListView> &list_view)
                                      {
                        auto none_item = MakeRef<UI::Text>("None");
                        none_item->SlotSizePolicy(ESizePolicy::kFixed, ESizePolicy::kFixed);
                        none_item->SlotSize(64.0f, 64.0f);
                        none_item->OnMouseClick() += [value_name,obj](UI::UIEvent &e)
                        {
                            obj->SetTexture(value_name,nullptr);
                            UIManager::Get()->HidePopup();
                        };
                        list_view->AddItem(none_item);
                        for (auto it = g_pResourceMgr->ResourceBegin<Render::Texture2D>(); it != g_pResourceMgr->ResourceEnd<Render::Texture2D>(); it++)
                        {
                            auto tex = g_pResourceMgr->IterToRefPtr<Render::Texture2D>(it).get();
                            auto item_hb = MakeRef<UI::HorizontalBox>();
                            auto img = item_hb->AddChild<UI::Image>(tex);
                            img->SlotSizePolicy(ESizePolicy::kFixed, ESizePolicy::kFixed);
                            img->SlotSize(64.0f, 64.0f);
                            img->OnMouseClick() += [value_name, tex, obj](UI::UIEvent &e)
                            {
                                obj->SetTexture(value_name,tex);
                                //prop.SetValue<Texture2D*>(tex);
                                UIManager::Get()->HidePopup();
                            };
                            auto item = item_hb->AddChild<UI::Text>(tex->Name());
                            item->SlotPadding({4.0f, 4.0f, 4.0f, 4.0f});
                            list_view->AddItem(item_hb);
                        } });
                };
            }
        }

        ObjectDetail::ObjectDetail() : DockWindow("Object Detail")
        {
            _root = _content_root->AddChild<UI::ScrollView>();
            _root->SlotSizePolicy(UI::ESizePolicy::kFill);
            _vb = _root->AddChild<UI::VerticalBox>();
            _vb->SlotPadding({4.0f, 6.0f, 0.0f, 0.0f});
            _vb->AddChild<UI::Text>("Name");
            _vb->SlotSizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
            auto transf_block = _vb->AddChild<UI::CollapsibleView>("Transform")->SlotSizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto).As<UI::CollapsibleView>()->GetContent()->AddChild<UI::VerticalBox>();
            _pos_block = AddVec3InputRow(transf_block, "Position", "0000");
            _rot_block = AddVec3InputRow(transf_block, "Rotation");
            _scale_block = AddVec3InputRow(transf_block, "Scale");
            _pos_block[0]->_on_content_changed += [this](String content)
            {
                if (auto selected = Selection::FirstEntity(); selected != ECS::kInvalidEntity)
                {
                    auto &r = SceneMgr::Get().ActiveScene()->GetRegister();
                    if (auto comp = r.GetComponent<ECS::TransformComponent>(selected); comp != nullptr)
                    {
                        if (auto opt = StringUtils::ParseFloat(content); opt.has_value())
                            comp->_transform._position.x = opt.value();
                    }
                }
            };
            _pos_block[1]->_on_content_changed += [this](String content)
            {
                if (auto selected = Selection::FirstEntity(); selected != ECS::kInvalidEntity)
                {
                    auto &r = SceneMgr::Get().ActiveScene()->GetRegister();
                    if (auto comp = r.GetComponent<ECS::TransformComponent>(selected); comp != nullptr)
                    {
                        if (auto opt = StringUtils::ParseFloat(content); opt.has_value())
                            comp->_transform._position.y = opt.value();
                    }
                }
            };
            _pos_block[2]->_on_content_changed += [this](String content)
            {
                if (auto selected = Selection::FirstEntity(); selected != ECS::kInvalidEntity)
                {
                    auto &r = SceneMgr::Get().ActiveScene()->GetRegister();
                    if (auto comp = r.GetComponent<ECS::TransformComponent>(selected); comp != nullptr)
                    {
                        if (auto opt = StringUtils::ParseFloat(content); opt.has_value())
                            comp->_transform._position.z = opt.value();
                    }
                }
            };

            _rot_block[0]->_on_content_changed += [this](String content)
            {
                if (auto selected = Selection::FirstEntity(); selected != ECS::kInvalidEntity)
                {
                    auto &r = SceneMgr::Get().ActiveScene()->GetRegister();
                    if (auto comp = r.GetComponent<ECS::TransformComponent>(selected); comp != nullptr)
                    {
                        if (auto opt = StringUtils::ParseFloat(content); opt.has_value())
                        {
                            Vector3f euler = Quaternion::EulerAngles(comp->_transform._rotation);
                            euler.x = opt.value();
                            comp->_transform._rotation = Quaternion::EulerAngles(euler);
                        }
                    }
                }
            };
            _rot_block[1]->_on_content_changed += [this](String content)
            {
                if (auto selected = Selection::FirstEntity(); selected != ECS::kInvalidEntity)
                {
                    auto &r = SceneMgr::Get().ActiveScene()->GetRegister();
                    if (auto comp = r.GetComponent<ECS::TransformComponent>(selected); comp != nullptr)
                    {
                        if (auto opt = StringUtils::ParseFloat(content); opt.has_value())
                        {
                            Vector3f euler = Quaternion::EulerAngles(comp->_transform._rotation);
                            euler.y = opt.value();
                            comp->_transform._rotation = Quaternion::EulerAngles(euler);
                        }
                    }
                }
            };
            _rot_block[2]->_on_content_changed += [this](String content)
            {
                if (auto selected = Selection::FirstEntity(); selected != ECS::kInvalidEntity)
                {
                    auto &r = SceneMgr::Get().ActiveScene()->GetRegister();
                    if (auto comp = r.GetComponent<ECS::TransformComponent>(selected); comp != nullptr)
                    {
                        if (auto opt = StringUtils::ParseFloat(content); opt.has_value())
                        {
                            Vector3f euler = Quaternion::EulerAngles(comp->_transform._rotation);
                            euler.z = opt.value();
                            comp->_transform._rotation = Quaternion::EulerAngles(euler);
                        }
                    }
                }
            };

            _scale_block[0]->_on_content_changed += [this](String content)
            {
                if (auto selected = Selection::FirstEntity(); selected != ECS::kInvalidEntity)
                {
                    auto &r = SceneMgr::Get().ActiveScene()->GetRegister();
                    if (auto comp = r.GetComponent<ECS::TransformComponent>(selected); comp != nullptr)
                    {
                        if (auto opt = StringUtils::ParseFloat(content); opt.has_value())
                            comp->_transform._scale.x = opt.value();
                    }
                }
            };
            _scale_block[1]->_on_content_changed += [this](String content)
            {
                if (auto selected = Selection::FirstEntity(); selected != ECS::kInvalidEntity)
                {
                    auto &r = SceneMgr::Get().ActiveScene()->GetRegister();
                    if (auto comp = r.GetComponent<ECS::TransformComponent>(selected); comp != nullptr)
                    {
                        if (auto opt = StringUtils::ParseFloat(content); opt.has_value())
                            comp->_transform._scale.y = opt.value();
                    }
                }
            };
            _scale_block[2]->_on_content_changed += [this](String content)
            {
                if (auto selected = Selection::FirstEntity(); selected != ECS::kInvalidEntity)
                {
                    auto &r = SceneMgr::Get().ActiveScene()->GetRegister();
                    if (auto comp = r.GetComponent<ECS::TransformComponent>(selected); comp != nullptr)
                    {
                        if (auto opt = StringUtils::ParseFloat(content); opt.has_value())
                            comp->_transform._scale.z = opt.value();
                    }
                }
            };

            //auto color_picker = _vb->AddChild<UI::ColorPicker>(Colors::kBlue);
            //color_picker->OnValueChanged() += [](Vector4f color)
            //{
            //    LOG_WARNING("Color Changed: R:{} G:{} B:{} A:{}", color.r, color.g, color.b, color.a);
            //};
        }
        ObjectDetail::~ObjectDetail()
        {
            JsonArchive ar;
            ar << *_content_widget;
            ar.Save("ObjectDetailLayout.json");
        }
        void ObjectDetail::Update(f32 dt)
        {
            DockWindow::Update(dt);
            if (auto selected = Selection::FirstEntity(); selected != ECS::kInvalidEntity)
            {
                static ECS::Entity s_prev_selected = ECS::kInvalidEntity;
                auto &r = SceneMgr::Get().ActiveScene()->GetRegister();
                if (auto comp = r.GetComponent<ECS::TagComponent>(selected); comp != nullptr)
                {
                    _vb->ChildAt(0)->As<UI::Text>()->SetText(comp->_name);
                }
                if (auto comp = r.GetComponent<ECS::TransformComponent>(selected); comp != nullptr)
                {
                    auto set_block = [&](UI::InputBlock *block, f32 value)
                    {
                        if (!block->IsEditing())
                            block->SetContent(std::format("{:.2f}", value), false);
                    };
                    set_block(_pos_block[0], comp->_transform._position.x);
                    set_block(_pos_block[1], comp->_transform._position.y);
                    set_block(_pos_block[2], comp->_transform._position.z);
                    Vector3f euler = Quaternion::EulerAngles(comp->_transform._rotation);
                    set_block(_rot_block[0], euler.x);
                    set_block(_rot_block[1], euler.y);
                    set_block(_rot_block[2], euler.z);
                    set_block(_scale_block[0], comp->_transform._scale.x);
                    set_block(_scale_block[1], comp->_transform._scale.y);
                    set_block(_scale_block[2], comp->_transform._scale.z);
                }
                if (auto comp = r.GetComponent<ECS::LightComponent>(selected); comp != nullptr)
                {
                    if (s_prev_selected != selected)
                    {
                        _vb->RemoveChild(_prev_comp_block);
                        _light_block = _vb->AddChild<UI::CollapsibleView>("LightComp")->SlotSizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto).As<UI::CollapsibleView>();
                        auto content = _light_block->GetContent()->AddChild<UI::VerticalBox>();
                        auto items = Vector<String>{"Directional", "Point", "Spot", "Area"};
                        auto light_type_dropdown = AddDropdownRow(content, "Type", items);
                        light_type_dropdown->SetSelectedIndex(static_cast<i32>(comp->_type));
                        light_type_dropdown->_on_selected_changed += [comp](i32 idx){
                            comp->_type = static_cast<ECS::ELightType::ELightType>(idx);
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
                                color_picker->SlotSizePolicy(ESizePolicy::kFixed, ESizePolicy::kFixed);
                                color_picker->SlotSize(320.0f, 240.0f);
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
                    _prev_comp_block = _light_block;
                }
                if (auto comp = r.GetComponent<ECS::StaticMeshComponent>(selected); comp != nullptr)
                {
                    if (s_prev_selected != selected)
                    {
                        _vb->RemoveChild(_prev_comp_block);
                        _static_mesh_block = _vb->AddChild<UI::CollapsibleView>("StaticMesh")->SlotSizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto).As<UI::CollapsibleView>();
                        auto content = _static_mesh_block->GetContent()->AddChild<UI::VerticalBox>();
                        //mesh
                        {
                            auto btn = AddButtonRow(content, "Mesh", comp->_p_mesh != nullptr ? comp->_p_mesh->Name() : "None");
                            btn->OnMouseClick() += [comp, btn](UI::UIEvent &e)
                            {
                                ShowPopupListView(e._current_target, 200.0f, [comp, btn](const Ref<UI::ListView> &list_view)
                                                  {
                                    for (auto it = g_pResourceMgr->ResourceBegin<Render::Mesh>(); it != g_pResourceMgr->ResourceEnd<Render::Mesh>(); it++)
                                    {
                                        const auto &mesh = g_pResourceMgr->IterToRefPtr<Render::Mesh>(it);
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
                                        for (auto it = g_pResourceMgr->ResourceBegin<Render::Material>(); it != g_pResourceMgr->ResourceEnd<Render::Material>(); it++)
                                        {
                                            const auto &mat = g_pResourceMgr->IterToRefPtr<Render::Material>(it);
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
                    _prev_comp_block = _static_mesh_block;
                }
                if (auto comp = r.GetComponent<ECS::CLightProbe>(selected); comp != nullptr)
                {
                    if (s_prev_selected != selected)
                    {
                        _vb->RemoveChild(_prev_comp_block);
                        _light_probe_block = _vb->AddChild<UI::CollapsibleView>("LightProbe")->SlotSizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto).As<UI::CollapsibleView>();
                        auto content = _light_probe_block->GetContent()->AddChild<UI::VerticalBox>();
                        {
                            auto btn = content->AddChild<UI::Button>();
                            btn->SetText("Update Probe");
                            btn->OnMouseClick() += [comp](UI::UIEvent &e)
                            {
                                comp->_is_dirty = true;
                            };
                        }
                    }
                    _prev_comp_block = _light_probe_block;
                }
                if (auto comp = r.GetComponent<ECS::CCamera>(selected); comp != nullptr)
                {
                    if (s_prev_selected != selected)
                    {
                        _vb->RemoveChild(_prev_comp_block);
                        _cam_block = _vb->AddChild<UI::CollapsibleView>("Camera")->SlotSizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto).As<UI::CollapsibleView>();
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
                    _prev_comp_block = _cam_block;
                }
                s_prev_selected = selected;
            }
            else
            {
                _vb->ChildAt(0)->As<UI::Text>()->SetText("Name: (No Selection)");
            }
        }
    }// namespace Editor
}// namespace Ailu