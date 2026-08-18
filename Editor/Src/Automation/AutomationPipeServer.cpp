#include "Automation/AutomationPipeServer.h"

#include "Automation/AutomationJson.h"
#include "Automation/AutomationService.h"
#include "Framework/Common/Utils.h"
#include "Framework/Math/Guid.h"

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <future>

namespace Ailu
{
    namespace Editor
    {
        namespace
        {
            constexpr u32 kPipeBufferSize = 65536u;
            constexpr u32 kMaxMessageSize = 16u * 1024u * 1024u;
            constexpr i64 kRequestTimeoutSeconds = 30;
        }// namespace

        AutomationPipeServer::~AutomationPipeServer()
        {
            Finalize();
        }

        void AutomationPipeServer::Initialize(EditorAutomationService &service)
        {
            if (_running.load())
                return;
            _service = &service;
            _session_id = std::format("ailu_editor_{}", Guid::Generate().ToString());
            _pipe_name = std::format("\\\\.\\pipe\\{}", _session_id);
            _running.store(true);
            _thread = std::thread([this] { ServerLoop(); });
        }

        void AutomationPipeServer::Finalize()
        {
            if (!_running.exchange(false))
                return;
            if (_thread.joinable())
            {
                // Closing a synchronous pipe handle from another thread does not reliably cancel its I/O.
                CancelSynchronousIo(_thread.native_handle());
                _thread.join();
            }
            _service = nullptr;
        }

        void AutomationPipeServer::ServerLoop()
        {
            while (_running.load())
            {
                HANDLE pipe = CreateNamedPipeW(ToWChar(_pipe_name).c_str(),
                                               PIPE_ACCESS_DUPLEX,
                                               PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                               1,
                                               kPipeBufferSize,
                                               kPipeBufferSize,
                                               0,
                                               nullptr);
                if (pipe == INVALID_HANDLE_VALUE)
                    break;

                if (!_running.load())
                {
                    CloseHandle(pipe);
                    return;
                }

                const bool connected = ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
                if (!connected)
                {
                    CloseHandle(pipe);
                    continue;
                }

                if (!_running.load())
                {
                    CloseHandle(pipe);
                    return;
                }
                HandleConnection(pipe);
                CloseHandle(pipe);
                if (!_running.load())
                    return;
            }
        }

        void AutomationPipeServer::HandleConnection(void *pipe_handle)
        {
            String payload;
            while (_running.load() && ReadMessage(pipe_handle, payload))
            {
                AutomationRequest request;
                if (!AutomationJson::ReadRequest(payload, request))
                {
                    const AutomationResult error = AutomationResult::Fail(AutomationErrors::kInvalidArgument,
                                                                          "malformed request JSON");
                    WriteMessage(pipe_handle, AutomationJson::WriteResult(0u, error));
                    continue;
                }
                const u64 request_id = request._request_id;
                std::future<AutomationResult> future = _service->Submit(std::move(request));
                const auto request_deadline = std::chrono::steady_clock::now() +
                                              std::chrono::seconds(kRequestTimeoutSeconds);
                bool request_timed_out = false;
                while (_running.load())
                {
                    if (future.wait_for(std::chrono::milliseconds(50)) == std::future_status::ready)
                        break;
                    if (std::chrono::steady_clock::now() >= request_deadline)
                    {
                        request_timed_out = true;
                        break;
                    }
                }
                if (!_running.load())
                    return;
                if (request_timed_out)
                {
                    const AutomationResult error = AutomationResult::Fail(AutomationErrors::kEditorNotReady,
                                                                          "request timed out waiting for the editor main thread");
                    WriteMessage(pipe_handle, AutomationJson::WriteResult(request_id, error));
                    continue;
                }
                const AutomationResult result = future.get();
                if (!WriteMessage(pipe_handle, AutomationJson::WriteResult(request_id, result)))
                    break;
            }
        }

        namespace
        {
            bool ReadExact(HANDLE pipe, void *buffer, u32 size)
            {
                auto *dst = static_cast<u8 *>(buffer);
                u32 total = 0u;
                while (total < size)
                {
                    DWORD bytes = 0u;
                    if (!ReadFile(pipe, dst + total, size - total, &bytes, nullptr) || bytes == 0u)
                        return false;
                    total += bytes;
                }
                return true;
            }

            bool WriteAll(HANDLE pipe, const void *buffer, u32 size)
            {
                const auto *src = static_cast<const u8 *>(buffer);
                u32 total = 0u;
                while (total < size)
                {
                    DWORD written = 0u;
                    if (!WriteFile(pipe, src + total, size - total, &written, nullptr) || written == 0u)
                        return false;
                    total += written;
                }
                return true;
            }
        }// namespace

        bool AutomationPipeServer::WriteMessage(void *pipe_handle, const String &payload)
        {
            HANDLE pipe = static_cast<HANDLE>(pipe_handle);
            if (payload.size() > kMaxMessageSize)
                return false;
            const u32 length = static_cast<u32>(payload.size());
            if (!WriteAll(pipe, &length, sizeof(length)))
                return false;
            if (length == 0u)
                return true;
            return WriteAll(pipe, payload.data(), length);
        }

        bool AutomationPipeServer::ReadMessage(void *pipe_handle, String &payload)
        {
            HANDLE pipe = static_cast<HANDLE>(pipe_handle);
            u32 length = 0u;
            if (!ReadExact(pipe, &length, sizeof(length)))
                return false;
            if (length > kMaxMessageSize)
                return false;
            payload.resize(length);
            if (length == 0u)
                return true;
            return ReadExact(pipe, payload.data(), length);
        }

    }// namespace Editor
}// namespace Ailu
