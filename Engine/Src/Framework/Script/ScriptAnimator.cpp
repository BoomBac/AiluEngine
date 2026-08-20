#include "Framework/Script/ScriptAnimator.h"

#include "Animation/AnimationControllerAsset.h"
#include "Animation/AnimationSystem.h"
#include "Animation/AnimationEvent.h"
#include "Framework/Common/ResourceMgr.h"
#include "Scene/Component.h"
#include "Scene/Scene.h"
#include "Framework/Script/ScriptSystem.h"

namespace Ailu
{
    namespace
    {
        ECS::AnimatorComponent *ResolveAnimator(SceneManagement::Scene *scene, ECS::Entity entity)
        {
            return scene != nullptr && scene->IsValidEntity(entity) ?
                       scene->GetRegister().GetComponent<ECS::AnimatorComponent>(entity) : nullptr;
        }

        Ref<AnimationControllerAsset> ResolveController(SceneManagement::Scene *scene, ECS::Entity entity)
        {
            const auto *animator = ResolveAnimator(scene, entity);
            if (animator == nullptr || animator->_controller.IsEmpty())
                return nullptr;

            ResourceMgr &resource_mgr = ResourceMgr::Get();
            Ref<AnimationControllerAsset> controller =
                resource_mgr.GetRef<AnimationControllerAsset>(animator->_controller);
            if (controller == nullptr)
            {
                resource_mgr.Load<AnimationControllerAsset>(animator->_controller);
                controller = resource_mgr.GetRef<AnimationControllerAsset>(animator->_controller);
            }
            return controller;
        }

        AnimationParameterId ResolveParameter(SceneManagement::Scene *scene, ECS::Entity entity, const String &name)
        {
            const Ref<AnimationControllerAsset> controller = ResolveController(scene, entity);
            return controller != nullptr ? controller->GetParameterId(name) : kInvalidAnimationParameter;
        }

        ECS::AnimationSystem *ResolveAnimationSystem(SceneManagement::Scene *scene)
        {
            return scene != nullptr ? scene->GetRegister().GetSystem<ECS::AnimationSystem>() : nullptr;
        }
    }

    ScriptAnimator::ScriptAnimator(SceneManagement::Scene *scene, ECS::Entity entity) : _scene(scene), _entity(entity)
    {
#if AILU_ENABLE_LUA_SCRIPTING
        if (_scene != nullptr && _scene->IsValidEntity(_entity))
            _on_event = ScriptSystem::Get().GetAnimatorEventView(_scene, _entity);
#endif
    }

    bool ScriptAnimator::IsValid() const
    {
        return ResolveAnimator(_scene, _entity) != nullptr;
    }

    bool ScriptAnimator::Play(const String &name) const
    {
        const Ref<AnimationControllerAsset> controller = ResolveController(_scene, _entity);
        ECS::AnimationSystem *system = ResolveAnimationSystem(_scene);
        if (controller == nullptr || system == nullptr)
            return false;
        for (u16 index = 0u; index < controller->States().size(); ++index)
        {
            if (controller->States()[index]._name == name)
            {
                system->PlayState(_entity, index);
                return true;
            }
        }
        return false;
    }

    u32 ScriptAnimator::GetParameterId(const String &name) const
    {
        return ResolveParameter(_scene, _entity, name);
    }

    void ScriptAnimator::SetFloat(const String &name, f32 value) const
    {
        if (auto *system = ResolveAnimationSystem(_scene))
            system->SetFloat(_entity, ResolveParameter(_scene, _entity, name), value);
    }

    void ScriptAnimator::SetInt(const String &name, i32 value) const
    {
        if (auto *system = ResolveAnimationSystem(_scene))
            system->SetInt(_entity, ResolveParameter(_scene, _entity, name), value);
    }

    void ScriptAnimator::SetBool(const String &name, bool value) const
    {
        if (auto *system = ResolveAnimationSystem(_scene))
            system->SetBool(_entity, ResolveParameter(_scene, _entity, name), value);
    }

    void ScriptAnimator::SetTrigger(const String &name) const
    {
        if (auto *system = ResolveAnimationSystem(_scene))
            system->SetTrigger(_entity, ResolveParameter(_scene, _entity, name));
    }

    void ScriptAnimator::ResetTrigger(const String &name) const
    {
        if (auto *system = ResolveAnimationSystem(_scene))
            system->ResetTrigger(_entity, ResolveParameter(_scene, _entity, name));
    }

    u32 ScriptAnimator::GetEventId(const String &name) const
    {
        return AnimationEventNameHash(name);
    }

    void ScriptAnimator::OnEvent(u32)
    {
    }

    f32 ScriptAnimator::GetSpeed() const
    {
        const auto *component = ResolveAnimator(_scene, _entity);
        return component != nullptr ? component->_speed : 0.0f;
    }

    void ScriptAnimator::SetSpeed(f32 speed) const
    {
        if (auto *component = ResolveAnimator(_scene, _entity))
            component->_speed = std::max(speed, 0.0f);
    }
}
