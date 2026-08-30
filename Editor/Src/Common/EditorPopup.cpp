#include "Common/EditorPopup.h"

#include "UI/Widget.h"
#include "UI/UIFramework.h"
#include "Framework/Common/KeyCode.h"

namespace Ailu
{
    namespace Editor
    {
        namespace
        {
            inline const Vector4f kPropLabelMargin = {2.0f, 0.0f, 2.0f, 2.0f};
            inline const Vector4f kPropValueMargin = {10.0f, 0.0f, 2.0f, 2.0f};
            inline const Vector4f kPropInnerMargin = {2.0f, 0.0f, 2.0f, 2.0f};
            inline constexpr f32 kPropLabelFill = 1.0f;
            inline constexpr f32 kPropValueFill = 3.0f;

            void ApplyPopupStyles(UI::UIElement *element)
            {
                if (element == nullptr)
                    return;
                if (auto *button = element->As<UI::Button>())
                    button->SetStyleId("Popup");
                else if (auto *input = element->As<UI::InputBlock>())
                    input->SetStyleId("Popup");
                else if (auto *border = element->As<UI::Border>())
                {
                    border->SetStyleId("Popup");
                    border->GetStyleOverride().SetCornerRadius(6.0f);
                }
                else if (auto *list_view = element->As<UI::ListView>())
                    list_view->SetStyleId("Popup");

                for (const auto &child : element->GetChildren())
                    ApplyPopupStyles(child.get());
            }

            struct InlineTextEditState
            {
                UI::UIElement *_parent = nullptr;
                UI::Text *_display = nullptr;
                UI::InputBlock *_input = nullptr;
                std::shared_ptr<String> _value;
                std::function<std::optional<String>(const String &)> _on_submit;
                UI::Padding _display_margin;
                Vector2f _display_size = Vector2f::kZero;
                UI::ESizePolicy _display_size_policy_h = UI::ESizePolicy::kAuto;
                UI::ESizePolicy _display_size_policy_v = UI::ESizePolicy::kAuto;
                f32 _display_fill_rate = 1.0f;
                UI::EAlignment _display_cross_align = UI::EAlignment::kCenter;
                bool _is_finished = false;
            };
        }

        UI::HorizontalBox *EditorPopup::AddPropertyRow(UI::UIElement *parent, const String &label, UI::HorizontalBox **out_value_box)
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

        UI::CheckBox *EditorPopup::AddCheckBoxRow(UI::UIElement *parent, const String &label, bool initial_state)
        {
            UI::HorizontalBox *value_box = nullptr;
            AddPropertyRow(parent, label, &value_box);
            auto checkbox = value_box->AddChild<UI::CheckBox>();
            checkbox->GetSlotAs<UI::LinearSlot>().Margin(kPropInnerMargin).CrossAlignment(UI::EAlignment::kRight)
                    .SizePolicy(UI::ESizePolicy::kAuto, UI::ESizePolicy::kAuto);
            checkbox->SetChecked(initial_state);
            return checkbox;
        }

        void EditorPopup::ShowActionMenuAt(Vector2f popup_pos, const Vector<PopupMenuAction> &actions)
        {
            if (actions.empty())
                return;

            constexpr f32 kMenuWidth = 180.0f;
            constexpr f32 kRowHeight = 24.0f;
            constexpr f32 kMaxHeight = 220.0f;

            auto list_view = MakeRef<UI::ListView>();
            list_view->Name("EditorPopupMenu");
            UI::UIBrush transparent_brush;
            transparent_brush._type = UI::EUIBrushType::kColor;
            transparent_brush._tint = Colors::kTransparent;
            list_view->SetBackgroundBrush(transparent_brush);
            const f32 popup_height = std::min(kRowHeight * static_cast<f32>(actions.size()), kMaxHeight);
            list_view->GetSlot()->Size({kMenuWidth, popup_height});
            list_view->SetViewportHeight(popup_height);

            for (const auto &action: actions)
            {
                auto item = MakeRef<UI::Text>(action._label);
                item->GetSlot()->Size({kMenuWidth, kRowHeight});
                item->SlotPadding() = UI::Padding(6.0f, 4.0f, 6.0f, 4.0f);
                item->InvalidateLayout();
                item->_horizontal_align = UI::EAlignment::kLeft;
                item->_vertical_align = UI::EAlignment::kCenter;
                if (action._is_destructive)
                    item->_color = Colors::kRed;
                item->OnMouseClick() += [on_click = action._on_click](UI::UIEvent &e)
                {
                    UI::UIManager::Get()->HidePopup();
                    on_click();
                    e._is_handled = true;
                };
                list_view->AddItem(item);
                item->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size({kMenuWidth, kRowHeight});
            }

            UI::UIManager::Get()->HidePopup();
            UI::UIManager::Get()->ShowPopupAt(popup_pos.x, popup_pos.y, list_view);
        }

        void EditorPopup::BeginInlineTextInput(UI::UIElement *parent, UI::Text *display, const String &initial_value,
                                               const std::function<std::optional<String>(const String &)> &on_submit)
        {
            if (parent == nullptr || display == nullptr || !display->IsVisible())
                return;

            UI::UIElement *input_parent = display->GetParent();
            if (input_parent == nullptr)
                return;

            auto *display_slot = dynamic_cast<UI::LinearSlot *>(display->GetSlot().get());
            if (display_slot == nullptr)
                return;

            auto state = std::make_shared<InlineTextEditState>();
            state->_parent = input_parent;
            state->_display = display;
            state->_value = std::make_shared<String>(initial_value);
            state->_on_submit = on_submit;

            state->_display_margin = display_slot->_margin;
            state->_display_size = display_slot->_size;
            state->_display_size_policy_h = display_slot->_size_policy_h;
            state->_display_size_policy_v = display_slot->_size_policy_v;
            state->_display_fill_rate = display_slot->_fill_rate;
            state->_display_cross_align = display_slot->_cross_align;
            display->SetVisible(false);

            auto *input = input_parent->AddChild<UI::InputBlock>(initial_value);
            state->_input = input;
            auto *input_slot = dynamic_cast<UI::LinearSlot *>(input->GetSlot().get());
            if (input_slot == nullptr)
            {
                display->SetVisible(true);
                input_parent->RemoveChild(input);
                return;
            }
            input_slot->_margin = display_slot->_margin;
            input_slot->_size = display_slot->_size;
            input_slot->_size_policy_h = display_slot->_size_policy_h;
            input_slot->_size_policy_v = display_slot->_size_policy_v;
            input_slot->_fill_rate = display_slot->_fill_rate;
            input_slot->_cross_align = display_slot->_cross_align;
            display_slot->_margin = UI::Padding();
            display_slot->_size = Vector2f::kZero;
            display_slot->_size_policy_h = UI::ESizePolicy::kFixed;
            display_slot->_size_policy_v = UI::ESizePolicy::kFixed;
            display_slot->_fill_rate = 0.0f;
            input->_on_content_changed += [value = state->_value](String content)
            {
                *value = std::move(content);
            };

            auto finish_edit = std::make_shared<std::function<void(bool)>>();
            *finish_edit = [state, finish_edit](bool commit)
            {
                if (state->_is_finished)
                    return;

                if (commit && state->_on_submit)
                {
                    if (auto error = state->_on_submit(*state->_value); error.has_value())
                    {
                        state->_input->RequestFocus();
                        return;
                    }
                }

                state->_is_finished = true;
                UI::UIManager::Get()->ClearFocus(state->_input);
                if (commit)
                {
                    state->_display->Name(*state->_value);
                    state->_display->SetText(*state->_value);
                }
                auto *display_slot = dynamic_cast<UI::LinearSlot *>(state->_display->GetSlot().get());
                if (display_slot != nullptr)
                {
                    display_slot->_margin = state->_display_margin;
                    display_slot->_size = state->_display_size;
                    display_slot->_size_policy_h = state->_display_size_policy_h;
                    display_slot->_size_policy_v = state->_display_size_policy_v;
                    display_slot->_fill_rate = state->_display_fill_rate;
                    display_slot->_cross_align = state->_display_cross_align;
                }
                state->_display->SetVisible(true);
                state->_parent->RemoveChild(state->_input);
                state->_parent->InvalidateLayout();
            };

            input->OnKeyDown() += [finish_edit](UI::UIEvent &e)
            {
                if (e._key_code == EKey::kESCAPE)
                {
                    (*finish_edit)(false);
                    e._is_handled = true;
                }
                else if (e._key_code == EKey::kRETURN)
                {
                    (*finish_edit)(true);
                    e._is_handled = true;
                }
            };
            input->OnMouseDown() += [](UI::UIEvent &e)
            {
                e._is_handled = true;
            };
            input->_on_focus_lost += [finish_edit]()
            {
                (*finish_edit)(true);
            };

            parent->InvalidateLayout();
            input->SetCursorToEnd();
            input->RequestFocus();
        }

        void EditorPopup::ShowDialogAt(Vector2f popup_pos, const String &popup_name, const String &title, Vector2f size,
                                       const std::function<void(UI::VerticalBox *content, UI::Text *title_text)> &build_content,
                                       const Vector<PopupDialogAction> &actions,
                                       const std::function<void()> &on_shown, bool is_modal)
        {
            auto root = MakeRef<UI::Border>();
            root->Name(popup_name);
            root->SetStyleId("Popup");
            root->GetSlot()->Size({size.x, size.y});
            root->Thickness(1.0f);
            root->SlotPadding() = UI::Padding(1.0f);
            root->InvalidateLayout();
            root->_bg_color = {0.12f, 0.12f, 0.12f, 0.96f};
            root->_border_color = Colors::kWhite;
            Color popup_border_color(0.28f, 0.31f, 0.35f, 1.0f);
            Vector4f popup_border_width(1.0f);
            if (auto *theme = UI::UIManager::Get()->GetTheme(); theme != nullptr)
            {
                if (const auto *popup_style = theme->FindBorderStyle("Popup"); popup_style != nullptr)
                {
                    popup_border_color = popup_style->_visual._border_color;
                    popup_border_width = popup_style->_visual._border_width;
                }
            }
            root->GetStyleOverride().SetBorderColor(popup_border_color);
            root->GetStyleOverride().SetBorderWidth(popup_border_width);

            auto *layout = root->AddChild<UI::VerticalBox>();
            layout->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);

            auto popup_position = std::make_shared<Vector2f>(popup_pos);
            auto is_dragging = std::make_shared<bool>(false);
            auto drag_offset = std::make_shared<Vector2f>(0.0f, 0.0f);

            auto *title_bar = layout->AddChild<UI::Border>();
            title_bar->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size({0.0f, 20.0f});
            title_bar->Thickness(0.0f);

            auto *title_text = title_bar->AddChild<UI::Text>(title);
            title_text->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            title_text->_horizontal_align = UI::EAlignment::kLeft;

            title_bar->OnMouseDown() += [popup_position, is_dragging, drag_offset](UI::UIEvent &e)
            {
                if (e._key_code != EKey::kLBUTTON)
                    return;
                *is_dragging = true;
                *drag_offset = *popup_position - e._mouse_position;
                e._is_handled = true;
            };
            title_bar->OnMouseUp() += [is_dragging](UI::UIEvent &e)
            {
                if (e._key_code != EKey::kLBUTTON)
                    return;
                *is_dragging = false;
                e._is_handled = true;
            };
            title_bar->OnMouseMove() += [popup_position, is_dragging, drag_offset](UI::UIEvent &e)
            {
                if (!*is_dragging)
                    return;
                *popup_position = e._mouse_position + *drag_offset;
                if (auto *popup_widget = UI::UIManager::Get()->GetPopupWidget())
                    popup_widget->SetPosition(*popup_position);
                e._is_handled = true;
            };

            auto *content = layout->AddChild<UI::VerticalBox>();
            content->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill).Margin({0.0f, 8.0f, 0.0f, 8.0f});

            if (build_content)
                build_content(content, title_text);

            if (!actions.empty())
            {
                auto *button_row = layout->AddChild<UI::HorizontalBox>();
                button_row->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size({0.0f, 24.0f});

                for (const auto &action: actions)
                {
                    auto btn = button_row->AddChild<UI::Button>();
                    btn->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                        .Size({0.0f, 24.0f}).Margin(UI::Padding(2.0f, 0.0f, 2.0f, 0.0f)).FillRate(1.0f);
                    btn->SetText(action._label);

                    btn->OnMouseClick() += [action, title, title_text](UI::UIEvent &e)
                    {
                        std::optional<String> error = std::nullopt;
                        if (action._on_click)
                            error = action._on_click();
                        if (error.has_value())
                        {
                            title_text->SetText(*error);
                            title_text->_color = Colors::kRed;
                            e._is_handled = true;
                            return;
                        }
                        title_text->SetText(title, false);
                        title_text->_color = Colors::kWhite;
                        if (action._close_on_success)
                            UI::UIManager::Get()->HidePopup();
                        e._is_handled = true;
                    };
                }
            }

            ApplyPopupStyles(root.get());
            UI::UIManager::Get()->HidePopup();
            UI::UIManager::Get()->ShowPopupAt(popup_pos.x, popup_pos.y, root, nullptr, nullptr, is_modal);
            title_text->SetText(title, false);
            title_text->_color = Colors::kWhite;
            if (on_shown)
                on_shown();
        }

        void EditorPopup::ShowTextInputAt(Vector2f popup_pos, const String &title, const String &initial_value,
                                          const std::function<std::optional<String>(const String &)> &on_submit)
        {
            auto input_value = std::make_shared<String>(initial_value);
            UI::InputBlock *input = nullptr;
            ShowDialogAt(popup_pos, "EditorTextPrompt", title, {260.0f, 110.0f},
                         [input_value, initial_value, &input](UI::VerticalBox *content, UI::Text *)
                         {
                             input = content->AddChild<UI::InputBlock>();
                             input->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size({240.0f, 24.0f});
                             input->_on_content_changed += [input_value](String value)
                             {
                                 *input_value = std::move(value);
                             };
                             input->SetContent(initial_value, false);
                         },
                         {
                                 {"OK", [input, input_value, on_submit]() -> std::optional<String>
                                  {
                                      if (auto error = on_submit(*input_value); error.has_value())
                                      {
                                          if (input != nullptr)
                                              input->RequestFocus();
                                          return error;
                                      }
                                      return std::nullopt;
                                  }},
                                 {"Cancel", []() -> std::optional<String> { return std::nullopt; }}
                         },
                         [input]()
                         {
                             if (input != nullptr)
                                 input->RequestFocus();
                         });
        }

        void EditorPopup::ShowConfirmAt(Vector2f popup_pos, const String &message, const std::function<void()> &on_confirm,
                                        const String &confirm_label)
        {
            ShowDialogAt(popup_pos, "EditorConfirmPrompt", message, {260.0f, 88.0f},
                         [message](UI::VerticalBox *content, UI::Text *)
                         {
                             auto *message_text = content->AddChild<UI::Text>(message);
                             message_text->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size({240.0f, 36.0f});
                             message_text->_horizontal_align = UI::EAlignment::kLeft;
                         },
                         {
                                 {confirm_label, [on_confirm]() -> std::optional<String>
                                  {
                                      on_confirm();
                                      return std::nullopt;
                                  }, true},
                                 {"Cancel", []() -> std::optional<String> { return std::nullopt; }}
                         });
        }
    }// namespace Editor
}// namespace Ailu
