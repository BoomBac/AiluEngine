// Unit tests for the automation transport: JSON codec, session file and the
// named-pipe round trip (client -> server -> service -> response).

#include "Automation/AutomationJson.h"
#include "Automation/AutomationPipeServer.h"
#include "Automation/AutomationService.h"
#include "Automation/AutomationSession.h"

#include "Framework/Common/Utils.h"

#include <windows.h>

#include <chrono>
#include <filesystem>

using namespace Ailu;

namespace Ailu::Editor::AutomationTransportTests
{
    bool TestJsonRoundtrip()
    {
        AutomationObject object;
        object.emplace("name", AutomationValue("Bob"));
        object.emplace("hp", AutomationValue(100));
        object.emplace("ratio", AutomationValue(1.5));
        object.emplace("ok", AutomationValue(true));
        object.emplace("nothing", AutomationValue{});
        AutomationArray list;
        list.emplace_back(AutomationValue(1));
        list.emplace_back(AutomationValue("two"));
        object.emplace("list", AutomationValue(std::move(list)));

        const String json = AutomationJson::Write(AutomationValue(object));
        AutomationValue parsed;
        if (!AutomationJson::Read(json, parsed) || !parsed.IsObject())
            return false;
        const AutomationObject &out = parsed.AsObject();
        if (out.at("name").AsString() != "Bob")
            return false;
        if (out.at("hp").AsInt() != 100)
            return false;
        if (out.at("ratio").AsFloat() != 1.5)
            return false;
        if (out.at("ok").AsBool() != true)
            return false;
        if (!out.at("nothing").IsNull())
            return false;
        if (out.at("list").AsArray().size() != 2u || out.at("list").AsArray()[1].AsString() != "two")
            return false;
        return true;
    }

    bool TestRequestResultJson()
    {
        AutomationRequest request;
        request._request_id = 7u;
        request._method = "scene.query_entities";
        request._arguments.emplace("name", AutomationValue("Player"));
        request._context._caller = "test";
        request._context._allow_write = true;

        const String json = AutomationJson::WriteRequest(request);
        AutomationRequest parsed;
        if (!AutomationJson::ReadRequest(json, parsed))
            return false;
        if (parsed._request_id != 7u || parsed._method != "scene.query_entities")
            return false;
        if (parsed._arguments.at("name").AsString() != "Player")
            return false;
        if (parsed._context._caller != "test" || parsed._context._allow_write != true)
            return false;

        AutomationResult ok = AutomationResult::Ok(AutomationValue(42));
        u64 rid = 0u;
        AutomationResult parsed_result;
        if (!AutomationJson::ReadResult(AutomationJson::WriteResult(7u, ok), rid, parsed_result))
            return false;
        if (rid != 7u || !parsed_result._success || parsed_result._data.AsInt() != 42)
            return false;

        AutomationResult fail = AutomationResult::Fail("boom_code", "boom message");
        if (!AutomationJson::ReadResult(AutomationJson::WriteResult(8u, fail), rid, parsed_result))
            return false;
        if (parsed_result._success || parsed_result._error._code != "boom_code")
            return false;
        return true;
    }

    bool TestSessionFile()
    {
        const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ailu_automation_test";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);

        AutomationSessionInfo info;
        info._pid = 12345;
        info._session_id = "sess-1";
        info._pipe_name = "\\\\.\\pipe\\ailu_editor_sess-1";
        info._project_path = "F:/Test/Project";
        info._editor_version = "0.1.0";
        if (!AutomationSession::Save(dir, info))
            return false;
        AutomationSessionInfo loaded;
        if (!AutomationSession::Load(dir, loaded))
            return false;
        if (loaded._pid != 12345 || loaded._session_id != "sess-1" || loaded._pipe_name != info._pipe_name)
            return false;
        if (loaded._project_path != "F:/Test/Project" || loaded._editor_version != "0.1.0")
            return false;
        std::filesystem::remove_all(dir, ec);
        return true;
    }

    bool TestPipeRoundTrip()
    {
        EditorAutomationService service;
        service.Initialize();
        AutomationPipeServer server;
        server.Initialize(service);

        const std::wstring pipe_name = ToWChar(server.PipeName());
        HANDLE client = INVALID_HANDLE_VALUE;
        for (int attempt = 0; attempt < 50; ++attempt)
        {
            client = CreateFileW(pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (client != INVALID_HANDLE_VALUE)
                break;
            Sleep(20);
        }
        if (client == INVALID_HANDLE_VALUE)
        {
            server.Finalize();
            service.Finalize();
            return false;
        }

        AutomationRequest request;
        request._request_id = 1u;
        request._method = "automation.ping";
        request._arguments.emplace("token", AutomationValue(1234));
        if (!AutomationPipeServer::WriteMessage(client, AutomationJson::WriteRequest(request)))
        {
            CloseHandle(client);
            server.Finalize();
            service.Finalize();
            return false;
        }

        // Let the server thread read + submit the request before processing.
        bool submitted = false;
        for (int attempt = 0; attempt < 300; ++attempt)
        {
            if (service.PendingCount() > 0u)
            {
                submitted = true;
                break;
            }
            Sleep(10);
        }
        if (!submitted)
        {
            CloseHandle(client);
            server.Finalize();
            service.Finalize();
            return false;
        }
        // Process on this (main) thread.
        for (int attempt = 0; attempt < 100; ++attempt)
        {
            service.Tick();
            Sleep(10);
        }

        String response;
        if (!AutomationPipeServer::ReadMessage(client, response))
        {
            CloseHandle(client);
            server.Finalize();
            service.Finalize();
            return false;
        }
        CloseHandle(client);

        u64 rid = 0u;
        AutomationResult result;
        if (!AutomationJson::ReadResult(response, rid, result))
        {
            server.Finalize();
            service.Finalize();
            return false;
        }
        if (rid != 1u || !result._success || result._data.AsObject().at("pong").AsBool() != true)
        {
            server.Finalize();
            service.Finalize();
            return false;
        }

        server.Finalize();
        service.Finalize();
        return true;
    }

    bool TestPipeFinalizeWithoutClient()
    {
        EditorAutomationService service;
        service.Initialize();
        AutomationPipeServer server;
        server.Initialize(service);
        if (!server.IsRunning())
        {
            service.Finalize();
            return false;
        }

        const auto start = std::chrono::steady_clock::now();
        server.Finalize();
        const auto elapsed = std::chrono::steady_clock::now() - start;
        service.Finalize();
        return elapsed < std::chrono::seconds(1);
    }
}// namespace Ailu::Editor::AutomationTransportTests
