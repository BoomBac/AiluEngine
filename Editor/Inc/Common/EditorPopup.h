#pragma once
#ifndef __EDITOR_POPUP_H__
#define __EDITOR_POPUP_H__

#include "UI/Basic.h"
#include "UI/Container.h"

#include <functional>
#include <memory>
#include <optional>

namespace Ailu
{
    namespace Editor
    {
        struct PopupMenuAction
        {
            String _label;
            std::function<void()> _on_click;
            bool _is_destructive = false;
        };

        struct PopupDialogAction
        {
            String _label;
            std::function<std::optional<String>()> _on_click;
            bool _is_destructive = false;
            bool _close_on_success = true;
        };

        class EditorPopup
        {
        public:
            static UI::HorizontalBox *AddPropertyRow(UI::UIElement *parent, const String &label, UI::HorizontalBox **out_value_box = nullptr);
            static UI::CheckBox *AddCheckBoxRow(UI::UIElement *parent, const String &label, bool initial_state);

            static void ShowActionMenuAt(Vector2f popup_pos, const Vector<PopupMenuAction> &actions);
            static void BeginInlineTextInput(UI::UIElement *parent, UI::Text *display, const String &initial_value,
                                             const std::function<std::optional<String>(const String &)> &on_submit);
            static void ShowTextInputAt(Vector2f popup_pos, const String &title, const String &initial_value,
                                        const std::function<std::optional<String>(const String &)> &on_submit);
            static void ShowConfirmAt(Vector2f popup_pos, const String &message, const std::function<void()> &on_confirm,
                                      const String &confirm_label = "Delete");
            static void ShowDialogAt(Vector2f popup_pos, const String &popup_name, const String &title, Vector2f size,
                                     const std::function<void(UI::VerticalBox *content, UI::Text *title_text)> &build_content,
                                     const Vector<PopupDialogAction> &actions,
                                     const std::function<void()> &on_shown = {}, bool is_modal = true);
        };
    }// namespace Editor
}// namespace Ailu

#endif// !__EDITOR_POPUP_H__
