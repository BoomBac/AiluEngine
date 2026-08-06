#include "Widgets/AIAssistantWindow.h"

#include "Automation/AIAssistant.h"
#include "UI/Container.h"
#include "UI/Basic.h"
#include "UI/Widget.h"

#include <format>

using namespace Ailu::UI;

namespace Ailu
{
    namespace Editor
    {
        AIAssistantWindow::AIAssistantWindow()
            : DockWindow("AI Assistant", {420.0f, 520.0f})
        {
            BuildUI();
            RefreshTranscript();
            RefreshPreview();
        }

        void AIAssistantWindow::BuildUI()
        {
            auto *root = _content_root->AddChild<UI::VerticalBox>();
            root->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFill);

            // Transcript scroll.
            {
                auto *scroll = root->AddChild<UI::ScrollView>();
                scroll->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFill);
                _transcript_content = scroll->AddChild<UI::VerticalBox>();
                _transcript_content->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFill);
            }

            // Preview area.
            {
                _preview_content = root->AddChild<UI::VerticalBox>();
                _preview_content->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto);
                _preview_title = _preview_content->AddChild<UI::Text>("Pending changes");
                _preview_title->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto);

                auto *buttons = _preview_content->AddChild<UI::HorizontalBox>();
                buttons->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFixed).Size(Vector2f(0.0f, 26.0f));
                _apply_button = buttons->AddChild<UI::Button>("Apply");
                _apply_button->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFixed, ESizePolicy::kFixed).Size(Vector2f(64.0f, 24.0f));
                _apply_button->OnMouseClick() += [this](UI::UIEvent &) { OnApplyClicked(); };
                _reject_button = buttons->AddChild<UI::Button>("Reject");
                _reject_button->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFixed, ESizePolicy::kFixed).Size(Vector2f(64.0f, 24.0f));
                _reject_button->OnMouseClick() += [this](UI::UIEvent &) { OnRejectClicked(); };
                _cancel_button = buttons->AddChild<UI::Button>("Cancel");
                _cancel_button->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFixed, ESizePolicy::kFixed).Size(Vector2f(64.0f, 24.0f));
                _cancel_button->OnMouseClick() += [this](UI::UIEvent &) { OnCancelClicked(); };
            }

            // Input row.
            {
                auto *input_row = root->AddChild<UI::HorizontalBox>();
                input_row->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFixed).Size(Vector2f(0.0f, 28.0f));
                _input = input_row->AddChild<UI::InputBlock>("");
                _input->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFill);
                _input->_on_content_changed += [this](String content) { _input_text = std::move(content); };
                auto *send = input_row->AddChild<UI::Button>("Send");
                send->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFixed, ESizePolicy::kFixed).Size(Vector2f(56.0f, 26.0f));
                send->OnMouseClick() += [this](UI::UIEvent &) { OnSendClicked(); };
            }
        }

        void AIAssistantWindow::Update(f32 dt)
        {
            DockWindow::Update(dt);
            AIAssistantService &service = AIAssistantService::Get();
            if (_observed_history_size != service.History().size())
            {
                _observed_history_size = static_cast<u32>(service.History().size());
                RefreshTranscript();
            }
            const bool pending = service.HasPendingPreview();
            if (pending != _preview_visible)
                RefreshPreview();
            else if (pending)
                RefreshPreview();
        }

        void AIAssistantWindow::RefreshTranscript()
        {
            if (_transcript_content == nullptr)
                return;
            _transcript_content->ClearChildren();
            const auto &history = AIAssistantService::Get().History();
            if (history.empty())
                _transcript_content->AddChild<UI::Text>("Ask me to inspect or modify the scene.");
            for (const AIConversationTurn &turn : history)
                AppendTurn(turn);
            if (AIAssistantService::Get().HasPendingPreview())
            {
                auto *pending = _transcript_content->AddChild<UI::Text>("> (pending preview for the latest request)");
                pending->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto);
            }
        }

        void AIAssistantWindow::AppendTurn(const AIConversationTurn &turn)
        {
            if (_transcript_content == nullptr)
                return;
            auto *user_line = _transcript_content->AddChild<UI::Text>("> " + turn._user_message);
            user_line->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto);
            if (!turn._ai_response.empty())
            {
                auto *response = _transcript_content->AddChild<UI::Text>(turn._ai_response);
                response->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto);
            }
        }

        void AIAssistantWindow::RefreshPreview()
        {
            AIAssistantService &service = AIAssistantService::Get();
            const bool pending = service.HasPendingPreview();
            _preview_visible = pending;
            if (_preview_content == nullptr)
                return;
            if (!pending)
            {
                _preview_content->SetVisible(false);
                return;
            }
            _preview_content->SetVisible(true);
            // The preview content is a fixed set of widgets; only the title reflects state.
            const AIConversationTurn &turn = service.CurrentTurn();
            _preview_title->SetText(std::format("Pending changes ({} action(s) need confirmation)", turn._planned_actions.size()));
        }

        void AIAssistantWindow::OnSendClicked()
        {
            if (_input == nullptr || _input_text.empty())
                return;
            const String text = std::move(_input_text);
            _input_text.clear();
            _input->SetContent("");
            AIAssistantService::Get().SubmitRequest(text);
            RefreshTranscript();
            RefreshPreview();
        }

        void AIAssistantWindow::OnApplyClicked()
        {
            AIAssistantService::Get().Apply();
            RefreshTranscript();
            RefreshPreview();
        }

        void AIAssistantWindow::OnRejectClicked()
        {
            AIAssistantService::Get().Reject();
            RefreshTranscript();
            RefreshPreview();
        }

        void AIAssistantWindow::OnCancelClicked()
        {
            AIAssistantService::Get().Cancel();
            RefreshTranscript();
            RefreshPreview();
        }
    }// namespace Editor
}// namespace Ailu
