#include "Scene/SceneCommand.h"


namespace Ailu::SceneManagement
{
    #pragma region Reparent
    ReparentSceneCommand::ReparentSceneCommand(ECS::Entity child, ECS::Entity new_parent, bool keep_world_transform)
        : _child(child), _new_parent(new_parent), _keep_world_transform(keep_world_transform)
    {
    }

    const String &ReparentSceneCommand::ToString() const
    {
        static String name = "Reparent";
        return name;
    }

    void ReparentSceneCommand::CaptureOldState(Scene &scene)
    {
        auto &reg = scene.GetRegister();
        if (auto *hier = reg.GetComponent<ECS::CHierarchy>(_child))
            _old_parent = hier->_parent;
        if (auto *transform = reg.GetComponent<ECS::TransformComponent>(_child))
            _old_local_transform = transform->_local_transform;
    }

    void ReparentSceneCommand::CaptureNewState(Scene &scene)
    {
        if (auto *transform = scene.GetRegister().GetComponent<ECS::TransformComponent>(_child))
            _new_local_transform = transform->_local_transform;
    }

    bool ReparentSceneCommand::Apply(Scene &scene, ECS::Entity parent, const Transform &local_transform) const
    {
        if (!scene.IsValidEntity(_child))
            return false;
        if (parent != ECS::kInvalidEntity && !scene.IsValidEntity(parent))
            return false;

        if (!scene.Reparent(_child, parent, false))
            return false;

        if (auto *transform = scene.GetRegister().GetComponent<ECS::TransformComponent>(_child))
        {
            transform->_local_transform = local_transform;
            transform->_local_dirty = true;
            transform->_world_dirty = true;
        }
        return true;
    }

    bool ReparentSceneCommand::Execute(Scene &scene)
    {
        if (!_has_executed)
        {
            CaptureOldState(scene);
            if (!scene.Reparent(_child, _new_parent, _keep_world_transform))
                return false;
            CaptureNewState(scene);
            _has_executed = true;
            return true;
        }

        return Apply(scene, _new_parent, _new_local_transform);
    }

    bool ReparentSceneCommand::Undo(Scene &scene)
    {
        if (!_has_executed)
            return false;
        return Apply(scene, _old_parent, _old_local_transform);
    }
    #pragma endregion

}// namespace Ailu::SceneManagement
