#pragma once

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/Delegate.h"
#include "Framework/Script/ScriptContact2D.h"
#include "Scene/Entity.h"

#include "generated/ScriptCollider2D.gen.h"

namespace Ailu
{
    namespace SceneManagement
    {
        class Scene;
    }

    struct ScriptEntity;

    ASTRUCT(Script)
    struct AILU_API ScriptCollider2D
    {
        using CollisionEventDelegate = Delegate<const ScriptEntity &, const ScriptContact2D &>;
        using EntityEventDelegate = Delegate<const ScriptEntity &>;

        struct EventViews
        {
            CollisionEventDelegate::EventView _on_collision_enter;
            CollisionEventDelegate::EventView _on_collision_exit;
            EntityEventDelegate::EventView _on_trigger_enter;
            EntityEventDelegate::EventView _on_trigger_exit;
        };

        GENERATED_BODY()
        SceneManagement::Scene *_scene = nullptr;
        ECS::Entity _entity = ECS::kInvalidEntity;

        ScriptCollider2D() = default;
        ScriptCollider2D(SceneManagement::Scene *scene, ECS::Entity entity);

        AFUNCTION(ScriptProperty)
        bool IsValid() const;
        AFUNCTION(ScriptProperty)
        u32 GetShapeCount() const;

        AEVENT(Script)
        DECLARE_DELEGATE_VIEW(on_collision_enter, const ScriptEntity &, const ScriptContact2D &);
        AEVENT(Script)
        DECLARE_DELEGATE_VIEW(on_collision_exit, const ScriptEntity &, const ScriptContact2D &);
        AEVENT(Script)
        DECLARE_DELEGATE_VIEW(on_trigger_enter, const ScriptEntity &);
        AEVENT(Script)
        DECLARE_DELEGATE_VIEW(on_trigger_exit, const ScriptEntity &);
    };
}
