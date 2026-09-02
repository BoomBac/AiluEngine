#include "Animation/AnimationSystem.h"

#include "Framework/Common/FileManager.h"
#include "Framework/Common/Profiler.h"
#include "Framework/Common/ResourceMgr.h"
#include "Scene/Component.h"
#include "pch.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace Ailu::ECS
{
    namespace
    {
        constexpr f32 kBindPoseMatrixTolerance = 1e-4f;
        bool s_validate_bind_pose = false;
        std::set<Entity> s_validated_bind_pose_entities;

        f32 GetMatrixIdentityError(const Matrix4x4f &matrix)
        {
            f32 max_error = 0.0f;
            for (u32 row = 0u; row < 4u; ++row)
            {
                for (u32 column = 0u; column < 4u; ++column)
                {
                    const f32 expected = row == column ? 1.0f : 0.0f;
                    max_error = std::max(max_error, std::abs(matrix[row][column] - expected));
                }
            }
            return max_error;
        }
    }

    void AnimationSystem::SetBindPoseValidationEnabled(bool enabled)
    {
        s_validate_bind_pose = enabled;
        s_validated_bind_pose_entities.clear();
    }

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

    const AnimationClip *AnimationSystem::ResolveClip(const Guid &clip_id)
    {
        if (clip_id.IsEmpty())
            return nullptr;

        ResourceMgr &resource_mgr = ResourceMgr::Get();
        Ref<AnimationClip> loaded_clip = resource_mgr.GetRef<AnimationClip>(clip_id);
        if (loaded_clip == nullptr)
        {
            resource_mgr.Load<AnimationClip>(clip_id);
            loaded_clip = resource_mgr.GetRef<AnimationClip>(clip_id);
        }
        return loaded_clip.get();
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
        if (_skinning_system == nullptr)
            _skinning_system = MakeScope<SkinningSystem>();
        _skinning_system->Clear();
        _event_queue.Clear();
        const f32 dt = delta_time * TimeMgr::s_time_scale;

        for (const Entity entity : _entities)
        {
            if (!r.IsEntityEnabled(entity) || !r.IsComponentEnabled<AnimatorComponent>(entity))
                continue;

            AnimatorComponent *animator = r.GetComponent<AnimatorComponent>(entity);
            if (animator == nullptr)
                continue;

            const bool use_direct_clip = animator->_controller.IsEmpty();
            const Guid source_id = use_direct_clip ? animator->_clip : animator->_controller;
            AnimatorRuntime &runtime = _animator_runtimes[entity];
            if (source_id.IsEmpty())
            {
                if (_controller_ids[entity] != source_id)
                {
                    if (animator->_instance != kInvalidAnimationInstanceHandle)
                        _animation_instances.Destroy(animator->_instance);
                    animator->_instance = kInvalidAnimationInstanceHandle;
                    _controller_ids[entity] = source_id;
                    _controller_assets.erase(entity);
                    _controllers.erase(entity);
                    _failed_direct_clips.erase(entity);
                    _blend_space_assets.erase(entity);
                    _sprite_bindings.erase(entity);
                    _animator_runtimes.erase(entity);
                    _matrix_palettes.erase(entity);
                }
                continue;
            }

            if (_controller_ids[entity] != source_id)
            {
                if (animator->_instance != kInvalidAnimationInstanceHandle)
                    _animation_instances.Destroy(animator->_instance);
                animator->_instance = kInvalidAnimationInstanceHandle;
                animator->_started = animator->_play_on_awake;
                _controller_ids[entity] = source_id;
                _controller_assets.erase(entity);
                _controllers.erase(entity);
                _failed_direct_clips.erase(entity);
                _blend_space_assets.erase(entity);
            }

            if (_controller_assets[entity] == nullptr)
            {
                if (use_direct_clip)
                {
                    ResourceMgr &resource_mgr = ResourceMgr::Get();
                    const WString asset_path = resource_mgr.GuidToAssetPath(source_id);
                    const WString system_path = asset_path.empty() ? WString{} : ResourceMgr::GetResSysPath(asset_path);
                    const bool file_exists = !system_path.empty() && FileManager::Exist(system_path);
                    auto failed_iter = _failed_direct_clips.find(entity);
                    if (failed_iter != _failed_direct_clips.end() && failed_iter->second._guid == source_id)
                    {
                        const bool asset_path_changed = asset_path != failed_iter->second._asset_path;
                        const bool file_restored = file_exists && !failed_iter->second._file_exists;
                        if (!asset_path_changed && !file_restored)
                            continue;
                        _failed_direct_clips.erase(failed_iter);
                    }

                    const Ref<AnimationClip> clip = resource_mgr.Load<AnimationClip>(source_id);
                    if (clip != nullptr)
                    {
                        auto direct_controller = MakeRef<AnimationControllerAsset>("DirectClipController");
                        AnimationState state;
                        state._motion._asset = source_id;
                        state._loop = clip->IsLooping();
                        direct_controller->AddState(std::move(state));
                        _controller_assets[entity] = std::move(direct_controller);
                    }
                    else
                    {
                        _failed_direct_clips[entity] = FailedDirectClip{source_id, asset_path, file_exists};
                        if (asset_path.empty())
                        {
                            LOG_ERROR("AnimationSystem: direct clip GUID {} is not registered in the asset database",
                                      source_id.ToString());
                        }
                        else
                        {
                            LOG_ERROR("AnimationSystem: failed to load direct clip {} from {}", source_id.ToString(),
                                      ToChar(system_path));
                        }
                    }
                }
                else
                {
                    ResourceMgr::Get().Load<AnimationControllerAsset>(source_id);
                    _controller_assets[entity] = ResourceMgr::Get().GetRef<AnimationControllerAsset>(source_id);
                }
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
                    const AnimationClip *clip = ResolveClip(motion._asset);
                    return clip != nullptr ? clip->Duration() : 0.0f;
                }
                const auto blend_iter = _blend_space_assets[entity].find(motion._asset);
                if (blend_iter == _blend_space_assets[entity].end() || blend_iter->second == nullptr)
                    return 0.0f;
                for (const auto &sample : blend_iter->second->Samples())
                {
                    const AnimationClip *clip = ResolveClip(sample._clip);
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
            runtime._evaluation = controller.Evaluate(*instance);
            for (u8 sample_index = 0u; sample_index < runtime._evaluation._sample_count; ++sample_index)
                ResolveClip(runtime._evaluation._samples[sample_index]._clip);

            const auto collect_state_events = [&](u16 state_index, f32 previous_time, f32 current_time,
                                                  bool allow_cosmetic)
            {
                if (state_index >= controller_asset.States().size())
                    return;
                const auto &state = controller_asset.States()[state_index];
                const f32 speed = state._speed * instance->_speed;
                if (state._motion._type == EAnimationMotionType::kClip)
                {
                    const AnimationClip *clip = ResolveClip(state._motion._asset);
                    if (clip == nullptr)
                        return;
                    _event_scratch.clear();
                    CollectAnimationEvents(*clip, previous_time * speed, current_time * speed, _event_scratch,
                                           state._loop);
                    for (const auto &event : _event_scratch)
                        if (event._kind == EAnimationEventKind::kGameplay || allow_cosmetic)
                            _event_queue.Push(AnimationEventMessage{entity, event._event_id, event._kind});
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
                    const AnimationClip *clip = ResolveClip(current_sample._clip);
                    if (clip == nullptr)
                        continue;
                    _event_scratch.clear();
                    CollectAnimationEvents(*clip, previous_sample != nullptr ? previous_sample->_time : 0.0f,
                                           current_sample._time, _event_scratch, state._loop);
                    const bool dominant = current_sample._weight >= dominant_weight;
                    for (const auto &event : _event_scratch)
                        if (event._kind == EAnimationEventKind::kGameplay || (allow_cosmetic && dominant))
                            _event_queue.Push(AnimationEventMessage{entity, event._event_id, event._kind});
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

            for (auto &[skeleton_key, group] : runtime._skeleton_groups)
                group._consumers.clear();
            const auto collect_skeleton_meshes = [&](auto &&self, Entity current) -> void
            {
                if (current != entity)
                {
                    if (r.GetComponent<AnimatorComponent>(current) != nullptr)
                        return;
                    if (r.GetComponent<CSkeletonMesh>(current) != nullptr)
                    {
                        auto *component = r.GetComponent<CSkeletonMesh>(current);
                        if (r.IsComponentEnabled<CSkeletonMesh>(current) && component->_p_mesh != nullptr)
                        {
                            const AssetRef<SkeletonAsset> &skeleton_ref = component->_p_mesh->GetSkeletonAsset();
                            const Ref<SkeletonAsset> &skeleton_asset = skeleton_ref.Get();
                            if (skeleton_ref.IsResolved())
                            {
                                auto [group_iter, inserted] = runtime._skeleton_groups.try_emplace(skeleton_asset.get());
                                group_iter->second._skeleton_asset = skeleton_asset;
                                group_iter->second._consumers.emplace_back(current);
                            }
                        }
                    }
                }
                else if (const auto *component = r.GetComponent<CSkeletonMesh>(current);
                         component != nullptr && r.IsComponentEnabled<CSkeletonMesh>(current) &&
                         component->_p_mesh != nullptr)
                {
                    const AssetRef<SkeletonAsset> &skeleton_ref = component->_p_mesh->GetSkeletonAsset();
                    const Ref<SkeletonAsset> &skeleton_asset = skeleton_ref.Get();
                    if (skeleton_ref.IsResolved())
                    {
                        auto [group_iter, inserted] = runtime._skeleton_groups.try_emplace(skeleton_asset.get());
                        group_iter->second._skeleton_asset = skeleton_asset;
                        group_iter->second._consumers.emplace_back(current);
                    }
                }

                const CHierarchy *hierarchy = r.GetComponent<CHierarchy>(current);
                if (hierarchy == nullptr)
                    return;
                for (Entity child = hierarchy->_first_child; child != kInvalidEntity;)
                {
                    const CHierarchy *child_hierarchy = r.GetComponent<CHierarchy>(child);
                    const Entity next = child_hierarchy != nullptr ? child_hierarchy->_next_sibling : kInvalidEntity;
                    self(self, child);
                    child = next;
                }
            };
            collect_skeleton_meshes(collect_skeleton_meshes, entity);

            for (auto &[skeleton_key, group] : runtime._skeleton_groups)
            {
                if (group._consumers.empty() || group._skeleton_asset == nullptr)
                    continue;
                const Skeleton &skeleton = group._skeleton_asset->GetSkeleton();
                for (u8 sample_index = 0u; sample_index < runtime._evaluation._sample_count; ++sample_index)
                {
                    const Guid &clip_id = runtime._evaluation._samples[sample_index]._clip;
                    if (group._binding.FindClip(clip_id) == nullptr)
                    {
                        const AnimationClip *clip = ResolveClip(clip_id);
                        if (clip != nullptr)
                            group._binding.Resolve(clip_id, *clip, skeleton);
                    }
                }
                if (group._pose.Size() != skeleton.JointNum())
                    group._pose = skeleton.GetBindPose();
                group._binding.Evaluate(runtime._evaluation, skeleton, group._pose);
                group._pose.GetMatrixPalette(group._global_pose_palette);

                for (const Entity mesh_entity : group._consumers)
                {
                    CSkeletonMesh *component = r.GetComponent<CSkeletonMesh>(mesh_entity);
                    if (component == nullptr || component->_p_mesh == nullptr)
                        continue;
                    const bool validate_bind_pose = s_validate_bind_pose && !s_validated_bind_pose_entities.contains(mesh_entity);
                    Vector<Matrix4x4f> &palette = _matrix_palettes[mesh_entity];
                    component->_p_mesh->BuildSkinMatrixPalette(
                        std::span<const Matrix4x4f>(group._global_pose_palette.data(), group._global_pose_palette.size()),
                        palette);
                    if (validate_bind_pose)
                    {
                        f32 max_error = 0.0f;
                        Vector<Matrix4x4f> bind_palette;
                        skeleton.GetBindPose().GetMatrixPalette(bind_palette);
                        const Matrix4x4f &mesh_bind_global = component->_p_mesh->GetMeshBindGlobalTransform();
                        const Matrix4x4f &mesh_current_global_inv =
                            component->_p_mesh->GetMeshCurrentGlobalInverseTransform();
                        for (const Joint &joint : skeleton)
                            max_error = std::max(max_error, GetMatrixIdentityError(mesh_bind_global * joint._inv_bind_pos *
                                                                                    bind_palette[joint._self] * mesh_current_global_inv));
                        if (max_error < kBindPoseMatrixTolerance)
                        {
                            LOG_INFO("Skinning bind pose validation passed for entity {} (max error {})", mesh_entity, max_error);
                        }
                        else
                        {
                            LOG_ERROR("Skinning bind pose validation failed for entity {} (max error {})", mesh_entity, max_error);
                        }
                        s_validated_bind_pose_entities.emplace(mesh_entity);
                    }
                    _skinning_system->Submit(component->_p_mesh.get(), palette, s_vertex_num_per_skin_task);
                }
            }

            if (SpriteRendererComponent *sprite_renderer = r.GetComponent<SpriteRendererComponent>(entity);
                sprite_renderer != nullptr && r.IsComponentEnabled<SpriteRendererComponent>(entity))
            {
                SpriteAnimationBinding &sprite_binding = _sprite_bindings[entity];
                for (u8 sample_index = 0u; sample_index < runtime._evaluation._sample_count; ++sample_index)
                {
                    const Guid &clip_id = runtime._evaluation._samples[sample_index]._clip;
                    const AnimationClip *clip = ResolveClip(clip_id);
                    if (clip != nullptr && sprite_binding.FindClip(clip_id) == nullptr)
                        sprite_binding.Resolve(clip_id, *clip);
                }
                sprite_binding.Evaluate(runtime._evaluation, *sprite_renderer);
            }
        }

        for (auto runtime_iter = _animator_runtimes.begin(); runtime_iter != _animator_runtimes.end();)
        {
            if (!_entities.contains(runtime_iter->first))
                runtime_iter = _animator_runtimes.erase(runtime_iter);
            else
                ++runtime_iter;
        }
        for (auto failed_iter = _failed_direct_clips.begin(); failed_iter != _failed_direct_clips.end();)
        {
            if (!_entities.contains(failed_iter->first))
                failed_iter = _failed_direct_clips.erase(failed_iter);
            else
                ++failed_iter;
        }
    }


    void AnimationSystem::OnPushEntity(Entity entity)
    {
        _pending_commands[entity];
    }

    void AnimationSystem::WaitFor() const
    {
        if (_skinning_system != nullptr)
            _skinning_system->WaitFor();
    }
}
