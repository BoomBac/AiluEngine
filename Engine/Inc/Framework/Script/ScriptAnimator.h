#pragma once

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/Delegate.h"
#include "Scene/Entity.h"

#include "generated/ScriptAnimator.gen.h"

namespace Ailu
{
    namespace SceneManagement
    {
        class Scene;
    }

    ASTRUCT(Script)
    struct AILU_API ScriptAnimator
    {
        using AnimationEventRouter = EventRouter<u32, u32>;

        GENERATED_BODY()
        SceneManagement::Scene *_scene = nullptr;
        ECS::Entity _entity = ECS::kInvalidEntity;

        ScriptAnimator() = default;
        ScriptAnimator(SceneManagement::Scene *scene, ECS::Entity entity);

        AFUNCTION(ScriptProperty)
        bool IsValid() const;
        AFUNCTION(Script)
        bool Play(const String &name) const;
        AFUNCTION(Script)
        u32 GetParameterId(const String &name) const;
        AFUNCTION(Script)
        void SetFloat(const String &name, f32 value) const;
        AFUNCTION(Script)
        void SetInt(const String &name, i32 value) const;
        AFUNCTION(Script)
        void SetBool(const String &name, bool value) const;
        AFUNCTION(Script)
        void SetTrigger(const String &name) const;
        AFUNCTION(Script)
        void ResetTrigger(const String &name) const;
        AFUNCTION(Script)
        u32 GetEventId(const String &name) const;
        AEVENT(Script, KeyName = event_id)
        void OnEvent(u32 event_id);
        AFUNCTION(ScriptProperty)
        f32 GetSpeed() const;
        AFUNCTION(ScriptProperty)
        void SetSpeed(f32 speed) const;

        AnimationEventRouter::EventView _on_event;
    };
}
