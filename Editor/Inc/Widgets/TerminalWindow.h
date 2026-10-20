#pragma once

#include "Common/ConsoleCommandRegistry.h"
#include "Common/EditorTaskService.h"
#include "Common/TerminalTranscript.h"
#include "Dock/DockWindow.h"
#include "generated/TerminalWindow.gen.h"

namespace Ailu
{
    class JsonArchive;
}

namespace Ailu
{
    namespace UI
    {
        class Button;
        class Dropdown;
        class InputBlock;
        class ListView;
        class Text;
        class VerticalBox;
        class UITableElement;
        struct UIEvent;
    }

    namespace Editor
    {
        class TerminalTranscriptDataSource;

    ACLASS()
    class TerminalWindow : public DockWindow
    {
        GENERATED_BODY()
        friend class TerminalTranscriptDataSource;

    public:
        TerminalWindow();
        ~TerminalWindow() override;
        void Update(f32 dt) override;
        void SaveDockLayoutState(JsonArchive &ar) override;
        void LoadDockLayoutState(JsonArchive &ar) override;
        void OnDockLayoutLoaded() override;

    private:
        enum class ETerminalWindowMode : u8
        {
            kEngine,
            kTasks
        };

        void BuildUI();
        void BuildTaskUI(UI::VerticalBox *root);
        void ExecuteInput(const String &line);
        void AppendMessage(ETerminalLineChannel channel, const String &message);
        void HandleInputKey(UI::UIEvent &event);
        void NavigateHistory(i32 direction);
        void RefreshCompletions();
        void MoveCompletion(i32 direction);
        void AcceptCompletion();
        void HideCompletions();
        void RefreshTable();
        void SetStatus(const String &message);
        void SetWindowMode(ETerminalWindowMode mode);
        void RefreshTaskSelector();
        void RefreshTaskView();
        void RunTask();
        void CancelTask();
        const TerminalTranscript *DisplayedTranscript() const;

        Scope<TerminalTranscriptDataSource> _data_source;
        TerminalTranscript _transcript;
        Vector<String> _history;
        i32 _history_index = -1;
        String _history_edit;
        Vector<String> _completion_items;
        i32 _completion_index = -1;
        u64 _observed_revision = 0u;
        u64 _observed_task_revision = 0u;
        u64 _observed_task_list_revision = 0u;
        u64 _selected_task_id = 0u;
        bool _refocus_input = false;
        ETerminalWindowMode _mode = ETerminalWindowMode::kEngine;
        UI::UITableElement *_table = nullptr;
        UI::InputBlock *_input = nullptr;
        UI::ListView *_completion_list = nullptr;
        UI::Text *_status = nullptr;
        UI::Button *_run_button = nullptr;
        UI::Button *_engine_clear_button = nullptr;
        UI::VerticalBox *_task_panel = nullptr;
        UI::Dropdown *_task_mode = nullptr;
        UI::Dropdown *_task_selector = nullptr;
        UI::InputBlock *_task_executable = nullptr;
        UI::InputBlock *_task_arguments = nullptr;
        UI::InputBlock *_task_command = nullptr;
        UI::InputBlock *_task_working_directory = nullptr;
        UI::Button *_task_run_button = nullptr;
        UI::Button *_task_cancel_button = nullptr;
        UI::Button *_task_clear_button = nullptr;
        UI::Text *_task_status = nullptr;
        };
    }
}
