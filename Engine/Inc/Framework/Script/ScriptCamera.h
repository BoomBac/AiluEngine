#pragma once

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Math/Quaternion.h"
#include "Scene/Entity.h"

#include "generated/ScriptCamera.gen.h"

namespace Ailu
{
    namespace SceneManagement
    {
        class Scene;
    }
    namespace Render
    {
        class Camera;
    }
    namespace ECS
    {
        struct TransformComponent;
    }

    ASTRUCT(Script; Global="camera")
    struct AILU_API ScriptCamera
    {
        GENERATED_BODY()
        SceneManagement::Scene *_scene = nullptr;
        ECS::Entity _entity = ECS::kInvalidEntity;

        AFUNCTION(ScriptProperty)
        bool IsValid() const;
        AFUNCTION(ScriptProperty)
        Vector3f GetPosition() const;
        AFUNCTION(ScriptProperty)
        void SetPosition(const Vector3f &position) const;
        AFUNCTION(ScriptProperty)
        Math::Quaternion GetRotation() const;
        AFUNCTION(ScriptProperty)
        void SetRotation(const Math::Quaternion &rotation) const;
        AFUNCTION(ScriptProperty)
        Vector3f GetForward() const;
        AFUNCTION(ScriptProperty)
        f32 GetFov() const;
        AFUNCTION(ScriptProperty)
        void SetFov(f32 fov) const;
        AFUNCTION(ScriptProperty)
        f32 GetNearClip() const;
        AFUNCTION(ScriptProperty)
        void SetNearClip(f32 near_clip) const;
        AFUNCTION(ScriptProperty)
        f32 GetFarClip() const;
        AFUNCTION(ScriptProperty)
        void SetFarClip(f32 far_clip) const;
        AFUNCTION(ScriptProperty)
        f32 GetAspect() const;
        AFUNCTION(ScriptProperty)
        void SetAspect(f32 aspect) const;
        AFUNCTION(ScriptProperty)
        bool IsOrthographic() const;
        AFUNCTION(ScriptProperty)
        void SetOrthographic(bool enabled) const;
        AFUNCTION(ScriptProperty)
        f32 GetOrthographicSize() const;
        AFUNCTION(ScriptProperty)
        void SetOrthographicSize(f32 size) const;
        AFUNCTION(Script) 
        void LookTo(const Vector3f &direction, const Vector3f &up) const;
        AFUNCTION(Script) 
        Vector2f WorldToScreen(const Vector3f &world_position) const;
        AFUNCTION(Script) 
        Vector3f ScreenToWorld(const Vector2f &screen_position, f32 depth) const;

    private:
        Render::Camera *Resolve() const;
        ECS::TransformComponent *ResolveTransform() const;
        void SetWorldTransform(const Vector3f &position, const Math::Quaternion &rotation) const;
    };
}
