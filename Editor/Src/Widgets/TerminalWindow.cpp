#include "Widgets/TerminalWindow.h"

#include "Common/EditorPopup.h"
#include "EditorApp.h"
#include "Framework/Common/KeyCode.h"
#include "Objects/JsonArchive.h"
#include "UI/Basic.h"
#include "UI/Container.h"
#include "UI/Element/UITableElement.h"
#include "UI/UIFramework.h"

#include <algorithm>
#include <sstream>

namespace Ailu::Editor
{
    namespace
    {
        String LinePrefix(ETerminalLineChannel channel)
        {
            switch (channel)
            {
                case ETerminalLineChannel::kPrompt: return "> ";
                case ETerminalLineChannel::kError: return "! ";
                case ETerminalLineChannel::kStatus: return "[status] ";
                default: return "  ";
            }
        }

        String TaskStateText(EEditorTaskState state)
        {
            switch (state)
            {
                case EEditorTaskState::kStarting: return "Starting";
                case EEditorTaskState::kRunning: return "Running";
                case EEditorTaskState::kExitedDraining: return "Draining";
                case EEditorTaskState::kCanceling: return "Canceling";
                case EEditorTaskState::kSucceeded: return "Exit 0";
                case EEditorTaskState::kFailed: return "Failed";
                case EEditorTaskState::kCanceled: return "Canceled";
                case EEditorTaskState::kLaunchFailed: return "LaunchFailed";
                default: return "Queued";
            }
        }
    }

    class TerminalTranscriptDataSource final : public UI::TableDataSource
    {
    public:
        explicit TerminalTranscriptDataSource(const TerminalWindow *window) : _window(window) {}

        i32 GetRowCount() const override
        {
            const auto *transcript = _window == nullptr ? nullptr : _window->DisplayedTranscript();
            return transcript == nullptr ? 0 : static_cast<i32>(transcript->Lines().size());
        }

        String GetCellText(i32 row, i32 column) const override
        {
            const auto *transcript = _window == nullptr ? nullptr : _window->DisplayedTranscript();
            if (transcript == nullptr || column != 0 || row < 0 || row >= static_cast<i32>(transcript->Lines().size()))
                return {};
            const auto &line = transcript->Lines()[row];
            return LinePrefix(line._channel) + line._text;
        }

    private:
        const TerminalWindow *_window = nullptr;
    };

    TerminalWindow::TerminalWindow() : DockWindow("Terminal", {900.0f, 360.0f}),
                                       _data_source(MakeScope<TerminalTranscriptDataSource>(this))
    {
        BuildUI();
    }

    TerminalWindow::~TerminalWindow()
    {
        if (_table != nullptr)
            _table->SetDataSource(nullptr);
    }

    void TerminalWindow::BuildUI()
    {
        auto *root = _content_root->AddChild<UI::VerticalBox>();
        root->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);

        auto *toolbar = root->AddChild<UI::HorizontalBox>();
        toolbar->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
            .Size({0.0f, 28.0f});
        auto *engine_tab = toolbar->AddChild<UI::Button>("Engine");
        engine_tab->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed)
            .Size({72.0f, 24.0f}).Margin(UI::Padding(0.0f, 0.0f, 4.0f, 0.0f));
        engine_tab->OnMouseClick() += [this](UI::UIEvent &) { SetWindowMode(ETerminalWindowMode::kEngine); };
        auto *tasks_tab = toolbar->AddChild<UI::Button>("Tasks");
        tasks_tab->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed)
            .Size({72.0f, 24.0f}).Margin(UI::Padding(0.0f, 0.0f, 4.0f, 0.0f));
        tasks_tab->OnMouseClick() += [this](UI::UIEvent &) { SetWindowMode(ETerminalWindowMode::kTasks); };
        _run_button = toolbar->AddChild<UI::Button>("Run");
        _run_button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed)
            .Size({64.0f, 24.0f}).Margin(UI::Padding(0.0f, 0.0f, 4.0f, 0.0f));
        _run_button->OnMouseClick() += [this](UI::UIEvent &) { ExecuteInput(_input->GetContent()); };
        _engine_clear_button = toolbar->AddChild<UI::Button>("Clear");
        _engine_clear_button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed)
            .Size({64.0f, 24.0f});
        _engine_clear_button->OnMouseClick() += [this](UI::UIEvent &)
        {
            _transcript.Clear();
            SetStatus("Transcript cleared");
            RefreshTable();
        };

        _task_panel = root->AddChild<UI::VerticalBox>();
        _task_panel->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
            .Size({0.0f, 230.0f});
        BuildTaskUI(_task_panel);

        _table = root->AddChild<UI::UITableElement>();
        _table->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
        UI::TableColumn column;
        column._name = "Engine transcript";
        column._width = 760.0f;
        column._stretch = true;
        _table->AddColumn(column);
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
                    _table->CopySelectedRowsToClipboard();
            }, false, {}, _table->HasTextSelection() || !_table->GetSelectedRows().empty(), false});
            actions.push_back({"Select All", [this]() { _table->SelectAllText(); }});
            EditorPopup::ShowActionMenuAt(event._mouse_position, actions);
            event._is_handled = true;
        };

        _completion_list = root->AddChild<UI::ListView>();
        _completion_list->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
            .Size({0.0f, 0.0f});
        _completion_list->SetStyleId("DropdownPopup");
        _completion_list->_on_item_clicked += [this](UI::UIElement *, i32 index)
        {
            _completion_index = index;
            AcceptCompletion();
        };
        _completion_list->SetVisible(false);

        auto *input_row = root->AddChild<UI::HorizontalBox>();
        input_row->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
            .Size({0.0f, 28.0f});
        _input = input_row->AddChild<UI::InputBlock>();
        _input->SetCommitLogEnabled(false);
        _input->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill)
            .Margin(UI::Padding(0.0f, 0.0f, 4.0f, 0.0f));
        _input->_on_content_changed += [this](const String &)
        {
            RefreshCompletions();
        };
        _input->_on_submit += [this](const String &content)
        {
            if (_completion_list != nullptr && _completion_list->IsVisible())
                AcceptCompletion();
            else
                ExecuteInput(content);
        };
        _input->OnKeyDown() += [this](UI::UIEvent &event) { HandleInputKey(event); };

        _status = root->AddChild<UI::Text>("Engine console ready. Type help.");
        _status->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
            .Size({0.0f, 22.0f});
        SetStatus("Engine console ready. Type help.");
        SetWindowMode(ETerminalWindowMode::kEngine);
    }

    void TerminalWindow::BuildTaskUI(UI::VerticalBox *root)
    {
        auto add_input_row = [root](const String &label, UI::InputBlock *&input, const String &content)
        {
            auto *row = root->AddChild<UI::HorizontalBox>();
            row->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                .Size({0.0f, 26.0f});
            row->AddChild<UI::Text>(label)->GetSlotAs<UI::LinearSlot>()
                .SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size({100.0f, 0.0f});
            input = row->AddChild<UI::InputBlock>(content);
            input->SetCommitLogEnabled(false);
            input->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
        };

        auto *mode_row = root->AddChild<UI::HorizontalBox>();
        mode_row->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
            .Size({0.0f, 26.0f});
        mode_row->AddChild<UI::Text>("Mode")->GetSlotAs<UI::LinearSlot>()
            .SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size({100.0f, 0.0f});
        _task_mode = mode_row->AddChild<UI::Dropdown>(Vector<String>{"Direct", "cmd.exe", "PowerShell"});
        _task_mode->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
        _task_mode->SetSelectedIndex(1, false);
        _task_mode->_on_selected_changed += [this](i32 index)
        {
            if (_task_executable != nullptr)
                _task_executable->SetVisible(index == 0);
            if (_task_arguments != nullptr)
                _task_arguments->SetVisible(index == 0);
            if (_task_command != nullptr)
                _task_command->SetVisible(index != 0);
        };

        auto *selector_row = root->AddChild<UI::HorizontalBox>();
        selector_row->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
            .Size({0.0f, 26.0f});
        selector_row->AddChild<UI::Text>("Task")->GetSlotAs<UI::LinearSlot>()
            .SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size({100.0f, 0.0f});
        _task_selector = selector_row->AddChild<UI::Dropdown>(Vector<String>{"No tasks"});
        _task_selector->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
        _task_selector->_on_selected_changed += [this](i32 index)
        {
            auto *service = EditorApp::GetEditor()->GetTaskService();
            if (service == nullptr)
                return;
            const Vector<u64> ids = service->GetTaskIds();
            _selected_task_id = index >= 0 && index < static_cast<i32>(ids.size()) ? ids[index] : 0u;
            RefreshTaskView();
        };

        add_input_row("Executable", _task_executable, "");
        add_input_row("Arguments", _task_arguments, "");
        add_input_row("Command", _task_command, "echo hello");
        add_input_row("Directory", _task_working_directory, ToChar(Application::GetProjectRootPath()));

        auto *buttons = root->AddChild<UI::HorizontalBox>();
        buttons->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
            .Size({0.0f, 26.0f});
        _task_run_button = buttons->AddChild<UI::Button>("Run Task");
        _task_run_button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed)
            .Size({90.0f, 24.0f}).Margin(UI::Padding(0.0f, 0.0f, 4.0f, 0.0f));
        _task_run_button->OnMouseClick() += [this](UI::UIEvent &) { RunTask(); };
        _task_cancel_button = buttons->AddChild<UI::Button>("Cancel");
        _task_cancel_button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed)
            .Size({72.0f, 24.0f}).Margin(UI::Padding(0.0f, 0.0f, 4.0f, 0.0f));
        _task_cancel_button->OnMouseClick() += [this](UI::UIEvent &) { CancelTask(); };
        _task_clear_button = buttons->AddChild<UI::Button>("Clear View");
        _task_clear_button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed)
            .Size({84.0f, 24.0f});
        _task_clear_button->OnMouseClick() += [this](UI::UIEvent &)
        {
            if (auto *service = EditorApp::GetEditor()->GetTaskService(); service != nullptr)
                service->ClearTranscript(_selected_task_id);
            RefreshTaskView();
        };
        _task_status = root->AddChild<UI::Text>("One-shot tasks only; no persistent shell session");
        _task_status->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
            .Size({0.0f, 22.0f});
        _task_executable->SetVisible(false);
        _task_arguments->SetVisible(false);
        _task_command->SetVisible(true);
    }

    void TerminalWindow::Update(f32 dt)
    {
        DockWindow::Update(dt);
        if (_refocus_input && _input != nullptr && UI::UIManager::Get() != nullptr)
        {
            UI::UIManager::Get()->SetFocus(_input);
            _refocus_input = false;
        }
        if (auto *service = EditorApp::GetEditor()->GetTaskService(); service != nullptr)
        {
            if (_observed_task_list_revision != service->TaskListRevision())
                RefreshTaskSelector();
            if (_mode == ETerminalWindowMode::kTasks)
            {
                const EditorTaskStatus status = service->GetStatus(_selected_task_id);
                if (_observed_task_revision != status._revision)
                    RefreshTaskView();
            }
        }
        if (_mode == ETerminalWindowMode::kEngine && _observed_revision != _transcript.Revision())
            RefreshTable();
    }

    void TerminalWindow::ExecuteInput(const String &line)
    {
        if (line.empty())
            return;
        HideCompletions();
        if (_history.empty() || _history.back() != line)
        {
            _history.push_back(line);
            if (_history.size() > 100u)
                _history.erase(_history.begin());
        }
        _history_index = -1;
        _history_edit.clear();
        AppendMessage(ETerminalLineChannel::kPrompt, line);

        ConsoleCommandResult command_result;
        if (auto *registry = EditorApp::GetEditor()->GetConsoleCommandRegistry(); registry != nullptr)
            command_result = registry->Execute(line);
        else
            command_result = {false, "Engine command registry is unavailable", false, false};
        if (command_result._clear_transcript)
        {
            _transcript.Clear();
            SetStatus("Transcript cleared");
        }
        else
        {
            if (!command_result._message.empty())
                AppendMessage(command_result._success ? ETerminalLineChannel::kOutput : ETerminalLineChannel::kError,
                              command_result._message);
            SetStatus(command_result._success ? "Command completed" : "Command failed");
        }
        _input->SetContent({}, false);
        _input->SetCursorToEnd();
        _refocus_input = true;
        RefreshTable();
    }

    void TerminalWindow::AppendMessage(ETerminalLineChannel channel, const String &message)
    {
        std::stringstream stream(message);
        String line;
        bool appended = false;
        while (std::getline(stream, line))
        {
            _transcript.Append(channel, line);
            appended = true;
        }
        if (!appended)
            _transcript.Append(channel, {});
    }

    void TerminalWindow::HandleInputKey(UI::UIEvent &event)
    {
        if (_completion_list != nullptr && _completion_list->IsVisible())
        {
            switch (event._key_code)
            {
                case EKey::kUP:
                    MoveCompletion(-1);
                    event._is_handled = true;
                    return;
                case EKey::kDOWN:
                    MoveCompletion(1);
                    event._is_handled = true;
                    return;
                case EKey::kTAB:
                case EKey::kRETURN:
                    AcceptCompletion();
                    event._is_handled = true;
                    return;
                case EKey::kESCAPE:
                    HideCompletions();
                    event._is_handled = true;
                    return;
                default:
                    break;
            }
        }
        switch (event._key_code)
        {
            case EKey::kUP:
                NavigateHistory(-1);
                event._is_handled = true;
                break;
            case EKey::kDOWN:
                NavigateHistory(1);
                event._is_handled = true;
                break;
            case EKey::kTAB:
                RefreshCompletions();
                event._is_handled = true;
                break;
            default:
                break;
        }
    }

    void TerminalWindow::NavigateHistory(i32 direction)
    {
        if (_history.empty())
            return;
        if (_history_index < 0)
        {
            if (direction > 0)
                return;
            _history_edit = _input->GetContent();
            _history_index = static_cast<i32>(_history.size()) - 1;
        }
        else
        {
            _history_index += direction;
            if (_history_index < 0)
                _history_index = 0;
            if (_history_index >= static_cast<i32>(_history.size()))
            {
                _history_index = -1;
                _input->SetContent(_history_edit, false);
                _input->SetCursorToEnd();
                return;
            }
        }
        _input->SetContent(_history[_history_index], false);
        _input->SetCursorToEnd();
    }

    void TerminalWindow::RefreshCompletions()
    {
        if (_mode != ETerminalWindowMode::kEngine || _input == nullptr || _completion_list == nullptr)
        {
            HideCompletions();
            return;
        }
        const String prefix = _input->GetContent();
        if (prefix.empty() || prefix.find_first_of(" \t") != String::npos)
        {
            HideCompletions();
            return;
        }
        auto *registry = EditorApp::GetEditor()->GetConsoleCommandRegistry();
        if (registry == nullptr)
        {
            HideCompletions();
            return;
        }

        _completion_items = registry->Complete(prefix);
        _completion_list->ClearItems();
        _completion_list->ResetScrollOffset();
        if (_completion_items.empty())
        {
            HideCompletions();
            return;
        }

        constexpr f32 kCompletionRowHeight = 24.0f;
        constexpr f32 kCompletionMaxHeight = 220.0f;
        for (u32 index = 0u; index < _completion_items.size(); ++index)
        {
            auto item = MakeRef<UI::HorizontalBox>();
            item->SlotPadding() = UI::Padding(6.0f, 2.0f, 6.0f, 2.0f);
            auto *label = item->AddChild<UI::Text>(_completion_items[index]);
            label->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            label->_horizontal_align = UI::EAlignment::kLeft;
            label->_vertical_align = UI::EAlignment::kCenter;
            item->OnMouseEnter() += [this, index](UI::UIEvent &)
            {
                _completion_index = static_cast<i32>(index);
                if (_completion_list != nullptr)
                    _completion_list->SetSelectedIndex(_completion_index);
            };
            item->OnMouseClick() += [this, index](UI::UIEvent &event)
            {
                _completion_index = static_cast<i32>(index);
                AcceptCompletion();
                event._is_handled = true;
            };
            _completion_list->AddItem(item);
            item->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                .Size({0.0f, kCompletionRowHeight});
        }
        _completion_index = 0;
        _completion_list->SetSelectedIndex(_completion_index);
        _completion_list->GetSlotAs<UI::LinearSlot>().Size(
            {0.0f, std::min(kCompletionMaxHeight,
                            kCompletionRowHeight * static_cast<f32>(_completion_items.size()))});
        _completion_list->SetVisible(true);
        _completion_list->InvalidateLayout();
    }

    void TerminalWindow::MoveCompletion(i32 direction)
    {
        if (_completion_items.empty())
            return;
        _completion_index = (_completion_index + direction + static_cast<i32>(_completion_items.size())) %
                            static_cast<i32>(_completion_items.size());
        if (_completion_list != nullptr)
        {
            _completion_list->SetSelectedIndex(_completion_index);
            _completion_list->ScrollItemIntoView(_completion_index);
        }
    }

    void TerminalWindow::AcceptCompletion()
    {
        if (_input == nullptr || _completion_index < 0 ||
            _completion_index >= static_cast<i32>(_completion_items.size()))
            return;
        const String completion = _completion_items[_completion_index];
        HideCompletions();
        _input->SetContent(completion, false);
        _input->SetCursorToEnd();
        _refocus_input = true;
    }

    void TerminalWindow::HideCompletions()
    {
        _completion_items.clear();
        _completion_index = -1;
        if (_completion_list == nullptr)
            return;
        _completion_list->ClearItems();
        _completion_list->SetVisible(false);
        _completion_list->GetSlotAs<UI::LinearSlot>().Size({0.0f, 0.0f});
    }

    void TerminalWindow::RefreshTable()
    {
        if (_table != nullptr)
            _table->Refresh();
        if (_mode == ETerminalWindowMode::kEngine)
            _observed_revision = _transcript.Revision();
        else if (const auto *transcript = DisplayedTranscript(); transcript != nullptr)
            _observed_task_revision = transcript->Revision();
        if (_table != nullptr)
            _table->RequestScrollToBottom();
    }

    const TerminalTranscript *TerminalWindow::DisplayedTranscript() const
    {
        if (_mode == ETerminalWindowMode::kEngine)
            return &_transcript;
        const auto *service = EditorApp::GetEditor()->GetTaskService();
        return service == nullptr ? nullptr : service->GetTranscript(_selected_task_id);
    }

    void TerminalWindow::SetWindowMode(ETerminalWindowMode mode)
    {
        _mode = mode;
        const bool tasks = mode == ETerminalWindowMode::kTasks;
        if (tasks)
            HideCompletions();
        if (_task_panel != nullptr)
        {
            _task_panel->GetSlotAs<UI::LinearSlot>().Size({0.0f, tasks ? 230.0f : 0.0f});
            _task_panel->SetVisible(tasks);
        }
        if (_input != nullptr)
            _input->SetVisible(!tasks);
        if (_run_button != nullptr)
            _run_button->SetVisible(!tasks);
        if (_engine_clear_button != nullptr)
            _engine_clear_button->SetVisible(!tasks);
        if (tasks)
        {
            RefreshTaskSelector();
            RefreshTaskView();
        }
        else
            RefreshTable();
    }

    void TerminalWindow::RefreshTaskSelector()
    {
        auto *service = EditorApp::GetEditor()->GetTaskService();
        if (service == nullptr || _task_selector == nullptr)
            return;
        const Vector<u64> ids = service->GetTaskIds();
        Vector<String> items;
        items.reserve(std::max<size_t>(ids.size(), 1u));
        i32 selected_index = -1;
        for (i32 index = 0; index < static_cast<i32>(ids.size()); ++index)
        {
            const EditorTaskStatus status = service->GetStatus(ids[index]);
            items.emplace_back(std::to_string(ids[index]) + " " + status._label + " [" +
                               TaskStateText(status._state) + "]");
            if (ids[index] == _selected_task_id)
                selected_index = index;
        }
        if (items.empty())
        {
            items.emplace_back("No tasks");
            _selected_task_id = 0u;
            selected_index = 0;
        }
        else if (selected_index < 0)
        {
            selected_index = 0;
            _selected_task_id = ids.front();
        }
        _task_selector->SetItems(items);
        _task_selector->SetSelectedIndex(selected_index, false);
        _observed_task_list_revision = service->TaskListRevision();
        RefreshTaskView();
    }

    void TerminalWindow::RefreshTaskView()
    {
        auto *service = EditorApp::GetEditor()->GetTaskService();
        if (service == nullptr)
            return;
        const EditorTaskStatus status = service->GetStatus(_selected_task_id);
        _observed_task_revision = status._revision;
        if (_task_status != nullptr)
        {
            if (_selected_task_id == 0u)
                _task_status->SetText("One-shot tasks only; no persistent shell session", false);
            else
                _task_status->SetText(TaskStateText(status._state) + " | output=" +
                                          std::to_string(status._output_bytes) + " bytes | dropped=" +
                                          std::to_string(status._dropped_output_bytes + status._transcript_dropped_bytes) +
                                          " bytes" + (status._error.empty() ? String{} : " | " + status._error),
                                      false);
        }
        const bool cancellable = status._state == EEditorTaskState::kStarting ||
                                 status._state == EEditorTaskState::kRunning ||
                                 status._state == EEditorTaskState::kExitedDraining;
        if (_task_cancel_button != nullptr)
            _task_cancel_button->SetInteractiveEnabled(cancellable);
        if (_task_clear_button != nullptr)
            _task_clear_button->SetInteractiveEnabled(_selected_task_id != 0u);
        RefreshTable();
    }

    void TerminalWindow::RunTask()
    {
        auto *service = EditorApp::GetEditor()->GetTaskService();
        if (service == nullptr)
            return;
        EditorTaskRequest request;
        request._label = "User Task";
        request._working_directory = ToWChar(_task_working_directory == nullptr ? String{} : _task_working_directory->GetContent());
        const i32 mode_index = _task_mode == nullptr ? 1 : _task_mode->GetSelectedIndex();
        if (mode_index == 0)
        {
            request._mode = EEditorTaskLaunchMode::kDirect;
            request._executable = ToWChar(_task_executable->GetContent());
            String parse_error;
            request._arguments = EditorTaskService::ParseWindowsArguments(ToWChar(_task_arguments->GetContent()), parse_error);
            if (!parse_error.empty())
            {
                SetStatus(parse_error);
                return;
            }
        }
        else
        {
            request._mode = mode_index == 1 ? EEditorTaskLaunchMode::kCmd : EEditorTaskLaunchMode::kPowerShell;
            request._command = ToWChar(_task_command->GetContent());
        }
        String error;
        _selected_task_id = service->StartTask(request, error);
        if (_selected_task_id == 0u)
            SetStatus(error.empty() ? "Failed to start external task" : error);
        else
        {
            _observed_task_list_revision = 0u;
            RefreshTaskSelector();
            SetWindowMode(ETerminalWindowMode::kTasks);
        }
    }

    void TerminalWindow::CancelTask()
    {
        auto *service = EditorApp::GetEditor()->GetTaskService();
        if (service != nullptr && _selected_task_id != 0u)
            service->CancelTask(_selected_task_id);
        RefreshTaskView();
    }

    void TerminalWindow::SetStatus(const String &message)
    {
        if (_status != nullptr)
            _status->SetText(message, false);
    }

    void TerminalWindow::SaveDockLayoutState(JsonArchive &ar)
    {
        (void) ar;
    }

    void TerminalWindow::LoadDockLayoutState(JsonArchive &ar)
    {
        (void) ar;
    }

    void TerminalWindow::OnDockLayoutLoaded()
    {
        RefreshTable();
    }
}
