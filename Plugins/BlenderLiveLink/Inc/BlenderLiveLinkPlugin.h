#pragma once

#include "Framework/Plugin/PluginApi.h"
#include "Framework/Platform/Platform.h"
#include "Framework/Core/CoreMinimal.h"
#include <array>
#include <chrono>

namespace Ailu
{
    class LiveLinkServer;
    class LiveLinkSession;

    class BlenderLiveLinkPlugin final : public IPlugin
    {
    public:
        bool OnLoad(const PluginContext& context) noexcept override;
        void OnUnload() noexcept override;
        void Tick(f32 delta_time) noexcept override;

    private:
        struct FullSyncMetrics
        {
            bool _active = false;
            std::chrono::steady_clock::time_point _started{};
            unsigned long long _receive_us = 0u;
            unsigned long long _queue_wait_us = 0u;
            unsigned long long _apply_us = 0u;
            std::array<unsigned long long, 24u> _counts{};
            std::array<unsigned long long, 24u> _payload_bytes{};
            std::array<unsigned long long, 24u> _apply_by_type_us{};
        };

        IPluginHost* _host = nullptr;
        Scope<LiveLinkServer> _server;
        Scope<LiveLinkSession> _session;
        FullSyncMetrics _full_sync_metrics;
    };
}
