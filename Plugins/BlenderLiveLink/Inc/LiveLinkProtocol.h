#pragma once

#include "Framework/Core/Types.h"
#include <array>
#include <span>
#include <vector>

namespace Ailu
{
    inline constexpr u32 kLiveLinkMagic = 0x4B4C4C41u;
    inline constexpr u16 kLiveLinkProtocolVersion = 1u;
    inline constexpr u32 kMaxLiveLinkPayloadSize = 256u * 1024u * 1024u;
    inline constexpr u16 kLiveLinkHeaderSize = 20u;

    enum class ELiveLinkMessageType : u16
    {
        kHello = 1,
        kHelloAck = 2,
        kPing = 3,
        kPong = 4,
        kFullSyncBegin = 10,
        kFullSyncEnd = 11,
        kObjectUpsert = 12,
        kObjectDelete = 13,
        kMeshUpsert = 20,
        kMaterialUpsert = 21,
        kLightUpsert = 22,
        kCameraUpsert = 23,
        kMeshPrototypeUpsert = 24,
        kServerStatus = 30,
        kInstanceBatchUpsert = 31,
    };

    struct LiveLinkPacketHeader
    {
        u32 _magic = kLiveLinkMagic;
        u16 _version = kLiveLinkProtocolVersion;
        ELiveLinkMessageType _type = ELiveLinkMessageType::kHello;
        u32 _payload_size = 0u;
        u64 _sequence = 0u;
    };

    using LiveLinkEncodedHeader = std::array<u8, kLiveLinkHeaderSize>;

    LiveLinkEncodedHeader EncodeLiveLinkHeader(const LiveLinkPacketHeader& header) noexcept;
    bool DecodeLiveLinkHeader(std::span<const u8, kLiveLinkHeaderSize> bytes,
        LiveLinkPacketHeader& header) noexcept;
}
