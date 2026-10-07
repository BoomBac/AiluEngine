#include "BlenderLiveLinkPlugin.h"
#include "LiveLinkProtocol.h"
#include "LiveLinkServer.h"
#include "LiveLinkSession.h"
#include <new>
#include <string>

namespace Ailu
{
    namespace
    {
        const PluginDescriptor s_descriptor{
            "com.ailu.blender_livelink", "Blender Live Link", "0.1.0", EPluginType::kEditor};
    }

    bool BlenderLiveLinkPlugin::OnLoad(const PluginContext& context) noexcept
    {
        if (_host || context._api_version != kPluginApiVersion || !context._host || !context._host->IsMainThread())
            return false;
        _host = context._host;
        try
        {
            _server = MakeScope<LiveLinkServer>();
            _session = MakeScope<LiveLinkSession>();
            if (!_server->Start())
            {
                _server.reset();
                _session.reset();
                _host->LogError("[BlenderLiveLink] Failed to start loopback server on 127.0.0.1:17777");
                _host = nullptr;
                return false;
            }
            _host->LogInfo("[BlenderLiveLink] Server listening on 127.0.0.1:17777");
            return true;
        }
        catch (...)
        {
            _server.reset();
            _session.reset();
            _host->LogError("[BlenderLiveLink] Server startup failed");
            _host = nullptr;
            return false;
        }
    }

    void BlenderLiveLinkPlugin::OnUnload() noexcept
    {
        if (_server)
        {
            _server->Stop();
            _server.reset();
        }
        _session.reset();
        if (_host)
            _host->LogInfo("[BlenderLiveLink] Server stopped");
        _host = nullptr;
    }

    void BlenderLiveLinkPlugin::Tick(f32) noexcept
    {
        if (!_host || !_server)
            return;
        LiveLinkCommand command{};
        while (_server->TryPopCommand(command))
        {
            switch (command._type)
            {
            case ELiveLinkCommandType::kClientConnected:
                _host->LogInfo("[BlenderLiveLink] Client connected");
                break;
            case ELiveLinkCommandType::kClientDisconnected:
                _host->LogInfo("[BlenderLiveLink] Client disconnected");
                _session->Apply(command);
                break;
            case ELiveLinkCommandType::kClientRejected:
                _host->LogWarning("[BlenderLiveLink] Rejected additional client; only one client is supported");
                break;
            case ELiveLinkCommandType::kHelloAccepted:
                _host->LogInfo("[BlenderLiveLink] Protocol v1 accepted");
                break;
            case ELiveLinkCommandType::kProtocolRejected:
                _host->LogWarning("[BlenderLiveLink] Client protocol version mismatch");
                break;
            case ELiveLinkCommandType::kMessage:
            {
                const auto message_type = command._message_type;
                const bool is_sync_begin = message_type
                    == static_cast<unsigned short>(ELiveLinkMessageType::kFullSyncBegin);
                const bool is_sync_end = message_type
                    == static_cast<unsigned short>(ELiveLinkMessageType::kFullSyncEnd);
                if (is_sync_begin)
                {
                    _full_sync_metrics = {};
                    _full_sync_metrics._active = true;
                    _full_sync_metrics._started = std::chrono::steady_clock::now();
                }
                const auto apply_start = std::chrono::steady_clock::now();
                const auto queue_wait_us = command._queued_at.time_since_epoch().count() == 0
                    ? 0ull : static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::microseconds>(
                        apply_start - command._queued_at).count());
                _session->Apply(command);
                const auto apply_us = static_cast<unsigned long long>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - apply_start).count());
                if (apply_us >= 50000u)
                {
                    const std::string slow_log = "[BlenderLiveLink] Slow Apply: type=" + std::to_string(message_type)
                        + ", sequence=" + std::to_string(command._sequence)
                        + ", payload_bytes=" + std::to_string(command._payload.size())
                        + ", receive_ms=" + std::to_string(command._receive_us / 1000.0)
                        + ", queue_wait_ms=" + std::to_string(queue_wait_us / 1000.0)
                        + ", apply_ms=" + std::to_string(apply_us / 1000.0);
                    _host->LogWarning(slow_log.c_str());
                }
                if (_full_sync_metrics._active && message_type < _full_sync_metrics._counts.size())
                {
                    auto& metrics = _full_sync_metrics;
                    ++metrics._counts[message_type];
                    metrics._payload_bytes[message_type] += command._payload.size();
                    metrics._receive_us += command._receive_us;
                    metrics._queue_wait_us += queue_wait_us;
                    metrics._apply_us += apply_us;
                    metrics._apply_by_type_us[message_type] += apply_us;
                }
                if (command._message_type == static_cast<unsigned short>(ELiveLinkMessageType::kObjectUpsert)
                    && !_session->LastObjectUpsertSucceeded())
                {
                    const std::string result = std::string("[BlenderLiveLink] ObjectUpsert failed: parented=")
                        + (_session->LastObjectReparentSucceeded() ? "true" : "false")
                        + ", tracked=" + std::to_string(_session->TrackedEntityCount())
                        + ", " + _session->LastObjectReparentDiagnostic();
                    _host->LogWarning(result.c_str());
                }
                if (is_sync_end && _full_sync_metrics._active)
                {
                    const auto elapsed_us = static_cast<unsigned long long>(
                        std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - _full_sync_metrics._started).count());
                    const auto& metrics = _full_sync_metrics;
                    std::string summary = "[BlenderLiveLink] FullSync import: total_ms="
                        + std::to_string(elapsed_us / 1000.0) + ", receive_ms="
                        + std::to_string(metrics._receive_us / 1000.0) + ", queue_wait_ms="
                        + std::to_string(metrics._queue_wait_us / 1000.0) + ", apply_ms="
                        + std::to_string(metrics._apply_us / 1000.0);
                    const std::array<unsigned short, 7u> message_types{10u, 11u, 12u, 20u, 21u, 22u, 23u};
                    for (const unsigned short type : message_types)
                    {
                        summary += ", type" + std::to_string(type) + "="
                            + std::to_string(metrics._counts[type]) + "/"
                            + std::to_string(metrics._payload_bytes[type]) + "B/"
                            + std::to_string(metrics._apply_by_type_us[type] / 1000.0) + "ms";
                    }
                    _host->LogInfo(summary.c_str());
                    _full_sync_metrics._active = false;
                }
                break;
            }
            case ELiveLinkCommandType::kQueueOverflow:
                _host->LogWarning("[BlenderLiveLink] Incoming queue limit reached; reconnect and full sync required");
                _session->Apply(command);
                break;
            }
        }
    }
}

AILU_PLUGIN_EXPORT Ailu::u32 AILU_PLUGIN_CALL AiluPluginGetApiVersion() noexcept
{
    return Ailu::kPluginApiVersion;
}

AILU_PLUGIN_EXPORT const Ailu::PluginDescriptor* AILU_PLUGIN_CALL AiluPluginGetDescriptor() noexcept
{
    return &Ailu::s_descriptor;
}

AILU_PLUGIN_EXPORT Ailu::IPlugin* AILU_PLUGIN_CALL AiluPluginCreate() noexcept
{
    return new (std::nothrow) Ailu::BlenderLiveLinkPlugin;
}

AILU_PLUGIN_EXPORT void AILU_PLUGIN_CALL AiluPluginDestroy(Ailu::IPlugin* plugin) noexcept
{
    delete plugin;
}
