#include "LiveLinkSession.h"
#include "LiveLinkProtocol.h"
#include "Framework/Common/Application.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Math/Guid.h"
#include "Render/Mesh.h"
#include "Scene/Scene.h"
#include <document.h>
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

        bool IsLiveLinkRoot(const ECS::TagComponent& tag) noexcept
        {
            const std::string_view name(tag._name);
            return tag._is_transient && name.starts_with(kLiveLinkRootPrefix)
                && name.size() == kLiveLinkRootPrefix.size() + 8u;
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
                Begin(session_guid);
            }
            else if (type == ELiveLinkMessageType::kObjectUpsert)
            {
                _last_object_upsert_succeeded = UpsertObject(std::string(
                    reinterpret_cast<const char*>(command._payload.data()), command._payload.size()));
            }
            else if (type == ELiveLinkMessageType::kObjectDelete)
            {
                rapidjson::Document document;
                document.Parse(reinterpret_cast<const char*>(command._payload.data()), command._payload.size());
                std::string guid;
                if (document.IsObject() && ReadString(document, "guid", guid))
                {
                    const auto iter = _entities.find(guid);
                    if (iter != _entities.end())
                    {
                        _scene->RemoveObject(iter->second);
                        _meshes.erase(guid);
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
                UpsertMesh(command._payload);
            else if (type == ELiveLinkMessageType::kMaterialUpsert)
                UpsertMaterial(std::string(reinterpret_cast<const char*>(command._payload.data()),
                    command._payload.size()));
            else if (type == ELiveLinkMessageType::kLightUpsert)
                UpsertLight(std::string(reinterpret_cast<const char*>(command._payload.data()),
                    command._payload.size()));
            else if (type == ELiveLinkMessageType::kCameraUpsert)
                UpsertCamera(std::string(reinterpret_cast<const char*>(command._payload.data()),
                    command._payload.size()));
            else if (type == ELiveLinkMessageType::kFullSyncEnd)
                End();
        }
        catch (...)
        {
            _needs_full_resync = true;
        }
    }

    bool LiveLinkSession::Begin(const std::string& session_guid)
    {
        Scene* active_scene = ActiveEditScene();
        Guid parsed_guid(session_guid);
        if (active_scene == nullptr || !parsed_guid.IsValid())
            return false;
        if (_scene != active_scene)
            ResetSceneState();
        _scene = active_scene;
        _session_guid = session_guid;
        auto& registry = _scene->GetRegister();
        ECS::Entity root = _root_entity != ECS::kInvalidEntity && _scene->IsValidEntity(_root_entity)
            ? _root_entity : ECS::kInvalidEntity;
        u32 index = 0u;
        for (const auto& tag : registry.View<ECS::TagComponent>())
        {
            const ECS::Entity entity = registry.GetEntity<ECS::TagComponent>(index++);
            if (!IsLiveLinkRoot(tag) || entity == root)
                continue;
            if (root == ECS::kInvalidEntity)
                root = entity;
            else
                _scene->RemoveObject(entity);
        }
        if (root == ECS::kInvalidEntity)
            root = _scene->AddObject("__BlenderLiveLink_" + session_guid.substr(0u, 8u), Guid::Generate());
        auto* root_tag = _scene->GetRegister().GetComponent<ECS::TagComponent>(root);
        if (root_tag == nullptr)
            return false;
        root_tag->_is_transient = true;
        _root_entity = root;
        _seen.clear();
        _full_sync_active = true;
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
        if (!guid.IsValid())
            return false;

        ECS::Entity entity = ECS::kInvalidEntity;
        const auto mapped = _entities.find(guid_text);
        if (mapped != _entities.end() && _scene->IsValidEntity(mapped->second))
            entity = mapped->second;
        else
        {
            entity = _scene->FindEntity(guid);
            if (entity == ECS::kInvalidEntity)
                entity = _scene->AddObject(name, guid, false);
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
        tag->_is_transient = true;
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

    bool LiveLinkSession::UpsertMesh(const std::vector<unsigned char>& payload)
    {
        constexpr size_t kGuidSize = 36u;
        constexpr size_t kFixedHeaderSize = kGuidSize + sizeof(u64) + sizeof(u32) * 2u + sizeof(f32) * 6u;
        if (payload.size() < kFixedHeaderSize || _scene == nullptr)
            return false;
        const std::string guid_text(reinterpret_cast<const char*>(payload.data()), kGuidSize);
        const auto entity_iter = _entities.find(guid_text);
        if (entity_iter == _entities.end() || !_scene->IsValidEntity(entity_iter->second))
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
            return false;
        auto mesh = MakeRef<Render::Mesh>("BlenderLiveLink_" + guid_text);
        mesh->SetVertices(std::move(positions));
        mesh->SetNormals(std::move(normals));
        mesh->SetTangents(std::move(tangents));
        mesh->SetUVs(std::move(uvs));
        for (auto& indices : submeshes)
            mesh->AddSubmesh(std::move(indices));
        const AABB bounds(Vector3f(bounds_values[0], bounds_values[1], bounds_values[2]),
            Vector3f(bounds_values[3], bounds_values[4], bounds_values[5]));
        std::vector<AABB> mesh_bounds;
        mesh_bounds.reserve(static_cast<size_t>(submesh_count) + 1u);
        mesh_bounds.push_back(bounds);
        for (const auto& indices : submeshes)
        {
            AABB submesh_bounds = AABB::Infinity();
            for (u32 vertex_index : indices)
                AABB::Encapsulate(submesh_bounds, positions[vertex_index]);
            mesh_bounds.push_back(indices.empty() ? bounds : submesh_bounds);
        }
        mesh->SetBounds(std::span<const AABB>(mesh_bounds.data(), mesh_bounds.size()));
        mesh->Apply();

        auto& registry = _scene->GetRegister();
        auto* component = registry.GetComponent<ECS::StaticMeshComponent>(entity_iter->second);
        if (component == nullptr)
            component = &registry.AddComponent<ECS::StaticMeshComponent>(entity_iter->second);
        component->_p_mesh = mesh;
        component->_transformed_aabbs.resize(mesh_bounds.size());
        component->_p_mats.clear();
        const auto object_materials = _entity_material_guids.find(guid_text);
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
        _meshes[guid_text] = std::move(mesh);
        _mesh_material_slots[guid_text] = std::move(material_slots);
        _mesh_revisions[guid_text] = revision;
        return true;
    }

    bool LiveLinkSession::UpsertMaterial(const std::string& payload)
    {
        rapidjson::Document document;
        document.Parse(payload.data(), payload.size());
        std::string material_guid;
        if (!document.IsObject() || !ReadString(document, "material_guid", material_guid))
            return false;
        auto iter = _materials.find(material_guid);
        if (iter == _materials.end())
        {
            auto material = Render::Material::CreateStandard("BlenderLiveLink_" + material_guid);
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
        f32 roughness = 0.5f, metallic = 0.f, emission_strength = 0.f;
        read_float("roughness", roughness);
        read_float("metallic", metallic);
        read_float("emission_strength", emission_strength);
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
        iter->second->SurfaceType(alpha_blend ? Render::ESurfaceType::kTransparent
            : (alpha_test ? Render::ESurfaceType::kAlphaTest : Render::ESurfaceType::kOpaque));
        if (alpha_blend)
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
        auto load_texture = [this, &document, &material_guid, &iter](const char* field_name,
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
                    throw;
                }
                const auto load_us = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - load_start).count();
                LOG_INFO(L"[BlenderLiveLink] Texture import: {} ({} bytes, usage={}, cache=miss, result={}, {:.3f} ms)",
                    ToWChar(path), has_file_size ? file_size : 0ull, static_cast<unsigned int>(usage),
                    loaded ? L"ok" : L"failed", load_us / 1000.0);
                if (!loaded)
                    return;
                texture = _textures.emplace(cache_key, std::move(loaded)).first;
            }
            iter->second->SetTexture(usage, texture->second.get());
        };
        load_texture("base_color_path", Render::ETextureUsage::kAlbedo);
        load_texture("normal_path", Render::ETextureUsage::kNormal);
        load_texture("emission_path", Render::ETextureUsage::kEmission);
        load_texture("roughness_metallic_path", Render::ETextureUsage::kRoughness);
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
        Vector4f color(1.f, 1.f, 1.f, energy);
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
        light->_light._light_color = Color(color.x * energy, color.y * energy, color.z * energy, 1.f);
        if (type == "SUN")
        {
            light->_type = ECS::ELightType::kDirectional;
            light->_light._light_param = Vector4f(0.f, 0.f, 0.f, radius);
        }
        else if (type == "POINT")
        {
            light->_type = ECS::ELightType::kPoint;
            light->_light._light_param = Vector4f(range, radius, 0.f, 0.f);
        }
        else if (type == "SPOT")
        {
            light->_type = ECS::ELightType::kSpot;
            light->_light._light_param = Vector4f(range, inner_angle, outer_angle, 0.f);
        }
        else if (type == "AREA")
        {
            light->_type = ECS::ELightType::kArea;
            f32 width = 1.f, height = 1.f;
            read_float("width", width);
            read_float("height", height);
            light->_light._light_param = Vector4f(range, width, height, 0.f);
        }
        else
            return false;
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
        if (!_full_sync_active || _scene == nullptr || _scene != ActiveEditScene())
        {
            _needs_full_resync = true;
            return;
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
        for (const auto& [guid, parent_guid] : _parent_guids)
        {
            const auto child = _entities.find(guid);
            const auto parent = parent_guid.empty() ? _entities.end() : _entities.find(parent_guid);
            const ECS::Entity parent_entity = parent == _entities.end() ? _root_entity : parent->second;
            if (child != _entities.end() && parent_entity != ECS::kInvalidEntity && child->second != parent_entity)
                _scene->Reparent(child->second, parent_entity, false);
        }
        _full_sync_active = false;
    }

    void LiveLinkSession::Clear() noexcept
    {
        ResetSceneState();
        _needs_full_resync = true;
        _meshes.clear();
        _materials.clear();
        _textures.clear();
        _mesh_revisions.clear();
    }

    void LiveLinkSession::ResetSceneState() noexcept
    {
        if (_scene != nullptr && _root_entity != ECS::kInvalidEntity && _scene->IsValidEntity(_root_entity))
            _scene->RemoveObject(_root_entity);
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
        _materials.clear();
        _textures.clear();
        _mesh_revisions.clear();
        _full_sync_active = false;
    }
}
