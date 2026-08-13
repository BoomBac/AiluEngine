#pragma once

#include <optional>

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Math/Quaternion.h"
#include "Framework/Math/Guid.h"
#include "Scene/Entity.h"
#include "Framework/Script/ScriptAnimator.h"
#include "Framework/Script/ScriptAssetValue.h"
#include "Framework/Script/ScriptAudioSource.h"
#include "Framework/Script/ScriptCollider2D.h"
#include "Framework/Script/ScriptRigidBody2D.h"
#include "Framework/Script/ScriptSpriteRenderer.h"

#include "generated/ScriptEntity.gen.h"

namespace Ailu
{
    namespace SceneManagement
    {
        class Scene;
    }

    struct ScriptEntity;

    ASTRUCT(Script)
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
        static bool Add(const ScriptEntity &entity, const String &type_name);
        AFUNCTION(Script)
        static bool SetEntityFloat(const ScriptEntity &entity, const String &type_name, const String &property, f32 value);
        AFUNCTION(Script)
        static bool SetEntityBool(const ScriptEntity &entity, const String &type_name, const String &property, bool value);
        AFUNCTION(Script)
        static bool SetEntityVector3(const ScriptEntity &entity, const String &type_name, const String &property, const Vector3f &value);
        AFUNCTION(Script)
        static bool AddEntityBoxShape(const ScriptEntity &entity, const String &type_name, const Vector2f &size, bool is_trigger = false);
    };

    ASTRUCT(Script)
    struct AILU_API ScriptTransform
    {
        GENERATED_BODY()
        SceneManagement::Scene *_scene = nullptr;
        ECS::Entity _entity = ECS::kInvalidEntity;

        AFUNCTION(ScriptProperty)
        bool IsValid() const;
        AFUNCTION(ScriptProperty)
        Vector3f GetLocalPosition() const;
        AFUNCTION(ScriptProperty)
        void SetLocalPosition(const Vector3f &position) const;
        AFUNCTION(ScriptProperty)
        Math::Quaternion GetLocalRotation() const;
        AFUNCTION(ScriptProperty)
        void SetLocalRotation(const Math::Quaternion &rotation) const;
        AFUNCTION(ScriptProperty)
        Vector3f GetLocalScale() const;
        AFUNCTION(ScriptProperty)
        void SetLocalScale(const Vector3f &scale) const;
        AFUNCTION(ScriptProperty)
        Vector3f GetPosition() const;
        AFUNCTION(ScriptProperty)
        void SetPosition(const Vector3f &position) const;
        AFUNCTION(ScriptProperty)
        Math::Quaternion GetRotation() const;
        AFUNCTION(ScriptProperty)
        void SetRotation(const Math::Quaternion &rotation) const;
        AFUNCTION(ScriptProperty)
        Vector3f GetScale() const;
        AFUNCTION(ScriptProperty)
        void SetScale(const Vector3f &scale) const;
        AFUNCTION(ScriptProperty)
        Vector3f GetForward() const;
        AFUNCTION(ScriptProperty)
        Vector3f GetRight() const;
        AFUNCTION(ScriptProperty)
        Vector3f GetUp() const;
    };

    ASTRUCT(Script)
    struct AILU_API ScriptEntity
    {
        GENERATED_BODY()
        SceneManagement::Scene *_scene = nullptr;
        ECS::Entity _entity = ECS::kInvalidEntity;

        AFUNCTION(Script)
        bool IsValid() const;
        AFUNCTION(ScriptProperty)
        String GetName() const;
        AFUNCTION(ScriptProperty)
        void SetName(const String &name) const;
        AFUNCTION(ScriptProperty)
        String GetGuid() const;
        AFUNCTION(ScriptProperty)
        ScriptTransform GetTransform() const;
        AFUNCTION(ScriptProperty)
        std::optional<ScriptRigidBody2D> GetRigidBody2D() const;
        AFUNCTION(ScriptProperty)
        std::optional<ScriptCollider2D> GetCollider2D() const;
        AFUNCTION(ScriptProperty)
        std::optional<ScriptSpriteRenderer> GetSprite() const;
        AFUNCTION(ScriptProperty)
        std::optional<ScriptAnimator> GetAnimator() const;
        AFUNCTION(ScriptProperty)
        std::optional<ScriptAudioSource> GetAudio() const;
        AFUNCTION(Script)
        void Destroy() const;
        AFUNCTION(Script)
        bool HasComponent(const String &type_name) const;
        ScriptComponent AddComponent(const String &type_name) const;
        ScriptComponent GetComponent(const String &type_name) const;
        AFUNCTION(Script)
        bool RemoveComponent(const String &type_name) const;
    };

}
