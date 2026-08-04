#pragma once

#include "Dock/DockWindow.h"
#include "Render/FrameDebugger/FrameCapture.h"

namespace Ailu
{
    namespace UI
    {
        class TreeView;
        class Text;
        class Button;
        class Border;
        class Image;
        class ScrollView;
        class VerticalBox;
        class InputBlock;
        class UITableElement;
    }

    namespace Editor
    {
        class FrameDebuggerWindow : public DockWindow
        {
        public:
            FrameDebuggerWindow();
            ~FrameDebuggerWindow() override;
            void Update(f32 dt) override;

        private:
            void BuildUI();
            void BindTreeEvents();
            void OnCaptureClicked();
            void RefreshCapture();
            void RefreshEventTree();
            void RefreshSelectedEventDetails(u32 event_id);
            void RefreshBindingCacheDetails(u32 binding_range_begin, u16 binding_count);
            void ClearBindingCacheDetails();
            void AddDiagnosticText(const String &text, Color color);
            void SetActiveTab(u32 tab_index);
            void SetEventFilter(u32 filter);

            u32 EventIndexFromTreeItem(u64 item) const { return item == 0u ? 0u : (u32)(item - 1u); }
            u64 TreeItemFromEventIndex(u32 idx) const { return (u64)idx + 1u; }

            class EventTreeDataSource;
            Scope<EventTreeDataSource> _data_source;

            class BindingCacheTableDataSource;
            class BindingInvalidationTableDataSource;
            Scope<BindingCacheTableDataSource> _binding_table_source;
            Scope<BindingInvalidationTableDataSource> _invalidation_table_source;

            Ref<const Render::FrameDebugger::FrameCapture> _capture;
            Render::FrameDebugger::EFrameCaptureState _observed_state = Render::FrameDebugger::EFrameCaptureState::kIdle;

            UI::Button *_capture_button = nullptr;
            UI::Text *_status_text = nullptr;
            UI::Text *_summary_text = nullptr;
            UI::TreeView *_event_tree = nullptr;
            UI::ScrollView *_detail_scroll = nullptr;
            UI::VerticalBox *_detail_content = nullptr;
            UI::ScrollView *_binding_scroll = nullptr;
            UI::VerticalBox *_binding_content = nullptr;
            UI::VerticalBox *_diagnostic_content = nullptr;
            UI::UITableElement *_binding_table = nullptr;
            UI::UITableElement *_invalidation_table = nullptr;
            Vector<UI::Button *> _tab_buttons;
            Vector<UI::Border *> _tab_frames;
            Vector<UI::Button *> _filter_buttons;
            u32 _active_tab = 0u;
            u32 _event_filter = 0u;

            u32 _selected_event_id = ~0u;
            u64 _observed_capture_revision = 0u;
        };
    } // namespace Editor
} // namespace Ailu
