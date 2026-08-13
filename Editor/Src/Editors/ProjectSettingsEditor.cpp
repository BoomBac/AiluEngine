#include "Editors/ProjectSettingsEditor.h"

#include "Framework/Common/Log.h"
#include "Project/ProjectManager.h"
#include "UI/Basic.h"
#include "UI/Container.h"

#include <algorithm>
#include <format>

namespace Ailu::Editor
{
    namespace
    {
        constexpr f32 kToolbarHeight = 28.0f;
        constexpr f32 kRowHeight = 24.0f;
        constexpr f32 kLabelWidth = 150.0f;
    }

    ProjectSettingsEditor::ProjectSettingsEditor()
        : DockWindow("Project Settings", Vector2f(720.0f, 720.0f))
    {
        auto *root = _content_root->AddChild<UI::VerticalBox>();
        root->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);

        auto *toolbar = root->AddChild<UI::HorizontalBox>();
        toolbar->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
            .Size(Vector2f(0.0f, kToolbarHeight));
        toolbar->SlotPadding() = UI::Padding(4.0f, 2.0f, 4.0f, 2.0f);

        auto *save_button = toolbar->AddChild<UI::Button>("Save");
        save_button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
            .Size(Vector2f(64.0f, 0.0f));
        save_button->OnMouseClick() += [this](UI::UIEvent &event)
        {
            Save();
            event._is_handled = true;
        };

        auto *reset_button = toolbar->AddChild<UI::Button>("Reset Defaults");
        reset_button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
            .Size(Vector2f(110.0f, 0.0f));
        reset_button->OnMouseClick() += [this](UI::UIEvent &event)
        {
            ResetLayersAndTags();
            event._is_handled = true;
        };

        _status_text = toolbar->AddChild<UI::Text>("Not saved");
        _status_text->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill)
            .FillRate(1.0f);
        _status_text->_horizontal_align = UI::EAlignment::kRight;

        auto *scroll = root->AddChild<UI::ScrollView>();
        scroll->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
        _content = scroll->AddChild<UI::VerticalBox>();
        _content->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
        _content->SlotPadding() = UI::Padding(8.0f);
    }

    void ProjectSettingsEditor::Update(f32 dt)
    {
        DockWindow::Update(dt);
    }

    void ProjectSettingsEditor::Open()
    {
        if (!ProjectManager::Get().HasOpenedProject())
            return;
        _editing = ProjectManager::Get().CurrentProject().Settings();
        _editing.EnsureValid();
        BuildContent();
        SetStatus("Ready");
    }

    void ProjectSettingsEditor::BuildContent()
    {
        if (_content == nullptr)
            return;
        _content->ClearChildren();
        _new_tag_input = nullptr;
        _new_tag.clear();

        AddSectionTitle(_content, "Layers");
        for (u32 index = 0u; index < ProjectSettings::kLayerCount; ++index)
        {
            auto *row = AddPropertyRow(_content, std::format("Layer {}", index));
            auto *input = row->AddChild<UI::InputBlock>(_editing.GetLayerName(static_cast<u8>(index)));
            input->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill)
                .FillRate(1.0f);
            input->_on_content_changed += [this, input, index](String value)
            {
                if (!_editing.SetLayerName(static_cast<u8>(index), value))
                {
                    input->SetContent(_editing.GetLayerName(static_cast<u8>(index)), false);
                    SetStatus("Layer names must be unique and non-empty");
                    return;
                }
                SetStatus("Unsaved changes");
            };
        }

        AddSectionTitle(_content, "Tags");
        auto *new_tag_row = AddPropertyRow(_content, "New Tag");
        _new_tag_input = new_tag_row->AddChild<UI::InputBlock>();
        _new_tag_input->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill)
            .FillRate(1.0f);
        _new_tag_input->_on_content_changed += [this](String value)
        {
            _new_tag = std::move(value);
        };
        auto *add_button = new_tag_row->AddChild<UI::Button>("Add");
        add_button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
            .Size(Vector2f(56.0f, 0.0f));
        add_button->OnMouseClick() += [this](UI::UIEvent &event)
        {
            AddTag();
            event._is_handled = true;
        };

        for (u32 index = 0u; index < _editing._tags.size(); ++index)
        {
            auto *row = AddPropertyRow(_content, std::format("Tag {}", index));
            auto *input = row->AddChild<UI::InputBlock>(_editing._tags[index]);
            input->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill)
                .FillRate(1.0f);
            input->_on_content_changed += [this, input, index](String value)
            {
                const bool is_duplicate = std::find(_editing._tags.begin(), _editing._tags.end(), value) != _editing._tags.end() &&
                                          _editing._tags[index] != value;
                if (index == 0u || value.empty() || is_duplicate)
                {
                    input->SetContent(_editing._tags[index], false);
                    SetStatus("Tag names must be unique; Untagged cannot be changed");
                    return;
                }
                _editing._tags[index] = std::move(value);
                SetStatus("Unsaved changes");
            };

            if (index > 0u)
            {
                auto *remove_button = row->AddChild<UI::Button>("Remove");
                remove_button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                    .Size(Vector2f(72.0f, 0.0f));
                remove_button->OnMouseClick() += [this, index](UI::UIEvent &event)
                {
                    if (index < _editing._tags.size())
                    {
                        _editing._tags.erase(_editing._tags.begin() + index);
                        BuildContent();
                        SetStatus("Unsaved changes");
                    }
                    event._is_handled = true;
                };
            }
        }
    }

    void ProjectSettingsEditor::Save()
    {
        if (!ProjectManager::Get().HasOpenedProject())
            return;
        _editing.EnsureValid();
        ProjectManager::Get().CurrentProject().Settings() = _editing;
        if (ProjectManager::Get().CurrentProject().Save())
            SetStatus("Saved");
        else
            SetStatus("Save failed");
    }

    void ProjectSettingsEditor::ResetLayersAndTags()
    {
        _editing.ResetLayerAndTagDefaults();
        BuildContent();
        SetStatus("Unsaved changes");
    }

    void ProjectSettingsEditor::AddTag()
    {
        if (!_editing.AddTag(_new_tag))
        {
            SetStatus("Tag must be unique and non-empty");
            return;
        }
        BuildContent();
        SetStatus("Unsaved changes");
    }

    void ProjectSettingsEditor::SetStatus(const String &status)
    {
        if (_status_text != nullptr)
            _status_text->SetText(status);
    }

    UI::Text *ProjectSettingsEditor::AddSectionTitle(UI::UIElement *parent, const String &title)
    {
        auto *text = parent->AddChild<UI::Text>(title);
        text->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
            .Size(Vector2f(0.0f, 26.0f)).Margin(Vector4f(0.0f, 8.0f, 0.0f, 2.0f));
        text->FontSize(14.0f);
        return text;
    }

    UI::HorizontalBox *ProjectSettingsEditor::AddPropertyRow(UI::UIElement *parent, const String &label)
    {
        auto *row = parent->AddChild<UI::HorizontalBox>();
        row->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
            .Size(Vector2f(0.0f, kRowHeight));
        auto *text = row->AddChild<UI::Text>(label);
        text->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
            .Size(Vector2f(kLabelWidth, 0.0f));
        text->_horizontal_align = UI::EAlignment::kLeft;
        return row;
    }
}
