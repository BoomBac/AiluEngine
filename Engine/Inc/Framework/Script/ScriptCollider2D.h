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

    AENUM(Script)
    enum class ECollisionChannel2D : u8
    {
        kWorldStatic,
        kWorldDynamic,
        kPlayer,
        kEnemy,
        kProjectile,
        kTrigger,
        kPickup,
        kCount
    };

    ASTRUCT(Script)
    struct AILU_API ScriptCollider2D
    {
        using CollisionEventRouter = EventRouter<ECollisionChannel2D, const ScriptEntity &, const ScriptContact2D &>;
        using EntityEventDelegate = Delegate<const ScriptEntity &>;

        struct EventViews
        {
            CollisionEventRouter::EventView _on_collision_enter;
            CollisionEventRouter::EventView _on_collision_exit;
            CollisionEventRouter::EventView _on_trigger_enter;
            CollisionEventRouter::EventView _on_trigger_exit;
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

        AEVENT(Script, KeyName = target_channel)
        void OnCollisionEnter(ECollisionChannel2D target_channel, const ScriptEntity &other, const ScriptContact2D &contact);
        AEVENT(Script, KeyName = target_channel)
        void OnCollisionExit(ECollisionChannel2D target_channel, const ScriptEntity &other, const ScriptContact2D &contact);
        AEVENT(Script, KeyName = target_channel)
        void OnTriggerEnter(ECollisionChannel2D target_channel, const ScriptEntity &other, const ScriptContact2D &contact);
        AEVENT(Script, KeyName = target_channel)
        void OnTriggerExit(ECollisionChannel2D target_channel, const ScriptEntity &other, const ScriptContact2D &contact);

        CollisionEventRouter::EventView _on_collision_enter;
        CollisionEventRouter::EventView _on_collision_exit;
        CollisionEventRouter::EventView _on_trigger_enter;
        CollisionEventRouter::EventView _on_trigger_exit;
    };
}
