#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Ailu
{
    namespace Render { class Mesh; }

    // Session-owned acceleration only: entities remain the owners of published Mesh resources.
    class LiveLinkMeshCache final
    {
    public:
        struct Match
        {
            std::shared_ptr<Render::Mesh> _mesh;
            std::vector<std::uint32_t> _material_slots;
        };

        explicit LiveLinkMeshCache(size_t max_bytes = 128u * 1024u * 1024u) : _max_bytes(max_bytes) {}

        Match Find(std::span<const unsigned char> geometry) const
        {
            if (geometry.empty()) return {};
            const auto bucket = _entries.find(GetKey(geometry));
            if (bucket == _entries.end()) return {};
            for (const auto& entry : bucket->second)
            {
                // Sampling only selects a bucket; complete byte equality is mandatory before reuse.
                if (entry._geometry.size() != geometry.size()
                    || std::memcmp(entry._geometry.data(), geometry.data(), geometry.size()) != 0) continue;
                if (auto mesh = entry._mesh.lock()) return {std::move(mesh), entry._material_slots};
            }
            return {};
        }

        void Store(std::span<const unsigned char> geometry, const std::shared_ptr<Render::Mesh>& mesh,
                   const std::vector<std::uint32_t>& material_slots)
        {
            Prune();
            if (geometry.empty() || geometry.size() > _max_bytes) return;
            if (geometry.size() > _max_bytes - _payload_bytes) Clear();
            _entries[GetKey(geometry)].push_back({{geometry.begin(), geometry.end()}, mesh, material_slots});
            _payload_bytes += geometry.size();
        }

        void Prune()
        {
            for (auto bucket = _entries.begin(); bucket != _entries.end();)
            {
                auto& entries = bucket->second;
                std::erase_if(entries, [this](const Entry& entry)
                {
                    if (!entry._mesh.expired()) return false;
                    _payload_bytes -= entry._geometry.size();
                    return true;
                });
                if (entries.empty()) bucket = _entries.erase(bucket);
                else ++bucket;
            }
        }

        void Clear() { _entries.clear(); _payload_bytes = 0u; }
        size_t PayloadBytes() const { return _payload_bytes; }

    private:
        struct Entry
        {
            std::vector<unsigned char> _geometry;
            std::weak_ptr<Render::Mesh> _mesh;
            std::vector<std::uint32_t> _material_slots;
        };

        static size_t GetKey(std::span<const unsigned char> geometry)
        {
            constexpr size_t kSampleSize = 64u;
            const size_t sample_size = std::min(kSampleSize, geometry.size());
            size_t hash = geometry.size();
            for (size_t offset : {size_t{0u}, (geometry.size() - sample_size) / 2u, geometry.size() - sample_size})
            {
                const std::string_view sample(reinterpret_cast<const char*>(geometry.data() + offset), sample_size);
                hash ^= std::hash<std::string_view>{}(sample) + 0x9e3779b9u + (hash << 6u) + (hash >> 2u);
            }
            return hash;
        }

        std::unordered_map<size_t, std::vector<Entry>> _entries;
        size_t _payload_bytes = 0u;
        size_t _max_bytes;
    };
}
