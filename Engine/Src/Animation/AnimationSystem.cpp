#include "Animation/AnimationSystem.h"

#include "Framework/Common/Profiler.h"
#include "Framework/Common/ResourceMgr.h"
#include "Scene/Component.h"
#include "pch.h"

#include <algorithm>

namespace Ailu::ECS
{
    void AnimationSystem::QueueCommand(Entity entity, ParameterCommand command)
    {
        _pending_commands[entity].push_back(std::move(command));
    }

    void AnimationSystem::SetFloat(Entity entity, AnimationParameterId id, f32 value)
    {
        QueueCommand(entity, ParameterCommand{EParameterCommandType::kFloat, id, value});
    }

    void AnimationSystem::SetInt(Entity entity, AnimationParameterId id, i32 value)
    {
        ParameterCommand command;
        command._type = EParameterCommandType::kInt;
        command._parameter = id;
        command._int_value = value;
        QueueCommand(entity, std::move(command));
    }

    void AnimationSystem::SetBool(Entity entity, AnimationParameterId id, bool value)
    {
        ParameterCommand command;
        command._type = EParameterCommandType::kBool;
        command._parameter = id;
        command._bool_value = value;
        QueueCommand(entity, std::move(command));
    }

    void AnimationSystem::SetTrigger(Entity entity, AnimationParameterId id)
    {
        ParameterCommand command;
        command._type = EParameterCommandType::kTrigger;
        command._parameter = id;
        QueueCommand(entity, std::move(command));
    }

    void AnimationSystem::ResetTrigger(Entity entity, AnimationParameterId id)
    {
        ParameterCommand command;
        command._type = EParameterCommandType::kResetTrigger;
        command._parameter = id;
        QueueCommand(entity, std::move(command));
    }

    void AnimationSystem::PlayState(Entity entity, u16 state_index)
    {
        ParameterCommand command;
        command._type = EParameterCommandType::kPlayState;
        command._state = state_index;
        QueueCommand(entity, std::move(command));
    }

    void AnimationSystem::ApplyCommands(Entity entity, AnimationInstance &instance, AnimatorComponent &animator,
                                         const AnimationControllerAsset &controller_asset)
    {
        const auto commands_iter = _pending_commands.find(entity);
        if (commands_iter == _pending_commands.end())
            return;

        for (const ParameterCommand &command : commands_iter->second)
        {
            switch (command._type)
            {
            case EParameterCommandType::kFloat:
                instance.SetFloat(command._parameter, command._float_value);
                break;
            case EParameterCommandType::kInt:
                instance.SetInt(command._parameter, command._int_value);
                break;
            case EParameterCommandType::kBool:
                instance.SetBool(command._parameter, command._bool_value);
                break;
            case EParameterCommandType::kTrigger:
                instance.SetTrigger(command._parameter);
                break;
            case EParameterCommandType::kResetTrigger:
                instance.ResetTrigger(command._parameter);
                break;
            case EParameterCommandType::kPlayState:
                if (command._state < controller_asset.States().size())
                {
                    instance._current_state = command._state;
                    instance._next_state = kInvalidAnimationState;
                    instance._state_time = 0.0f;
                    instance._next_state_time = 0.0f;
                    instance._transition_time = 0.0f;
                    instance._transition_duration = 0.0f;
                    instance._in_transition = false;
                    instance._previous_state = command._state;
                    instance._previous_next_state = kInvalidAnimationState;
                    instance._previous_state_time = 0.0f;
                    instance._previous_next_state_time = 0.0f;
                    animator._started = true;
                }
                break;
            }
        }
        _pending_commands.erase(commands_iter);
    }

    const AnimationClip *AnimationSystem::ResolveClip(Entity entity, const Guid &clip_id, Skeleton *skeleton,
                                                       SpriteAnimationBinding *sprite_binding,
                                                       SkeletonAnimationBinding *skeleton_binding)
    {
        if (clip_id.IsEmpty())
            return nullptr;

        const AnimationClip *clip = skeleton_binding != nullptr ? skeleton_binding->FindClip(clip_id) : nullptr;
        if (clip == nullptr && sprite_binding != nullptr)
            clip = sprite_binding->FindClip(clip_id);
        if (clip == nullptr)
        {
            ResourceMgr::Get().Load<AnimationClip>(clip_id);
            const Ref<AnimationClip> loaded_clip = ResourceMgr::Get().GetRef<AnimationClip>(clip_id);
            clip = loaded_clip.get();
            if (clip == nullptr)
                return nullptr;
        }

        if (skeleton_binding != nullptr && skeleton != nullptr && skeleton_binding->FindClip(clip_id) == nullptr)
            skeleton_binding->Resolve(clip_id, *clip, *skeleton);
        if (sprite_binding != nullptr && sprite_binding->FindClip(clip_id) == nullptr)
            sprite_binding->Resolve(clip_id, *clip);
        return clip;
    }

    void AnimationSystem::ResolveMotionAssets(Entity entity, const AnimationControllerAsset &controller_asset,
                                              AnimationController &controller)
    {
        controller.ClearBlendSpaces();
        auto &blend_space_assets = _blend_space_assets[entity];
        for (const auto &state : controller_asset.States())
        {
            if (state._motion._type != EAnimationMotionType::kBlendSpace || state._motion._asset.IsEmpty())
                continue;

            const Guid &asset_id = state._motion._asset;
            auto blend_space_iter = blend_space_assets.find(asset_id);
            if (blend_space_iter == blend_space_assets.end())
            {
                ResourceMgr &resource_mgr = ResourceMgr::Get();
                Ref<BlendSpaceAsset> blend_space = resource_mgr.GetRef<BlendSpaceAsset>(asset_id);
                if (blend_space == nullptr)
                    blend_space = resource_mgr.Load<BlendSpaceAsset>(asset_id);
                if (blend_space == nullptr)
                    continue;
                blend_space_iter = blend_space_assets.emplace(asset_id, std::move(blend_space)).first;
            }

            controller.BindBlendSpace(asset_id, blend_space_iter->second.get());
        }
    }

    void AnimationSystem::Update(Register &r, f32 delta_time)
    {
        PROFILE_BLOCK_CPU("AnimationSystem::Update")
        _skinning_system->Clear();
        _event_queue.Clear();
        const f32 dt = delta_time * TimeMgr::s_time_scale;

        for (const Entity entity : _entities)
        {
            if (!r.IsEntityEnabled(entity) || !r.IsComponentEnabled<AnimatorComponent>(entity))
                continue;

            AnimatorComponent *animator = r.GetComponent<AnimatorComponent>(entity);
            if (animator == nullptr || animator->_controller.IsEmpty())
                continue;

            CSkeletonMesh *skeleton_mesh = r.GetComponent<CSkeletonMesh>(entity);
            SpriteRendererComponent *sprite_renderer = r.GetComponent<SpriteRendererComponent>(entity);
            if (skeleton_mesh != nullptr && !r.IsComponentEnabled<CSkeletonMesh>(entity))
                skeleton_mesh = nullptr;
            if (sprite_renderer != nullptr && !r.IsComponentEnabled<SpriteRendererComponent>(entity))
                sprite_renderer = nullptr;

            Skeleton *skeleton = skeleton_mesh != nullptr && skeleton_mesh->_p_mesh != nullptr ?
                &skeleton_mesh->_p_mesh->GetSkeleton() : nullptr;
            SkeletonAnimationBinding *skeleton_binding = skeleton != nullptr ? &_skeleton_bindings[entity] : nullptr;
            SpriteAnimationBinding *sprite_binding = sprite_renderer != nullptr ? &_sprite_bindings[entity] : nullptr;

            if (_controller_ids[entity] != animator->_controller)
            {
                if (animator->_instance != kInvalidAnimationInstanceHandle)
                    _animation_instances.Destroy(animator->_instance);
                animator->_instance = kInvalidAnimationInstanceHandle;
                animator->_started = animator->_play_on_awake;
                _controller_ids[entity] = animator->_controller;
                _controller_assets[entity].reset();
                _controllers.erase(entity);
                _blend_space_assets.erase(entity);
            }

            if (_controller_assets[entity] == nullptr)
            {
                ResourceMgr::Get().Load<AnimationControllerAsset>(animator->_controller);
                _controller_assets[entity] = ResourceMgr::Get().GetRef<AnimationControllerAsset>(animator->_controller);
            }
            if (_controller_assets[entity] == nullptr)
                continue;

            const AnimationControllerAsset &controller_asset = *_controller_assets[entity];
            if (animator->_instance == kInvalidAnimationInstanceHandle)
                animator->_instance = _animation_instances.Create(controller_asset);
            AnimationInstance *instance = _animation_instances.Get(animator->_instance);
            if (instance == nullptr)
                continue;

            AnimationController &controller = _controllers[entity];
            controller.SetAsset(&controller_asset);
            ApplyCommands(entity, *instance, *animator, controller_asset);
            if (!animator->_started)
                continue;

            instance->_speed = std::max(animator->_speed, 0.0f);
            ResolveMotionAssets(entity, controller_asset, controller);

            const auto resolve_motion_duration = [&](u16 state_index) -> f32
            {
                if (state_index >= controller_asset.States().size())
                    return 0.0f;
                const auto &motion = controller_asset.States()[state_index]._motion;
                if (motion._type == EAnimationMotionType::kClip)
                {
                    const AnimationClip *clip = ResolveClip(entity, motion._asset, skeleton, sprite_binding, skeleton_binding);
                    return clip != nullptr ? clip->Duration() : 0.0f;
                }
                const auto blend_iter = _blend_space_assets[entity].find(motion._asset);
                if (blend_iter == _blend_space_assets[entity].end() || blend_iter->second == nullptr)
                    return 0.0f;
                for (const auto &sample : blend_iter->second->Samples())
                {
                    const AnimationClip *clip = ResolveClip(entity, sample._clip, skeleton, sprite_binding, skeleton_binding);
                    if (clip != nullptr)
                        return clip->Duration();
                }
                return 0.0f;
            };

            instance->_previous_state = instance->_current_state;
            instance->_previous_next_state = instance->_next_state;
            instance->_previous_state_time = instance->_state_time;
            instance->_previous_next_state_time = instance->_next_state_time;
            instance->_current_motion_duration = resolve_motion_duration(instance->_current_state);
            if (instance->_in_transition)
                instance->_next_motion_duration = resolve_motion_duration(instance->_next_state);

            controller.Update(*instance, dt);
            const AnimationEvaluation evaluation = controller.Evaluate(*instance);
            for (u8 sample_index = 0u; sample_index < evaluation._sample_count; ++sample_index)
                ResolveClip(entity, evaluation._samples[sample_index]._clip, skeleton, sprite_binding, skeleton_binding);

            const auto collect_state_events = [&](u16 state_index, f32 previous_time, f32 current_time,
                                                  bool allow_cosmetic)
            {
                if (state_index >= controller_asset.States().size())
                    return;
                const auto &state = controller_asset.States()[state_index];
                const f32 speed = state._speed * instance->_speed;
                if (state._motion._type == EAnimationMotionType::kClip)
                {
                    const AnimationClip *clip = ResolveClip(entity, state._motion._asset, skeleton, sprite_binding,
                                                            skeleton_binding);
                    if (clip == nullptr)
                        return;
                    _event_scratch.clear();
                    CollectAnimationEvents(*clip, previous_time * speed, current_time * speed, _event_scratch,
                                           state._loop);
                    for (const auto &event : _event_scratch)
                    {
                        if (event._kind == EAnimationEventKind::kGameplay || allow_cosmetic)
                            _event_queue.Push(AnimationEventMessage{entity, event._event_id, event._kind});
                    }
                    return;
                }

                const auto blend_iter = _blend_space_assets[entity].find(state._motion._asset);
                if (blend_iter == _blend_space_assets[entity].end() || blend_iter->second == nullptr)
                    return;
                f32 position = 0.0f;
                u16 parameter_index = state._motion._parameter_index;
                if (parameter_index == kInvalidAnimationParameter)
                {
                    for (u16 index = 0u; index < controller_asset.Parameters().size(); ++index)
                    {
                        if (controller_asset.Parameters()[index]._type == EAnimationParameterType::kFloat)
                        {
                            parameter_index = index;
                            break;
                        }
                    }
                }
                if (parameter_index < instance->_float_parameters.size())
                    position = instance->_float_parameters[parameter_index];
                AnimationEvaluation previous_evaluation;
                AnimationEvaluation current_evaluation;
                blend_iter->second->AddSamples(position, previous_time * speed, 1.0f, state._loop, previous_evaluation);
                blend_iter->second->AddSamples(position, current_time * speed, 1.0f, state._loop, current_evaluation);
                f32 dominant_weight = 0.0f;
                for (u8 sample_index = 0u; sample_index < current_evaluation._sample_count; ++sample_index)
                    dominant_weight = std::max(dominant_weight, current_evaluation._samples[sample_index]._weight);
                for (u8 sample_index = 0u; sample_index < current_evaluation._sample_count; ++sample_index)
                {
                    const AnimationSample &current_sample = current_evaluation._samples[sample_index];
                    const AnimationSample *previous_sample = nullptr;
                    for (u8 previous_index = 0u; previous_index < previous_evaluation._sample_count; ++previous_index)
                    {
                        if (previous_evaluation._samples[previous_index]._clip == current_sample._clip)
                        {
                            previous_sample = &previous_evaluation._samples[previous_index];
                            break;
                        }
                    }
                    const f32 previous_sample_time = previous_sample != nullptr ? previous_sample->_time : 0.0f;
                    const AnimationClip *clip = ResolveClip(entity, current_sample._clip, skeleton, sprite_binding,
                                                            skeleton_binding);
                    if (clip == nullptr)
                        continue;
                    _event_scratch.clear();
                    CollectAnimationEvents(*clip, previous_sample_time, current_sample._time, _event_scratch,
                                           state._loop);
                    const bool dominant = current_sample._weight >= dominant_weight;
                    for (const auto &event : _event_scratch)
                    {
                        if (event._kind == EAnimationEventKind::kGameplay || (allow_cosmetic && dominant))
                            _event_queue.Push(AnimationEventMessage{entity, event._event_id, event._kind});
                    }
                }
            };

            const u16 current_state = instance->_current_state;
            const f32 current_previous_time = instance->_previous_state == current_state ?
                instance->_previous_state_time : instance->_previous_next_state_time;
            const bool in_transition = instance->_in_transition && instance->_next_state != kInvalidAnimationState;
            const f32 transition_weight = in_transition && instance->_transition_duration > 0.0f ?
                std::clamp(instance->_transition_time / instance->_transition_duration, 0.0f, 1.0f) : 0.0f;
            collect_state_events(current_state, current_previous_time, instance->_state_time,
                                 !in_transition || transition_weight <= 0.5f);
            if (in_transition)
            {
                const f32 next_previous_time = instance->_previous_next_state == instance->_next_state ?
                    instance->_previous_next_state_time : 0.0f;
                collect_state_events(instance->_next_state, next_previous_time, instance->_next_state_time,
                                     transition_weight > 0.5f);
            }

            if (skeleton_binding != nullptr && skeleton != nullptr)
            {
                SkeletonPose &pose = _skeleton_poses[entity];
                if (pose.Size() != skeleton->JointNum())
                    pose = skeleton->GetBindPose();
                skeleton_binding->Evaluate(evaluation, *skeleton, pose);
                Vector<Matrix4x4f> &palette = _matrix_palettes[entity];
                pose.GetMatrixPalette(palette);
                for (auto &joint : *skeleton)
                    palette[joint._self] = joint._inv_bind_pos * palette[joint._self] * joint._node_inv_world_mat;
                _skinning_system->Submit(skeleton_mesh->_p_mesh.get(), palette, s_vertex_num_per_skin_task);
            }
            if (sprite_binding != nullptr)
                sprite_binding->Evaluate(evaluation, *sprite_renderer);
        }
    }

    void AnimationSystem::OnPushEntity(Entity entity)
    {
        _pending_commands[entity];
    }

    void AnimationSystem::WaitFor() const
    {
        _skinning_system->WaitFor();
    }
}
