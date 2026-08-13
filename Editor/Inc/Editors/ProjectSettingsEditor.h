#pragma once
#ifndef __PROJECT_SETTINGS_EDITOR_H__
#define __PROJECT_SETTINGS_EDITOR_H__

#include "Dock/DockWindow.h"
#include "Project/ProjectSettings.h"

namespace Ailu::UI
{
    class Button;
    class InputBlock;
    class UIElement;
    class HorizontalBox;
    class ScrollView;
    class Text;
    class VerticalBox;
}

namespace Ailu::Editor
{
    class ProjectSettingsEditor final : public DockWindow
    {
    public:
        ProjectSettingsEditor();
        ~ProjectSettingsEditor() override = default;

        void Update(f32 dt) override;
        void Open();

    private:
        void BuildContent();
        void Save();
        void ResetLayersAndTags();
        void AddTag();
        void SetStatus(const String &status);

        static UI::Text *AddSectionTitle(UI::UIElement *parent, const String &title);
        static UI::HorizontalBox *AddPropertyRow(UI::UIElement *parent, const String &label);

        ProjectSettings _editing;
        UI::VerticalBox *_content = nullptr;
        UI::InputBlock *_new_tag_input = nullptr;
        UI::Text *_status_text = nullptr;
        String _new_tag;
    };
}

#endif
