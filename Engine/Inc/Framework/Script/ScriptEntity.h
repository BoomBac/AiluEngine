#pragma once

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Math/Quaternion.h"
#include "Framework/Math/Guid.h"
#include "Scene/Entity.h"

#include "generated/ScriptEntity.gen.h"

namespace Ailu
{
    namespace SceneManagement
    {
        class Scene;
    }

    struct ScriptEntity;

    ASTRUCT()
    struct AILU_API ScriptComponent
    {
        GENERATED_BODY()
        SceneManagement::Scene *_scene = nullptr;
        ECS::Entity _entity = ECS::kInvalidEntity;
        String _type_name;

        AFUNCTION(Script)
        bool IsValid() const;
        AFUNCTION(Script)
        String GetTypeName() const;
        AFUNCTION(Script)
        bool IsEnabled() const;
        AFUNCTION(Script)
        void SetEnabled(bool enabled) const;
        AFUNCTION(Script)
        bool SetFloat(const String &property, f32 value) const;
        AFUNCTION(Script)
        bool SetInt(const String &property, i32 value) const;
        AFUNCTION(Script)
        bool SetBool(const String &property, bool value) const;
        AFUNCTION(Script)
        bool SetString(const String &property, const String &value) const;
        AFUNCTION(Script)
        bool SetVector2(const String &property, const Vector2f &value) const;
        AFUNCTION(Script)
        bool SetVector3(const String &property, const Vector3f &value) const;
        AFUNCTION(Script)
        bool AddBoxShape(const Vector2f &size, bool is_trigger = false) const;
        AFUNCTION(Script)
        bool SetSprite(const String &sprite_guid) const;

        AFUNCTION(Script)
        static bool Add(const ScriptEntity &entity, const String &type_name);
        AFUNCTION(Script)
        static bool SetEntityFloat(const ScriptEntity &entity, const String &type_name, const String &property, f32 value);
        AFUNCTION(Script)
        static bool SetEntityBool(const ScriptEntity &entity, const String &type_name, const String &property, bool value);
        AFUNCTION(Script)
        static bool SetEntityVector3(const ScriptEntity &entity, const String &type_name, const String &property, const Vector3f &value);
        AFUNCTION(Script)
        static bool AddEntityBoxShape(const ScriptEntity &entity, const String &type_name, const Vector2f &size, bool is_trigger = false);
        AFUNCTION(Script)
        static bool SetEntitySprite(const ScriptEntity &entity, const String &type_name, const String &sprite_guid);
    };

    ASTRUCT()
    struct AILU_API ScriptTransform
    {
        GENERATED_BODY()
        SceneManagement::Scene *_scene = nullptr;
        ECS::Entity _entity = ECS::kInvalidEntity;

        AFUNCTION(Script)
        bool IsValid() const;
        AFUNCTION(Script)
        Vector3f GetLocalPosition() const;
        AFUNCTION(Script)
        void SetLocalPosition(const Vector3f &position) const;
        AFUNCTION(Script)
        Math::Quaternion GetLocalRotation() const;
        AFUNCTION(Script)
        void SetLocalRotation(const Math::Quaternion &rotation) const;
        AFUNCTION(Script)
        Vector3f GetLocalScale() const;
        AFUNCTION(Script)
        void SetLocalScale(const Vector3f &scale) const;
        Vector3f GetPosition() const;
        Math::Quaternion GetRotation() const;
        Vector3f GetScale() const;
    };

    ASTRUCT()
    struct AILU_API ScriptEntity
    {
        GENERATED_BODY()
        SceneManagement::Scene *_scene = nullptr;
        ECS::Entity _entity = ECS::kInvalidEntity;

        AFUNCTION(Script)
        bool IsValid() const;
        AFUNCTION(Script)
        String GetName() const;
        AFUNCTION(Script)
        void SetName(const String &name) const;
        AFUNCTION(Script)
        String GetGuid() const;
        AFUNCTION(Script)
        ScriptTransform GetTransform() const;
        AFUNCTION(Script)
        void Destroy() const;
        AFUNCTION(Script)
        bool HasComponent(const String &type_name) const;
        ScriptComponent AddComponent(const String &type_name) const;
        ScriptComponent GetComponent(const String &type_name) const;
        AFUNCTION(Script)
        bool RemoveComponent(const String &type_name) const;
    };

    ASTRUCT()
    struct AILU_API ScriptAssetValue
    {
        GENERATED_BODY()
        Guid _guid = Guid::EmptyGuid();
        String _asset_type;

        AFUNCTION(Script)
        static ScriptAssetValue Sprite();
        AFUNCTION(Script)
        static ScriptAssetValue Texture2D();
        AFUNCTION(Script)
        static ScriptAssetValue Material();
        AFUNCTION(Script)
        static ScriptAssetValue Mesh();
        AFUNCTION(Script)
        static ScriptAssetValue SkeletonMesh();
        AFUNCTION(Script)
        static ScriptAssetValue AnimationClip();
        AFUNCTION(Script)
        static ScriptAssetValue AudioClip();
        AFUNCTION(Script)
        static ScriptAssetValue Script();

        AFUNCTION(Script)
        bool IsValid() const;
        AFUNCTION(Script)
        String GetGuid() const;
        AFUNCTION(Script)
        String GetName() const;
        AFUNCTION(Script)
        String GetPath() const;
        AFUNCTION(Script)
        String GetAssetType() const;
    };
}
