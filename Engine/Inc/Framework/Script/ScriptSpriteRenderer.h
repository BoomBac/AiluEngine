#pragma once

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Script/ScriptAssetValue.h"
#include "Scene/Entity.h"

#include "generated/ScriptSpriteRenderer.gen.h"

namespace Ailu
{
    namespace SceneManagement
    {
        class Scene;
    }

    ASTRUCT(Script)
    struct AILU_API ScriptSpriteRenderer
    {
        GENERATED_BODY()
        SceneManagement::Scene *_scene = nullptr;
        ECS::Entity _entity = ECS::kInvalidEntity;

        AFUNCTION(ScriptProperty)
        bool IsValid() const;
        AFUNCTION(ScriptProperty)
        ScriptAssetValue GetSprite() const;
        AFUNCTION(ScriptProperty)
        void SetSprite(const ScriptAssetValue &sprite) const;
        AFUNCTION(ScriptProperty)
        bool IsVisible() const;
        AFUNCTION(ScriptProperty)
        void SetVisible(bool visible) const;
        AFUNCTION(ScriptProperty)
        bool IsFlipX() const;
        AFUNCTION(ScriptProperty)
        void SetFlipX(bool flip_x) const;
        AFUNCTION(ScriptProperty)
        bool IsFlipY() const;
        AFUNCTION(ScriptProperty)
        void SetFlipY(bool flip_y) const;
        AFUNCTION(ScriptProperty)
        i32 GetOrder() const;
        AFUNCTION(ScriptProperty)
        void SetOrder(i32 order) const;
        AFUNCTION(ScriptProperty)
        Color GetColor() const;
        AFUNCTION(ScriptProperty)
        void SetColor(const Color &color) const;
    };
}
