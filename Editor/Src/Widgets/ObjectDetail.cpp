#include "Widgets/ObjectDetail.h"
#include "UI/Container.h"
#include "UI/Basic.h"
#include "UI/UIFramework.h"
#include "UI/ColorPicker.h"
#include "Common/Selection.h"
#include "Scene/Scene.h"
#include "Framework/Common/ResourceMgr.h"

#include "Objects/JsonArchive.h"

namespace Ailu
{
    using namespace UI;
	namespace Editor
	{
        static void CreateMaterialPropWidget(UI::UIElement *root, Render::ShaderPropertyInfo &prop, Material *obj)
        {
            auto hb = root->AddChild<UI::HorizontalBox>();
            hb->AddChild<UI::Text>(prop._prop_name)->SlotMargin({2.0f, 0.0f, 2.0f, 2.0f}).SlotSizePolicy(UI::ESizePolicy::kAuto, UI::ESizePolicy::kAuto);

            if (prop._type == Render::EShaderPropertyType::kRange)
            {
                auto slider = hb->AddChild<UI::Slider>();
                slider->SlotMargin({10.0f, 0.0f, 2.0f, 2.0f}).SlotAlignmentH(UI::EAlignment::kRight);
                slider->_range = {prop._default_value[0],prop._default_value[1]};
                slider->_on_value_change += [&prop, obj](f32 v)
                {
                    prop.SetValue<f32>(v);
                };
            }
            else if (prop._type == Render::EShaderPropertyType::kFloat)
            {
                hb->AddChild<UI::InputBlock>()->SlotMargin({10.0f, 0.0f, 2.0f, 2.0f}).SlotAlignmentH(UI::EAlignment::kRight).As<UI::InputBlock>()->_on_content_changed += [&](String content)
                {
                    if (auto opt = StringUtils::ParseFloat(content); opt.has_value())
                        prop.SetValue<f32>(opt.value());
                };
            }
            else if (prop._type == Render::EShaderPropertyType::kColor)
            {
                auto btn = hb->AddChild<UI::Button>();
                btn->SlotMargin({10.0f, 0.0f, 2.0f, 2.0f}).SlotAlignmentH(UI::EAlignment::kRight).As<UI::Button>()->OnMouseClick() += [&prop](UI::UIEvent &e)
                {
                    auto color_picker = MakeRef<UI::ColorPicker>(prop.GetValue<Vector4f>());
                    color_picker->Name(std::format("ColorPicker_{}",prop._prop_name));
                    color_picker->SlotSizePolicy(ESizePolicy::kFixed, ESizePolicy::kFixed);
                    color_picker->SlotSize(260.0f, 220.0f);
                    color_picker->OnValueChanged() += [&prop](Vector4f color)
                    {
                        prop.SetValue<Vector4f>(color);
                    };
                    auto abs_rect = e._current_target->GetArrangeRect();
                    Vector2f show_pos = abs_rect.xy;
                    show_pos.y += abs_rect.w;
                    UI::UIManager::Get()->ShowPopupAt(show_pos.x, show_pos.y, color_picker);
                };
            }
        }

		ObjectDetail::ObjectDetail():DockWindow("Object Detail")
		{
            _root = _content_root->AddChild<UI::ScrollView>();
            _root->SlotSizePolicy(UI::ESizePolicy::kFill);
            _vb = _root->AddChild<UI::VerticalBox>();
            _vb->SlotPadding({4.0f,6.0f,0.0f,0.0f});
            _vb->AddChild<UI::Text>("Name");
            _vb->SlotSizePolicy(UI::ESizePolicy::kFill,UI::ESizePolicy::kAuto);
            auto transf_block = _vb->AddChild<UI::CollapsibleView>("Transform")->SlotSizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto)
                .As<UI::CollapsibleView>()->GetContent()->AddChild<UI::VerticalBox>();
            auto pos_hb = transf_block->AddChild<UI::HorizontalBox>();
            pos_hb->AddChild<UI::Text>("Position");
            _pos_block[0] = pos_hb->AddChild<UI::InputBlock>("0000")->SlotMargin({2.0f,0.0f,2.0f,2.0f}).SlotAlignmentH(UI::EAlignment::kRight).As<UI::InputBlock>();
            _pos_block[1] = pos_hb->AddChild<UI::InputBlock>()->SlotMargin({2.0f, 0.0f, 2.0f, 2.0f}).SlotAlignmentH(UI::EAlignment::kRight).As<UI::InputBlock>();
            _pos_block[2] = pos_hb->AddChild<UI::InputBlock>()->SlotMargin({2.0f, 0.0f, 2.0f, 2.0f}).SlotAlignmentH(UI::EAlignment::kRight).As<UI::InputBlock>();
            auto rot_hb = transf_block->AddChild<UI::HorizontalBox>();
            rot_hb->AddChild<UI::Text>("Rotation");
            _rot_block[0] = rot_hb->AddChild<UI::InputBlock>()->SlotMargin({2.0f,0.0f,2.0f,2.0f}).SlotAlignmentH(UI::EAlignment::kRight).As<UI::InputBlock>();
            _rot_block[1] = rot_hb->AddChild<UI::InputBlock>()->SlotMargin({2.0f,0.0f,2.0f,2.0f}).SlotAlignmentH(UI::EAlignment::kRight).As<UI::InputBlock>();
            _rot_block[2] = rot_hb->AddChild<UI::InputBlock>()->SlotMargin({2.0f,0.0f,2.0f,2.0f}).SlotAlignmentH(UI::EAlignment::kRight).As<UI::InputBlock>();
            auto scale_hb = transf_block->AddChild<UI::HorizontalBox>();
            scale_hb->AddChild<UI::Text>("Scale");
            _scale_block[0] = scale_hb->AddChild<UI::InputBlock>()->SlotMargin({2.0f,0.0f,2.0f,2.0f}).SlotAlignmentH(UI::EAlignment::kRight).As<UI::InputBlock>();
            _scale_block[1] = scale_hb->AddChild<UI::InputBlock>()->SlotMargin({2.0f,0.0f,2.0f,2.0f}).SlotAlignmentH(UI::EAlignment::kRight).As<UI::InputBlock>();
            _scale_block[2] = scale_hb->AddChild<UI::InputBlock>()->SlotMargin({2.0f,0.0f,2.0f,2.0f}).SlotAlignmentH(UI::EAlignment::kRight).As<UI::InputBlock>();
            _pos_block[0]->_on_content_changed += [this](String content)
            {
                if (auto selected = Selection::FirstEntity(); selected != ECS::kInvalidEntity)
                {
                    auto &r = g_pSceneMgr->ActiveScene()->GetRegister();
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
                    auto &r = g_pSceneMgr->ActiveScene()->GetRegister();
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
                    auto &r = g_pSceneMgr->ActiveScene()->GetRegister();
                    if (auto comp = r.GetComponent<ECS::TransformComponent>(selected); comp != nullptr)
                    {
                        if (auto opt = StringUtils::ParseFloat(content); opt.has_value())
                            comp->_transform._position.z = opt.value();
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
                auto &r = g_pSceneMgr->ActiveScene()->GetRegister();
                if (auto comp = r.GetComponent<ECS::TagComponent>(selected); comp != nullptr)
				{
                    _vb->ChildAt(0)->As<UI::Text>()->SetText(comp->_name);
				}
                if (auto comp = r.GetComponent<ECS::TransformComponent>(selected); comp != nullptr)
                {
                    auto set_block = [&](UI::InputBlock *block, f32 value)
                    {
                        if (!block->IsEditing())
                            block->SetContent(std::to_string(value));
                    };
                    set_block(_pos_block[0], comp->_transform._position.x);
                    set_block(_pos_block[1], comp->_transform._position.y);
                    set_block(_pos_block[2], comp->_transform._position.z);
                    set_block(_rot_block[0], comp->_transform._rotation.x);
                    set_block(_rot_block[1], comp->_transform._rotation.y);
                    set_block(_rot_block[2], comp->_transform._rotation.z);
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
                        content->AddChild<UI::Dropdown>(items);
                        {
                            auto hb = content->AddChild<UI::HorizontalBox>();
                            hb->AddChild<UI::Text>("Intensity")->SlotMargin({2.0f, 0.0f, 2.0f, 2.0f}).SlotSizePolicy(UI::ESizePolicy::kAuto,UI::ESizePolicy::kAuto);
                            auto ints = hb->AddChild<UI::Slider>(0.0f, 100.0f, comp->_light._light_color.a)->SlotSizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto).SlotMargin({10.0f, 0.0f, 0.0f, 0.0f})
                                .As<UI::Slider>();
                            ints->_on_value_change += [=](f32 value)
                            {
                                comp->_light._light_color.a = value;
                            };
                        }
                        {
                            auto hb = content->AddChild<UI::HorizontalBox>();
                            hb->AddChild<UI::Text>("Color")->SlotMargin({2.0f, 0.0f, 2.0f, 2.0f}).SlotSizePolicy(UI::ESizePolicy::kAuto, UI::ESizePolicy::kAuto);
                            hb->AddChild<UI::InputBlock>(std::format("{}", comp->_light._light_color.r))->SlotMargin({10.0f, 0.0f, 2.0f, 2.0f}).SlotAlignmentH(UI::EAlignment::kRight).As<UI::InputBlock>()->_on_content_changed += [=](String content)
                            {
                                if (auto opt = StringUtils::ParseFloat(content); opt.has_value())
                                    comp->_light._light_color.r = opt.value();
                                Clamp(comp->_light._light_color.r, 0.0f, 1.0f);
                            };
                            hb->AddChild<UI::InputBlock>(std::format("{}", comp->_light._light_color.g))->SlotMargin({2.0f, 0.0f, 2.0f, 2.0f}).SlotAlignmentH(UI::EAlignment::kRight).As<UI::InputBlock>()->_on_content_changed += [=](String content)
                            {
                                if (auto opt = StringUtils::ParseFloat(content); opt.has_value())
                                    comp->_light._light_color.g = opt.value();
                                Clamp(comp->_light._light_color.g, 0.0f, 1.0f);
                            };
                            hb->AddChild<UI::InputBlock>(std::format("{}", comp->_light._light_color.b))->SlotMargin({2.0f, 0.0f, 2.0f, 2.0f}).SlotAlignmentH(UI::EAlignment::kRight).As<UI::InputBlock>()->_on_content_changed += [=](String content)
                            {
                                if (auto opt = StringUtils::ParseFloat(content); opt.has_value())
                                    comp->_light._light_color.b = opt.value();
                                Clamp(comp->_light._light_color.b, 0.0f, 1.0f);
                            };
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
                            auto hb = content->AddChild<UI::HorizontalBox>();
                            hb->AddChild<UI::Text>("Mesh: ")->SlotMargin({2.0f, 0.0f, 2.0f, 2.0f}).SlotSizePolicy(UI::ESizePolicy::kAuto, UI::ESizePolicy::kAuto);
                            auto btn = hb->AddChild<UI::Button>();
                            btn->SlotMargin({10.0f, 0.0f, 2.0f, 2.0f}).SlotAlignmentH(UI::EAlignment::kRight);
                            btn->SetText(std::format("Mesh: {}", comp->_p_mesh->Name()));
                            btn->OnMouseClick() += [comp, btn](UI::UIEvent &e)
                            {
                                auto list_view = MakeRef<UI::ListView>();
                                list_view->Name("MeshListView");
                                list_view->SlotSizePolicy(ESizePolicy::kFixed, ESizePolicy::kAuto);
                                auto abs_rect = e._current_target->GetArrangeRect();
                                list_view->SlotSize(abs_rect.z, list_view->SlotSize().y);

                                for (auto it = g_pResourceMgr->ResourceBegin<Render::Mesh>(); it != g_pResourceMgr->ResourceEnd<Render::Mesh>(); it++)
                                {
                                    const auto &mesh = g_pResourceMgr->IterToRefPtr<Render::Mesh>(it);
                                    auto text = MakeRef<Text>(mesh->Name());
                                    text->OnMouseClick() += [mesh, comp, btn](UIEvent &e)
                                    {
                                        LOG_INFO("StaticMesh item clicked: {}", mesh->Name());
                                        comp->_p_mesh = mesh;
                                        btn->SetText(comp->_p_mesh->Name());
                                        UIManager::Get()->HidePopup();
                                    };
                                    list_view->AddItem(text);
                                }
                                list_view->SetViewportHeight(200.0f);
                                Vector2f show_pos = abs_rect.xy;
                                show_pos.y += abs_rect.w;
                                UI::UIManager::Get()->ShowPopupAt(show_pos.x, show_pos.y, list_view);
                            };
                        }
                        //material
                        {
                            auto hb = content->AddChild<UI::HorizontalBox>();
                            hb->AddChild<UI::Text>("Material: ")->SlotMargin({2.0f, 0.0f, 2.0f, 2.0f}).SlotSizePolicy(UI::ESizePolicy::kAuto, UI::ESizePolicy::kAuto);
                            auto btn = hb->AddChild<UI::Button>();
                            btn->SlotMargin({10.0f, 0.0f, 2.0f, 2.0f}).SlotAlignmentH(UI::EAlignment::kRight);
                            btn->SetText(std::format("Material: {}", comp->_p_mats[0]->Name()));
                            btn->OnMouseClick() += [comp, btn](UI::UIEvent &e)
                            {
                                auto list_view = MakeRef<UI::ListView>();
                                list_view->Name("MaterialListView");
                                list_view->SlotSizePolicy(ESizePolicy::kFixed, ESizePolicy::kAuto);
                                auto abs_rect = e._current_target->GetArrangeRect();
                                list_view->SlotSize(abs_rect.z, list_view->SlotSize().y);

                                for (auto it = g_pResourceMgr->ResourceBegin<Render::Material>(); it != g_pResourceMgr->ResourceEnd<Render::Material>(); it++)
                                {
                                    const auto &mat = g_pResourceMgr->IterToRefPtr<Render::Material>(it);
                                    auto text = MakeRef<Text>(mat->Name());
                                    text->OnMouseClick() += [mat, comp, btn](UIEvent &e)
                                    {
                                        LOG_INFO("StaticMesh item clicked: {}", mat->Name());
                                        comp->_p_mats[0] = mat;
                                        btn->SetText(comp->_p_mats[0]->Name());
                                        UIManager::Get()->HidePopup();
                                    };
                                    list_view->AddItem(text);
                                }
                                list_view->SetViewportHeight(200.0f);
                                Vector2f show_pos = abs_rect.xy;
                                show_pos.y += abs_rect.w;
                                UI::UIManager::Get()->ShowPopupAt(show_pos.x, show_pos.y, list_view);
                            };
                            if (auto mat = comp->_p_mats[0]; mat != nullptr)
                            {
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
                        _light_probe_block= _vb->AddChild<UI::CollapsibleView>("LightProbe")->SlotSizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto).As<UI::CollapsibleView>();
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
                s_prev_selected = selected;
            }
			else
			{
                _vb->ChildAt(0)->As<UI::Text>()->SetText("Name: (No Selection)");
            }
		}
    }// namespace Editor
}