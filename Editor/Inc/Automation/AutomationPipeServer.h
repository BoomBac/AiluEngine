#pragma once
#ifndef __AUTOMATION_PIPE_SERVER_H__
#define __AUTOMATION_PIPE_SERVER_H__

#include "Framework/Common/NonCopyable.h"
#include "Framework/Core/String.h"

#include <atomic>
#include <mutex>
#include <thread>

namespace Ailu
{
    namespace Editor
    {
        class EditorAutomationService;

        // Named-pipe transport. Accepts one client connection at a time, reads
        // length-prefixed JSON request messages, submits them to the automation
        // service and writes the JSON result back. The worker thread only touches
        // JSON and the service queue, never engine objects.
        class AutomationPipeServer final : public NonCopyable
        {
        public:
            ~AutomationPipeServer();

            void Initialize(EditorAutomationService &service);
            void Finalize();

            const String &PipeName() const { return _pipe_name; }
            const String &SessionId() const { return _session_id; }
            bool IsRunning() const { return _running.load(); }

            // Framing helpers (u32 little-endian length prefix + UTF-8 payload).
            static bool WriteMessage(void *pipe_handle, const String &payload);
            static bool ReadMessage(void *pipe_handle, String &payload);

        private:
            void ServerLoop();
            void HandleConnection(void *pipe_handle);

        private:
            EditorAutomationService *_service = nullptr;
            String _pipe_name;
            String _session_id;
            // Current pipe instance; owned by the worker thread, but Finalize may
            // take it (close it) to unblock a pending connect/read during shutdown.
            void *_pipe = nullptr;
            std::mutex _pipe_mutex;
            std::thread _thread;
            std::atomic<bool> _running = false;
        };
    }// namespace Editor
}// namespace Ailu

#endif// !__AUTOMATION_PIPE_SERVER_H__
