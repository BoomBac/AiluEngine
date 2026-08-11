#include "Framework/Script/ScriptEntity.h"

#include "Scene/Component.h"
#include "Scene/Scene.h"
#include "Assets/Asset.h"
#include "Framework/Common/ResourceMgr.h"
#include "Physics/2D/Physics2DComponents.h"
#include "Render/2D/Sprite.h"

namespace Ailu
{
    namespace
    {
        Asset *ResolveScriptAsset(const Guid &guid);

        String NormalizeComponentProperty(String property)
        {
            if (!property.empty() && property.front() != '_')
                property.insert(property.begin(), '_');
            return property;
        }

        bool IsComponentType(const String &type_name, StringView short_name)
        {
            return type_name == std::format("Ailu.ECS.{}", short_name);
        }

        template<typename T>
        T *GetComponent(const ScriptComponent &component)
        {
            return component.IsValid() && component._type_name == T::StaticTypeName()
                       ? component._scene->GetRegister().GetComponent<T>(component._entity)
                       : nullptr;
        }

        bool AddComponentByName(SceneManagement::Scene &scene, ECS::Entity entity, const String &type_name)
        {
            auto &reg = scene.GetRegister();
            if (type_name == ECS::TagComponent::StaticTypeName()) reg.AddComponent<ECS::TagComponent>(entity);
            else if (type_name == ECS::PersistentIdComponent::StaticTypeName()) reg.AddComponent<ECS::PersistentIdComponent>(entity);
            else if (type_name == ECS::TransformComponent::StaticTypeName()) reg.AddComponent<ECS::TransformComponent>(entity);
            else if (type_name == ECS::ScriptComponent::StaticTypeName()) reg.AddComponent<ECS::ScriptComponent>(entity);
            else if (type_name == ECS::StaticMeshComponent::StaticTypeName()) reg.AddComponent<ECS::StaticMeshComponent>(entity);
            else if (type_name == ECS::LightComponent::StaticTypeName()) reg.AddComponent<ECS::LightComponent>(entity);
            else if (type_name == ECS::CCamera::StaticTypeName()) reg.AddComponent<ECS::CCamera>(entity);
            else if (type_name == ECS::CHierarchy::StaticTypeName()) reg.AddComponent<ECS::CHierarchy>(entity);
            else if (type_name == ECS::CLightProbe::StaticTypeName()) reg.AddComponent<ECS::CLightProbe>(entity);
            else if (type_name == ECS::CRigidBody::StaticTypeName()) reg.AddComponent<ECS::CRigidBody>(entity);
            else if (type_name == ECS::CCollider::StaticTypeName()) reg.AddComponent<ECS::CCollider>(entity);
            else if (type_name == ECS::RigidBody2DComponent::StaticTypeName()) reg.AddComponent<ECS::RigidBody2DComponent>(entity);
            else if (type_name == ECS::Collider2DComponent::StaticTypeName()) reg.AddComponent<ECS::Collider2DComponent>(entity);
            else if (type_name == ECS::CSkeletonMesh::StaticTypeName()) reg.AddComponent<ECS::CSkeletonMesh>(entity);
            else if (type_name == ECS::CVXGI::StaticTypeName()) reg.AddComponent<ECS::CVXGI>(entity);
            else if (type_name == ECS::SpriteRendererComponent::StaticTypeName()) reg.AddComponent<ECS::SpriteRendererComponent>(entity);
            else if (type_name == ECS::AudioSourceComponent::StaticTypeName()) reg.AddComponent<ECS::AudioSourceComponent>(entity);
            else if (type_name == ECS::AudioListenerComponent::StaticTypeName()) reg.AddComponent<ECS::AudioListenerComponent>(entity);
            else return false;
            scene.MarkStructureChanged();
            return true;
        }

        bool RemoveComponentByName(SceneManagement::Scene &scene, ECS::Entity entity, const String &type_name)
        {
            auto &reg = scene.GetRegister();
            if (type_name == ECS::TagComponent::StaticTypeName()) reg.RemoveComponent<ECS::TagComponent>(entity);
            else if (type_name == ECS::PersistentIdComponent::StaticTypeName()) reg.RemoveComponent<ECS::PersistentIdComponent>(entity);
            else if (type_name == ECS::TransformComponent::StaticTypeName()) reg.RemoveComponent<ECS::TransformComponent>(entity);
            else if (type_name == ECS::ScriptComponent::StaticTypeName()) reg.RemoveComponent<ECS::ScriptComponent>(entity);
            else if (type_name == ECS::RigidBody2DComponent::StaticTypeName()) reg.RemoveComponent<ECS::RigidBody2DComponent>(entity);
            else if (type_name == ECS::Collider2DComponent::StaticTypeName()) reg.RemoveComponent<ECS::Collider2DComponent>(entity);
            else if (type_name == ECS::SpriteRendererComponent::StaticTypeName()) reg.RemoveComponent<ECS::SpriteRendererComponent>(entity);
            else return false;
            scene.MarkStructureChanged();
            return true;
        }
    }

    bool ScriptComponent::IsValid() const
    {
        return _scene != nullptr && _scene->IsValidEntity(_entity) &&
               _scene->GetRegister().HasComponentType(_entity, ECS::RegisterComponentType(_type_name));
    }

    String ScriptComponent::GetTypeName() const { return _type_name; }

    bool ScriptComponent::IsEnabled() const
    {
        if (!IsValid()) return false;
        const auto &reg = _scene->GetRegister();
        if (IsComponentType(_type_name, "RigidBody2DComponent")) return reg.IsComponentEnabled<ECS::RigidBody2DComponent>(_entity);
        if (IsComponentType(_type_name, "Collider2DComponent")) return reg.IsComponentEnabled<ECS::Collider2DComponent>(_entity);
        if (IsComponentType(_type_name, "SpriteRendererComponent")) return reg.IsComponentEnabled<ECS::SpriteRendererComponent>(_entity);
        return true;
    }

    void ScriptComponent::SetEnabled(bool enabled) const
    {
        if (!IsValid()) return;
        auto &reg = _scene->GetRegister();
        if (IsComponentType(_type_name, "RigidBody2DComponent")) reg.SetComponentEnabled<ECS::RigidBody2DComponent>(_entity, enabled);
        else if (IsComponentType(_type_name, "Collider2DComponent")) reg.SetComponentEnabled<ECS::Collider2DComponent>(_entity, enabled);
        else if (IsComponentType(_type_name, "SpriteRendererComponent")) reg.SetComponentEnabled<ECS::SpriteRendererComponent>(_entity, enabled);
        _scene->MarkEdited();
    }

    bool ScriptComponent::SetFloat(const String &property, f32 value) const
    {
        if (!IsValid()) return false;
        const String name = NormalizeComponentProperty(property);
        if (auto *comp = GetComponent<ECS::RigidBody2DComponent>(*this))
        {
            if (name == "_gravity_scale") comp->_gravity_scale = value;
            else if (name == "_linear_damping") comp->_linear_damping = value;
            else if (name == "_angular_damping") comp->_angular_damping = value;
            else return false;
        }
        else if (auto *comp = GetComponent<ECS::Collider2DComponent>(*this); comp != nullptr && !comp->_shapes.empty())
        {
            auto &shape = comp->_shapes.back();
            if (name == "_radius") shape._radius = value;
            else if (name == "_height") shape._height = value;
            else if (name == "_density") shape._density = value;
            else if (name == "_friction") shape._friction = value;
            else if (name == "_restitution") shape._restitution = value;
            else return false;
        }
        else return false;
        _scene->MarkEdited();
        return true;
    }

    bool ScriptComponent::SetInt(const String &property, i32 value) const
    {
        if (!IsValid()) return false;
        const String name = NormalizeComponentProperty(property);
        if (auto *comp = GetComponent<ECS::SpriteRendererComponent>(*this))
        {
            if (name == "_sorting_layer") comp->_sorting_layer = static_cast<i16>(value);
            else if (name == "_order_in_layer") comp->_order_in_layer = value;
            else return false;
        }
        else return false;
        _scene->MarkEdited();
        return true;
    }

    bool ScriptComponent::SetBool(const String &property, bool value) const
    {
        if (!IsValid()) return false;
        const String name = NormalizeComponentProperty(property);
        if (auto *comp = GetComponent<ECS::RigidBody2DComponent>(*this))
        {
            if (name == "_fixed_rotation") comp->_fixed_rotation = value;
            else if (name == "_continuous") comp->_continuous = value;
            else if (name == "_allow_sleep") comp->_allow_sleep = value;
            else return false;
        }
        else if (auto *comp = GetComponent<ECS::SpriteRendererComponent>(*this))
        {
            if (name == "_flip_x") comp->_flip_x = value;
            else if (name == "_flip_y") comp->_flip_y = value;
            else if (name == "_visible") comp->_visible = value;
            else return false;
        }
        else if (auto *comp = GetComponent<ECS::Collider2DComponent>(*this); comp != nullptr && !comp->_shapes.empty())
        {
            if (name != "_is_trigger") return false;
            comp->_shapes.back()._is_trigger = value;
        }
        else return false;
        _scene->MarkEdited();
        return true;
    }

    bool ScriptComponent::SetString(const String &, const String &) const { return false; }
    bool ScriptComponent::SetVector2(const String &property, const Vector2f &value) const
    {
        if (!IsValid()) return false;
        auto *comp = GetComponent<ECS::Collider2DComponent>(*this);
        if (comp == nullptr || comp->_shapes.empty() || NormalizeComponentProperty(property) != "_size") return false;
        comp->_shapes.back()._size = value;
        _scene->MarkEdited();
        return true;
    }

    bool ScriptComponent::SetVector3(const String &property, const Vector3f &value) const
    {
        if (!IsValid()) return false;
        const String name = NormalizeComponentProperty(property);
        if (auto *rigid_body = GetComponent<ECS::CRigidBody>(*this); rigid_body != nullptr && name == "_velocity")
            rigid_body->_velocity = value;
        else if (auto *collider = GetComponent<ECS::CCollider>(*this); collider != nullptr && name == "_center")
            collider->_center = value;
        else if (auto *collider = GetComponent<ECS::CCollider>(*this); collider != nullptr && name == "_param")
            collider->_param = value;
        else return false;
        _scene->MarkEdited();
        return true;
    }

    bool ScriptComponent::AddBoxShape(const Vector2f &size, bool is_trigger) const
    {
        auto *comp = GetComponent<ECS::Collider2DComponent>(*this);
        if (comp == nullptr) return false;
        ECS::ColliderShape2D shape;
        shape._size = size;
        shape._is_trigger = is_trigger;
        comp->_shapes.emplace_back(shape);
        _scene->MarkEdited();
        return true;
    }

    bool ScriptComponent::SetSprite(const String &sprite_guid) const
    {
        auto *comp = GetComponent<ECS::SpriteRendererComponent>(*this);
        if (comp == nullptr) return false;
        Asset *asset = ResolveScriptAsset(Guid(sprite_guid));
        auto *resolved = asset == nullptr ? nullptr : asset->As<Render::Sprite>();
        if (resolved == nullptr) return false;
        comp->_sprite = resolved;
        _scene->MarkEdited();
        return true;
    }

    bool ScriptComponent::Add(const ScriptEntity &entity, const String &type_name)
    {
        return entity.AddComponent(type_name).IsValid();
    }

    bool ScriptComponent::SetEntityFloat(const ScriptEntity &entity, const String &type_name, const String &property, f32 value)
    {
        return entity.GetComponent(type_name).SetFloat(property, value);
    }

    bool ScriptComponent::SetEntityBool(const ScriptEntity &entity, const String &type_name, const String &property, bool value)
    {
        return entity.GetComponent(type_name).SetBool(property, value);
    }

    bool ScriptComponent::SetEntityVector3(const ScriptEntity &entity, const String &type_name, const String &property,
                                           const Vector3f &value)
    {
        return entity.GetComponent(type_name).SetVector3(property, value);
    }

    bool ScriptComponent::AddEntityBoxShape(const ScriptEntity &entity, const String &type_name, const Vector2f &size,
                                            bool is_trigger)
    {
        return entity.GetComponent(type_name).AddBoxShape(size, is_trigger);
    }

    bool ScriptComponent::SetEntitySprite(const ScriptEntity &entity, const String &type_name, const String &sprite_guid)
    {
        return entity.GetComponent(type_name).SetSprite(sprite_guid);
    }

    bool ScriptTransform::IsValid() const
    {
        return _scene != nullptr && _scene->IsValidEntity(_entity) &&
               _scene->GetRegister().GetComponent<ECS::TransformComponent>(_entity) != nullptr;
    }
    Vector3f ScriptTransform::GetLocalPosition() const
    {
        const auto *transform = IsValid() ? _scene->GetRegister().GetComponent<ECS::TransformComponent>(_entity) : nullptr;
        return transform != nullptr ? transform->GetLocalPosition() : Vector3f::kZero;
    }
    void ScriptTransform::SetLocalPosition(const Vector3f &position) const
    {
        if (auto *transform = IsValid() ? _scene->GetRegister().GetComponent<ECS::TransformComponent>(_entity) : nullptr)
            transform->SetLocalPosition(position);
    }
    Math::Quaternion ScriptTransform::GetLocalRotation() const
    {
        const auto *transform = IsValid() ? _scene->GetRegister().GetComponent<ECS::TransformComponent>(_entity) : nullptr;
        return transform != nullptr ? transform->GetLocalRotation() : Math::Quaternion::Identity();
    }
    void ScriptTransform::SetLocalRotation(const Math::Quaternion &rotation) const
    {
        if (auto *transform = IsValid() ? _scene->GetRegister().GetComponent<ECS::TransformComponent>(_entity) : nullptr)
            transform->SetLocalRotation(rotation);
    }
    Vector3f ScriptTransform::GetLocalScale() const
    {
        const auto *transform = IsValid() ? _scene->GetRegister().GetComponent<ECS::TransformComponent>(_entity) : nullptr;
        return transform != nullptr ? transform->GetLocalScale() : Vector3f::kOne;
    }
    void ScriptTransform::SetLocalScale(const Vector3f &scale) const
    {
        if (auto *transform = IsValid() ? _scene->GetRegister().GetComponent<ECS::TransformComponent>(_entity) : nullptr)
            transform->SetLocalScale(scale);
    }
    Vector3f ScriptTransform::GetPosition() const
    {
        const auto *transform = IsValid() ? _scene->GetRegister().GetComponent<ECS::TransformComponent>(_entity) : nullptr;
        return transform != nullptr && !transform->_world_dirty ? transform->GetPosition() : GetLocalPosition();
    }
    Math::Quaternion ScriptTransform::GetRotation() const
    {
        const auto *transform = IsValid() ? _scene->GetRegister().GetComponent<ECS::TransformComponent>(_entity) : nullptr;
        return transform != nullptr && !transform->_world_dirty ? transform->GetRotation() : GetLocalRotation();
    }
    Vector3f ScriptTransform::GetScale() const
    {
        const auto *transform = IsValid() ? _scene->GetRegister().GetComponent<ECS::TransformComponent>(_entity) : nullptr;
        return transform != nullptr && !transform->_world_dirty ? transform->GetScale() : GetLocalScale();
    }
    bool ScriptEntity::IsValid() const { return _scene != nullptr && _scene->IsValidEntity(_entity); }
    String ScriptEntity::GetName() const
    {
        if (!IsValid()) return {};
        const auto *tag = _scene->GetRegister().GetComponent<ECS::TagComponent>(_entity);
        return tag != nullptr ? tag->_name : String{};
    }
    void ScriptEntity::SetName(const String &name) const
    {
        if (IsValid()) _scene->RenameEntity(_entity, name);
    }
    String ScriptEntity::GetGuid() const { return IsValid() ? _scene->GetEntityGuid(_entity).ToString() : String{}; }
    ScriptTransform ScriptEntity::GetTransform() const { return {_scene, _entity}; }
    void ScriptEntity::Destroy() const
    {
        if (IsValid()) _scene->RemoveObject(_entity);
    }

    bool ScriptEntity::HasComponent(const String &type_name) const
    {
        return IsValid() && _scene->GetRegister().HasComponentType(_entity, ECS::RegisterComponentType(type_name));
    }

    ScriptComponent ScriptEntity::AddComponent(const String &type_name) const
    {
        if (!IsValid() || HasComponent(type_name) || !AddComponentByName(*_scene, _entity, type_name)) return {};
        return {_scene, _entity, type_name};
    }

    ScriptComponent ScriptEntity::GetComponent(const String &type_name) const
    {
        return HasComponent(type_name) ? ScriptComponent{_scene, _entity, type_name} : ScriptComponent{};
    }

    bool ScriptEntity::RemoveComponent(const String &type_name) const
    {
        return IsValid() && HasComponent(type_name) && RemoveComponentByName(*_scene, _entity, type_name);
    }

    namespace
    {
        Asset *ResolveScriptAsset(const Guid &guid)
        {
            if (guid == Guid::EmptyGuid())
                return nullptr;
            const WString &path = ResourceMgr::Get().GuidToAssetPath(guid);
            return path.empty() ? nullptr : ResourceMgr::Get().GetAsset(path);
        }
    }

    ScriptAssetValue ScriptAssetValue::Sprite() { return {Guid::EmptyGuid(), "Sprite"}; }
    ScriptAssetValue ScriptAssetValue::Texture2D() { return {Guid::EmptyGuid(), "Texture2D"}; }
    ScriptAssetValue ScriptAssetValue::Material() { return {Guid::EmptyGuid(), "Material"}; }
    ScriptAssetValue ScriptAssetValue::Mesh() { return {Guid::EmptyGuid(), "Mesh"}; }
    ScriptAssetValue ScriptAssetValue::SkeletonMesh() { return {Guid::EmptyGuid(), "SkeletonMesh"}; }
    ScriptAssetValue ScriptAssetValue::AnimationClip() { return {Guid::EmptyGuid(), "AnimationClip"}; }
    ScriptAssetValue ScriptAssetValue::AudioClip() { return {Guid::EmptyGuid(), "AudioClip"}; }
    ScriptAssetValue ScriptAssetValue::Script() { return {Guid::EmptyGuid(), "Script"}; }

    bool ScriptAssetValue::IsValid() const
    {
        return ResolveScriptAsset(_guid) != nullptr;
    }
    String ScriptAssetValue::GetGuid() const
    {
        return _guid == Guid::EmptyGuid() ? String{} : _guid.ToString();
    }
    String ScriptAssetValue::GetName() const
    {
        const auto *asset = ResolveScriptAsset(_guid);
        return asset == nullptr ? String{} : asset->Name();
    }
    String ScriptAssetValue::GetPath() const
    {
        const auto *asset = ResolveScriptAsset(_guid);
        return asset == nullptr ? String{} : ToChar(asset->_asset_path);
    }
    String ScriptAssetValue::GetAssetType() const
    {
        const auto *asset = ResolveScriptAsset(_guid);
        return asset == nullptr || asset->_asset_type == nullptr ? String{} : asset->_asset_type->Name();
    }
}
