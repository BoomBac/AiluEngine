#pragma once

#include "Common/EditorLogService.h"
#include "Dock/DockWindow.h"
#include "Framework/Common/Log.h"
#include "generated/OutputWindow.gen.h"

namespace Ailu
{
    class JsonArchive;

    namespace UI
    {
        class Button;
        class Dropdown;
        class InputBlock;
        class ScrollView;
        class SplitView;
        class Text;
        class UITableElement;
        class VerticalBox;
    }

    namespace Editor
    {
        class OutputLogDataSource;

        ACLASS()
        class OutputWindow : public DockWindow
        {
            GENERATED_BODY()
            friend class OutputLogDataSource;

        public:
            OutputWindow();
            ~OutputWindow() override;
            void Update(f32 dt) override;
            void SaveDockLayoutState(JsonArchive &ar) override;
            void LoadDockLayoutState(JsonArchive &ar) override;
            void OnDockLayoutLoaded() override;

        private:
            struct OutputFilter
            {
                bool _show_info = true;
                bool _show_warning = true;
                bool _show_error = true;
                bool _show_debug = false;
                bool _show_trace = false;
                bool _collapse_duplicates = false;
                bool _follow_tail = true;
                String _search_text;
                i32 _module_index = -1;
            };

            struct VisibleLogRow
            {
                u64 _sequence = 0;
                u64 _last_sequence = 0;
                u32 _repeat_count = 1;
            };

            void BuildUI();
            void MarkFilterDirty();
            void RefreshView();
            void RefreshToolbarLabels();
            void RefreshDetails(i32 row);
            void CopySelectedRow();
            void CopySelectedDetails();
            void OpenSourceLocation(const EditorLogRecord &record) const;
            void SetLevelVisible(u32 level_index);
            bool PassesFilter(const EditorLogRecord &record) const;
            const EditorLogRecord *FindRecord(u64 sequence) const;
            String FormatRecord(const EditorLogRecord &record, u32 repeat_count = 1) const;
            String FormatDetails(const EditorLogRecord &record, u32 repeat_count = 1) const;

            Scope<OutputLogDataSource> _data_source;
            Vector<EditorLogRecord> _records;
            Vector<VisibleLogRow> _visible_rows;
            Vector<UI::Button *> _level_buttons;
            UI::Button *_collapse_button = nullptr;
            UI::Button *_follow_button = nullptr;
            UI::Dropdown *_module_dropdown = nullptr;
            UI::InputBlock *_search_input = nullptr;
            UI::SplitView *_main_split = nullptr;
            UI::UITableElement *_table = nullptr;
            UI::ScrollView *_details_scroll = nullptr;
            UI::VerticalBox *_details_content = nullptr;
            UI::Text *_details_text = nullptr;
            OutputFilter _filter;
            u64 _observed_revision = 0;
            u64 _selected_sequence = 0;
            bool _filter_dirty = true;
        };
    }
}
