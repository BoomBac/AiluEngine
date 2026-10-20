#include "Widgets/EditorPreferencesWindow.h"

#include "EditorApp.h"
#include "Common/EditorStyle.h"
#include "Inspector/ReflectedPropertyPanel.h"
#include "UI/Basic.h"
#include "UI/ColorPicker.h"
#include "UI/Container.h"
#include "UI/Style/UITheme.h"
#include "UI/UIFramework.h"
#include "Widgets/StyleThemeEditor.h"

#include <algorithm>
#include <cctype>

namespace Ailu::Editor
{
    namespace
    {
        constexpr f32 kToolbarHeight = 28.0f;
        constexpr f32 kRowHeight = 28.0f;
        constexpr f32 kLabelWidth = 170.0f;
        constexpr f32 kSwatchWidth = 96.0f;
        constexpr f32 kPageWidthRatio = 0.22f;

        Color DefaultLevelColor(u32 index)
        {
            static const Color kColors[] = {
                Color(0.82f, 0.86f, 0.92f, 1.0f), Color(1.0f, 0.78f, 0.32f, 1.0f),
                Color(1.0f, 0.36f, 0.36f, 1.0f), Color(0.62f, 0.78f, 1.0f, 1.0f),
                Color(0.65f, 0.68f, 0.74f, 1.0f),
            };
            return kColors[std::min(index, 4u)];
        }

        Color DefaultModuleColor(u32 index)
        {
            static const Color kColors[] = {
                Color(0.78f, 0.82f, 0.88f, 1.0f), Color(0.55f, 0.86f, 0.68f, 1.0f),
                Color(0.78f, 0.62f, 1.0f, 1.0f), Color(0.98f, 0.64f, 0.42f, 1.0f),
                Color(0.40f, 0.82f, 1.0f, 1.0f),
            };
            return kColors[std::min(index, 4u)];
        }

        String Lowercase(String value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            });
            return value;
        }

        void SetColorSwatch(UI::Border *swatch, Color color)
        {
            if (swatch == nullptr)
                return;
            UI::UIBrush background;
            background._type = UI::EUIBrushType::kColor;
            background._tint = color;
            auto &style = swatch->GetStyleOverride();
            style.SetBackground(background);
            style.SetBorderColor(Color(0.75f, 0.78f, 0.84f, 1.0f));
            style.SetBorderWidth(1.0f);
        }
    }

    EditorPreferencesWindow::EditorPreferencesWindow()
        : DockWindow("Editor Preferences", Vector2f(760.0f, 620.0f)), _property_panel(MakeScope<ReflectedPropertyPanel>())
    {
        _theme_path = GetDefaultUIThemePath();
        _editor_style_path = GetDefaultEditorStylePath();
        LoadFromConfig();
        BuildUI();
    }

    EditorPreferencesWindow::~EditorPreferencesWindow()
    {
        _lifetime.reset();
    }

    void EditorPreferencesWindow::Update(f32 dt)
    {
        DockWindow::Update(dt);
    }

    void EditorPreferencesWindow::BuildUI()
    {
        auto *root = _content_root->AddChild<UI::VerticalBox>();
        root->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);

        auto *toolbar = root->AddChild<UI::HorizontalBox>();
        toolbar->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
            .Size(Vector2f(0.0f, kToolbarHeight));
        auto add_button = [toolbar](const String &label, f32 width)
        {
            auto *button = toolbar->AddChild<UI::Button>(label);
            button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                .Size(Vector2f(width, 0.0f));
            return button;
        };
        auto *save_button = add_button("Save", 64.0f);
        save_button->OnMouseClick() += [this](UI::UIEvent &event) { Save(); event._is_handled = true; };
        auto *load_button = add_button("Load", 64.0f);
        load_button->OnMouseClick() += [this](UI::UIEvent &event) { Load(); event._is_handled = true; };
        auto *apply_button = add_button("Apply", 64.0f);
        apply_button->OnMouseClick() += [this](UI::UIEvent &event) { Apply(); event._is_handled = true; };
        auto *reset_button = add_button("Reset", 72.0f);
        reset_button->OnMouseClick() += [this](UI::UIEvent &event) { ResetDefaults(); event._is_handled = true; };
        _path_text = toolbar->AddChild<UI::Text>();
        _path_text->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill)
            .Margin(UI::Padding(8.0f, 0.0f, 0.0f, 0.0f));
        _status_text = toolbar->AddChild<UI::Text>("Saved");
        _status_text->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
            .Size(Vector2f(150.0f, 0.0f));
        _status_text->_horizontal_align = UI::EAlignment::kRight;
        _search_input = toolbar->AddChild<UI::InputBlock>();
        _search_input->SetCommitLogEnabled(false);
        _search_input->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
            .Size(Vector2f(180.0f, 0.0f)).Margin(UI::Padding(8.0f, 2.0f, 4.0f, 2.0f));
        _search_input->_on_content_changed += [this](String content)
        {
            _search_text = std::move(content);
            RebuildDetails();
        };

        _main_split = root->AddChild<UI::SplitView>();
        _main_split->_is_horizontal = true;
        _main_split->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
        _main_split->SetRatio(kPageWidthRatio);

        auto *page_scroll = _main_split->AddChild<UI::ScrollView>();
        _page_list = page_scroll->AddChild<UI::VerticalBox>();
        _page_list->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
        _page_list->SlotPadding() = UI::Padding(4.0f);

        auto *detail_scroll = _main_split->AddChild<UI::ScrollView>();
        _details_content = detail_scroll->AddChild<UI::VerticalBox>();
        _details_content->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
        _details_content->SlotPadding() = UI::Padding(8.0f);

        AddPageButton("Output", EPage::kOutput);
        AddPageButton("UI Theme", EPage::kUITheme);
        AddPageButton("Editor Style", EPage::kEditorStyle);
        SelectPage(EPage::kOutput);
    }

    void EditorPreferencesWindow::AddPageButton(const String &label, EPage page)
    {
        auto *button = _page_list->AddChild<UI::Button>(label);
        button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
            .Size(Vector2f(0.0f, 28.0f)).Margin(UI::Padding(0.0f, 0.0f, 0.0f, 2.0f));
        button->OnMouseClick() += [this, page](UI::UIEvent &event)
        {
            SelectPage(page);
            event._is_handled = true;
        };
        _page_buttons.push_back(button);
    }

    void EditorPreferencesWindow::SelectPage(EPage page)
    {
        _selected_page = page;
        const String page_labels[] = {"Output", "UI Theme", "Editor Style"};
        for (u32 index = 0u; index < _page_buttons.size(); ++index)
            _page_buttons[index]->SetText(index == static_cast<u32>(page) ? String("> ") + page_labels[index] : page_labels[index], false);
        RebuildDetails();
        RefreshToolbar();
    }

    void EditorPreferencesWindow::RebuildDetails()
    {
        if (_details_content == nullptr)
            return;
        _property_panel->Clear();
        _details_content->ClearChildren();
        _bindings.clear();
        switch (_selected_page)
        {
            case EPage::kOutput: BuildOutputDetails(); break;
            case EPage::kUITheme: BuildReflectedDetails(UI::UITheme::StaticType(), &g_editor_ui_theme); break;
            case EPage::kEditorStyle: BuildReflectedDetails(EditorStyle::StaticType(), &g_editor_style); break;
        }
    }

    void EditorPreferencesWindow::BuildReflectedDetails(const Type *type, void *instance)
    {
        const String search = Lowercase(_search_text);
        ReflectedPropertyPanel::BuildArgs args;
        args._type = type;
        args._instance = instance;
        args._parent = _details_content;
        args._filter = [search](const PropertyInfo &property)
        {
            if (search.empty())
                return true;
            String name = property.Name();
            if (!name.empty() && name.front() == '_')
                name.erase(name.begin());
            return Lowercase(name).find(search) != String::npos;
        };
        args._on_property_changed = [this](const PropertyInfo &)
        {
            if (_selected_page == EPage::kUITheme)
                ApplyUIThemeChanges();
            _dirty = true;
            SetStatus("Modified");
        };
        _property_panel->Build(args);
    }

    void EditorPreferencesWindow::BuildOutputDetails()
    {
        AddSectionTitle(_details_content, "Output Level Colors");
        const char *level_names[] = {"Info", "Warning", "Error / Fatal", "Debug", "Trace"};
        for (u32 index = 0u; index < 5u; ++index)
            if (MatchesSearch(level_names[index]))
                AddColorRow(_details_content, level_names[index], &_level_colors[index]);
        AddSectionTitle(_details_content, "Output Module Colors");
        const char *module_names[] = {"General", "Asset", "Shader", "RHI", "Editor"};
        for (u32 index = 0u; index < 5u; ++index)
            if (MatchesSearch(module_names[index]))
                AddColorRow(_details_content, module_names[index], &_module_colors[index]);
    }

    bool EditorPreferencesWindow::MatchesSearch(const String &text) const
    {
        return _search_text.empty() || Lowercase(text).find(Lowercase(_search_text)) != String::npos;
    }

    void EditorPreferencesWindow::RefreshToolbar()
    {
        if (_path_text == nullptr)
            return;
        _path_text->SetText(_selected_page == EPage::kOutput ? "Editor/EditorConfig.json" :
                            (_selected_page == EPage::kUITheme ? _theme_path.string() : _editor_style_path.string()), false);
    }

    void EditorPreferencesWindow::LoadFromConfig()
    {
        const auto &config = EditorApp::GetEditor()->GetEditorConfig();
        _level_colors[0] = config._output_info_color; _level_colors[1] = config._output_warning_color;
        _level_colors[2] = config._output_error_color; _level_colors[3] = config._output_debug_color;
        _level_colors[4] = config._output_trace_color; _module_colors[0] = config._output_general_color;
        _module_colors[1] = config._output_asset_color; _module_colors[2] = config._output_shader_color;
        _module_colors[3] = config._output_rhi_color; _module_colors[4] = config._output_editor_color;
    }

    void EditorPreferencesWindow::ApplyToConfig()
    {
        auto &config = EditorApp::GetEditor()->GetEditorConfig();
        config._output_info_color = _level_colors[0]; config._output_warning_color = _level_colors[1];
        config._output_error_color = _level_colors[2]; config._output_debug_color = _level_colors[3];
        config._output_trace_color = _level_colors[4]; config._output_general_color = _module_colors[0];
        config._output_asset_color = _module_colors[1]; config._output_shader_color = _module_colors[2];
        config._output_rhi_color = _module_colors[3]; config._output_editor_color = _module_colors[4];
    }

    void EditorPreferencesWindow::ResetDefaults()
    {
        switch (_selected_page)
        {
            case EPage::kOutput:
                for (u32 index = 0u; index < 5u; ++index)
                {
                    _level_colors[index] = DefaultLevelColor(index);
                    _module_colors[index] = DefaultModuleColor(index);
                }
                ApplyToConfig();
                break;
            case EPage::kUITheme: LoadUIThemeFromFile(GetDefaultUIThemePath()); break;
            case EPage::kEditorStyle: LoadEditorStyleFromFile(GetDefaultEditorStylePath()); break;
        }
        RebuildDetails();
        _dirty = true;
        SetStatus("Unsaved changes");
    }

    void EditorPreferencesWindow::Save()
    {
        bool saved = false;
        switch (_selected_page)
        {
            case EPage::kOutput: ApplyToConfig(); EditorApp::GetEditor()->SaveEditorConfig(); saved = true; break;
            case EPage::kUITheme: saved = SaveUIThemeToFile(_theme_path); break;
            case EPage::kEditorStyle: saved = SaveEditorStyleToFile(_editor_style_path); break;
        }
        _dirty = !saved;
        SetStatus(saved ? "Saved" : "Save failed");
    }

    void EditorPreferencesWindow::Load()
    {
        bool loaded = false;
        switch (_selected_page)
        {
            case EPage::kOutput: LoadFromConfig(); loaded = true; break;
            case EPage::kUITheme: loaded = LoadUIThemeFromFile(_theme_path); break;
            case EPage::kEditorStyle: loaded = LoadEditorStyleFromFile(_editor_style_path); break;
        }
        if (loaded)
            RebuildDetails();
        _dirty = !loaded;
        SetStatus(loaded ? "Loaded" : "Load failed");
    }

    void EditorPreferencesWindow::Apply()
    {
        if (_selected_page == EPage::kOutput)
            ApplyToConfig();
        else if (_selected_page == EPage::kUITheme)
            ApplyUIThemeChanges();
        _dirty = false;
        SetStatus("Applied");
    }

    void EditorPreferencesWindow::SetStatus(const String &status)
    {
        if (_status_text != nullptr)
            _status_text->SetText(status, false);
    }

    void EditorPreferencesWindow::AddSectionTitle(UI::UIElement *parent, const String &title)
    {
        auto *text = parent->AddChild<UI::Text>(title);
        text->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
            .Size(Vector2f(0.0f, 26.0f)).Margin(UI::Padding(0.0f, 8.0f, 0.0f, 2.0f));
    }

    void EditorPreferencesWindow::AddColorRow(UI::UIElement *parent, const String &label, Color *value)
    {
        auto *row = parent->AddChild<UI::HorizontalBox>();
        row->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size({0.0f, kRowHeight});
        auto *text = row->AddChild<UI::Text>(label);
        text->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size({kLabelWidth, 0.0f});
        auto *swatch = row->AddChild<UI::Border>();
        swatch->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size({kSwatchWidth, 0.0f});
        swatch->SetWantsMouseEvents(true);
        SetColorSwatch(swatch, *value);
        _bindings.push_back({label, value, swatch});
        swatch->OnMouseClick() += [this, value](UI::UIEvent &event)
        {
            auto binding_it = std::find_if(_bindings.begin(), _bindings.end(), [value](const ColorBinding &binding)
            {
                return binding._value == value;
            });
            if (binding_it != _bindings.end())
                OpenColorPicker(*binding_it);
            event._is_handled = true;
        };
    }

    void EditorPreferencesWindow::OpenColorPicker(ColorBinding &binding)
    {
        Color *value = binding._value;
        UI::Border *swatch = binding._swatch;
        auto picker = MakeRef<UI::ColorPicker>(*value);
        picker->GetSlot()->Size({320.0f, 240.0f});
        picker->OnValueChanged() += [this, weak = Weak<int>(_lifetime), value, swatch](Color color)
        {
            if (weak.expired())
                return;
            *value = color;
            ApplyToConfig();
            SetColorSwatch(swatch, *value);
            swatch->InvalidatePaint();
            _dirty = true;
            SetStatus("Unsaved changes");
        };
        const Vector4f rect = swatch->GetArrangeRect();
        UI::UIManager::Get()->ShowPopupAt(rect.x, rect.y + rect.w, picker);
    }
}
