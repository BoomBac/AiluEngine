#pragma once

#include "Dock/DockWindow.h"
#include "Framework/Common/Path.h"
#include "Framework/Math/Color.h"
#include "generated/EditorPreferencesWindow.gen.h"

namespace Ailu
{
    namespace UI
    {
        class Border;
        class Button;
        class HorizontalBox;
        class InputBlock;
        class ScrollView;
        class SplitView;
        class Text;
        class UIElement;
        class VerticalBox;
    }
}

namespace Ailu
{
    class Type;
    namespace Editor
    {
        class ReflectedPropertyPanel;

        ACLASS()
        class EditorPreferencesWindow final : public DockWindow
        {
            GENERATED_BODY()

        public:
            EditorPreferencesWindow();
            ~EditorPreferencesWindow() override;

            void Update(f32 dt) override;

        private:
            enum class EPage : u8
            {
                kOutput,
                kUITheme,
                kEditorStyle,
            };

            struct ColorBinding
            {
                String _label;
                Color *_value = nullptr;
                UI::Border *_swatch = nullptr;
            };

            void BuildUI();
            void RebuildDetails();
            void SelectPage(EPage page);
            void AddPageButton(const String &label, EPage page);
            void BuildOutputDetails();
            void BuildReflectedDetails(const Type *type, void *instance);
            void LoadFromConfig();
            void ApplyToConfig();
            void ResetDefaults();
            void Save();
            void Load();
            void Apply();
            bool MatchesSearch(const String &text) const;
            void RefreshToolbar();
            void SetStatus(const String &status);
            void AddSectionTitle(UI::UIElement *parent, const String &title);
            void AddColorRow(UI::UIElement *parent, const String &label, Color *value);
            void OpenColorPicker(ColorBinding &binding);

            Color _level_colors[5]{};
            Color _module_colors[5]{};
            Vector<ColorBinding> _bindings;
            Vector<UI::Button *> _page_buttons;
            UI::SplitView *_main_split = nullptr;
            UI::VerticalBox *_page_list = nullptr;
            UI::VerticalBox *_details_content = nullptr;
            UI::InputBlock *_search_input = nullptr;
            UI::Text *_status_text = nullptr;
            UI::Text *_path_text = nullptr;
            Scope<ReflectedPropertyPanel> _property_panel;
            Path _theme_path;
            Path _editor_style_path;
            EPage _selected_page = EPage::kOutput;
            String _search_text;
            Ref<int> _lifetime = MakeRef<int>(0);
            bool _dirty = false;
        };
    }
}
