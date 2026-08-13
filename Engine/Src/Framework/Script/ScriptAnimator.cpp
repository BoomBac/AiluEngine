#include "Framework/Script/ScriptAnimator.h"

#include "Animation/Clip.h"
#include "Scene/Component.h"
#include "Scene/Scene.h"

namespace Ailu
{
    namespace
    {
        ECS::CSkeletonMesh *ResolveAnimator(SceneManagement::Scene *scene, ECS::Entity entity)
        {
            return scene != nullptr && scene->IsValidEntity(entity) ?
                       scene->GetRegister().GetComponent<ECS::CSkeletonMesh>(entity) : nullptr;
        }
    }

    bool ScriptAnimator::IsValid() const { return ResolveAnimator(_scene, _entity) != nullptr; }
    bool ScriptAnimator::Play(const String &name) const
    {
        auto *component = ResolveAnimator(_scene, _entity);
        if (component == nullptr) return false;
        Ref<AnimationClip> clip = AnimationClipLibrary::GetClip(name);
        if (!clip) return false;
        component->_anim_clip = std::move(clip);
        component->_blend_anim_clip.reset();
        component->_anim_type = 0;
        component->_anim_time = 0.0f;
        return true;
    }
    f32 ScriptAnimator::GetSpeed() const
    {
        const auto *component = ResolveAnimator(_scene, _entity);
        return component != nullptr ? component->_anim_speed : 0.0f;
    }
    void ScriptAnimator::SetSpeed(f32 speed) const
    {
        if (auto *component = ResolveAnimator(_scene, _entity)) component->_anim_speed = speed;
    }
}
