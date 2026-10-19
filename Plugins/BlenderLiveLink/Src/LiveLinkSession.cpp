#include "LiveLinkSession.h"
#include "LiveLinkProtocol.h"
#include "Assets/Asset.h"
#include <stdexcept>
#include "Framework/Common/Application.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Math/Guid.h"
#include "Render/Mesh.h"
#include "Scene/Scene.h"
#include <document.h>
#include <stringbuffer.h>
#include <writer.h>
#include <bit>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <limits>
#include <string_view>

namespace Ailu
{
    namespace
    {
        using Scene = SceneManagement::Scene;
        constexpr std::string_view kLiveLinkRootPrefix = "__BlenderLiveLink_";

        std::string_view TextureAssetSuffix(const char *field_name) noexcept
        {
            const std::string_view name(field_name != nullptr ? field_name : "");
            if (name == "base_color_path") return "albedo";
            if (name == "normal_path") return "normal";
            if (name == "emission_path") return "emission";
            if (name == "roughness_path") return "roughness";
            if (name == "metallic_path") return "metallic";
            return "texture";
        }

        bool IsSourceGuid(std::string_view value) noexcept
        {
            if (value.size() != 36u) return false;
            for (size_t index = 0u; index < value.size(); ++index)
            {
                if (index == 8u || index == 13u || index == 18u || index == 23u)
                {
                    if (value[index] != '-') return false;
                }
                else if (!std::isxdigit(static_cast<unsigned char>(value[index]))) return false;
            }
            return true;
        }

        bool IsLiveLinkRoot(const ECS::TagComponent& tag) noexcept
        {
            const std::string_view name(tag._name);
            return !tag._source_scene_guid.empty() && tag._source_object_guid.empty()
                && name.starts_with(kLiveLinkRootPrefix);
        }

        bool ReadString(const rapidjson::Value& object, const char* key, std::string& value)
        {
            const auto iter = object.FindMember(key);
            if (iter == object.MemberEnd() || !iter->value.IsString())
                return false;
            value.assign(iter->value.GetString(), iter->value.GetStringLength());
            return true;
        }

        bool ReadVector3(const rapidjson::Value& object, const char* key, Vector3f& value)
        {
            const auto iter = object.FindMember(key);
            if (iter == object.MemberEnd() || !iter->value.IsArray() || iter->value.Size() != 3u)
                return false;
            for (rapidjson::SizeType index = 0; index < 3u; ++index)
            {
                if (!iter->value[index].IsNumber())
                    return false;
            }
            value = Vector3f(iter->value[0].GetFloat(), iter->value[1].GetFloat(), iter->value[2].GetFloat());
            return true;
        }

        bool ReadQuaternion(const rapidjson::Value& object, Quaternion& value)
        {
            const auto iter = object.FindMember("rotation");
            if (iter == object.MemberEnd() || !iter->value.IsArray() || iter->value.Size() != 4u)
                return false;
            for (rapidjson::SizeType index = 0; index < 4u; ++index)
            {
                if (!iter->value[index].IsNumber())
                    return false;
            }
            const f32 x = iter->value[0].GetFloat();
            const f32 y = iter->value[1].GetFloat();
            const f32 z = iter->value[2].GetFloat();
            const f32 w = iter->value[3].GetFloat();
            const f32 length = std::sqrt(x * x + y * y + z * z + w * w);
            if (!std::isfinite(length) || length < 1.0e-6f)
                return false;
            value = Quaternion(x / length, y / length, z / length, w / length);
            return true;
        }

        Scene* ActiveEditScene() noexcept
        {
            if (Application::Get()._is_playing_mode)
                return nullptr;
            auto& manager = SceneManagement::SceneMgr::Get();
            if (manager.IsTemporaryScene())
                return nullptr;
            return manager.ActiveScene();
        }

        template<typename T>
        bool ReadValue(const std::vector<unsigned char>& payload, size_t& offset, T& value)
        {
            if (offset > payload.size() || sizeof(T) > payload.size() - offset)
                return false;
            if constexpr (sizeof(T) == 4u && std::is_floating_point_v<T>)
            {
                u32 bits = static_cast<u32>(payload[offset])
                    | (static_cast<u32>(payload[offset + 1u]) << 8u)
                    | (static_cast<u32>(payload[offset + 2u]) << 16u)
                    | (static_cast<u32>(payload[offset + 3u]) << 24u);
                value = std::bit_cast<T>(bits);
            }
            else
            {
                using U = std::make_unsigned_t<T>;
                U bits = 0u;
                for (size_t byte = 0u; byte < sizeof(T); ++byte)
                    bits |= static_cast<U>(payload[offset + byte]) << (byte * 8u);
                value = static_cast<T>(bits);
            }
            offset += sizeof(T);
            return true;
        }
    }

    void LiveLinkSession::Apply(const LiveLinkCommand& command) noexcept
    {
        _last_apply_succeeded = false;
        try
        {
            if (command._type == ELiveLinkCommandType::kClientDisconnected)
            {
                _full_sync_active = false;
                _needs_full_resync = true;
                _seen.clear();
                return;
            }
            if (command._type == ELiveLinkCommandType::kQueueOverflow)
            {
                _full_sync_active = false;
                _needs_full_resync = true;
                _seen.clear();
                return;
            }
            if (command._type != ELiveLinkCommandType::kMessage)
                return;
            if (command._message_type == static_cast<unsigned short>(ELiveLinkMessageType::kObjectUpsert))
            {
                _last_object_upsert_succeeded = false;
                _last_object_reparent_succeeded = false;
                _last_object_reparent_diagnostic.clear();
            }

            Scene* active_scene = ActiveEditScene();
            if (active_scene == nullptr)
            {
                _needs_full_resync = true;
                return;
            }
            if (_scene != nullptr && _scene != active_scene)
                ResetSceneState();

            const auto type = static_cast<ELiveLinkMessageType>(command._message_type);
            if (type == ELiveLinkMessageType::kFullSyncBegin)
            {
                rapidjson::Document document;
                document.Parse(reinterpret_cast<const char*>(command._payload.data()), command._payload.size());
                std::string session_guid;
                if (!document.IsObject() || !ReadString(document, "session_guid", session_guid))
                    return;
                _last_apply_succeeded = Begin(session_guid);
            }
            else if (_scene == nullptr || _needs_full_resync)
            {
                return; // Never apply a delta to a different scene or an interrupted baseline.
            }
            else if (type == ELiveLinkMessageType::kObjectUpsert)
            {
                _last_apply_succeeded = _last_object_upsert_succeeded = UpsertObject(std::string(
                    reinterpret_cast<const char*>(command._payload.data()), command._payload.size()));
            }
            else if (type == ELiveLinkMessageType::kObjectDelete)
            {
                rapidjson::Document document;
                document.Parse(reinterpret_cast<const char*>(command._payload.data()), command._payload.size());
                std::string guid;
                if (document.IsObject() && ReadString(document, "guid", guid))
                {
                    _last_apply_succeeded = true;
                    const auto iter = _entities.find(guid);
                    if (iter != _entities.end())
                    {
                        _scene->RemoveObject(iter->second);
                        _meshes.erase(guid);
                        _mesh_cache.Prune();
                        _mesh_revisions.erase(guid);
                        _mesh_material_slots.erase(guid);
                        _entity_material_guids.erase(guid);
                        _object_types.erase(guid);
                        _entities.erase(iter);
                        _parent_guids.erase(guid);
                    }
                }
            }
            else if (type == ELiveLinkMessageType::kMeshUpsert)
                _last_apply_succeeded = UpsertMesh(command._payload);
            else if (type == ELiveLinkMessageType::kMeshPrototypeUpsert)
                _last_apply_succeeded = UpsertMesh(command._payload, true);
            else if (type == ELiveLinkMessageType::kMeshStaticBatchUpsert)
                _last_apply_succeeded = UpsertMesh(command._payload, true, false, true);
            else if (type == ELiveLinkMessageType::kInstanceBatchUpsert)
                _last_apply_succeeded = UpsertInstanceBatch(std::string(
                    reinterpret_cast<const char*>(command._payload.data()), command._payload.size()));
            else if (type == ELiveLinkMessageType::kMaterialUpsert)
                _last_apply_succeeded = UpsertMaterial(std::string(reinterpret_cast<const char*>(command._payload.data()),
                    command._payload.size()));
            else if (type == ELiveLinkMessageType::kLightUpsert)
                _last_apply_succeeded = UpsertLight(std::string(reinterpret_cast<const char*>(command._payload.data()),
                    command._payload.size()));
            else if (type == ELiveLinkMessageType::kCameraUpsert)
                _last_apply_succeeded = UpsertCamera(std::string(reinterpret_cast<const char*>(command._payload.data()),
                    command._payload.size()));
            else if (type == ELiveLinkMessageType::kSceneSettingsUpsert)
                _last_apply_succeeded = UpsertSceneSettings(std::string(
                    reinterpret_cast<const char*>(command._payload.data()), command._payload.size()));
            else if (type == ELiveLinkMessageType::kWorldUpsert)
                _last_apply_succeeded = UpsertWorld(std::string(
                    reinterpret_cast<const char*>(command._payload.data()), command._payload.size()));
            else if (type == ELiveLinkMessageType::kFullSyncEnd)
            {
                _last_apply_succeeded = _full_sync_active && !_needs_full_resync;
                End();
                _last_apply_succeeded &= !_needs_full_resync;
            }
            else
                _last_apply_succeeded = true;
            if (_full_sync_active && !_last_apply_succeeded) _full_sync_failed = true;
            if (_last_apply_succeeded && _scene) _scene->MarkEdited();
        }
        catch (const std::exception &exception)
        {
            LOG_ERROR("[BlenderLiveLink] Apply exception: type={}, sequence={}: {}", command._message_type,
                command._sequence, exception.what());
            _last_apply_succeeded = false;
            _needs_full_resync = true;
        }
        catch (...)
        {
            LOG_ERROR("[BlenderLiveLink] Apply exception: type={}, sequence={}: unknown exception",
                command._message_type, command._sequence);
            _last_apply_succeeded = false;
            _needs_full_resync = true;
        }
    }

    std::wstring LiveLinkSession::AssetPath(const std::string& category, const std::string& guid) const
    {
        return ToWChar("project://BlenderLiveLink/" + _session_guid + "/" + category + "/" + guid + ".alasset");
    }

    bool LiveLinkSession::Begin(const std::string& session_guid)
    {
        Scene* active_scene = ActiveEditScene();
        const Guid parsed_guid(session_guid);
        if (!active_scene || !IsSourceGuid(session_guid)) return false;
        // Forget runtime handles without deleting the durable imported scene.
        ResetSceneState();
        _scene = active_scene;
        _session_guid = parsed_guid.ToString();
        auto& registry = _scene->GetRegister();
        u32 index = 0u;
        for (const auto& tag : registry.View<ECS::TagComponent>())
        {
            const ECS::Entity entity = registry.GetEntity<ECS::TagComponent>(index++);
            if (tag._source_scene_guid != _session_guid) continue;
            if (IsLiveLinkRoot(tag))
                _root_entity = entity;
            else if (!tag._source_object_guid.empty())
            {
                _entities[tag._source_object_guid] = entity;
                if (registry.GetComponent<ECS::StaticMeshComponent>(entity)) _object_types[tag._source_object_guid] = "MESH";
                else if (registry.GetComponent<ECS::LightComponent>(entity)) _object_types[tag._source_object_guid] = "LIGHT";
                else if (registry.GetComponent<ECS::CCamera>(entity)) _object_types[tag._source_object_guid] = "CAMERA";
                else _object_types[tag._source_object_guid] = "EMPTY";
            }
        }
        if (_root_entity == ECS::kInvalidEntity)
            _root_entity = _scene->AddObject("__BlenderLiveLink_" + _session_guid.substr(0u, 8u), Guid::Generate());
        auto* root_tag = registry.GetComponent<ECS::TagComponent>(_root_entity);
        if (!root_tag) return false;
        root_tag->_is_transient = false;
        root_tag->_source_scene_guid = _session_guid;
        root_tag->_source_object_guid.clear();
        _full_sync_active = true;
        _full_sync_failed = false;
        _needs_full_resync = false;
        return true;
    }

    bool LiveLinkSession::UpsertObject(const std::string& payload)
    {
        Scene* active_scene = ActiveEditScene();
        if (_scene == nullptr || _scene != active_scene || (_needs_full_resync && !_full_sync_active))
        {
            _last_object_reparent_diagnostic = std::string("session_scene=")
                + (_scene == nullptr ? "null" : "set")
                + ", active_scene=" + (active_scene == nullptr ? "null" : "set")
                + ", full_sync=" + (_full_sync_active ? "true" : "false")
                + ", needs_resync=" + (_needs_full_resync ? "true" : "false");
            _needs_full_resync = true;
            return false;
        }
        rapidjson::Document document;
        document.Parse(payload.data(), payload.size());
        if (!document.IsObject())
            return false;

        std::string guid_text;
        std::string name;
        std::string parent_guid;
        std::string object_type;
        if (!ReadString(document, "guid", guid_text) || !ReadString(document, "name", name))
            return false;
        ReadString(document, "parent_guid", parent_guid);
        ReadString(document, "object_type", object_type);
        Guid guid(guid_text);
        if (!IsSourceGuid(guid_text))
            return false;

        ECS::Entity entity = ECS::kInvalidEntity;
        const auto mapped = _entities.find(guid_text);
        if (mapped != _entities.end() && _scene->IsValidEntity(mapped->second))
            entity = mapped->second;
        else
        {
            entity = _scene->FindEntity(guid);
            if (entity != ECS::kInvalidEntity)
            {
                const auto* existing_tag = _scene->GetRegister().GetComponent<ECS::TagComponent>(entity);
                if (!existing_tag || (!existing_tag->_is_transient
                    && (existing_tag->_source_scene_guid != _session_guid || existing_tag->_source_object_guid != guid_text)))
                    entity = ECS::kInvalidEntity; // Source identity is scoped; do not take ownership of another source.
            }
            if (entity == ECS::kInvalidEntity)
                entity = _scene->AddObject(name, _scene->HasEntityGuid(guid) ? Guid::Generate() : guid, false);
            _entities[guid_text] = entity;
        }
        auto& registry = _scene->GetRegister();
        const auto old_type = _object_types.find(guid_text);
        if (old_type != _object_types.end() && old_type->second != object_type)
        {
            if (object_type != "MESH")
                registry.RemoveComponent<ECS::StaticMeshComponent>(entity);
            if (object_type != "LIGHT")
                registry.RemoveComponent<ECS::LightComponent>(entity);
            if (object_type != "CAMERA")
                registry.RemoveComponent<ECS::CCamera>(entity);
            _meshes.erase(guid_text);
            _mesh_revisions.erase(guid_text);
            _mesh_material_slots.erase(guid_text);
        }
        _object_types[guid_text] = object_type;
        auto* tag = registry.GetComponent<ECS::TagComponent>(entity);
        if (tag == nullptr)
            return false;
        tag->_is_transient = false;
        tag->_source_scene_guid = _session_guid;
        tag->_source_object_guid = guid_text;
        tag->_name = name;
        const auto visible_field = document.FindMember("visible");
        if (visible_field != document.MemberEnd() && visible_field->value.IsBool())
            _scene->SetEntityEnabled(entity, visible_field->value.GetBool());

        Vector3f position;
        Vector3f scale;
        Quaternion rotation;
        auto* transform = registry.GetComponent<ECS::TransformComponent>(entity);
        if (transform != nullptr && ReadVector3(document, "position", position)
            && ReadVector3(document, "scale", scale) && ReadQuaternion(document, rotation))
        {
            transform->SetLocalPosition(position);
            transform->SetLocalRotation(rotation);
            transform->SetLocalScale(scale);
        }
        if (_full_sync_active)
            _seen.insert(guid_text);
        _parent_guids[guid_text] = parent_guid;
        std::vector<std::string> material_guids;
        const auto material_array = document.FindMember("material_guids");
        if (material_array != document.MemberEnd() && material_array->value.IsArray())
        {
            for (const auto& material : material_array->value.GetArray())
            {
                if (material.IsString())
                    material_guids.emplace_back(material.GetString(), material.GetStringLength());
            }
        }
        _entity_material_guids[guid_text] = std::move(material_guids);

        const auto parent_iter = parent_guid.empty() ? _entities.end() : _entities.find(parent_guid);
        const ECS::Entity parent = parent_iter == _entities.end() ? _root_entity : parent_iter->second;
        const auto* child_hierarchy = registry.GetComponent<ECS::CHierarchy>(entity);
        const auto* parent_hierarchy = registry.GetComponent<ECS::CHierarchy>(parent);
        const bool child_valid = _scene->IsValidEntity(entity);
        const bool parent_valid = _scene->IsValidEntity(parent);
        const bool would_cycle = parent_valid && child_valid && _scene->IsDescendantOf(parent, entity);
        _last_object_reparent_diagnostic = "child=" + std::to_string(entity)
            + ", parent=" + std::to_string(parent)
            + ", scene_root=" + std::to_string(_scene->SceneRoot())
            + ", child_valid=" + (child_valid ? "true" : "false")
            + ", parent_valid=" + (parent_valid ? "true" : "false")
            + ", child_hierarchy=" + (child_hierarchy ? "true" : "false")
            + ", parent_hierarchy=" + (parent_hierarchy ? "true" : "false")
            + ", child_is_scene_root=" + (_scene->IsSceneRoot(entity) ? "true" : "false")
            + ", same_entity=" + (entity == parent ? "true" : "false")
            + ", would_cycle=" + (would_cycle ? "true" : "false");
        if (parent != ECS::kInvalidEntity && entity != parent)
        {
            _last_object_reparent_succeeded = _scene->Reparent(entity, parent, false);
        }
        if (!_last_object_reparent_succeeded)
            return false;
        return true;
    }

    bool LiveLinkSession::UpsertInstanceBatch(const std::string& payload)
    {
        rapidjson::Document document;
        document.Parse(payload.data(), payload.size());
        std::string prototype_guid;
        if (!document.IsObject() || !ReadString(document, "prototype_guid", prototype_guid)
            || !IsSourceGuid(prototype_guid))
            return false;
        const auto prototype_mesh = _meshes.find(prototype_guid);
        const auto material_slots = _mesh_material_slots.find(prototype_guid);
        if (prototype_mesh == _meshes.end() || material_slots == _mesh_material_slots.end())
            return false;
        const auto instances = document.FindMember("instances");
        if (instances == document.MemberEnd() || !instances->value.IsArray() || instances->value.Empty()
            || instances->value.Size() > 128u)
            return false;

        for (const auto& instance : instances->value.GetArray())
        {
            if (!instance.IsObject())
                return false;
            rapidjson::StringBuffer buffer;
            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
            instance.Accept(writer);
            if (!UpsertObject(std::string(buffer.GetString(), buffer.GetSize())))
                return false;
            std::string guid_text;
            if (!ReadString(instance, "guid", guid_text))
                return false;
            const auto entity = _entities.find(guid_text);
            if (entity == _entities.end() || !_scene->IsValidEntity(entity->second))
                return false;
            const auto revision = _mesh_revisions.find(prototype_guid);
            PublishMesh(guid_text, entity->second, prototype_mesh->second, material_slots->second,
                        revision == _mesh_revisions.end() ? 1u : revision->second);
        }
        return true;
    }

    bool LiveLinkSession::UpsertMesh(const std::vector<unsigned char>& payload, bool prototype_only,
                                     bool build_derived_data, bool transient)
    {
        constexpr size_t kGuidSize = 36u;
        constexpr size_t kFixedHeaderSize = kGuidSize + sizeof(u64) + sizeof(u32) * 2u + sizeof(f32) * 6u;
        if (payload.size() < kFixedHeaderSize || _scene == nullptr)
            return false;
        const std::string guid_text(reinterpret_cast<const char*>(payload.data()), kGuidSize);
        const auto entity_iter = _entities.find(guid_text);
        if (!prototype_only && (entity_iter == _entities.end() || !_scene->IsValidEntity(entity_iter->second)))
            return false;

        size_t offset = kGuidSize;
        u64 revision = 0u;
        u32 vertex_count = 0u;
        u32 submesh_count = 0u;
        std::array<f32, 6> bounds_values{};
        if (!ReadValue(payload, offset, revision) || !ReadValue(payload, offset, vertex_count)
            || !ReadValue(payload, offset, submesh_count) || vertex_count == 0u || submesh_count == 0u
            || vertex_count > 16u * 1024u * 1024u || submesh_count > 65535u)
            return false;
        for (f32& value : bounds_values)
        {
            if (!ReadValue(payload, offset, value) || !std::isfinite(value))
                return false;
        }
        const u64 vertex_bytes = static_cast<u64>(vertex_count) * (3u + 3u + 4u + 2u) * sizeof(f32);
        if (vertex_bytes > payload.size() - offset)
            return false;

        // GUID and revision identify the object update; the remaining validated bytes identify shared geometry.
        const std::span<const unsigned char> geometry(payload.data() + kGuidSize + sizeof(u64),
                                                      payload.size() - kGuidSize - sizeof(u64));
        if (auto cached = _mesh_cache.Find(geometry); cached._mesh)
        {
            const auto previous_revision = _mesh_revisions.find(guid_text);
            if (previous_revision == _mesh_revisions.end() || revision > previous_revision->second)
            {
                if (prototype_only)
                {
                    _meshes[guid_text] = cached._mesh;
                    _mesh_material_slots[guid_text] = cached._material_slots;
                    _mesh_revisions[guid_text] = revision;
                    if (transient)
                        _transient_meshes.insert(cached._mesh.get());
                    else
                        _transient_meshes.erase(cached._mesh.get());
                }
                else
                    PublishMesh(guid_text, entity_iter->second, cached._mesh, cached._material_slots, revision);
            }
            return true;
        }

        Vector<Vector3f> positions;
        Vector<Vector3f> normals;
        Vector<Vector4f> tangents;
        Vector<Vector2f> uvs;
        positions.reserve(vertex_count);
        normals.reserve(vertex_count);
        tangents.reserve(vertex_count);
        uvs.reserve(vertex_count);
        auto read_float = [&payload, &offset](f32& value)
        {
            return ReadValue(payload, offset, value) && std::isfinite(value);
        };
        for (u32 index = 0u; index < vertex_count; ++index)
        {
            f32 x = 0.f, y = 0.f, z = 0.f;
            if (!read_float(x) || !read_float(y) || !read_float(z))
                return false;
            positions.emplace_back(x, y, z);
        }
        for (u32 index = 0u; index < vertex_count; ++index)
        {
            f32 x = 0.f, y = 0.f, z = 0.f;
            if (!read_float(x) || !read_float(y) || !read_float(z))
                return false;
            normals.emplace_back(x, y, z);
        }
        for (u32 index = 0u; index < vertex_count; ++index)
        {
            f32 x = 0.f, y = 0.f, z = 0.f, w = 0.f;
            if (!read_float(x) || !read_float(y) || !read_float(z) || !read_float(w))
                return false;
            tangents.emplace_back(x, y, z, w);
        }
        for (u32 index = 0u; index < vertex_count; ++index)
        {
            f32 x = 0.f, y = 0.f;
            if (!read_float(x) || !read_float(y))
                return false;
            uvs.emplace_back(x, y);
        }

        Vector<Vector<u32>> submeshes;
        std::vector<u32> material_slots;
        submeshes.reserve(submesh_count);
        u64 total_indices = 0u;
        for (u32 index = 0u; index < submesh_count; ++index)
        {
            u32 material_slot = 0u;
            u32 index_count = 0u;
            if (!ReadValue(payload, offset, material_slot) || !ReadValue(payload, offset, index_count)
                || index_count % 3u != 0u || index_count > vertex_count * 3ull
                || static_cast<u64>(index_count) * sizeof(u32) > payload.size() - offset)
                return false;
            total_indices += index_count;
            if (total_indices > static_cast<u64>(vertex_count) * 3u)
                return false;
            Vector<u32> indices;
            material_slots.push_back(material_slot);
            indices.reserve(index_count);
            for (u32 index_value = 0u; index_value < index_count; ++index_value)
            {
                u32 value = 0u;
                if (!ReadValue(payload, offset, value) || value >= vertex_count)
                    return false;
                indices.push_back(value);
            }
            submeshes.push_back(std::move(indices));
        }
        if (offset != payload.size())
            return false;

        const auto previous_revision = _mesh_revisions.find(guid_text);
        if (previous_revision != _mesh_revisions.end() && revision <= previous_revision->second)
            return true; // A stale/duplicate mesh is already applied, not an import failure.
        const auto* existing = prototype_only ? nullptr
            : _scene->GetRegister().GetComponent<ECS::StaticMeshComponent>(entity_iter->second);
        const auto equal_bytes = [](const auto& source, const auto& incoming)
        {
            return source.size() == incoming.size() && (source.empty()
                || std::memcmp(source.data(), incoming.data(), source.size() * sizeof(incoming[0])) == 0);
        };
        if (existing && existing->_p_mesh)
        {
            const auto& previous = existing->_p_mesh;
            bool identical = previous->SubmeshCount() == submeshes.size() && equal_bytes(previous->GetVertices(), positions)
                && equal_bytes(previous->GetNormals(), normals) && equal_bytes(previous->GetTangents(), tangents)
                && equal_bytes(previous->GetUVs(), uvs);
            for (u16 index = 0u; identical && index < previous->SubmeshCount(); ++index)
                identical = equal_bytes(previous->GetIndices(index), submeshes[index]);
            if (identical)
            {
                PublishMesh(guid_text, entity_iter->second, previous, material_slots, revision);
                _mesh_cache.Store(geometry, previous, material_slots);
                return true;
            }
        }
        const AABB bounds(Vector3f(bounds_values[0], bounds_values[1], bounds_values[2]),
            Vector3f(bounds_values[3], bounds_values[4], bounds_values[5]));
        auto mesh = Render::Mesh::CreateFromData("BlenderLiveLink_" + guid_text, std::move(positions),
            std::move(normals), std::move(tangents), std::move(uvs), std::move(submeshes), bounds, build_derived_data);
        if (prototype_only)
        {
            if (transient)
                _transient_meshes.insert(mesh.get());
            else if (!PersistMesh(mesh))
                throw std::runtime_error("Live Link prototype mesh persistence failed");
            _meshes[guid_text] = mesh;
            _mesh_material_slots[guid_text] = material_slots;
            _mesh_revisions[guid_text] = revision;
        }
        else
            PublishMesh(guid_text, entity_iter->second, mesh, material_slots, revision);
        _mesh_cache.Store(geometry, mesh, material_slots);
        return true;
    }

    bool LiveLinkSession::PersistMesh(const Ref<Render::Mesh>& mesh)
    {
        auto& resources = ResourceMgr::Get();
        if (!resources.GetAssetGuid(mesh.get()).IsEmpty())
            return true;
        // Published meshes may be shared by objects which diverge later; never overwrite their durable source.
        auto* asset = resources.CreateAsset(AssetPath("Meshes", Guid::Generate().ToString()), mesh);
        return asset && resources.SaveAsset(asset);
    }

    void LiveLinkSession::PublishMesh(const std::string& guid, ECS::Entity entity, const Ref<Render::Mesh>& mesh,
                                     const std::vector<u32>& material_slots, u64 revision)
    {
        auto& resources = ResourceMgr::Get();
        if (!_transient_meshes.contains(mesh.get()) && !PersistMesh(mesh))
            throw std::runtime_error("Live Link mesh persistence failed");
        auto& registry = _scene->GetRegister();
        auto* component = registry.GetComponent<ECS::StaticMeshComponent>(entity);
        if (component == nullptr)
            component = &registry.AddComponent<ECS::StaticMeshComponent>(entity);
        component->_p_mesh = mesh;
        component->_mesh_guid = resources.GetAssetGuid(mesh.get());
        component->_material_guids.clear();
        component->_transformed_aabbs.resize(mesh->BoundBox().size());
        component->_p_mats.clear();
        const auto object_materials = _entity_material_guids.find(guid);
        if (object_materials != _entity_material_guids.end())
        {
            for (u32 slot : material_slots)
            {
                if (slot < object_materials->second.size())
                {
                    const auto material = _materials.find(object_materials->second[slot]);
                    if (material != _materials.end())
                    {
                        component->_p_mats.push_back(material->second);
                        continue;
                    }
                }
                component->_p_mats.push_back(Render::Material::s_checker.lock());
            }
        }
        else
            component->_p_mats.push_back(Render::Material::s_checker.lock());
        _meshes[guid] = mesh;
        _mesh_material_slots[guid] = material_slots;
        _mesh_revisions[guid] = revision;
        _mesh_cache.Prune();
    }

    bool LiveLinkSession::UpsertMaterial(const std::string& payload)
    {
        rapidjson::Document document;
        document.Parse(payload.data(), payload.size());
        std::string material_guid;
        if (!document.IsObject() || !ReadString(document, "material_guid", material_guid))
            return false;
        if (!IsSourceGuid(material_guid)) return false;
        auto iter = _materials.find(material_guid);
        if (iter == _materials.end())
        {
            std::string material_name;
            ReadString(document, "name", material_name);
            if (material_name.empty())
                material_name = "BlenderLiveLink_" + material_guid;
            auto material = Render::Material::CreateStandard(material_name);
            if (!material)
                return false;
            iter = _materials.emplace(material_guid, std::move(material)).first;
        }
        const auto read_float = [&document](const char* key, f32& output)
        {
            const auto field = document.FindMember(key);
            if (field != document.MemberEnd() && field->value.IsNumber())
                output = field->value.GetFloat();
        };
        f32 roughness = 0.5f, metallic = 0.f, emission_strength = 0.f, transmission = 0.f, ior = 1.5f;
        read_float("roughness", roughness);
        read_float("metallic", metallic);
        read_float("emission_strength", emission_strength);
        read_float("transmission", transmission);
        read_float("ior", ior);
        const auto alpha_test_field = document.FindMember("alpha_test");
        const bool alpha_test = alpha_test_field != document.MemberEnd() && alpha_test_field->value.IsBool()
            && alpha_test_field->value.GetBool();
        const auto alpha_blend_field = document.FindMember("alpha_blend");
        const bool alpha_blend = alpha_blend_field != document.MemberEnd() && alpha_blend_field->value.IsBool()
            && alpha_blend_field->value.GetBool();
        const auto double_sided_field = document.FindMember("double_sided");
        const bool double_sided = double_sided_field != document.MemberEnd() && double_sided_field->value.IsBool()
            && double_sided_field->value.GetBool();
        f32 alpha_cutoff = 0.5f;
        read_float("alpha_cutoff", alpha_cutoff);
        const bool thin_glass = transmission > 0.f;
        iter->second->SurfaceType((alpha_blend || thin_glass) ? Render::ESurfaceType::kTransparent
            : (alpha_test ? Render::ESurfaceType::kAlphaTest : Render::ESurfaceType::kOpaque));
        if (alpha_blend || thin_glass)
            iter->second->EnableKeyword("LINEAR_ALPHA");
        else
            iter->second->DisableKeyword("LINEAR_ALPHA");
        const auto emission_factor_field = document.FindMember("emission_texture_factor");
        if (emission_factor_field != document.MemberEnd() && emission_factor_field->value.IsBool()
            && emission_factor_field->value.GetBool())
            iter->second->EnableKeyword("EMISSION_TEXTURE_FACTOR");
        else
            iter->second->DisableKeyword("EMISSION_TEXTURE_FACTOR");
        iter->second->SetFloat("_AlphaCulloff", std::clamp(alpha_cutoff, 0.f, 1.f));
        iter->second->SetCullMode(double_sided ? Render::ECullMode::kNone : Render::ECullMode::kBack);
        iter->second->SetFloat("_RoughnessValue", std::clamp(roughness, 0.f, 1.f));
        iter->second->SetFloat("_MetallicValue", std::clamp(metallic, 0.f, 1.f));
        iter->second->SetFloat("_Transmission", std::clamp(transmission, 0.f, 1.f));
        iter->second->SetFloat("_IOR", std::max(ior, 1.f));
        f32 specular_level = 0.5f;
        read_float("specular_ior_level", specular_level);
        const bool has_source_specular = document.HasMember("specular_ior_level");
        const f32 safe_ior = std::max(ior, 1.0f);
        const f32 source_f0 = std::clamp(2.0f * specular_level *
            std::pow((safe_ior - 1.0f) / (safe_ior + 1.0f), 2.0f), 0.0f, 1.0f);
        iter->second->SetInt("_UseSourceDielectric", has_source_specular ? 1 : 0);
        if (has_source_specular)
            iter->second->SetVector("_SpecularValue", Vector4f(source_f0, source_f0, source_f0, 1.0f));
        Vector4f base_color(0.8f, 0.8f, 0.8f, 1.f);
        const auto color = document.FindMember("base_color");
        if (color != document.MemberEnd() && color->value.IsArray() && color->value.Size() == 4u)
        {
            for (rapidjson::SizeType index = 0u; index < 4u; ++index)
            {
                if (!color->value[index].IsNumber())
                    return false;
                base_color.data[index] = color->value[index].GetFloat();
            }
        }
        iter->second->SetVector("_AlbedoValue", base_color);
        Vector4f emission(0.f, 0.f, 0.f, 0.f);
        const auto emission_field = document.FindMember("emission");
        if (emission_field != document.MemberEnd() && emission_field->value.IsArray()
            && emission_field->value.Size() >= 3u)
        {
            for (rapidjson::SizeType index = 0u; index < 3u; ++index)
            {
                if (!emission_field->value[index].IsNumber())
                    return false;
                emission.data[index] = emission_field->value[index].GetFloat() * emission_strength;
            }
            emission.w = 1.f;
        }
        iter->second->SetVector("_EmissionValue", emission);
        std::string normal_mode;
        ReadString(document, "normal_mode", normal_mode);
        const bool object_box_height = normal_mode == "OBJECT_BOX_HEIGHT";
        const auto height_color = document.FindMember("normal_height_provides_color");
        const bool height_provides_color = object_box_height && height_color != document.MemberEnd()
            && height_color->value.IsBool() && height_color->value.GetBool();
        f32 normal_strength = 0.f, normal_distance = 0.f, normal_box_blend = 0.f;
        read_float("normal_strength", normal_strength);
        read_float("normal_distance", normal_distance);
        read_float("normal_box_blend", normal_box_blend);
        if (!std::isfinite(normal_strength) || !std::isfinite(normal_distance) || !std::isfinite(normal_box_blend))
            return false;
        iter->second->SetVector("_NormalHeightParameters", Vector4f(std::clamp(normal_strength, -1.f, 1.f),
            std::max(normal_distance, 0.f), std::clamp(normal_box_blend, 0.f, 1.f),
            object_box_height ? (height_provides_color ? 2.f : 1.f) : 0.f));
        auto load_texture = [this, &document, &material_guid, &iter, object_box_height](const char* field_name,
            Render::ETextureUsage usage)
        {
            const auto field = document.FindMember(field_name);
            if (field == document.MemberEnd() || !field->value.IsString())
                return;
            const std::string path(field->value.GetString(), field->value.GetStringLength());
            if (path.empty())
            {
                iter->second->SetTexture(usage, nullptr);
                return;
            }
            TextureImportSetting settings;
            settings._content = usage == Render::ETextureUsage::kNormal ? ETextureContent::kNormal
                : (usage == Render::ETextureUsage::kAlbedo || usage == Render::ETextureUsage::kEmission
                    ? ETextureContent::kColor : ETextureContent::kData);
            settings._is_srgb = settings._content == ETextureContent::kColor;
            if (usage == Render::ETextureUsage::kNormal && object_box_height)
            {
                const auto srgb_field = document.FindMember("normal_height_srgb");
                settings._is_srgb = srgb_field != document.MemberEnd() && srgb_field->value.IsBool()
                    && srgb_field->value.GetBool();
                settings._content = settings._is_srgb ? ETextureContent::kColor : ETextureContent::kData;
                settings._sampler_desc._filter = Render::EFilterMode::kAnisotropic;
            }
            // TODO: Enable texture compression after Live Link validation; keep imports uncompressed for now.
            settings._compression = ETextureCompression::kNone;

            std::string normalized_path = std::filesystem::path(path).lexically_normal().generic_string();
            std::transform(normalized_path.begin(), normalized_path.end(), normalized_path.begin(),
                [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
            std::error_code file_error;
            const auto file_size = std::filesystem::file_size(path, file_error);
            const bool has_file_size = !file_error;
            if (file_error)
                file_error.clear();
            const auto write_time = std::filesystem::last_write_time(path, file_error);
            const auto write_ticks = file_error ? 0ll : static_cast<long long>(write_time.time_since_epoch().count());
            const std::string cache_key = normalized_path + ":"
                + std::to_string(static_cast<unsigned int>(settings._content)) + ":"
                + std::to_string(settings._is_srgb) + ":" + std::to_string(file_size) + ":"
                + std::to_string(write_ticks);
            auto texture = _textures.find(cache_key);
            if (texture == _textures.end())
            {
                const auto load_start = std::chrono::steady_clock::now();
                Ref<Render::Texture2D> loaded;
                try
                {
                    loaded = ResourceMgr::Get().LoadExternalTexture(std::filesystem::path(path).wstring(), settings);
                }
                catch (const std::exception& exception)
                {
                    const auto load_us = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - load_start).count();
                    LOG_ERROR(L"[BlenderLiveLink] Texture import exception: {} ({} bytes, {:.3f} ms): {}",
                        ToWChar(path), has_file_size ? file_size : 0ull, load_us / 1000.0, ToWChar(exception.what()));
                    iter->second->SetTexture(usage, nullptr);
                    return;
                }
                const auto load_us = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - load_start).count();
                LOG_INFO(L"[BlenderLiveLink] Texture import: {} ({} bytes, usage={}, cache=miss, result={}, {:.3f} ms)",
                    ToWChar(path), has_file_size ? file_size : 0ull, static_cast<unsigned int>(usage),
                    loaded ? L"ok" : L"failed", load_us / 1000.0);
                if (!loaded)
                {
                    LOG_WARNING(L"[BlenderLiveLink] Texture unavailable; importing material without it: {}", ToWChar(path));
                    iter->second->SetTexture(usage, nullptr);
                    return;
                }
                if (usage == Render::ETextureUsage::kNormal && object_box_height)
                {
                    loaded->SetSamplerDesc(settings._sampler_desc);
                }
                auto& resources = ResourceMgr::Get();
                const auto texture_path = AssetPath("Textures", material_guid + "_"
                    + std::string(TextureAssetSuffix(field_name)));
                const auto source_path = texture_path + std::filesystem::path(path).extension().wstring();
                const auto source_system_path = resources.GetResSysPath(source_path);
                std::filesystem::create_directories(std::filesystem::path(source_system_path).parent_path());
                std::filesystem::copy_file(std::filesystem::path(path), source_system_path, std::filesystem::copy_options::overwrite_existing);
                auto* asset = resources.CreateAsset(texture_path, loaded);
                if (!asset) throw std::runtime_error("Live Link texture registration failed");
                asset->_external_asset_path = source_path;
                resources.SetImportSetting(texture_path, settings);
                if (!resources.SaveAsset(asset)) throw std::runtime_error("Live Link texture persistence failed");
                texture = _textures.emplace(cache_key, std::move(loaded)).first;
            }
            iter->second->SetTexture(usage, texture->second.get());
        };
        load_texture("base_color_path", Render::ETextureUsage::kAlbedo);
        load_texture("normal_path", Render::ETextureUsage::kNormal);
        load_texture("emission_path", Render::ETextureUsage::kEmission);
        load_texture("roughness_path", Render::ETextureUsage::kRoughness);
        load_texture("metallic_path", Render::ETextureUsage::kMetallic);
        auto& resources = ResourceMgr::Get();
        const auto material_path = AssetPath("Materials", material_guid);
        const auto material_system_path = resources.GetResSysPath(material_path);
        std::filesystem::create_directories(std::filesystem::path(material_system_path).parent_path());
        auto* material_asset = resources.GetLinkedAsset(iter->second.get());
        if (!material_asset) material_asset = resources.CreateAsset(material_path, iter->second);
        if (!material_asset) return false;
        material_asset->MarkDirty();
        if (!resources.SaveAsset(material_asset)) return false;
        for (const auto& [object_guid, guids] : _entity_material_guids)
        {
            if (std::find(guids.begin(), guids.end(), material_guid) == guids.end())
                continue;
            const auto entity = _entities.find(object_guid);
            const auto mesh = _meshes.find(object_guid);
            const auto slots = _mesh_material_slots.find(object_guid);
            if (entity == _entities.end() || mesh == _meshes.end() || slots == _mesh_material_slots.end()
                || !_scene->IsValidEntity(entity->second))
                continue;
            auto* component = _scene->GetRegister().GetComponent<ECS::StaticMeshComponent>(entity->second);
            if (component == nullptr)
                continue;
            component->_p_mats.clear();
            component->_material_guids.clear();
            for (u32 slot : slots->second)
            {
                const std::string slot_guid = slot < guids.size() ? guids[slot] : std::string{};
                const auto slot_material = _materials.find(slot_guid);
                component->_p_mats.push_back(slot_material == _materials.end()
                    ? Render::Material::s_checker.lock() : slot_material->second);
            }
        }
        return true;
    }

    bool LiveLinkSession::UpsertLight(const std::string& payload)
    {
        if (_scene == nullptr)
            return false;
        rapidjson::Document document;
        document.Parse(payload.data(), payload.size());
        std::string guid_text, type;
        if (!document.IsObject() || !ReadString(document, "guid", guid_text) || !ReadString(document, "type", type))
            return false;
        const auto entity = _entities.find(guid_text);
        if (entity == _entities.end() || !_scene->IsValidEntity(entity->second))
            return false;
        auto& registry = _scene->GetRegister();
        auto* light = registry.GetComponent<ECS::LightComponent>(entity->second);
        if (light == nullptr)
            light = &registry.AddComponent<ECS::LightComponent>(entity->second);
        f32 energy = 1.f, range = 10.f, radius = 0.05f, inner_angle = 30.f, outer_angle = 45.f;
        const auto read_float = [&document](const char* key, f32& value)
        {
            const auto field = document.FindMember(key);
            if (field != document.MemberEnd() && field->value.IsNumber())
                value = field->value.GetFloat();
        };
        read_float("energy", energy);
        read_float("range", range);
        read_float("radius", radius);
        read_float("inner_angle_degrees", inner_angle);
        read_float("outer_angle_degrees", outer_angle);
        bool use_temperature = false;
        const auto temperature_enabled_field = document.FindMember("use_temperature");
        if (temperature_enabled_field != document.MemberEnd() && temperature_enabled_field->value.IsBool())
            use_temperature = temperature_enabled_field->value.GetBool();
        f32 temperature_kelvin = 6500.0f;
        read_float("temperature_kelvin", temperature_kelvin);
        if (!std::isfinite(temperature_kelvin))
            return false;
        Vector4f color(1.f, 1.f, 1.f, 1.f);
        const auto color_field = document.FindMember("color");
        if (color_field != document.MemberEnd() && color_field->value.IsArray() && color_field->value.Size() >= 3u)
        {
            for (rapidjson::SizeType index = 0u; index < 3u; ++index)
            {
                if (!color_field->value[index].IsNumber())
                    return false;
                color.data[index] = color_field->value[index].GetFloat();
            }
        }
        light->_light._light_color = Color(color.x, color.y, color.z, 1.f);
        light->_light._use_color_temperature = use_temperature;
        light->_light._color_temperature_kelvin = std::clamp(temperature_kelvin, 1000.0f, 40000.0f);
        light->_light._intensity = energy;
        if (type == "SUN")
        {
            light->_type = ECS::ELightType::kDirectional;
            light->_light._intensity_unit = ECS::ELightIntensityUnit::kWattPerSquareMeter;
            light->_light._range = 0.f;
            light->_light._angular_radius = radius;
        }
        else if (type == "POINT")
        {
            light->_type = ECS::ELightType::kPoint;
            light->_light._intensity_unit = ECS::ELightIntensityUnit::kWatt;
            light->_light._range = range;
            light->_light._source_radius = radius;
        }
        else if (type == "SPOT")
        {
            light->_type = ECS::ELightType::kSpot;
            light->_light._intensity_unit = ECS::ELightIntensityUnit::kWatt;
            light->_light._range = range;
            light->_light._spot_inner_angle = inner_angle;
            light->_light._spot_outer_angle = outer_angle;
        }
        else if (type == "AREA")
        {
            light->_type = ECS::ELightType::kArea;
            f32 width = 1.f, height = 1.f;
            read_float("width", width);
            read_float("height", height);
            light->_light._intensity_unit = ECS::ELightIntensityUnit::kWatt;
            light->_light._range = range;
            light->_light._area_width = width;
            light->_light._area_height = height;
        }
        else
            return false;
        return true;
    }

    bool LiveLinkSession::UpsertWorld(const std::string& payload)
    {
        if (_scene == nullptr || !_scene->IsValidEntity(_root_entity))
            return false;
        rapidjson::Document document;
        document.Parse(payload.data(), payload.size());
        if (!document.IsObject() || !document.HasMember("enabled") || !document["enabled"].IsBool())
            return false;
        auto &registry = _scene->GetRegister();
        auto *environment = registry.GetComponent<ECS::EnvironmentComponent>(_root_entity);
        if (!document["enabled"].GetBool())
        {
            if (environment != nullptr)
                registry.SetComponentEnabled<ECS::EnvironmentComponent>(_root_entity, false);
            return true;
        }
        std::string lighting_path, background_path, revision;
        if (!ReadString(document, "lighting_path", lighting_path)
            || !ReadString(document, "background_path", background_path)
            || !ReadString(document, "revision", revision) || lighting_path.empty() || background_path.empty())
            return false;
        const auto import_world_texture = [this, &revision](const std::string &path, const std::string &role) -> Guid
        {
            const std::string key = "World/" + role;
            auto &resources = ResourceMgr::Get();
            const auto asset_path = AssetPath("Textures", "World_" + role);
            if (_world_revision == revision && _textures.contains(key))
                return resources.GetAssetGuid(_textures.at(key).get());
            TextureImportSetting settings;
            settings._content = ETextureContent::kHdr;
            settings._is_srgb = false;
            settings._compression = ETextureCompression::kNone;
            auto loaded = resources.LoadExternalTexture(std::filesystem::path(path).wstring(), settings);
            if (loaded == nullptr)
                throw std::runtime_error("Live Link World texture import failed");
            const auto source_path = asset_path + L".exr";
            const auto system_path = resources.GetResSysPath(source_path);
            std::filesystem::create_directories(std::filesystem::path(system_path).parent_path());
            std::filesystem::copy_file(path, system_path, std::filesystem::copy_options::overwrite_existing);
            auto *asset = resources.CreateAsset(asset_path, loaded);
            if (asset == nullptr)
                throw std::runtime_error("Live Link World texture registration failed");
            asset->_external_asset_path = source_path;
            resources.SetImportSetting(asset_path, settings);
            if (!resources.SaveAsset(asset))
                throw std::runtime_error("Live Link World texture persistence failed");
            _textures[key] = loaded;
            return asset->GetGuid();
        };
        const Guid lighting = import_world_texture(lighting_path, "Lighting");
        const Guid background = import_world_texture(background_path, "Background");
        if (environment == nullptr)
            environment = &registry.AddComponent<ECS::EnvironmentComponent>(_root_entity);
        _world_revision = revision;
        environment->_source = ECS::EEnvironmentSource::kTexture;
        environment->_texture = lighting;
        environment->_background_texture = background;
        environment->_intensity = 1.0f;
        environment->_rotation = 0.0f;
        environment->_render_sky = true;
        environment->_enable_ibl = true;
        registry.SetComponentEnabled<ECS::EnvironmentComponent>(_root_entity, true);
        return true;
    }

    bool LiveLinkSession::UpsertSceneSettings(const std::string& payload)
    {
        if (_scene == nullptr || !_scene->IsValidEntity(_root_entity))
            return false;
        rapidjson::Document document;
        document.Parse(payload.data(), payload.size());
        std::string view_transform, display_device, look;
        if (!document.IsObject() || !ReadString(document, "view_transform", view_transform) ||
            !ReadString(document, "display_device", display_device) || !ReadString(document, "look", look))
            return false;
        const auto exposure = document.FindMember("exposure_ev");
        const auto gamma = document.FindMember("gamma");
        if (exposure == document.MemberEnd() || !exposure->value.IsNumber() ||
            gamma == document.MemberEnd() || !gamma->value.IsNumber())
            return false;
        const f32 exposure_ev = exposure->value.GetFloat(), display_gamma = gamma->value.GetFloat();
        if (!std::isfinite(exposure_ev) || !std::isfinite(display_gamma) || display_gamma <= 0.0f ||
            exposure_ev < -32.0f || exposure_ev > 32.0f)
            return false;
        auto &registry = _scene->GetRegister();
        auto *volume = registry.GetComponent<ECS::PostprocessVolumeComponent>(_root_entity);
        if (display_device != "sRGB" || look != "None" ||
            (view_transform != "Standard" && view_transform != "AgX"))
        {
            if (volume != nullptr)
                volume->_is_global = false;
            LOG_WARNING("[BlenderLiveLink] Unsupported display settings: view={}, display={}, look={}",
                        view_transform, display_device, look);
            return true;
        }
        if (volume == nullptr)
            volume = &registry.AddComponent<ECS::PostprocessVolumeComponent>(_root_entity);
        constexpr i32 kSourceDisplayPriority = 100;
        volume->_is_global = true;
        volume->_priority = kSourceDisplayPriority;
        volume->_settings._bloom._enabled = false;
        volume->_settings._tone_mapping._mode = view_transform == "Standard"
            ? Render::EToneMappingMode::kStandard : Render::EToneMappingMode::kAgx;
        volume->_settings._tone_mapping._exposure = std::exp2(exposure_ev);
        volume->_settings._tone_mapping._gamma = display_gamma;
        return true;
    }

    bool LiveLinkSession::UpsertCamera(const std::string& payload)
    {
        if (_scene == nullptr)
            return false;
        rapidjson::Document document;
        document.Parse(payload.data(), payload.size());
        std::string guid_text, type;
        if (!document.IsObject() || !ReadString(document, "guid", guid_text) || !ReadString(document, "type", type))
            return false;
        const auto entity = _entities.find(guid_text);
        if (entity == _entities.end() || !_scene->IsValidEntity(entity->second))
            return false;
        auto& registry = _scene->GetRegister();
        auto* camera_component = registry.GetComponent<ECS::CCamera>(entity->second);
        if (camera_component == nullptr)
            camera_component = &registry.AddComponent<ECS::CCamera>(entity->second);
        auto& camera = camera_component->_camera;
        f32 fov_degrees = 50.f, aspect = 16.f / 9.f, ortho_size = 10.f, near_clip = 0.1f, far_clip = 1000.f;
        const auto read_float = [&document](const char* key, f32& value)
        {
            const auto field = document.FindMember(key);
            if (field != document.MemberEnd() && field->value.IsNumber())
                value = field->value.GetFloat();
        };
        read_float("fov_degrees", fov_degrees);
        read_float("aspect", aspect);
        read_float("ortho_size", ortho_size);
        read_float("near_clip", near_clip);
        read_float("far_clip", far_clip);
        const auto vertical_fov = document.FindMember("fov_y_degrees");
        if (vertical_fov != document.MemberEnd() && vertical_fov->value.IsNumber())
            fov_degrees = vertical_fov->value.GetFloat();
        else
            fov_degrees = 2.0f * std::atan(std::tan(fov_degrees * Math::k2Radius * 0.5f) / std::max(aspect, 0.001f)) / Math::k2Radius;
        Vector2f projection_offset = Vector2f::kZero;
        const auto offset = document.FindMember("projection_offset");
        if (offset != document.MemberEnd())
        {
            if (!offset->value.IsArray() || offset->value.Size() != 2u
                || !offset->value[0].IsNumber() || !offset->value[1].IsNumber()) return false;
            projection_offset = Vector2f(offset->value[0].GetFloat(), offset->value[1].GetFloat());
        }
        if (!std::isfinite(fov_degrees) || !std::isfinite(aspect) || !std::isfinite(ortho_size)
            || !std::isfinite(near_clip) || !std::isfinite(far_clip)
            || !std::isfinite(projection_offset.x) || !std::isfinite(projection_offset.y)) return false;
        // The entity remains in the ordinary converted object basis; only the camera interprets its local view axes.
        camera.SetLocalViewAxes(Vector3f(0.f, -1.f, 0.f), Vector3f(0.f, 0.f, -1.f));
        camera.ReverseProjectionX(true);
        camera.ProjectionOffset(projection_offset);
        camera.LockAspect(true);
        camera.Type(type == "ORTHO" ? Render::ECameraType::kOrthographic : Render::ECameraType::kPerspective);
        camera.FovH(std::clamp(fov_degrees, 1.f, 179.f));
        camera.Aspect(std::max(aspect, 0.001f));
        camera.Size(std::max(ortho_size, 0.001f));
        camera.Near(std::max(near_clip, 0.001f));
        camera.Far(std::max(far_clip, camera.Near() + 0.01f));
        return type == "PERSP" || type == "ORTHO";
    }

    void LiveLinkSession::End() noexcept
    {
        if (!_full_sync_active || _full_sync_failed || _scene == nullptr || _scene != ActiveEditScene())
        {
            _needs_full_resync = true;
            return;
        }
        for (const auto& [guid, parent_guid] : _parent_guids)
        {
            const auto child = _entities.find(guid);
            const auto parent = _entities.find(parent_guid);
            const ECS::Entity parent_entity = parent == _entities.end() || !_seen.contains(parent_guid) ? _root_entity : parent->second;
            if (child != _entities.end() && child->second != parent_entity && !_scene->Reparent(child->second, parent_entity, false))
            {
                _needs_full_resync = true;
                return;
            }
        }
        for (auto iter = _entities.begin(); iter != _entities.end();)
        {
            if (!_seen.contains(iter->first))
            {
                _scene->RemoveObject(iter->second);
                _meshes.erase(iter->first);
                _mesh_revisions.erase(iter->first);
                _mesh_material_slots.erase(iter->first);
                _entity_material_guids.erase(iter->first);
                _object_types.erase(iter->first);
                _parent_guids.erase(iter->first);
                iter = _entities.erase(iter);
            }
            else
                ++iter;
        }
        _full_sync_active = false;
        _mesh_cache.Prune();
    }

    void LiveLinkSession::Clear() noexcept
    {
        ResetSceneState();
        _needs_full_resync = true;
        _meshes.clear();
        _transient_meshes.clear();
        _mesh_cache.Clear();
        _materials.clear();
        _textures.clear();
        _world_revision.clear();
        _mesh_revisions.clear();
    }

    void LiveLinkSession::ResetSceneState() noexcept
    {
        _scene = nullptr;
        _root_entity = ECS::kInvalidEntity;
        _session_guid.clear();
        _entities.clear();
        _seen.clear();
        _parent_guids.clear();
        _object_types.clear();
        _entity_material_guids.clear();
        _mesh_material_slots.clear();
        _meshes.clear();
        _transient_meshes.clear();
        _mesh_cache.Clear();
        _materials.clear();
        _textures.clear();
        _world_revision.clear();
        _mesh_revisions.clear();
        _full_sync_active = false;
        _full_sync_failed = false;
        _needs_full_resync = true;
    }
}
