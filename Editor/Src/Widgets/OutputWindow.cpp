#include "Widgets/OutputWindow.h"

#include "Common/EditorPopup.h"
#include "EditorApp.h"
#include "Framework/Common/Input.h"
#include "Framework/Common/Utils.h"
#include "Objects/JsonArchive.h"
#include "Platform/Process.h"
#include "Project/ProjectManager.h"
#include "UI/Basic.h"
#include "UI/Container.h"
#include "UI/Element/UITableElement.h"
#include "UI/Table/TableRenderer.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <format>
#include <imgui.h>
#include <sstream>

namespace Ailu::Editor
{
    namespace
    {
        constexpr const char *kModuleNames[] = {"All Modules", "General", "Asset", "Shader", "RHI", "Editor"};
        constexpr f32 kDefaultOutputSplitRatio = 0.88f;
        const Color kSourceLinkColor = Color(0.35f, 0.65f, 1.0f, 1.0f);

        bool IsControlDown()
        {
            return Input::IsKeyDown(EKey::kCONTROL) || Input::IsKeyDown(EKey::kLCONTROL) ||
                   Input::IsKeyDown(EKey::kRCONTROL);
        }

        std::filesystem::path ResolveSourcePath(const EditorLogRecord &record)
        {
            std::filesystem::path source_path = ToWChar(record._file);
            if (source_path.empty())
                return source_path;

            std::error_code error;
            const std::filesystem::path engine_root = std::filesystem::path(Application::GetAiluRoot());
            if (source_path.is_absolute())
            {
                if (std::filesystem::exists(source_path, error))
                    return source_path;

                const WString generic_path = source_path.generic_wstring();
                constexpr const wchar_t *kRepositoryDirectories[] = {
                    L"Engine", L"Editor", L"Test", L"Player", L"Plugins", L"AiluHeadTool"};
                for (const wchar_t *directory : kRepositoryDirectories)
                {
                    const WString marker = std::format(L"/{}/", directory);
                    const size_t marker_pos = generic_path.find(marker);
                    if (marker_pos == WString::npos)
                        continue;
                    const std::filesystem::path remapped_path = engine_root / generic_path.substr(marker_pos + 1u);
                    error.clear();
                    if (std::filesystem::exists(remapped_path, error))
                        return remapped_path;
                }
                return source_path;
            }

            const std::filesystem::path project_path = std::filesystem::path(Application::GetProjectRootPath()) / source_path;
            if (std::filesystem::exists(project_path, error))
                return project_path;

            error.clear();
            const std::filesystem::path engine_path = engine_root / source_path;
            if (std::filesystem::exists(engine_path, error))
                return engine_path;

            return std::filesystem::absolute(source_path, error);
        }

        bool OpenInVSCode(const std::filesystem::path &source_path, i32 line)
        {
            std::error_code error;
            if (source_path.empty() || line <= 0 || !std::filesystem::exists(source_path, error))
                return false;

            WString workspace_path = source_path.parent_path().wstring();
            if (ProjectManager::Get().HasOpenedProject())
                workspace_path = ProjectManager::Get().CurrentProject().RootDirectory();
            ProcessStartInfo process_start_info(std::format(L"cmd.exe /c code -r --goto \"{}\" \"{}:{}\"",
                                                             workspace_path, source_path.wstring(), line));
            auto process = ProcessFactory::Create();
            return process != nullptr && process->Start(process_start_info);
        }

        const char *ModuleName(i32 module_index)
        {
            return module_index >= 0 && module_index < static_cast<i32>(std::size(kModuleNames))
                       ? kModuleNames[module_index]
                       : kModuleNames[0];
        }

        ELogModule ModuleValue(i32 module_index)
        {
            return static_cast<ELogModule>(std::clamp(module_index - 1, 0, 4));
        }

        String LevelName(ELogLevel level)
        {
            switch (level)
            {
                case ELogLevel::kTrace: return "TRACE";
                case ELogLevel::kDebug: return "DEBUG";
                case ELogLevel::kInfo: return "INFO";
                case ELogLevel::kWarning: return "WARNING";
                case ELogLevel::kError: return "ERROR";
                case ELogLevel::kFatal: return "FATAL";
                default: return "UNKNOWN";
            }
        }

        String ModuleLabel(ELogModule module)
        {
            return kModuleNames[std::clamp(static_cast<i32>(module) + 1, 1, 5)];
        }

        String Lowercase(String value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            });
            return value;
        }

        bool ContainsInsensitive(const String &value, const String &query)
        {
            return Lowercase(value).find(Lowercase(query)) != String::npos;
        }

        String OneLine(String value)
        {
            std::replace(value.begin(), value.end(), '\r', ' ');
            std::replace(value.begin(), value.end(), '\n', ' ');
            return value;
        }

        String FormatTime(const std::chrono::system_clock::time_point &timestamp)
        {
            const auto time = std::chrono::system_clock::to_time_t(timestamp);
            std::tm local_time{};
            localtime_s(&local_time, &time);
            const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch()) % 1000;
            return std::format("{:02}:{:02}:{:02}.{:03}", local_time.tm_hour, local_time.tm_min, local_time.tm_sec,
                               milliseconds.count());
        }

        String SourceLabel(const EditorLogRecord &record)
        {
            if (record._file.empty())
                return {};
            return std::format("{}:{}", record._file, record._line);
        }

        void SetClipboardText(const String &text)
        {
            if (ImGui::GetCurrentContext() != nullptr)
                ImGui::SetClipboardText(text.c_str());
        }
    }

    class OutputLogDataSource final : public UI::TableDataSource
    {
    public:
        explicit OutputLogDataSource(const OutputWindow *window) : _window(window) {}

        i32 GetRowCount() const override
        {
            return _window == nullptr ? 0 : static_cast<i32>(_window->_visible_rows.size());
        }

        String GetCellText(i32 row, i32 column) const override
        {
            if (_window == nullptr || row < 0 || row >= static_cast<i32>(_window->_visible_rows.size()))
                return {};
            const auto &visible_row = _window->_visible_rows[row];
            const auto *record = _window->FindRecord(visible_row._sequence);
            if (record == nullptr)
                return {};
            switch (column)
            {
                case 0: return FormatTime(record->_timestamp);
                case 1: return LevelName(record->_level);
                case 2: return ModuleLabel(record->_module);
                case 3:
                {
                    String text = OneLine(ToChar(record->_message));
                    if (visible_row._repeat_count > 1u)
                        text += std::format(" [x{}]", visible_row._repeat_count);
                    return text;
                }
                case 4: return SourceLabel(*record);
                default: return {};
            }
        }

        bool DrawCell(UI::UIRenderer &renderer, i32 row, i32 column, Vector4f rect, const Matrix4x4f &matrix,
                      bool is_hovered) override
        {
            if (_window == nullptr || row < 0 || row >= static_cast<i32>(_window->_visible_rows.size()))
                return false;
            const auto &visible_row = _window->_visible_rows[row];
            const auto *record = _window->FindRecord(visible_row._sequence);
            if (record == nullptr)
                return false;

            if (column == 4)
            {
                const String source_label = SourceLabel(*record);
                if (source_label.empty())
                    return false;
                const bool link_active = is_hovered && IsControlDown();
                UI::TableRenderer::DrawCellTextColored(renderer, _window->_table->GetStyle(), source_label, rect, matrix,
                                                        link_active ? kSourceLinkColor : _window->_table->GetStyle()._cell._content_color,
                                                        UI::ETableColumnAlignment::kLeft, link_active);
                return true;
            }
            if (column != 1 && column != 2)
                return false;

            const auto &config = EditorApp::GetEditor()->GetEditorConfig();
            Color color = config._output_general_color;
            if (column == 1)
            {
                switch (record->_level)
                {
                    case ELogLevel::kInfo: color = config._output_info_color; break;
                    case ELogLevel::kWarning: color = config._output_warning_color; break;
                    case ELogLevel::kError:
                    case ELogLevel::kFatal: color = config._output_error_color; break;
                    case ELogLevel::kDebug: color = config._output_debug_color; break;
                    case ELogLevel::kTrace: color = config._output_trace_color; break;
                    default: break;
                }
            }
            else
            {
                switch (record->_module)
                {
                    case ELogModule::kGeneral: color = config._output_general_color; break;
                    case ELogModule::kAsset: color = config._output_asset_color; break;
                    case ELogModule::kShader: color = config._output_shader_color; break;
                    case ELogModule::kRHI: color = config._output_rhi_color; break;
                    case ELogModule::kEditor: color = config._output_editor_color; break;
                    default: break;
                }
            }
            UI::TableRenderer::DrawCellTextColored(renderer, _window->_table->GetStyle(), GetCellText(row, column), rect, matrix,
                                                    color, UI::ETableColumnAlignment::kCenter);
            return true;
        }

        bool IsCellLink(i32 row, i32 column) const override
        {
            if (_window == nullptr || column != 4 || row < 0 || row >= static_cast<i32>(_window->_visible_rows.size()))
                return false;
            const auto *record = _window->FindRecord(_window->_visible_rows[row]._sequence);
            return record != nullptr && !record->_file.empty() && record->_line > 0;
        }

        bool OnCellClicked(i32 row, i32 column) override
        {
            if (!IsControlDown() || !IsCellLink(row, column))
                return false;
            const auto *record = _window->FindRecord(_window->_visible_rows[row]._sequence);
            if (record == nullptr)
                return false;
            _window->OpenSourceLocation(*record);
            return true;
        }

    private:
        const OutputWindow *_window = nullptr;
    };

    OutputWindow::OutputWindow() : DockWindow("Output", {900.0f, 360.0f}), _data_source(MakeScope<OutputLogDataSource>(this))
    {
        BuildUI();
    }

    OutputWindow::~OutputWindow()
    {
        if (_table != nullptr)
            _table->SetDataSource(nullptr);
    }

    void OutputWindow::BuildUI()
    {
        auto *root = _content_root->AddChild<UI::VerticalBox>();
        root->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);

        auto *toolbar = root->AddChild<UI::HorizontalBox>();
        toolbar->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size({0.0f, 26.0f});
        auto add_button = [toolbar](const String &text, f32 width)
        {
            auto *button = toolbar->AddChild<UI::Button>(text);
            button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed)
                .Size({width, 24.0f}).Margin(UI::Padding(0.0f, 0.0f, 4.0f, 0.0f));
            return button;
        };
        auto *clear_button = add_button("Clear History", 104.0f);
        clear_button->OnMouseClick() += [this](UI::UIEvent &) {
            if (auto *service = EditorApp::GetEditor()->GetLogService())
                service->ClearHistory();
        };
        _collapse_button = add_button("Collapse: Off", 92.0f);
        _collapse_button->OnMouseClick() += [this](UI::UIEvent &) {
            _filter._collapse_duplicates = !_filter._collapse_duplicates;
            MarkFilterDirty();
        };
        _follow_button = add_button("Follow: On", 82.0f);
        _follow_button->OnMouseClick() += [this](UI::UIEvent &) {
            _filter._follow_tail = !_filter._follow_tail;
            MarkFilterDirty();
        };
        auto *copy_row = add_button("Copy Row", 78.0f);
        copy_row->OnMouseClick() += [this](UI::UIEvent &) { CopySelectedRow(); };
        auto *copy_details = add_button("Copy Details", 96.0f);
        copy_details->OnMouseClick() += [this](UI::UIEvent &) { CopySelectedDetails(); };

        Vector<String> module_items;
        for (const auto *module_name : kModuleNames)
            module_items.emplace_back(module_name);
        _module_dropdown = toolbar->AddChild<UI::Dropdown>(module_items);
        _module_dropdown->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed)
            .Size({120.0f, 24.0f});
        _module_dropdown->SetSelectedIndex(0, false);
        _module_dropdown->_on_selected_changed += [this](i32 index) {
            _filter._module_index = index - 1;
            MarkFilterDirty();
        };

        auto *levels = root->AddChild<UI::HorizontalBox>();
        levels->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size({0.0f, 26.0f});
        const String level_names[] = {"Info", "Warning", "Error/Fatal", "Debug", "Trace"};
        for (u32 level_index = 0; level_index < 5u; ++level_index)
        {
            auto *button = levels->AddChild<UI::Button>(level_names[level_index]);
            button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed)
                .Size({100.0f, 24.0f}).Margin(UI::Padding(0.0f, 0.0f, 4.0f, 0.0f));
            button->OnMouseClick() += [this, level_index](UI::UIEvent &) { SetLevelVisible(level_index); };
            _level_buttons.push_back(button);
        }

        auto *search_row = root->AddChild<UI::HorizontalBox>();
        search_row->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size({0.0f, 28.0f});
        auto *search_label = search_row->AddChild<UI::Text>("Search");
        search_label->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
            .Size({48.0f, 0.0f});
        _search_input = search_row->AddChild<UI::InputBlock>();
        _search_input->SetCommitLogEnabled(false);
        _search_input->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
        _search_input->_on_content_changed += [this](const String &content) {
            _filter._search_text = content;
            MarkFilterDirty();
        };

        _main_split = root->AddChild<UI::SplitView>();
        _main_split->_is_horizontal = false;
        _main_split->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
        _main_split->SetRatio(kDefaultOutputSplitRatio);

        _table = _main_split->AddChild<UI::UITableElement>();
        const String column_names[] = {"Time", "Level", "Module", "Message", "Source"};
        const f32 column_widths[] = {94.0f, 86.0f, 86.0f, 420.0f, 240.0f};
        for (u32 index = 0; index < 5u; ++index)
        {
            UI::TableColumn column;
            column._name = column_names[index];
            column._width = column_widths[index];
            column._stretch = index == 3u;
            _table->AddColumn(column);
        }
        _table->SetHorizontalScrollEnabled(false);
        _table->_on_selection_changed += [this](i32 row) { RefreshDetails(row); };
        _table->SetDataSource(_data_source.get());
        _table->OnMouseDown() += [this](UI::UIEvent &event)
        {
            if (event._key_code != EKey::kRBUTTON)
                return;
            Vector<PopupMenuAction> actions;
            actions.push_back({"Copy", [this]()
            {
                if (_table->HasTextSelection())
                    _table->CopyTextSelectionToClipboard();
                else
                    CopySelectedRow();
            }});
            actions.push_back({"Select All", [this]() { _table->SelectAllText(); }});
            actions.push_back({"Copy Row", [this]() { CopySelectedRow(); }});
            actions.push_back({"Copy Details", [this]() { CopySelectedDetails(); }});
            EditorPopup::ShowActionMenuAt(event._mouse_position, actions);
            event._is_handled = true;
        };

        _details_scroll = _main_split->AddChild<UI::ScrollView>();
        _details_content = _details_scroll->AddChild<UI::VerticalBox>();
        _details_content->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
        _details_text = _details_content->AddChild<UI::Text>("Select a row to inspect details.");
        _details_text->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto)
            .Margin(UI::Padding(4.0f));
        _details_text->SetSelectable(true);
        RefreshToolbarLabels();
    }

    void OutputWindow::Update(f32 dt)
    {
        DockWindow::Update(dt);
        static const EditorConfig *s_previous_config = nullptr;
        static EditorConfig s_previous_colors;
        const EditorConfig &config = EditorApp::GetEditor()->GetEditorConfig();
        const bool colors_changed = s_previous_config != &config ||
            !NearbyEqual(s_previous_colors._output_info_color, config._output_info_color) ||
            !NearbyEqual(s_previous_colors._output_warning_color, config._output_warning_color) ||
            !NearbyEqual(s_previous_colors._output_error_color, config._output_error_color) ||
            !NearbyEqual(s_previous_colors._output_debug_color, config._output_debug_color) ||
            !NearbyEqual(s_previous_colors._output_trace_color, config._output_trace_color) ||
            !NearbyEqual(s_previous_colors._output_general_color, config._output_general_color) ||
            !NearbyEqual(s_previous_colors._output_asset_color, config._output_asset_color) ||
            !NearbyEqual(s_previous_colors._output_shader_color, config._output_shader_color) ||
            !NearbyEqual(s_previous_colors._output_rhi_color, config._output_rhi_color) ||
            !NearbyEqual(s_previous_colors._output_editor_color, config._output_editor_color);
        if (colors_changed && _table != nullptr)
        {
            _table->InvalidatePaint();
            s_previous_config = &config;
            s_previous_colors = config;
        }
        auto *service = EditorApp::GetEditor()->GetLogService();
        if (service == nullptr)
            return;
        service->DrainPending(512u, std::chrono::milliseconds(2));
        if (_filter_dirty || service->Revision() != _observed_revision)
            RefreshView();
    }

    void OutputWindow::MarkFilterDirty()
    {
        _filter_dirty = true;
    }

    bool OutputWindow::PassesFilter(const EditorLogRecord &record) const
    {
        bool level_visible = false;
        switch (record._level)
        {
            case ELogLevel::kInfo: level_visible = _filter._show_info; break;
            case ELogLevel::kWarning: level_visible = _filter._show_warning; break;
            case ELogLevel::kError:
            case ELogLevel::kFatal: level_visible = _filter._show_error; break;
            case ELogLevel::kDebug: level_visible = _filter._show_debug; break;
            case ELogLevel::kTrace: level_visible = _filter._show_trace; break;
            default: break;
        }
        if (!level_visible || (_filter._module_index >= 0 && record._module != ModuleValue(_filter._module_index + 1)))
            return false;
        if (_filter._search_text.empty())
            return true;
        const String search = _filter._search_text;
        return ContainsInsensitive(ToChar(record._message), search) || ContainsInsensitive(record._file, search) ||
               ContainsInsensitive(record._function, search) || ContainsInsensitive(ModuleLabel(record._module), search);
    }

    const EditorLogRecord *OutputWindow::FindRecord(u64 sequence) const
    {
        const auto record_it = std::find_if(_records.begin(), _records.end(), [sequence](const EditorLogRecord &record)
        {
            return record._sequence == sequence;
        });
        return record_it == _records.end() ? nullptr : &*record_it;
    }

    void OutputWindow::RefreshView()
    {
        auto *service = EditorApp::GetEditor()->GetLogService();
        if (service == nullptr)
            return;
        const u64 selected_sequence = _selected_sequence;
        const bool should_follow = _filter._follow_tail && _table != nullptr && _table->IsAtBottom();
        service->GetSnapshot(_records);
        _visible_rows.clear();
        for (const auto &record : _records)
        {
            if (!PassesFilter(record))
                continue;
            if (_filter._collapse_duplicates && !_visible_rows.empty())
            {
                auto &last = _visible_rows.back();
                const auto *last_record = FindRecord(last._last_sequence);
                if (last_record != nullptr && last_record->_level == record._level && last_record->_module == record._module &&
                    last_record->_message == record._message && last_record->_file == record._file && last_record->_line == record._line)
                {
                    last._last_sequence = record._sequence;
                    ++last._repeat_count;
                    continue;
                }
            }
            _visible_rows.push_back({record._sequence, record._sequence, 1u});
        }
        const auto selected_it = std::find_if(_visible_rows.begin(), _visible_rows.end(), [selected_sequence](const VisibleLogRow &row)
        {
            return row._sequence == selected_sequence;
        });
        if (selected_sequence != 0u && selected_it != _visible_rows.end())
        {
            const i32 selected_row = static_cast<i32>(std::distance(_visible_rows.begin(), selected_it));
            _table->SetSelectedRow(selected_row);
            RefreshDetails(selected_row);
        }
        else
        {
            _table->ClearSelection();
            RefreshDetails(-1);
        }
        _table->Refresh();
        if (should_follow)
            _table->RequestScrollToBottom();
        _observed_revision = service->Revision();
        _filter_dirty = false;
        RefreshToolbarLabels();
    }

    void OutputWindow::RefreshToolbarLabels()
    {
        if (_collapse_button != nullptr)
            _collapse_button->SetText(_filter._collapse_duplicates ? "Collapse: On" : "Collapse: Off", false);
        if (_follow_button != nullptr)
            _follow_button->SetText(_filter._follow_tail ? "Follow: On" : "Follow: Off", false);
        if (_module_dropdown != nullptr && _module_dropdown->GetSelectedIndex() != _filter._module_index + 1)
            _module_dropdown->SetSelectedIndex(_filter._module_index + 1, false);
        u64 mutable_counts[5] = {};
        for (const auto &record : _records)
        {
            switch (record._level)
            {
                case ELogLevel::kInfo: ++mutable_counts[0]; break;
                case ELogLevel::kWarning: ++mutable_counts[1]; break;
                case ELogLevel::kError:
                case ELogLevel::kFatal: ++mutable_counts[2]; break;
                case ELogLevel::kDebug: ++mutable_counts[3]; break;
                case ELogLevel::kTrace: ++mutable_counts[4]; break;
                default: break;
            }
        }
        const bool visible[] = {_filter._show_info, _filter._show_warning, _filter._show_error, _filter._show_debug, _filter._show_trace};
        const String names[] = {"Info", "Warning", "Error/Fatal", "Debug", "Trace"};
        for (u32 index = 0; index < _level_buttons.size(); ++index)
            _level_buttons[index]->SetText(std::format("{} {} ({})", visible[index] ? "[x]" : "[ ]", names[index], mutable_counts[index]), false);
    }

    void OutputWindow::RefreshDetails(i32 row)
    {
        if (row < 0 || row >= static_cast<i32>(_visible_rows.size()))
        {
            _selected_sequence = 0;
            _details_text->SetText("Select a row to inspect details.", false);
            return;
        }
        _selected_sequence = _visible_rows[row]._sequence;
        const auto *record = FindRecord(_selected_sequence);
        if (record != nullptr)
            _details_text->SetText(FormatDetails(*record, _visible_rows[row]._repeat_count), false);
    }

    String OutputWindow::FormatRecord(const EditorLogRecord &record, u32 repeat_count) const
    {
        String text = std::format("[{}] [{}] {}", FormatTime(record._timestamp), LevelName(record._level), OneLine(ToChar(record._message)));
        if (repeat_count > 1u)
            text += std::format(" [x{}]", repeat_count);
        return text;
    }

    String OutputWindow::FormatDetails(const EditorLogRecord &record, u32 repeat_count) const
    {
        std::ostringstream thread_id;
        thread_id << record._thread_id;
        return std::format("{}\nModule: {}\nSource: {}\nFunction: {}\nThread: {} ({})\nMessage:\n{}",
                           FormatRecord(record, repeat_count), ModuleLabel(record._module), SourceLabel(record), record._function,
                           record._thread_name.empty() ? "unnamed" : record._thread_name, thread_id.str(), ToChar(record._message));
    }

    void OutputWindow::SetLevelVisible(u32 level_index)
    {
        bool *value = nullptr;
        switch (level_index)
        {
            case 0: value = &_filter._show_info; break;
            case 1: value = &_filter._show_warning; break;
            case 2: value = &_filter._show_error; break;
            case 3: value = &_filter._show_debug; break;
            case 4: value = &_filter._show_trace; break;
            default: return;
        }
        *value = !*value;
        MarkFilterDirty();
    }

    void OutputWindow::CopySelectedRow()
    {
        const auto *record = FindRecord(_selected_sequence);
        if (record != nullptr)
        {
            const auto row_it = std::find_if(_visible_rows.begin(), _visible_rows.end(), [this](const VisibleLogRow &row)
            {
                return row._sequence == _selected_sequence;
            });
            SetClipboardText(FormatRecord(*record, row_it == _visible_rows.end() ? 1u : row_it->_repeat_count));
        }
    }

    void OutputWindow::OpenSourceLocation(const EditorLogRecord &record) const
    {
        const std::filesystem::path source_path = ResolveSourcePath(record);
        if (!OpenInVSCode(source_path, record._line))
            LOG_WARNING(L"Output: open source location in VSCode failed: {}:{}", source_path.wstring(), record._line);
    }

    void OutputWindow::CopySelectedDetails()
    {
        const auto *record = FindRecord(_selected_sequence);
        if (record != nullptr)
        {
            const auto row_it = std::find_if(_visible_rows.begin(), _visible_rows.end(), [this](const VisibleLogRow &row)
            {
                return row._sequence == _selected_sequence;
            });
            SetClipboardText(FormatDetails(*record, row_it == _visible_rows.end() ? 1u : row_it->_repeat_count));
        }
    }

    void OutputWindow::SaveDockLayoutState(JsonArchive &ar)
    {
        ar.BeginObject("_show_info"); ar << _filter._show_info; ar.EndObject();
        ar.BeginObject("_show_warning"); ar << _filter._show_warning; ar.EndObject();
        ar.BeginObject("_show_error"); ar << _filter._show_error; ar.EndObject();
        ar.BeginObject("_show_debug"); ar << _filter._show_debug; ar.EndObject();
        ar.BeginObject("_show_trace"); ar << _filter._show_trace; ar.EndObject();
        ar.BeginObject("_collapse_duplicates"); ar << _filter._collapse_duplicates; ar.EndObject();
        ar.BeginObject("_follow_tail"); ar << _filter._follow_tail; ar.EndObject();
        ar.BeginObject("_search_text"); ar << _filter._search_text; ar.EndObject();
        ar.BeginObject("_module_index"); ar << _filter._module_index; ar.EndObject();
        if (_main_split != nullptr)
        {
            f32 split_ratio = _main_split->GetRatio();
            ar.BeginObject("_split_ratio"); ar << split_ratio; ar.EndObject();
        }
    }

    void OutputWindow::LoadDockLayoutState(JsonArchive &ar)
    {
        auto read_bool = [&ar](const char *name, bool &value)
        {
            if (ar.HasField(name)) { ar.BeginObject(name); ar >> value; ar.EndObject(); }
        };
        read_bool("_show_info", _filter._show_info);
        read_bool("_show_warning", _filter._show_warning);
        read_bool("_show_error", _filter._show_error);
        read_bool("_show_debug", _filter._show_debug);
        read_bool("_show_trace", _filter._show_trace);
        read_bool("_collapse_duplicates", _filter._collapse_duplicates);
        read_bool("_follow_tail", _filter._follow_tail);
        if (ar.HasField("_search_text")) { ar.BeginObject("_search_text"); ar >> _filter._search_text; ar.EndObject(); }
        if (ar.HasField("_module_index")) { ar.BeginObject("_module_index"); ar >> _filter._module_index; ar.EndObject(); }
        if (ar.HasField("_split_ratio"))
        {
            f32 split_ratio = kDefaultOutputSplitRatio;
            ar.BeginObject("_split_ratio"); ar >> split_ratio; ar.EndObject();
            if (_main_split != nullptr)
                _main_split->SetRatio(split_ratio);
        }
    }

    void OutputWindow::OnDockLayoutLoaded()
    {
        _search_input->SetContent(_filter._search_text, false);
        _module_dropdown->SetSelectedIndex(_filter._module_index + 1, false);
        MarkFilterDirty();
        RefreshToolbarLabels();
    }
}
