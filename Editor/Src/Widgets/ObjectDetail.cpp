#include "Widgets/ObjectDetail.h"
#include "UI/Container.h"
#include "UI/Basic.h"
#include "Common/Selection.h"
#include "Scene/Scene.h"

#include "Objects/JsonArchive.h"

namespace Ailu
{
	namespace Editor
	{
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
                        _vb->RemoveChild(_light_block);
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