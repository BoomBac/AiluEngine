#include "Widgets/ObjectDetail.h"
#include "Common/Selection.h"
#include "Common/Undo.h"
#include "Framework/Common/ResourceMgr.h"
#include "Scene/Scene.h"
#include "UI/Basic.h"
#include "UI/Container.h"
#include "UI/UIFramework.h"
#include "Objects/JsonArchive.h"
#include "Inspector/ComponentEditorRegistry.h"
#include "Inspector/ReflectedPropertyPanel.h"
#include "Inspector/ComponentEditorHelpers.h"
#include <cctype>

namespace Ailu
{
    using namespace UI;
    using SceneManagement::SceneMgr;
    namespace Editor
    {
        namespace
        {
            inline const UI::Padding kComponentBlockMargin = {2.0f, 4.0f, 2.0f, 4.0f};
            inline const UI::Padding kComponentBlockPadding = {4.0f, 4.0f, 4.0f, 4.0f};
            inline const Color kComponentBlockBg = Color(0.105f, 0.115f, 0.145f, 1.0f);
            inline const Color kComponentBlockBorder = Color(0.22f, 0.245f, 0.30f, 1.0f);

            inline String ToLower(String value)
            {
                for (char &ch : value)
                    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                return value;
            }
        }// namespace

        UI::CollapsibleView *ObjectDetail::CreateComponentBlock(const ComponentEditorInfo &info, ECS::Entity entity, bool allow_remove)
        {
            auto frame = _vb->AddChild<UI::Border>();
            frame->_bg_color = kComponentBlockBg;
            frame->_border_color = kComponentBlockBorder;
            frame->Thickness(1.0f);
            frame->SlotPadding() = kComponentBlockPadding;
            auto &style_override = frame->GetStyleOverride();
            style_override._corner_radius = Vector4f{6.0f};
            style_override._override_mask |= static_cast<u32>(UI::EUIControlVisualOverride::kCornerRadius);
            frame->GetSlotAs<UI::LinearSlot>().Margin(kComponentBlockMargin)
                    .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);

            auto block = frame->AddChild<UI::CollapsibleView>(info._display_name);
            block->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);

            {
                auto &header_style = static_cast<UI::LinearBox *>(block->GetHeader())->GetStyleOverride();
                UI::UIBrush header_bg;
                header_bg._type = UI::EUIBrushType::kColor;
                header_bg._tint = Color(0.055f, 0.06f, 0.075f, 1.0f);
                header_style.SetBackground(header_bg);
            }

            if (auto *header_box = static_cast<UI::LinearBox *>(block->GetHeader()))
            {
                if (auto *title_widget = header_box->ChildAt(0))
                    title_widget->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
            }

            if (allow_remove && info._allow_remove)
            {
                auto *header_box = static_cast<UI::LinearBox *>(block->GetHeader());
                auto *remove_btn = header_box->AddChild<UI::Button>();
                remove_btn->SetText("X");
                remove_btn->GetSlotAs<UI::LinearSlot>()
                        .SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed)
                        .Size({UI::CollapsibleView::s_header_height, UI::CollapsibleView::s_header_height});

                remove_btn->OnMouseClick() += [this, &info, frame, entity](UI::UIEvent &e)
                {
                    if (entity != ECS::kInvalidEntity)
                    {
                        auto *scene = SceneMgr::Get().ActiveScene();
                        if (scene != nullptr && info._remove_component)
                        {
                            info._remove_component(scene->GetRegister(), entity);
                            SceneMgr::Get().MarkCurSceneDirty();
                        }
                        _needs_rebuild = true;
                    }
                    else
                    {
                        _vb->RemoveChild(frame);
                    }
                    e._is_handled = true;
                };
            }

            return block;
        }

        ComponentEditorContext ObjectDetail::BuildContext(const ComponentEditorInfo &info, UI::CollapsibleView *block)
        {
            ComponentEditorContext context;
            context._scene = SceneMgr::Get().ActiveScene();
            context._entity = _selected_entity;
            context._component_info = &info;
            context._content = static_cast<UI::VerticalBox *>(block->GetContent()->AddChild<UI::VerticalBox>());
            context._request_rebuild = [this]()
            {
                _needs_rebuild = true;
            };
            return context;
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
            auto *scene = SceneMgr::Get().ActiveScene();
            if (_selected_entity == ECS::kInvalidEntity || scene == nullptr || anchor == nullptr)
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

            const auto &all_components = ComponentEditorRegistry::Get().Components();
            auto populate = std::make_shared<std::function<void(const String &)>>();
            *populate = [this, list_view, scene, all_components](const String &query)
            {
                list_view->ClearItems();
                const String lowered_query = ToLower(query);
                auto &scene_register = scene->GetRegister();
                for (const auto &comp_info : all_components)
                {
                    if (!comp_info._allow_add)
                        continue;
                    if (comp_info._has_component && comp_info._has_component(scene_register, Selection::FirstEntity()))
                        continue;
                    if (ToLower(comp_info._display_name).find(lowered_query) == String::npos)
                        continue;

                    auto item_text = MakeRef<UI::Text>(comp_info._display_name);
                    item_text->OnMouseClick() += [this, &comp_info, scene](UI::UIEvent &e)
                    {
                        auto entity = Selection::FirstEntity();
                        auto &scene_register = scene->GetRegister();
                        if (comp_info._has_component && !comp_info._has_component(scene_register, entity))
                        {
                            comp_info._add_component(scene_register, entity);
                            SceneMgr::Get().MarkCurSceneDirty();
                            _needs_rebuild = true;
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

        void ObjectDetail::ClearComponentEntries()
        {
            for (auto &entry : _component_entries)
            {
                if (entry._block != nullptr)
                {
                    UI::UIElement *frame = entry._block->GetParent();
                    _vb->RemoveChild(frame != nullptr ? frame : entry._block);
                }
            }
            _component_entries.clear();
        }

        void ObjectDetail::BuildComponentEntry(const ComponentEditorInfo &info, ECS::Entity entity)
        {
            ObjectDetailComponentEntry entry;
            entry._component_info = &info;

            auto *scene = SceneMgr::Get().ActiveScene();
            if (scene == nullptr)
                return;

            entry._block = CreateComponentBlock(info, entity, info._allow_remove);

            if (info._create_editor)
            {
                entry._custom_editor = info._create_editor();
                auto context = BuildContext(info, entry._block);
                entry._custom_editor->Build(context);
            }
            else if (info._reflection_type != nullptr)
            {
                auto context = BuildContext(info, entry._block);
                void *instance = context.ComponentInstance();
                if (instance != nullptr)
                {
                    entry._reflected_panel = MakeScope<ReflectedPropertyPanel>();
                    ReflectedPropertyPanel::BuildArgs args;
                    args._type = info._reflection_type;
                    args._instance = instance;
                    args._parent = context._content;
                    args._on_property_changed = [context](const PropertyInfo &)
                    {
                        context.MarkSceneDirty();
                    };
                    entry._reflected_panel->Build(args);
                }
            }

            _component_entries.emplace_back(std::move(entry));
        }

        void ObjectDetail::Rebuild(ECS::Entity entity)
        {
            ClearComponentEntries();

            auto *scene = SceneMgr::Get().ActiveScene();
            if (scene == nullptr)
                return;

            auto &registry = scene->GetRegister();

            for (const ComponentEditorInfo &info : ComponentEditorRegistry::Get().Components())
            {
                if (!info._has_component || !info._has_component(registry, entity))
                    continue;

                BuildComponentEntry(info, entity);
            }
        }

        void ObjectDetail::Update(f32 dt)
        {
            DockWindow::Update(dt);

            const ECS::Entity selected = Selection::FirstEntity();
            if (selected == ECS::kInvalidEntity)
            {
                if (_selected_entity != ECS::kInvalidEntity)
                {
                    _selected_entity = ECS::kInvalidEntity;
                    _name_text->SetText("Name: (No Selection)");
                    ClearComponentEntries();
                }
                return;
            }

            if (_selected_entity != selected || _needs_rebuild)
            {
                _selected_entity = selected;
                Rebuild(selected);
                _needs_rebuild = false;
            }

            auto *scene = SceneMgr::Get().ActiveScene();
            if (scene == nullptr)
                return;

            if (auto comp = scene->GetRegister().GetComponent<ECS::TagComponent>(selected); comp != nullptr)
            {
                _name_text->SetText(comp->_name);
            }

            for (ObjectDetailComponentEntry &entry : _component_entries)
            {
                if (entry._custom_editor == nullptr)
                    continue;

                ComponentEditorContext context;
                context._scene = scene;
                context._entity = selected;
                context._component_info = entry._component_info;
                context._content = entry._block != nullptr
                    ? static_cast<UI::VerticalBox *>(entry._block->GetContent()->ChildAt(0))
                    : nullptr;
                context._request_rebuild = [this]() { _needs_rebuild = true; };

                if (entry._custom_editor->NeedsRebuild(context))
                {
                    _needs_rebuild = true;
                    break;
                }

                entry._custom_editor->Refresh(context);
            }
        }
    }// namespace Editor
}// namespace Ailu
