#include "Framework/Script/ScriptCollider2D.h"

#include "Framework/Script/ScriptSystem.h"
#include "Physics/2D/Physics2DComponents.h"
#include "Scene/Scene.h"

namespace Ailu
{
    ScriptCollider2D::ScriptCollider2D(SceneManagement::Scene *scene, ECS::Entity entity) : _scene(scene), _entity(entity)
    {
#if AILU_ENABLE_LUA_SCRIPTING
        if (_scene != nullptr && _scene->IsValidEntity(_entity))
        {
            const EventViews event_views = ScriptSystem::Get().GetColliderEventViews(_scene, _entity);
            _on_collision_enter = event_views._on_collision_enter;
            _on_collision_exit = event_views._on_collision_exit;
            _on_trigger_enter = event_views._on_trigger_enter;
            _on_trigger_exit = event_views._on_trigger_exit;
        }
#endif
    }

    bool ScriptCollider2D::IsValid() const
    {
        return _scene != nullptr && _scene->IsValidEntity(_entity) &&
               _scene->GetRegister().HasComponent<ECS::Collider2DComponent>(_entity);
    }

    u32 ScriptCollider2D::GetShapeCount() const
    {
        if (!IsValid())
            return 0u;
        const auto *component = _scene->GetRegister().GetComponent<ECS::Collider2DComponent>(_entity);
        return component != nullptr ? static_cast<u32>(component->_shapes.size()) : 0u;
    }
}
