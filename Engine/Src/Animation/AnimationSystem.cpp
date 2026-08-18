#include "Animation/AnimationSystem.h"
#include "Animation/AnimationController.h"
#include "Animation/CrossFade.h"
#include "Animation/Skeleton/SkeletonAnimationBinding.h"
#include "Animation/Solver.h"
#include "Framework/Common/Profiler.h"
#include "Framework/Common/ResourceMgr.h"
#include "Scene/Component.h"
#include "pch.h"
#include <Render/Gizmo.h>


namespace Ailu
{
    using namespace Render;
    namespace ECS
    {
        static Map<Entity, Pose> s_cur_pose;
        static Map<Entity, Vector<Matrix4x4f>> s_mat_palette;
        static Map<Entity, CrossFadeController> s_cross_fade_controller;
        static Map<Entity, SkeletonAnimationBinding> s_skeleton_bindings;
        static Map<Entity, SpriteAnimationBinding> s_sprite_bindings;
        static AnimationInstancePool s_animation_instances;
        static Map<Entity, AnimationController> s_controllers;
        static Map<Entity, Guid> s_controller_ids;
        static Map<Entity, Ref<AnimationControllerAsset>> s_controller_assets;
        void AnimationSystem::Update(Register &r, f32 delta_time)
        {
            PROFILE_BLOCK_CPU("AnimationSystem::Update")
            _skinning_system->Clear();
            _event_queue.Clear();
            for (auto e: _entities)
            {
                if (!r.IsEntityEnabled(e) || !r.IsComponentEnabled<AnimatorComponent>(e))
                    continue;
                AnimatorComponent *animator = r.GetComponent<AnimatorComponent>(e);
                CSkeletonMesh *c = r.GetComponent<CSkeletonMesh>(e);
                SpriteRendererComponent *sprite_renderer = r.GetComponent<SpriteRendererComponent>(e);
                if (c != nullptr && !r.IsComponentEnabled<CSkeletonMesh>(e))
                    c = nullptr;
                if (sprite_renderer != nullptr && !r.IsComponentEnabled<SpriteRendererComponent>(e))
                    sprite_renderer = nullptr;
                if (animator == nullptr || (c == nullptr && sprite_renderer == nullptr))
                    continue;
                if (c != nullptr && !c->_p_mesh)
                    c = nullptr;
                Skeleton *skeleton = c != nullptr ? &c->_p_mesh->GetSkeleton() : nullptr;
                if (skeleton != nullptr && !s_cur_pose.contains(e))
                {
                    s_cur_pose[e] = skeleton->GetBindPose();
                    s_mat_palette[e].resize(skeleton->JointNum());
                }
                if (!animator->_controller.IsEmpty())
                {
                    if (!s_controller_ids.contains(e) || s_controller_ids[e] != animator->_controller)
                    {
                        if (animator->_instance != kInvalidAnimationInstanceHandle)
                            s_animation_instances.Destroy(animator->_instance);
                        animator->_instance = kInvalidAnimationInstanceHandle;
                        s_controller_ids[e] = animator->_controller;
                        s_controller_assets[e].reset();
                    }
                    if (s_controller_assets[e] == nullptr)
                    {
                        ResourceMgr::Get().Load<AnimationControllerAsset>(animator->_controller);
                        s_controller_assets[e] = ResourceMgr::Get().GetRef<AnimationControllerAsset>(animator->_controller);
                    }
                    if (s_controller_assets[e] != nullptr)
                    {
                        const auto &controller_asset = *s_controller_assets[e];
                        if (animator->_instance == kInvalidAnimationInstanceHandle)
                            animator->_instance = s_animation_instances.Create(controller_asset);
                        auto *instance = s_animation_instances.Get(animator->_instance);
                        if (instance == nullptr)
                            continue;

                        auto &controller = s_controllers[e];
                        controller.SetAsset(s_controller_assets[e].get());
                        instance->_speed = std::max(animator->_speed, 0.0f);
                        auto *skeleton_binding = skeleton != nullptr ? &s_skeleton_bindings[e] : nullptr;
                        auto *sprite_binding = sprite_renderer != nullptr ? &s_sprite_bindings[e] : nullptr;
                        auto resolve_clip = [&](const Guid &clip_id) -> const AnimationClip *
                        {
                            if (clip_id.IsEmpty())
                                return nullptr;
                            const AnimationClip *cached_clip = skeleton_binding != nullptr ?
                                skeleton_binding->FindClip(clip_id) : nullptr;
                            if (cached_clip == nullptr && sprite_binding != nullptr)
                                cached_clip = sprite_binding->FindClip(clip_id);
                            if (cached_clip != nullptr)
                            {
                                if (skeleton_binding != nullptr && skeleton_binding->FindClip(clip_id) == nullptr)
                                    skeleton_binding->Resolve(clip_id, *cached_clip, *skeleton);
                                if (sprite_binding != nullptr && sprite_binding->FindClip(clip_id) == nullptr)
                                    sprite_binding->Resolve(clip_id, *cached_clip);
                                return cached_clip;
                            }
                            ResourceMgr::Get().Load<AnimationClip>(clip_id);
                            Ref<AnimationClip> clip = ResourceMgr::Get().GetRef<AnimationClip>(clip_id);
                            if (clip == nullptr)
                                return nullptr;
                            if (skeleton_binding != nullptr)
                                skeleton_binding->Resolve(clip_id, *clip, *skeleton);
                            if (sprite_binding != nullptr)
                                sprite_binding->Resolve(clip_id, *clip);
                            return clip.get();
                        };

                        if (instance->_current_state < controller_asset.States().size())
                        {
                            const auto &motion = controller_asset.States()[instance->_current_state]._motion;
                            if (const AnimationClip *clip = resolve_clip(motion._asset); clip != nullptr)
                                instance->_current_motion_duration = clip->Duration();
                        }
                        const f32 dt = delta_time * TimeMgr::s_time_scale;
                        instance->_previous_state = instance->_current_state;
                        instance->_previous_next_state = instance->_next_state;
                        instance->_previous_state_time = instance->_state_time;
                        instance->_previous_next_state_time = instance->_next_state_time;
                        controller.Update(*instance, dt);
                        const AnimationEvaluation evaluation = controller.Evaluate(*instance);
                        for (u8 sample_index = 0u; sample_index < evaluation._sample_count; ++sample_index)
                            resolve_clip(evaluation._samples[sample_index]._clip);

                        const auto collect_state_events = [&](u16 state_index, f32 previous_time, f32 current_time,
                                                              bool allow_cosmetic)
                        {
                            if (state_index >= controller_asset.States().size())
                                return;
                            const Guid clip_id = controller_asset.States()[state_index]._motion._asset;
                            const AnimationClip *clip = resolve_clip(clip_id);
                            if (clip == nullptr)
                                return;
                            _event_scratch.clear();
                            CollectAnimationEvents(*clip, previous_time, current_time, _event_scratch);
                            for (const auto &event : _event_scratch)
                            {
                                if (event._kind == EAnimationEventKind::kGameplay || allow_cosmetic)
                                    _event_queue.Push(AnimationEventMessage{e, event._event_id, event._kind});
                            }
                        };
                        const u16 current_state = instance->_current_state;
                        const f32 current_previous_time = instance->_previous_state == current_state ?
                            instance->_previous_state_time : instance->_previous_next_state_time;
                        const bool in_transition = instance->_in_transition &&
                            instance->_next_state != kInvalidAnimationState;
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
                        else if (instance->_previous_state != current_state &&
                                 instance->_previous_next_state == current_state)
                        {
                            collect_state_events(instance->_previous_state, instance->_previous_state_time,
                                                 instance->_previous_state_time + dt, false);
                        }
                        if (skeleton_binding != nullptr)
                        {
                            skeleton_binding->Evaluate(evaluation, *skeleton, s_cur_pose[e]);
                            s_cur_pose[e].GetMatrixPalette(s_mat_palette[e]);
                            for (auto &joint : *skeleton)
                                s_mat_palette[e][joint._self] = joint._inv_bind_pos * s_mat_palette[e][joint._self] * joint._node_inv_world_mat;
                            _skinning_system->Submit(c->_p_mesh.get(), s_mat_palette[e], s_vertex_num_per_skin_task);
                        }
                        if (sprite_binding != nullptr)
                            sprite_binding->Evaluate(evaluation, *sprite_renderer);
                        continue;
                    }
                }

                if (c == nullptr || !c->_anim_clip)
                    continue;
                auto &sk = *skeleton;
                if (!s_cross_fade_controller.contains(e))
                {
                    c->_anim_clip->IsLooping(true);
                    s_cross_fade_controller[e] = CrossFadeController(sk);
                    s_cross_fade_controller[e].Play(c->_anim_clip.get());
                    c->_blend_space = BlendSpace(&sk, Vector2f(0.f, 90.f), Vector2f(0.f, 90.f));
                    c->_blend_space.AddClip(c->_anim_clip.get(), 0.f, 0.f);
                }
                if (c->_anim_clip.get() != s_cross_fade_controller[e].GetcurrentClip())
                    s_cross_fade_controller[e].Play(c->_anim_clip.get());
                f32 dt = delta_time * TimeMgr::s_time_scale * std::max(c->_anim_speed, 0.0f);
                _anim_playtime[e] += dt;
                if (c->_blend_anim_clip)
                    s_cross_fade_controller[e].FadeTo(c->_blend_anim_clip.get(), 0.5f);
                f32 current_frame = c->_anim_time < 0.f ? _anim_playtime[e] : c->_anim_time;
                auto transf = r.GetComponent<TransformComponent>(e);
                Matrix4x4f sk_to_world = sk[0]._node_inv_world_mat * transf->GetWorldMatrix();
                if (true)
                {
                    bool _is_do_skin = true;
                    {
                        PROFILE_BLOCK_CPU("AnimClipBake")
                        bool use_crossfade = false;
                        if (use_crossfade)
                        {
                            s_cross_fade_controller[e].Update(dt);
                            auto &cur_pose = s_cross_fade_controller[e].GetCurrentPose();
                            cur_pose.GetMatrixPalette(s_mat_palette[e]);
                        }
                        else
                        {
                            if (c->_anim_type == 0)
                            {
                                auto &cur_pose = s_cur_pose[e];
                                auto &binding = s_skeleton_bindings[e];
                                binding.Resolve(Guid::EmptyGuid(), *c->_anim_clip, sk);
                                AnimationEvaluation evaluation;
                                AnimationSample sample;
                                sample._time = current_frame;
                                evaluation.AddSample(sample);
                                binding.Evaluate(evaluation, sk, cur_pose);
                                cur_pose.GetMatrixPalette(s_mat_palette[e]);
                            }
                            else if (c->_anim_type == 1)
                            {
                                c->_blend_space.Update(dt);
                                c->_blend_space.GetCurrentPose().GetMatrixPalette(s_mat_palette[e]);
                                _is_do_skin = !s_mat_palette[e].empty();
                            }
                            else {};
                            //auto &cur_solver = *sk.GetSolvers().begin()->second;
                            //cur_solver[0] = cur_pose.GetGlobalTransform(66);
                            //cur_solver[1] = cur_pose.GetLocalTransform(67);
                            //cur_solver[2] = cur_pose.GetLocalTransform(68);
                            //Transform t = Solver::s_global_goal;
                            //Gizmo::DrawLine(TransformCoord(sk_to_world, cur_solver.GetGlobalTransform(cur_solver.Size() - 1)._position), t._position, Colors::kRed);
                            //t._position = TransformCoord(world_to_sk, t._position);
                            //cur_solver.Solve(t);
                            //{
                            //    auto parent_transf = cur_pose[cur_pose.GetParent(66)];
                            //    auto to_local = Transform::Inverse(parent_transf);
                            //    cur_pose.SetLocalTransform(66, Transform::Combine(to_local, cur_solver[0]));
                            //    cur_pose.SetLocalTransform(67, cur_solver[1]);
                            //    cur_pose.SetLocalTransform(68, cur_solver[2]);
                            //}

                        }
                        if (_is_do_skin)
                        {
                            for (auto &it: sk)
                                s_mat_palette[e][it._self] = it._inv_bind_pos * s_mat_palette[e][it._self] * it._node_inv_world_mat;
                        }

                    }
                    if (_is_do_skin)
                    {
                        PROFILE_BLOCK_CPU("Skin")
                        _skinning_system->Submit(c->_p_mesh.get(), s_mat_palette[e], s_vertex_num_per_skin_task);
                    }
                }
                //debug
                auto &pose = s_cur_pose[e];
                for (u16 i = 0; i < pose.Size(); i++)
                {
                    u16 parent = pose.GetParent(i);
                    if (parent != Joint::kInvalidJointIndex)
                    {
                        Vector3f from = TransformCoord(sk_to_world, pose[i]._position);
                        Vector3f to = TransformCoord(sk_to_world, pose[parent]._position);
                        Gizmo::DrawLine(from, to, Random::RandomColor(i));
                    }
                }
            }
        }

        void AnimationSystem::OnPushEntity(Entity entity)
        {
            _anim_playtime[entity] = 0.f;
        }

        void AnimationSystem::WaitFor() const
        {
            _skinning_system->WaitFor();
        }

    }// namespace ECS
}// namespace Ailu
