#pragma once

#include "LiveLinkCommandQueue.h"
#include <atomic>
#include <thread>

namespace Ailu
{
    class LiveLinkServer final
    {
    public:
        LiveLinkServer() = default;
        ~LiveLinkServer();
        LiveLinkServer(const LiveLinkServer&) = delete;
        LiveLinkServer& operator=(const LiveLinkServer&) = delete;

        bool Start();
        void Stop() noexcept;
        bool TryPopCommand(LiveLinkCommand& command);

    private:
        void WorkerMain() noexcept;
        void ProcessClient(unsigned long long client_socket) noexcept;

        std::atomic<bool> _stopping{false};
        std::atomic<unsigned long long> _listen_socket{~0ull};
        std::atomic<unsigned long long> _client_socket{~0ull};
        std::thread _worker;
        LiveLinkCommandQueue _commands;
        bool _winsock_started = false;
    };
}
