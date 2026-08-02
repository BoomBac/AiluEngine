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

        void EditorPopup::ShowDialogAt(Vector2f popup_pos, const String &popup_name, const String &title, Vector2f size,
                                       const std::function<void(UI::VerticalBox *content, UI::Text *title_text)> &build_content,
                                       const Vector<PopupDialogAction> &actions,
                                       const std::function<void()> &on_shown)
        {
            auto root = MakeRef<UI::Border>();
            root->Name(popup_name);
            root->GetSlot()->Size({size.x, size.y});
            root->Thickness(1.0f);
            root->SlotPadding() = UI::Padding(4.0f);
            root->InvalidateLayout();
            root->_bg_color = {0.12f, 0.12f, 0.12f, 0.96f};
            root->_border_color = Colors::kWhite;

            auto *layout = root->AddChild<UI::VerticalBox>();
            layout->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);

            auto popup_position = std::make_shared<Vector2f>(popup_pos);
            auto is_dragging = std::make_shared<bool>(false);
            auto drag_offset = std::make_shared<Vector2f>(0.0f, 0.0f);

            auto *title_bar = layout->AddChild<UI::Border>();
            title_bar->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size({size.x - 20.0f, 20.0f});
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
                button_row->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size({size.x - 20.0f, 24.0f});

                for (const auto &action: actions)
                {
                    auto btn = button_row->AddChild<UI::Button>();
                    btn->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size({0.0f, 24.0f}).FillRate(1.0f);
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

            UI::UIManager::Get()->HidePopup();
            UI::UIManager::Get()->ShowPopupAt(popup_pos.x, popup_pos.y, root);
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
