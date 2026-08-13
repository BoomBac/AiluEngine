#pragma once

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Math/Guid.h"

#include "generated/ScriptAssetValue.gen.h"

namespace Ailu
{
    ASTRUCT(Script)
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
        static ScriptAssetValue Prefab();

        AFUNCTION(ScriptProperty)
        bool IsValid() const;
    };
}
