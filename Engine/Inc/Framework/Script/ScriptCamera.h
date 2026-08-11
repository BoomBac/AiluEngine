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

    ASTRUCT()
    struct AILU_API ScriptCamera
    {
        GENERATED_BODY()
        SceneManagement::Scene *_scene = nullptr;
        ECS::Entity _entity = ECS::kInvalidEntity;

        AFUNCTION(Script) 
        bool IsValid() const;
        AFUNCTION(Script) 
        Vector3f GetPosition() const;
        AFUNCTION(Script) 
        void SetPosition(const Vector3f &position) const;
        AFUNCTION(Script) 
        Math::Quaternion GetRotation() const;
        AFUNCTION(Script) 
        void SetRotation(const Math::Quaternion &rotation) const;
        AFUNCTION(Script) 
        Vector3f GetForward() const;
        AFUNCTION(Script) 
        f32 GetFov() const;
        AFUNCTION(Script) 
        void SetFov(f32 fov) const;
        AFUNCTION(Script) 
        f32 GetNearClip() const;
        AFUNCTION(Script) 
        void SetNearClip(f32 near_clip) const;
        AFUNCTION(Script) 
        f32 GetFarClip() const;
        AFUNCTION(Script) 
        void SetFarClip(f32 far_clip) const;
        AFUNCTION(Script) 
        f32 GetAspect() const;
        AFUNCTION(Script) 
        void SetAspect(f32 aspect) const;
        AFUNCTION(Script) 
        bool IsOrthographic() const;
        AFUNCTION(Script) 
        void SetOrthographic(bool enabled) const;
        AFUNCTION(Script) 
        f32 GetOrthographicSize() const;
        AFUNCTION(Script) 
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
