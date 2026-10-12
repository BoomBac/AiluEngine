#include "LiveLinkServer.h"
#include "LiveLinkProtocol.h"
#include <array>
#include <chrono>
#include <string_view>
#include <vector>
#include <winsock2.h>
#include <ws2tcpip.h>

namespace Ailu
{
    namespace
    {
        constexpr SOCKET kInvalidSocket = INVALID_SOCKET;

        SOCKET UnpackSocket(unsigned long long value) noexcept
        {
            return static_cast<SOCKET>(value);
        }

        void RejectPendingConnections(SOCKET listen_socket, LiveLinkCommandQueue& commands) noexcept
        {
            while (true)
            {
                fd_set listen_set;
                FD_ZERO(&listen_set);
                FD_SET(listen_socket, &listen_set);
                timeval timeout{};
                if (select(0, &listen_set, nullptr, nullptr, &timeout) <= 0)
                    return;
                sockaddr_in client_address{};
                int address_size = sizeof(client_address);
                const SOCKET pending_socket = accept(listen_socket,
                    reinterpret_cast<sockaddr*>(&client_address), &address_size);
                if (pending_socket == kInvalidSocket)
                    return;
                shutdown(pending_socket, SD_BOTH);
                closesocket(pending_socket);
                commands.Push({ELiveLinkCommandType::kClientRejected});
            }
        }

        bool ReceiveExact(SOCKET socket, SOCKET listen_socket, std::atomic<bool>& stopping,
            LiveLinkCommandQueue& commands, u8* destination, size_t size) noexcept
        {
            size_t received = 0u;
            while (received < size && !stopping.load())
            {
                RejectPendingConnections(listen_socket, commands);
                fd_set client_set;
                FD_ZERO(&client_set);
                FD_SET(socket, &client_set);
                timeval timeout{};
                timeout.tv_usec = 100000;
                const int ready = select(0, &client_set, nullptr, nullptr, &timeout);
                if (ready == 0)
                    continue;
                if (ready == SOCKET_ERROR)
                    return false;
                const auto result = recv(socket, reinterpret_cast<char*>(destination + received),
                    static_cast<int>(size - received), 0);
                if (result <= 0)
                    return false;
                received += static_cast<size_t>(result);
            }
            return true;
        }

        bool SendExact(SOCKET socket, const u8* source, size_t size) noexcept
        {
            size_t sent = 0u;
            while (sent < size)
            {
                const auto result = send(socket, reinterpret_cast<const char*>(source + sent),
                    static_cast<int>(size - sent), 0);
                if (result <= 0)
                    return false;
                sent += static_cast<size_t>(result);
            }
            return true;
        }

        bool SendPacket(SOCKET socket, ELiveLinkMessageType type, u64 sequence, std::string_view payload) noexcept
        {
            LiveLinkPacketHeader header;
            header._type = type;
            header._sequence = sequence;
            header._payload_size = static_cast<u32>(payload.size());
            const auto encoded_header = EncodeLiveLinkHeader(header);
            return SendExact(socket, encoded_header.data(), encoded_header.size())
                && (payload.empty() || SendExact(socket, reinterpret_cast<const u8*>(payload.data()), payload.size()));
        }
    }

    LiveLinkServer::~LiveLinkServer()
    {
        Stop();
    }

    bool LiveLinkServer::Start(u16 port)
    {
        if (_worker.joinable() || _winsock_started)
            return false;
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
            return false;
        _winsock_started = true;

        const SOCKET listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listen_socket == kInvalidSocket)
        {
            Stop();
            return false;
        }
        const BOOL exclusive_address = TRUE;
        if (setsockopt(listen_socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                reinterpret_cast<const char*>(&exclusive_address), sizeof(exclusive_address)) == SOCKET_ERROR)
        {
            closesocket(listen_socket);
            Stop();
            return false;
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        if (InetPtonA(AF_INET, "127.0.0.1", &address.sin_addr) != 1
            || bind(listen_socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR
            || listen(listen_socket, 1) == SOCKET_ERROR)
        {
            closesocket(listen_socket);
            Stop();
            return false;
        }

        _stopping = false;
        _listen_socket = static_cast<unsigned long long>(listen_socket);
        try
        {
            _worker = std::thread([this] { WorkerMain(); });
        }
        catch (...)
        {
            Stop();
            return false;
        }
        return true;
    }

    void LiveLinkServer::Stop() noexcept
    {
        _stopping = true;
        const auto client_socket = UnpackSocket(_client_socket.load());
        if (client_socket != kInvalidSocket)
            shutdown(client_socket, SD_BOTH);
        const auto listen_socket = UnpackSocket(_listen_socket.load());
        if (listen_socket != kInvalidSocket)
            shutdown(listen_socket, SD_BOTH);
        if (_worker.joinable())
            _worker.join();
        else
        {
            const auto unowned_socket = UnpackSocket(_listen_socket.exchange(~0ull));
            if (unowned_socket != kInvalidSocket)
                closesocket(unowned_socket);
        }
        if (_winsock_started)
        {
            WSACleanup();
            _winsock_started = false;
        }
        _commands.Clear();
    }

    bool LiveLinkServer::TryPopCommand(LiveLinkCommand& command)
    {
        return _commands.TryPop(command);
    }

    void LiveLinkServer::WorkerMain() noexcept
    {
        const SOCKET listen_socket = UnpackSocket(_listen_socket.load());
        while (!_stopping.load())
        {
            fd_set listen_set;
            FD_ZERO(&listen_set);
            FD_SET(listen_socket, &listen_set);
            timeval timeout{};
            timeout.tv_usec = 100000;
            const int ready = select(0, &listen_set, nullptr, nullptr, &timeout);
            if (_stopping.load())
                break;
            if (ready == SOCKET_ERROR || ready == 0)
                continue;

            sockaddr_in client_address{};
            int address_size = sizeof(client_address);
            const SOCKET client_socket = accept(listen_socket,
                reinterpret_cast<sockaddr*>(&client_address), &address_size);
            if (client_socket == kInvalidSocket)
            {
                if (_stopping.load())
                    break;
                continue;
            }

            _client_socket = static_cast<unsigned long long>(client_socket);
            _commands.Push({ELiveLinkCommandType::kClientConnected});
            ProcessClient(static_cast<unsigned long long>(client_socket));
            shutdown(client_socket, SD_BOTH);
            closesocket(client_socket);
            _client_socket = ~0ull;
            _commands.Push({ELiveLinkCommandType::kClientDisconnected});
        }
        const auto owned_listen_socket = UnpackSocket(_listen_socket.exchange(~0ull));
        if (owned_listen_socket != kInvalidSocket)
            closesocket(owned_listen_socket);
    }

    void LiveLinkServer::ProcessClient(unsigned long long raw_socket) noexcept
    {
        const SOCKET client_socket = UnpackSocket(raw_socket);
        const SOCKET listen_socket = UnpackSocket(_listen_socket.load());
        std::array<u8, kLiveLinkHeaderSize> encoded_header{};
        bool hello_completed = false;
        while (!_stopping.load())
        {
            const auto receive_start = std::chrono::steady_clock::now();
            if (!ReceiveExact(client_socket, listen_socket, _stopping, _commands,
                    encoded_header.data(), encoded_header.size()))
                return;

            LiveLinkPacketHeader header;
            if (!DecodeLiveLinkHeader(encoded_header, header))
            {
                const u32 magic = static_cast<u32>(encoded_header[0])
                    | (static_cast<u32>(encoded_header[1]) << 8u)
                    | (static_cast<u32>(encoded_header[2]) << 16u)
                    | (static_cast<u32>(encoded_header[3]) << 24u);
                const u16 version = static_cast<u16>(encoded_header[4]
                    | (static_cast<u16>(encoded_header[5]) << 8u));
                if (magic == kLiveLinkMagic && version != kLiveLinkProtocolVersion)
                {
                    const u64 sequence = static_cast<u64>(encoded_header[12])
                        | (static_cast<u64>(encoded_header[13]) << 8u)
                        | (static_cast<u64>(encoded_header[14]) << 16u)
                        | (static_cast<u64>(encoded_header[15]) << 24u)
                        | (static_cast<u64>(encoded_header[16]) << 32u)
                        | (static_cast<u64>(encoded_header[17]) << 40u)
                        | (static_cast<u64>(encoded_header[18]) << 48u)
                        | (static_cast<u64>(encoded_header[19]) << 56u);
                    _commands.Push({ELiveLinkCommandType::kProtocolRejected, version});
                    SendPacket(client_socket, ELiveLinkMessageType::kHelloAck, sequence,
                        "{\"accepted\":false,\"protocol_version\":1}");
                }
                return;
            }

            if (header._payload_size > kMaxLiveLinkPayloadSize)
                return;
            std::vector<u8> payload;
            try
            {
                payload.resize(header._payload_size);
            }
            catch (...)
            {
                return;
            }
            if (!payload.empty() && !ReceiveExact(client_socket, listen_socket, _stopping, _commands,
                    payload.data(), payload.size()))
                return;

            if (header._type == ELiveLinkMessageType::kHello)
            {
                if (!SendPacket(client_socket, ELiveLinkMessageType::kHelloAck, header._sequence,
                        "{\"accepted\":true,\"protocol_version\":1}"))
                    return;
                hello_completed = true;
                _commands.Push({ELiveLinkCommandType::kHelloAccepted, header._version});
            }
            else if (header._type == ELiveLinkMessageType::kPing)
            {
                if (!hello_completed)
                    return;
                LiveLinkPacketHeader response;
                response._type = ELiveLinkMessageType::kPong;
                response._sequence = header._sequence;
                response._payload_size = header._payload_size;
                const auto response_header = EncodeLiveLinkHeader(response);
                if (!SendExact(client_socket, response_header.data(), response_header.size())
                    || (!payload.empty() && !SendExact(client_socket, payload.data(), payload.size())))
                    return;
            }
            else if (hello_completed)
            {
                LiveLinkCommand command{};
                command._type = ELiveLinkCommandType::kMessage;
                command._message_type = static_cast<unsigned short>(header._type);
                command._sequence = header._sequence;
                command._payload = std::move(payload);
                command._receive_us = static_cast<unsigned long long>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - receive_start).count());
                if (!_commands.Push(std::move(command)))
                    return;
            }
            else
                return;
        }
    }
}
