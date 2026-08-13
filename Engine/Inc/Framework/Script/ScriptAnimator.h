#pragma once

#include "Framework/Core/CoreMinimal.h"
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
        GENERATED_BODY()
        SceneManagement::Scene *_scene = nullptr;
        ECS::Entity _entity = ECS::kInvalidEntity;

        AFUNCTION(ScriptProperty)
        bool IsValid() const;
        AFUNCTION(Script)
        bool Play(const String &name) const;
        AFUNCTION(ScriptProperty)
        f32 GetSpeed() const;
        AFUNCTION(ScriptProperty)
        void SetSpeed(f32 speed) const;
    };
}
