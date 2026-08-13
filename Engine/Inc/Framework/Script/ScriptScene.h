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

    ASTRUCT(Script)
    struct AILU_API ScriptScene
    {
        GENERATED_BODY()
        SceneManagement::Scene *_scene = nullptr;
        AFUNCTION(Script)
        static ScriptScene CurrentScene();
        AFUNCTION(Script)
        bool IsValid() const;
        AFUNCTION(Script)
        ScriptEntity FindEntity(const String &guid) const;
        AFUNCTION(Script)
        ScriptEntity FindEntityByName(const String &name) const;
        AFUNCTION(Script)
        ScriptEntity Find(const String &name) const;
        AFUNCTION(Script)
        ScriptEntity FindGuid(const String &guid) const;
        AFUNCTION(Script)
        ScriptEntity CreateEntity(const String &name) const;
        // The Lua binding generator combines this overload with the name-based overload.
        AFUNCTION(Script)
        ScriptEntity CreateEntity(const ScriptAssetValue &prefab) const;
        AFUNCTION(Script)
        ScriptEntity Spawn(const ScriptAssetValue &prefab) const;
        AFUNCTION(ScriptProperty)
        ScriptCamera GetMainCamera() const;
    };
}
