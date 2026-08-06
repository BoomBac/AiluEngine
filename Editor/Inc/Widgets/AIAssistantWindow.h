#pragma once
#ifndef __AI_ASSISTANT_WINDOW_H__
#define __AI_ASSISTANT_WINDOW_H__

#include "Automation/AIAssistant.h"
#include "Dock/DockWindow.h"

namespace Ailu
{
    namespace UI
    {
        class VerticalBox;
        class ScrollView;
        class InputBlock;
        class Text;
        class Button;
        class HorizontalBox;
    }// namespace UI

    namespace Editor
    {
        // Built-in AI assistant dock panel. Calls the automation services directly
        // (no MCP round trip) and previews write actions before applying them.
        class AIAssistantWindow final : public DockWindow
        {
        public:
            AIAssistantWindow();
            void Update(f32 dt) override;

        private:
            void BuildUI();
            void RefreshTranscript();
            void RefreshPreview();
            void OnSendClicked();
            void OnApplyClicked();
            void OnRejectClicked();
            void OnCancelClicked();
            void AppendTurn(const AIConversationTurn &turn);

            UI::VerticalBox *_transcript_content = nullptr;
            UI::VerticalBox *_preview_content = nullptr;
            UI::InputBlock *_input = nullptr;
            String _input_text;
            UI::Button *_apply_button = nullptr;
            UI::Button *_reject_button = nullptr;
            UI::Button *_cancel_button = nullptr;
            UI::Text *_preview_title = nullptr;
            u32 _observed_history_size = 0u;
            bool _preview_visible = false;
        };
    }// namespace Editor
}// namespace Ailu

#endif// !__AI_ASSISTANT_WINDOW_H__
