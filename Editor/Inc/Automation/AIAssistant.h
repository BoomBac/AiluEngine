#pragma once
#ifndef __AI_ASSISTANT_H__
#define __AI_ASSISTANT_H__

#include "Automation/AutomationRegistry.h"
#include "Framework/Common/NonCopyable.h"

namespace Ailu
{
    namespace Editor
    {
        // A tool call the AI wants to make. The provider names the automation
        // operation; the service derives read/write/destructive from the method's
        // declared permission and stages writes for user preview.
        struct AIPlannedAction
        {
            String _operation;
            AutomationObject _arguments;
            String _summary;
        };

        struct AIConversationTurn
        {
            String _user_message;
            String _selection_context;
            Vector<AIPlannedAction> _planned_actions;
            Vector<AutomationResult> _results;
            String _ai_response;
            bool _applied = false;
            bool _rejected = false;
            bool _cancelled = false;
            bool _had_preview = false;
        };

        // Pluggable AI decision maker. A real HTTP LLM provider can be added
        // later; the demo provider maps a few request patterns to tool plans.
        class IAIProvider
        {
        public:
            virtual ~IAIProvider() = default;
            virtual Vector<AIPlannedAction> GeneratePlan(const String &user_request, const String &selection_context) = 0;
            virtual String GenerateResponse(const String &user_request, const Vector<AutomationResult> &results) = 0;
        };

        // Keyword-based demo provider demonstrating the read -> preview -> apply loop.
        class DemoAIProvider final : public IAIProvider
        {
        public:
            Vector<AIPlannedAction> GeneratePlan(const String &user_request, const String &selection_context) override;
            String GenerateResponse(const String &user_request, const Vector<AutomationResult> &results) override;
        };

        class AIAssistantService final : public NonCopyable
        {
        public:
            static AIAssistantService &Get();

            void SetRegistry(EditorAutomationRegistry *registry);
            void SetProvider(Scope<IAIProvider> provider);

            // Starts a turn: read actions execute immediately, write/destructive
            // actions are staged for preview. Returns false if a preview is pending.
            bool SubmitRequest(String user_message);

            bool HasPendingPreview() const { return _has_pending; }
            const AIConversationTurn &CurrentTurn() const { return _current; }
            const Vector<AIConversationTurn> &History() const { return _history; }

            // Confirms the staged writes; executes them, then finishes the turn.
            AutomationResult Apply();
            void Reject();
            void Cancel();

            // Build a context string from the editor's current selection.
            static String BuildSelectionContext();

            // Clear history / pending state (used by tests).
            void Reset();

        private:
            AutomationResult ExecuteAction(const AIPlannedAction &action);
            void FinishTurn();

            EditorAutomationRegistry *_registry = nullptr;
            Scope<IAIProvider> _provider;
            Vector<AIConversationTurn> _history;
            AIConversationTurn _current;
            Vector<size_t> _staged_indices; // indices into _current._planned_actions needing preview
            bool _has_pending = false;
        };
    }// namespace Editor
}// namespace Ailu

#endif// !__AI_ASSISTANT_H__
