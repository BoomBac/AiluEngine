#ifndef INSPECTOR_COMPONENTEDITORHELPERS_H
#define INSPECTOR_COMPONENTEDITORHELPERS_H
#include "UI/Basic.h"
#include "UI/Container.h"
#include "UI/ObjectAssetDropdown.h"
#include "Framework/Core/String.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/Containers/Array.h"

namespace Ailu
{
    namespace Editor
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

        inline UI::ObjectAssetDropdown *AddObjectAssetDropdownRow(UI::UIElement *parent, const String &label,
                                                                    const Type *object_type, const Guid &selected_guid = Guid::EmptyGuid(),
                                                                    bool allow_none = true)
        {
            UI::HorizontalBox *value_box = nullptr;
            AddPropertyRow(parent, label, &value_box);
            auto *dropdown = value_box->AddChild<UI::ObjectAssetDropdown>(object_type);
            dropdown->GetSlotAs<UI::LinearSlot>().Margin(kPropInnerMargin).CrossAlignment(UI::EAlignment::kRight)
                .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto).FillRate(1.0f);
            dropdown->SetAllowNone(allow_none);
            dropdown->SetSelectedGuid(selected_guid);
            return dropdown;
        }

        inline UI::CheckBox *AddCheckBoxRow(UI::UIElement *parent, const String &label, bool initial_state)
        {
            UI::HorizontalBox *value_box = nullptr;
            AddPropertyRow(parent, label, &value_box);
            auto checkbox = value_box->AddChild<UI::CheckBox>();
            checkbox->GetSlotAs<UI::LinearSlot>().Margin(kPropInnerMargin).CrossAlignment(UI::EAlignment::kRight)
                    .SizePolicy(UI::ESizePolicy::kAuto, UI::ESizePolicy::kAuto);
            checkbox->SetChecked(initial_state);
            return checkbox;
        }
    }// namespace Editor
}// namespace Ailu
#endif// INSPECTOR_COMPONENTEDITORHELPERS_H
