#pragma once

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Script/ScriptCamera.h"
#include "Framework/Script/ScriptEntity.h"

#include "generated/ScriptScene.gen.h"

namespace Ailu
{
    namespace SceneManagement
    {
        class Scene;
    }

    ASTRUCT()
    struct AILU_API ScriptScene
    {
        GENERATED_BODY()
        SceneManagement::Scene *_scene = nullptr;

        AFUNCTION(Script)
        bool IsValid() const;
        AFUNCTION(Script)
        ScriptEntity FindEntity(const String &guid) const;
        AFUNCTION(Script)
        ScriptEntity FindEntityByName(const String &name) const;
        AFUNCTION(Script)
        ScriptEntity CreateEntity(const String &name) const;
        AFUNCTION(Script)
        ScriptCamera GetMainCamera() const;
    };
}
