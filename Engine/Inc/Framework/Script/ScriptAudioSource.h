#pragma once

#include "Framework/Core/CoreMinimal.h"
#include "Scene/Entity.h"

#include "generated/ScriptAudioSource.gen.h"

namespace Ailu
{
    namespace SceneManagement
    {
        class Scene;
    }

    ASTRUCT(Script)
    struct AILU_API ScriptAudioSource
    {
        GENERATED_BODY()
        SceneManagement::Scene *_scene = nullptr;
        ECS::Entity _entity = ECS::kInvalidEntity;

        AFUNCTION(ScriptProperty)
        bool IsValid() const;
        AFUNCTION(Script)
        void Play() const;
        AFUNCTION(Script)
        void Stop() const;
        AFUNCTION(ScriptProperty)
        f32 GetVolume() const;
        AFUNCTION(ScriptProperty)
        void SetVolume(f32 volume) const;
        AFUNCTION(ScriptProperty)
        f32 GetPitch() const;
        AFUNCTION(ScriptProperty)
        void SetPitch(f32 pitch) const;
        AFUNCTION(ScriptProperty)
        bool IsLoop() const;
        AFUNCTION(ScriptProperty)
        void SetLoop(bool loop) const;
    };
}
