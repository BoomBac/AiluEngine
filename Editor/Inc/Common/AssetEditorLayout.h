#pragma once

#include "UI/Basic.h"
#include "UI/Container.h"

namespace Ailu
{
    namespace Editor
    {
        namespace AssetEditorLayout
        {
            inline UI::Text *AddSectionTitle(UI::UIElement *parent, const String &title)
            {
                auto *label = parent->AddChild<UI::Text>(title);
                label->_color = {0.86f, 0.86f, 0.86f, 1.0f};
                label->FontSize(13.0f);
                label->_horizontal_align = UI::EAlignment::kLeft;
                label->_vertical_align = UI::EAlignment::kCenter;
                label->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                    .Size({0.0f, 24.0f}).Margin({4.0f, 7.0f, 4.0f, 2.0f});
                return label;
            }

            inline UI::HorizontalBox *AddPropertyRow(UI::UIElement *parent, const String &label, f32 label_width = 78.0f)
            {
                auto *row = parent->AddChild<UI::HorizontalBox>();
                row->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                    .Size({0.0f, 24.0f}).Margin({4.0f, 1.0f, 4.0f, 1.0f});
                auto *name = row->AddChild<UI::Text>(label);
                name->_color = {0.58f, 0.61f, 0.65f, 1.0f};
                name->_horizontal_align = UI::EAlignment::kLeft;
                name->_vertical_align = UI::EAlignment::kCenter;
                name->FontSize(12.0f);
                name->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                    .Size({label_width, 0.0f});
                return row;
            }

            inline UI::Button *AddToolbarButton(UI::HorizontalBox *toolbar, const String &label, f32 width = 72.0f)
            {
                auto *button = toolbar->AddChild<UI::Button>(label);
                button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed)
                    .Size({width, 24.0f}).Margin({4.0f, 3.0f, 0.0f, 3.0f});
                return button;
            }
        }
    }
}
