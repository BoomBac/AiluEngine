#pragma once
#ifndef __AUTOMATION_SERVICE_H__
#define __AUTOMATION_SERVICE_H__

#include "Automation/AutomationRegistry.h"
#include "Automation/AutomationTypes.h"
#include "Automation/ConcurrentQueue.h"
#include "Framework/Common/NonCopyable.h"

#include <future>
#include <memory>

namespace Ailu
{
    namespace Editor
    {
        struct PendingAutomationRequest
        {
            AutomationRequest _request;
            std::shared_ptr<std::promise<AutomationResult>> _promise;
        };

        // Receives requests from transport threads, queues them, executes them on
        // the editor main thread during Tick() and completes the caller's future.
        class EditorAutomationService final : public NonCopyable
        {
        public:
            void Initialize();
            void Finalize();
            void Tick();

            std::future<AutomationResult> Submit(AutomationRequest request);

            EditorAutomationRegistry &Registry() { return _registry; }
            bool IsReady() const { return _is_initialized && !_is_shutting_down; }
            u32 PendingCount() const { return static_cast<u32>(_pending_requests.Size()); }

        private:
            void DrainRequests();
            AutomationResult Execute(const AutomationRequest &request);

        private:
            EditorAutomationRegistry _registry;
            ConcurrentQueue<PendingAutomationRequest> _pending_requests;
            bool _is_initialized = false;
            bool _is_shutting_down = false;
            u32 _max_requests_per_tick = 16u;
        };
    }// namespace Editor
}// namespace Ailu

#endif// !__AUTOMATION_SERVICE_H__
