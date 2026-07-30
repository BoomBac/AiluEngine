#include "Graph/GraphActionMenu.h"

#include "Framework/Common/KeyCode.h"
#include "UI/Basic.h"
#include "UI/Container.h"
#include "UI/UIFramework.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <set>

namespace Ailu
{
    namespace Editor
    {
        namespace
        {
            constexpr f32 kMenuWidth = 300.0f;
            constexpr f32 kMenuHeight = 360.0f;
            constexpr f32 kSearchHeight = 26.0f;
            constexpr f32 kRowHeight = 24.0f;
            constexpr f32 kHeaderHeight = 20.0f;
            constexpr u32 kMaxRecentActions = 6u;
            Vector<String> s_recent_node_types;

            String ToLower(String value)
            {
                std::transform(value.begin(), value.end(), value.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return value;
            }

            bool MatchesSearch(const GraphNodeAction &action, const String &search_text)
            {
                if (search_text.empty())
                    return true;

                const String needle = ToLower(search_text);
                return ToLower(action._display_name).find(needle) != String::npos ||
                       ToLower(action._node_type).find(needle) != String::npos ||
                       ToLower(action._category).find(needle) != String::npos;
            }

            i32 RecentIndex(const String &node_type)
            {
                for (u32 index = 0u; index < s_recent_node_types.size(); ++index)
                {
                    if (s_recent_node_types[index] == node_type)
                        return static_cast<i32>(index);
                }
                return -1;
            }

            void RememberRecentAction(const String &node_type)
            {
                s_recent_node_types.erase(std::remove(s_recent_node_types.begin(), s_recent_node_types.end(), node_type),
                                          s_recent_node_types.end());
                s_recent_node_types.insert(s_recent_node_types.begin(), node_type);
                if (s_recent_node_types.size() > kMaxRecentActions)
                    s_recent_node_types.resize(kMaxRecentActions);
            }

            void AddSectionHeader(UI::VerticalBox *list_root, const String &title)
            {
                auto *header = list_root->AddChild<UI::Text>(title);
                header->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                      .Size({0.0f, kHeaderHeight}).Margin({6.0f, 4.0f, 6.0f, 0.0f});
                header->FontSize(12.0f);
                header->_color = Color(0.58f, 0.62f, 0.68f, 1.0f);
                header->_horizontal_align = UI::EAlignment::kLeft;
                header->_vertical_align = UI::EAlignment::kCenter;
            }

            void AddActionButton(UI::VerticalBox *list_root, const GraphNodeAction &action, bool selected,
                                 const GraphActionMenu::ActionCallback &on_action)
            {
                String label = action._display_name.empty() ? action._node_type : action._display_name;
                if (selected)
                    label = "> " + label;
                auto *button = list_root->AddChild<UI::Button>(label);
                button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                      .Size({0.0f, kRowHeight}).Margin({6.0f, 1.0f, 6.0f, 1.0f});
                button->OnMouseClick() += [action, on_action](UI::UIEvent &e)
                {
                    RememberRecentAction(action._node_type);
                    UI::UIManager::Get()->HidePopup();
                    if (on_action)
                        on_action(action);
                    e._is_handled = true;
                };
            }

            Vector<GraphNodeAction> FilterAndSortActions(const Vector<GraphNodeAction> &actions,
                                                         const String &search_text)
            {
                Vector<GraphNodeAction> filtered;
                for (const GraphNodeAction &action : actions)
                {
                    if (MatchesSearch(action, search_text))
                        filtered.emplace_back(action);
                }
                std::sort(filtered.begin(), filtered.end(), [](const GraphNodeAction &lhs, const GraphNodeAction &rhs)
                {
                    const i32 lhs_recent = RecentIndex(lhs._node_type);
                    const i32 rhs_recent = RecentIndex(rhs._node_type);
                    if (lhs_recent != rhs_recent)
                    {
                        if (lhs_recent < 0)
                            return false;
                        if (rhs_recent < 0)
                            return true;
                        return lhs_recent < rhs_recent;
                    }
                    if (lhs._category != rhs._category)
                        return lhs._category < rhs._category;
                    return lhs._display_name < rhs._display_name;
                });
                return filtered;
            }

            void RebuildActionList(UI::VerticalBox *list_root, const Vector<GraphNodeAction> &actions,
                                   const String &search_text, Vector<GraphNodeAction> &visible_actions,
                                   i32 &selected_index, const GraphActionMenu::ActionCallback &on_action)
            {
                list_root->ClearChildren();
                visible_actions = FilterAndSortActions(actions, search_text);
                if (visible_actions.empty())
                {
                    selected_index = -1;
                    auto *empty_text = list_root->AddChild<UI::Text>("No actions");
                    empty_text->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                              .Size({0.0f, kRowHeight}).Margin({6.0f, 4.0f, 6.0f, 0.0f});
                    empty_text->_color = Color(0.58f, 0.62f, 0.68f, 1.0f);
                    empty_text->_horizontal_align = UI::EAlignment::kLeft;
                    return;
                }
                if (selected_index < 0)
                    selected_index = 0;
                if (selected_index >= static_cast<i32>(visible_actions.size()))
                    selected_index = static_cast<i32>(visible_actions.size()) - 1;

                String current_category;
                bool is_in_recent_section = false;
                for (u32 index = 0u; index < visible_actions.size(); ++index)
                {
                    const GraphNodeAction &action = visible_actions[index];
                    const bool is_recent = RecentIndex(action._node_type) >= 0;
                    if (is_recent && !is_in_recent_section)
                    {
                        AddSectionHeader(list_root, "Recent");
                        is_in_recent_section = true;
                        current_category.clear();
                    }
                    else if (!is_recent && action._category != current_category)
                    {
                        current_category = action._category;
                        AddSectionHeader(list_root, current_category.empty() ? "Actions" : current_category);
                    }
                    AddActionButton(list_root, action, static_cast<i32>(index) == selected_index, on_action);
                }
                list_root->InvalidateLayout();
                list_root->InvalidatePaint();
            }
        } // namespace

        bool GraphActionMenu::ShowAt(Vector2f popup_pos, const GraphActionMenuContext &context, ActionCallback on_action,
                                     CloseCallback on_close)
        {
            if (context._document == nullptr || context._document->Schema() == nullptr)
                return false;

            Vector<GraphNodeAction> actions;
            context._document->Schema()->CollectNodeActions(*context._document, context._source_pin, actions);
            if (actions.empty())
                return false;

            auto root = MakeRef<UI::Border>();
            root->Name("GraphActionMenu");
            root->GetSlot()->Size({kMenuWidth, kMenuHeight});
            root->Thickness(1.0f);
            root->CornerRadius(4.0f);
            root->SlotPadding() = UI::Padding(5.0f);
            root->_bg_color = Color(0.095f, 0.10f, 0.11f, 0.98f);
            root->_border_color = Color(0.45f, 0.50f, 0.56f, 0.85f);

            auto *layout = root->AddChild<UI::VerticalBox>();
            layout->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);

            auto *search = layout->AddChild<UI::InputBlock>("");
            search->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                  .Size({0.0f, kSearchHeight}).Margin({2.0f, 2.0f, 2.0f, 5.0f});

            auto *scroll = layout->AddChild<UI::ScrollView>();
            scroll->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            auto *list_root = scroll->AddChild<UI::VerticalBox>();
            list_root->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);

            auto all_actions = std::make_shared<Vector<GraphNodeAction>>(std::move(actions));
            auto visible_actions = std::make_shared<Vector<GraphNodeAction>>();
            auto search_text = std::make_shared<String>();
            auto selected_index = std::make_shared<i32>(0);
            RebuildActionList(list_root, *all_actions, *search_text, *visible_actions, *selected_index, on_action);

            search->_on_content_changed += [all_actions, visible_actions, search_text, selected_index, list_root,
                                            on_action](String value)
            {
                *search_text = std::move(value);
                *selected_index = 0;
                RebuildActionList(list_root, *all_actions, *search_text, *visible_actions, *selected_index,
                                  on_action);
            };
            auto handle_key = [all_actions, visible_actions, search_text, selected_index, list_root,
                               on_action](UI::UIEvent &e)
            {
                if (e._key_code == EKey::kESCAPE)
                {
                    UI::UIManager::Get()->HidePopup();
                    e._is_handled = true;
                }
                else if (e._key_code == EKey::kUP || e._key_code == EKey::kDOWN)
                {
                    if (!visible_actions->empty())
                    {
                        const i32 direction = e._key_code == EKey::kUP ? -1 : 1;
                        const i32 max_index = static_cast<i32>(visible_actions->size()) - 1;
                        *selected_index = std::clamp(*selected_index + direction, 0, max_index);
                        RebuildActionList(list_root, *all_actions, *search_text, *visible_actions, *selected_index,
                                          on_action);
                    }
                    e._is_handled = true;
                }
                else if (e._key_code == EKey::kRETURN)
                {
                    if (*selected_index >= 0 && *selected_index < static_cast<i32>(visible_actions->size()))
                    {
                        const GraphNodeAction action = (*visible_actions)[*selected_index];
                        RememberRecentAction(action._node_type);
                        UI::UIManager::Get()->HidePopup();
                        if (on_action)
                            on_action(action);
                    }
                    e._is_handled = true;
                }
            };
            root->OnKeyDown() += handle_key;
            search->OnKeyDown() += handle_key;

            UI::UIManager::Get()->HidePopup();
            UI::UIManager::Get()->ShowPopupAt(popup_pos.x, popup_pos.y, root, on_close);
            search->RequestFocus();
            return true;
        }
    } // namespace Editor
} // namespace Ailu
