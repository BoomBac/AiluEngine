#include "BlenderLiveLinkPlugin.h"
#include "LiveLinkProtocol.h"
#include "LiveLinkServer.h"
#include "LiveLinkSession.h"
#include <new>
#include <string>
#include <string_view>
#include <cstdlib>

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
        _progress_host = dynamic_cast<IPluginProgressHost*>(_host);
        try
        {
            _server = MakeScope<LiveLinkServer>();
            char* disable_mesh_cache = nullptr;
            size_t cache_option_size = 0u;
            _dupenv_s(&disable_mesh_cache, &cache_option_size, "AILU_BLENDER_LIVE_LINK_DISABLE_MESH_CACHE");
            const bool share_meshes = disable_mesh_cache == nullptr || std::string_view(disable_mesh_cache) != "1";
            std::free(disable_mesh_cache);
            _session = MakeScope<LiveLinkSession>(share_meshes);
            _full_sync_budget_ms = 32u;
            char* budget_text = nullptr;
            size_t budget_text_size = 0u;
            if (_dupenv_s(&budget_text, &budget_text_size, "AILU_BLENDER_LIVE_LINK_FULL_SYNC_BUDGET_MS") == 0
                && budget_text != nullptr)
            {
                char* end = nullptr;
                const auto budget_ms = std::strtoul(budget_text, &end, 10);
                if (end != budget_text && *end == '\0' && budget_ms >= 4u && budget_ms <= 64u)
                    _full_sync_budget_ms = static_cast<u32>(budget_ms);
            }
            std::free(budget_text);
            u16 port = 17777u;
            char *port_text = nullptr;
            size_t port_text_size = 0u;
            if (_dupenv_s(&port_text, &port_text_size, "AILU_BLENDER_LIVE_LINK_PORT") == 0 && port_text != nullptr)
            {
                char *end = nullptr;
                const auto parsed_port = std::strtoul(port_text, &end, 10);
                if (end != port_text && *end == '\0' && parsed_port > 0u && parsed_port <= 65535u)
                    port = static_cast<u16>(parsed_port);
            }
            std::free(port_text);
            const auto endpoint = std::string("127.0.0.1:") + std::to_string(port);
            if (!_server->Start(port))
            {
                _server.reset();
                _session.reset();
                _host->LogError(("[BlenderLiveLink] Failed to start loopback server on " + endpoint).c_str());
                _host = nullptr;
                return false;
            }
            _host->LogInfo(("[BlenderLiveLink] Server listening on " + endpoint).c_str());
            _host->LogInfo(("[BlenderLiveLink] Full Sync tick budget: "
                + std::to_string(_full_sync_budget_ms) + " ms; incremental: 4 ms").c_str());
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

    void BlenderLiveLinkPlugin::ReportProgress(const char* message, EPluginProgressState state) noexcept
    {
        if (_progress_host) _progress_host->ReportProgress("Blender Live Link: Full Sync", message, _progress_completed, state);
        _progress_active = state == EPluginProgressState::kBegin || state == EPluginProgressState::kRunning;
    }

    void BlenderLiveLinkPlugin::OnUnload() noexcept
    {
        if (_progress_active) ReportProgress("Plugin unloaded", EPluginProgressState::kCancelled);
        _progress_host = nullptr;
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
        const auto tick_start = std::chrono::steady_clock::now();
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
                if (_progress_active) ReportProgress("Disconnected before Full Sync completed", EPluginProgressState::kFailed);
                _full_sync_metrics._active = false;
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
                    if (_progress_active) ReportProgress("Replaced by a new Full Sync", EPluginProgressState::kCancelled);
                    _progress_completed = 0;
                    _progress_failed = false;
                    ReportProgress("Receiving Blender data", EPluginProgressState::kBegin);
                    _full_sync_metrics = {};
                    _full_sync_metrics._active = true;
                    _full_sync_metrics._started = std::chrono::steady_clock::now();
                }
                const auto apply_start = std::chrono::steady_clock::now();
                const auto queue_wait_us = command._queued_at.time_since_epoch().count() == 0
                    ? 0ull : static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::microseconds>(
                        apply_start - command._queued_at).count());
                _session->Apply(command);
                if (_progress_active)
                {
                    _progress_failed |= !_session->LastApplySucceeded();
                    if (!_session->LastApplySucceeded())
                    {
                        const std::string failure = "[BlenderLiveLink] Apply failed: type=" + std::to_string(message_type)
                            + ", sequence=" + std::to_string(command._sequence);
                        _host->LogWarning(failure.c_str());
                    }
                    if (!is_sync_begin && !is_sync_end) ++_progress_completed;
                    const char* stage = message_type == 20u ? "Applying mesh" : message_type == 21u ? "Applying material"
                        : message_type == 12u ? "Applying object" : "Receiving Blender data";
                    const std::string message = std::string(stage) + " (" + std::to_string(_progress_completed) + " packets applied)";
                    ReportProgress(message.c_str(), EPluginProgressState::kRunning);
                }
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
                    const std::array<unsigned short, 9u> message_types{10u, 11u, 12u, 20u, 21u, 22u, 23u, 24u, 31u};
                    for (const unsigned short type : message_types)
                    {
                        summary += ", type" + std::to_string(type) + "="
                            + std::to_string(metrics._counts[type]) + "/"
                            + std::to_string(metrics._payload_bytes[type]) + "B/"
                            + std::to_string(metrics._apply_by_type_us[type] / 1000.0) + "ms";
                    }
                    _host->LogInfo(summary.c_str());
                    _full_sync_metrics._active = false;
                    ReportProgress(_progress_failed ? "Full Sync completed with apply failures; see log" : "Full Sync completed",
                        _progress_failed ? EPluginProgressState::kFailed : EPluginProgressState::kCompleted);
                }
                break;
            }
            case ELiveLinkCommandType::kQueueOverflow:
                _host->LogWarning("[BlenderLiveLink] Incoming queue limit reached; reconnect and full sync required");
                if (_progress_active) ReportProgress("Receive queue overflow; reconnect and Full Sync required", EPluginProgressState::kFailed);
                _full_sync_metrics._active = false;
                _session->Apply(command);
                break;
            }
            // Yield after Begin and between bounded batches so the Editor can display progress.
            if (command._type == ELiveLinkCommandType::kMessage
                && command._message_type == static_cast<unsigned short>(ELiveLinkMessageType::kFullSyncBegin)) break;
            const auto budget_ms = _full_sync_metrics._active ? _full_sync_budget_ms : 4u;
            if (std::chrono::steady_clock::now() - tick_start >= std::chrono::milliseconds(budget_ms)) break;
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
