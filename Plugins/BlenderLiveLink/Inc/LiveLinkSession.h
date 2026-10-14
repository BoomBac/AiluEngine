#pragma once

#include "LiveLinkCommandQueue.h"
#include "LiveLinkMeshCache.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "Scene/Entity.h"
#include "Render/Mesh.h"
#include "Render/Material.h"
#include "Render/Texture.h"

namespace Ailu
{
    namespace SceneManagement
    {
        class Scene;
    }

    class LiveLinkSession final
    {
    public:
        explicit LiveLinkSession(bool share_meshes = true) : _mesh_cache(share_meshes ? 128u * 1024u * 1024u : 0u) {}
        void Apply(const LiveLinkCommand& command) noexcept;
        void Clear() noexcept;
        bool LastApplySucceeded() const noexcept { return _last_apply_succeeded; }
        bool LastObjectUpsertSucceeded() const noexcept { return _last_object_upsert_succeeded; }
        bool LastObjectReparentSucceeded() const noexcept { return _last_object_reparent_succeeded; }
        const std::string& LastObjectReparentDiagnostic() const noexcept { return _last_object_reparent_diagnostic; }
        size_t TrackedEntityCount() const noexcept { return _entities.size(); }

    private:
        bool Begin(const std::string& session_guid);
        std::wstring AssetPath(const std::string& category, const std::string& guid) const;
        bool UpsertObject(const std::string& payload);
        bool UpsertInstanceBatch(const std::string& payload);
        bool UpsertMesh(const std::vector<unsigned char>& payload, bool prototype_only = false);
        bool PersistMesh(const Ref<Render::Mesh>& mesh);
        void PublishMesh(const std::string& guid, ECS::Entity entity, const Ref<Render::Mesh>& mesh,
                         const std::vector<u32>& material_slots, u64 revision);
        bool UpsertMaterial(const std::string& payload);
        bool UpsertLight(const std::string& payload);
        bool UpsertCamera(const std::string& payload);
        void End() noexcept;
        void ResetSceneState() noexcept;

        SceneManagement::Scene* _scene = nullptr;
        ECS::Entity _root_entity = ECS::kInvalidEntity;
        std::string _session_guid;
        std::unordered_map<std::string, ECS::Entity> _entities;
        std::unordered_set<std::string> _seen;
        std::unordered_map<std::string, std::string> _parent_guids;
        std::unordered_map<std::string, std::string> _object_types;
        std::unordered_map<std::string, std::vector<std::string>> _entity_material_guids;
        std::unordered_map<std::string, std::vector<u32>> _mesh_material_slots;
        std::unordered_map<std::string, Ref<Render::Mesh>> _meshes;
        LiveLinkMeshCache _mesh_cache;
        std::unordered_map<std::string, Ref<Render::Material>> _materials;
        std::unordered_map<std::string, Ref<Render::Texture2D>> _textures;
        std::unordered_map<std::string, u64> _mesh_revisions;
        bool _last_apply_succeeded = false;
        bool _full_sync_active = false;
        bool _full_sync_failed = false;
        bool _needs_full_resync = true;
        bool _last_object_upsert_succeeded = false;
        bool _last_object_reparent_succeeded = false;
        std::string _last_object_reparent_diagnostic;
    };
}
