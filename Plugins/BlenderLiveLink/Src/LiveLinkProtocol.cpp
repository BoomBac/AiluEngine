#include "LiveLinkProtocol.h"

namespace Ailu
{
    namespace
    {
        template<typename T>
        void WriteLittleEndian(u8* destination, T value) noexcept
        {
            for (size_t index = 0; index < sizeof(T); ++index)
                destination[index] = static_cast<u8>((value >> (index * 8u)) & 0xffu);
        }

        template<typename T>
        T ReadLittleEndian(const u8* source) noexcept
        {
            T value = 0;
            for (size_t index = 0; index < sizeof(T); ++index)
                value |= static_cast<T>(source[index]) << (index * 8u);
            return value;
        }
    }

    LiveLinkEncodedHeader EncodeLiveLinkHeader(const LiveLinkPacketHeader& header) noexcept
    {
        LiveLinkEncodedHeader bytes{};
        WriteLittleEndian(bytes.data(), header._magic);
        WriteLittleEndian(bytes.data() + 4u, header._version);
        WriteLittleEndian(bytes.data() + 6u, static_cast<u16>(header._type));
        WriteLittleEndian(bytes.data() + 8u, header._payload_size);
        WriteLittleEndian(bytes.data() + 12u, header._sequence);
        return bytes;
    }

    bool DecodeLiveLinkHeader(std::span<const u8, kLiveLinkHeaderSize> bytes,
        LiveLinkPacketHeader& header) noexcept
    {
        header._magic = ReadLittleEndian<u32>(bytes.data());
        header._version = ReadLittleEndian<u16>(bytes.data() + 4u);
        header._type = static_cast<ELiveLinkMessageType>(ReadLittleEndian<u16>(bytes.data() + 6u));
        header._payload_size = ReadLittleEndian<u32>(bytes.data() + 8u);
        header._sequence = ReadLittleEndian<u64>(bytes.data() + 12u);
        return header._magic == kLiveLinkMagic && header._version == kLiveLinkProtocolVersion
            && header._payload_size <= kMaxLiveLinkPayloadSize;
    }
}
