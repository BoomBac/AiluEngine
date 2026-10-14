#pragma once

#include <deque>
#include <chrono>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace Ailu
{
    enum class ELiveLinkCommandType
    {
        kClientConnected,
        kClientDisconnected,
        kClientRejected,
        kHelloAccepted,
        kProtocolRejected,
        kMessage,
        kQueueOverflow,
    };

    struct LiveLinkCommand
    {
        ELiveLinkCommandType _type{};
        unsigned short _protocol_version = 0u;
        unsigned short _message_type = 0u;
        unsigned long long _sequence = 0u;
        std::vector<unsigned char> _payload;
        std::chrono::steady_clock::time_point _queued_at{};
        unsigned long long _receive_us = 0u;
    };

    class LiveLinkCommandQueue final
    {
    public:
        bool WaitForCapacity(size_t payload_bytes, const std::atomic<bool>& stopping)
        {
            if (payload_bytes > kMaxBytes)
                return false;
            std::unique_lock lock(_mutex);
            while (!stopping.load() && !HasCapacity(payload_bytes))
                _capacity_changed.wait_for(lock, std::chrono::milliseconds(50));
            return !stopping.load() && HasCapacity(payload_bytes);
        }

        bool Push(LiveLinkCommand command)
        {
            std::lock_guard lock(_mutex);
            command._queued_at = std::chrono::steady_clock::now();
            const std::string key = CoalesceKey(command);
            if (command._type == ELiveLinkCommandType::kMessage && command._message_type == 13u && !key.empty())
            {
                for (auto iter = _commands.begin(); iter != _commands.end();)
                {
                    if (iter->_type == ELiveLinkCommandType::kMessage
                        && (iter->_message_type == 12u || iter->_message_type == 20u || iter->_message_type == 24u
                            || iter->_message_type == 22u || iter->_message_type == 23u)
                        && CoalesceKey(*iter) == key)
                    {
                        _queued_bytes -= iter->_payload.size();
                        iter = _commands.erase(iter);
                    }
                    else
                        ++iter;
                }
            }
            if (!key.empty())
            {
                for (auto iter = _commands.begin(); iter != _commands.end();)
                {
                    if (iter->_type == ELiveLinkCommandType::kMessage
                        && iter->_message_type == command._message_type && CoalesceKey(*iter) == key)
                    {
                        _queued_bytes -= iter->_payload.size();
                        iter = _commands.erase(iter);
                    }
                    else
                        ++iter;
                }
            }
            if (_commands.size() >= kMaxCommands || command._payload.size() > kMaxBytes - _queued_bytes)
            {
                _commands.clear();
                _queued_bytes = 0u;
                _commands.push_back({ELiveLinkCommandType::kQueueOverflow});
                return false;
            }
            _queued_bytes += command._payload.size();
            _commands.emplace_back(std::move(command));
            return true;
        }

        bool TryPop(LiveLinkCommand& command)
        {
            std::lock_guard lock(_mutex);
            if (_commands.empty())
                return false;
            _queued_bytes -= _commands.front()._payload.size();
            command = std::move(_commands.front());
            _commands.pop_front();
            _capacity_changed.notify_all();
            return true;
        }

        void Clear()
        {
            std::lock_guard lock(_mutex);
            _commands.clear();
            _queued_bytes = 0u;
            _capacity_changed.notify_all();
        }

    private:
        bool HasCapacity(size_t payload_bytes) const noexcept
        {
            return _commands.size() + 1u < kMaxCommands && payload_bytes <= kMaxBytes - _queued_bytes;
        }

        static std::string CoalesceKey(const LiveLinkCommand& command)
        {
            if (command._type != ELiveLinkCommandType::kMessage)
                return {};
            if (command._message_type == 31u)
                return {}; // A batch carries many identities and cannot be safely coalesced by its first GUID.
            const auto& payload = command._payload;
            if ((command._message_type == 20u || command._message_type == 24u) && payload.size() >= 36u)
                return std::string(payload.begin(), payload.begin() + 36);
            const std::string_view text(reinterpret_cast<const char*>(payload.data()), payload.size());
            size_t start = text.find("\"material_guid\"");
            if (start == std::string_view::npos)
                start = text.find("\"guid\"");
            if (start == std::string_view::npos)
                return {};
            const size_t colon = text.find(':', start + 6u);
            if (colon == std::string_view::npos)
                return {};
            const size_t first_quote = text.find('"', colon + 1u);
            if (first_quote == std::string_view::npos)
                return {};
            const size_t end_quote = text.find('"', first_quote + 1u);
            if (end_quote == std::string_view::npos)
                return {};
            return std::string(text.substr(first_quote + 1u, end_quote - first_quote - 1u));
        }

        static constexpr size_t kMaxCommands = 4096u;
        static constexpr size_t kMaxBytes = 256u * 1024u * 1024u;
        std::mutex _mutex;
        std::condition_variable _capacity_changed;
        std::deque<LiveLinkCommand> _commands;
        size_t _queued_bytes = 0u;
    };
}
