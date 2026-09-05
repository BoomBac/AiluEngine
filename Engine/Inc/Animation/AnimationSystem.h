#pragma once
#ifndef __ANIM_SYS_H__
#define __ANIM_SYS_H__

#include "Animation/AnimationController.h"
#include "Animation/AnimationEvent.h"
#include "Animation/AnimationInstance.h"
#include "Animation/BlendSpace.h"
#include "Animation/SkeletonAsset.h"
#include "Animation/Skeleton/SkeletonAnimationBinding.h"
#include "Animation/SkinningSystem.h"
#include "Animation/SpriteAnimationBinding.h"
#include "Scene/Entity.h"

namespace Ailu
{
    namespace ECS
    {
        struct AnimatorComponent;

        class AnimationSystem : public System
        {
            DECLARE_SYSTEM(AnimationSystem)

        public:
            inline static u32 s_vertex_num_per_skin_task = 2000u;

            void Update(Register &r, f32 delta_time) final;
            std::span<const AnimationEventMessage> Events() const { return _event_queue.Events(); }
            ESystemPhase GetPhase() const final { return ESystemPhase::kAnimation; }
            void OnPushEntity(Entity entity) final;
            Ref<System> Clone() final
            {
                auto copy = MakeRef<AnimationSystem>();
                copy->_entities = _entities;
                return copy;
            }
            void WaitFor() const final;

            void SetFloat(Entity entity, AnimationParameterId id, f32 value);
            void SetInt(Entity entity, AnimationParameterId id, i32 value);
            void SetBool(Entity entity, AnimationParameterId id, bool value);
            void SetTrigger(Entity entity, AnimationParameterId id);
            void ResetTrigger(Entity entity, AnimationParameterId id);
            void PlayState(Entity entity, u16 state_index);
            RootMotionDelta ConsumeRootMotion(Entity entity);
            void SetBindPoseValidationEnabled(bool enabled);
            void PrepareVisibleSkinning(const Render::CullResult &cull_results);
            void RecordSkinningRenderGraph(Render::RDG::RenderGraph &graph, Render::RenderingData &data);
            [[nodiscard]] const Vector<Render::RDG::RGHandle> &GetSkinningOutputHandles() const noexcept;

        private:
            enum class EParameterCommandType : u8
            {
                kFloat,
                kInt,
                kBool,
                kTrigger,
                kResetTrigger,
                kPlayState
            };

            struct ParameterCommand
            {
                EParameterCommandType _type = EParameterCommandType::kFloat;
                AnimationParameterId _parameter = kInvalidAnimationParameter;
                f32 _float_value = 0.0f;
                i32 _int_value = 0;
                bool _bool_value = false;
                u16 _state = kInvalidAnimationState;
            };

            void QueueCommand(Entity entity, ParameterCommand command);
            void ApplyCommands(Entity entity, AnimationInstance &instance, AnimatorComponent &animator,
                               const AnimationControllerAsset &controller_asset);
            const AnimationClip *ResolveClip(const Guid &clip_id);
            void ResolveMotionAssets(Entity entity, const AnimationControllerAsset &controller_asset,
                                     AnimationController &controller);

            struct SkeletonRuntimeGroup
            {
                Ref<SkeletonAsset> _skeleton_asset;
                SkeletonAnimationBinding _binding;
                SkeletonPose _pose;
                Vector<Matrix4x4f> _global_pose_palette;
                Vector<Entity> _consumers;
            };

            struct AnimatorRuntime
            {
                AnimationEvaluation _evaluation;
                RootMotionDelta _root_motion;
                Map<const SkeletonAsset *, SkeletonRuntimeGroup> _skeleton_groups;
            };

            struct FailedDirectClip
            {
                Guid _guid = Guid::EmptyGuid();
                WString _asset_path;
                bool _file_exists = false;
            };

            Map<Entity, AnimatorRuntime> _animator_runtimes;
            Map<Entity, Vector<Matrix4x4f>> _matrix_palettes;
            Map<Entity, PoseHandle> _pose_handles;
            Map<Entity, SpriteAnimationBinding> _sprite_bindings;
            Map<Entity, AnimationController> _controllers;
            Map<Entity, Guid> _controller_ids;
            Map<Entity, Ref<AnimationControllerAsset>> _controller_assets;
            Map<Entity, FailedDirectClip> _failed_direct_clips;
            Map<Entity, Map<Guid, Ref<BlendSpaceAsset>>> _blend_space_assets;
            Map<Entity, Vector<ParameterCommand>> _pending_commands;
            AnimationInstancePool _animation_instances;
            Scope<SkinningSystem> _skinning_system = MakeScope<SkinningSystem>();
            AnimationEventQueue _event_queue;
            Vector<AnimationEvent> _event_scratch;
        };
    }
}

#endif // __ANIM_SYS_H__
