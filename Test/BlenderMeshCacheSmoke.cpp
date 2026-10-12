#include "Render/Mesh.h"
#include "../Plugins/BlenderLiveLink/Inc/LiveLinkMeshCache.h"
#include <iostream>
#include <stdexcept>

static void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

int main()
{
    try
    {
        Ailu::LiveLinkMeshCache cache(2048u);
        std::vector<unsigned char> geometry(512u, 7u);
        auto first = std::make_shared<Ailu::Render::Mesh>("cache_first");
        cache.Store(geometry, first, {2u, 0u});
        auto match = cache.Find(geometry);
        Require(match._mesh == first && match._material_slots == std::vector<std::uint32_t>{2u, 0u},
                "Identical geometry must retain mesh identity and material slot mapping");

        // This byte is outside all fingerprint samples; a bucket collision must never share geometry.
        auto changed = geometry;
        changed[100u] ^= 1u;
        Require(!cache.Find(changed)._mesh, "Unsampled geometry changes must not alias the original mesh");
        auto second = std::make_shared<Ailu::Render::Mesh>("cache_second");
        cache.Store(changed, second, {1u});
        Require(cache.Find(changed)._mesh == second && cache.Find(geometry)._mesh == first,
                "Different meshes in one fingerprint bucket must resolve independently");

        match = {};
        first.reset();
        cache.Prune();
        Require(!cache.Find(geometry)._mesh && cache.PayloadBytes() == changed.size(),
                "Cache must not own retired meshes and must release their canonical bytes");
        cache.Clear();
        Require(second && !cache.Find(changed)._mesh && cache.PayloadBytes() == 0u,
                "Cache clearing must preserve entity-owned mesh lifetime");
        Ailu::LiveLinkMeshCache bounded(600u);
        bounded.Store(geometry, second, {0u});
        bounded.Store(changed, second, {1u});
        Require(bounded.PayloadBytes() == changed.size() && !bounded.Find(geometry)._mesh,
                "Canonical payload storage must remain bounded");
        bounded.Store(std::vector<unsigned char>(601u, 0u), second, {0u});
        Require(bounded.PayloadBytes() == changed.size(), "Oversized payload must bypass the cache");
        Require(!bounded.Find({})._mesh, "Empty geometry must not match");
        std::cout << "BlenderMeshCacheSmoke PASS: identity, byte collision, slots, weak lifetime, capacity\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "BlenderMeshCacheSmoke FAIL: " << error.what() << '\n';
        return 1;
    }
}
