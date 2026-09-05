#include "Animation/Skeleton/SkeletonAnimationBinding.h"

#include <algorithm>

namespace Ailu
{
    namespace
    {
        i32 FindRootBone(const Skeleton &skeleton, const RootMotionSettings &settings)
        {
            if (!settings._root_bone_name.empty())
            {
                const i32 index = Skeleton::GetJointIndexByName(skeleton, settings._root_bone_name);
                if (index >= 0)
                    return index;
            }
            if (settings._root_bone_index >= 0 && settings._root_bone_index < skeleton.JointNum())
                return settings._root_bone_index;
            for (const char *name : {"Root", "root", "Armature/root"})
            {
                const i32 index = Skeleton::GetJointIndexByName(skeleton, name);
                if (index >= 0)
                    return index;
            }
            for (const Joint &joint : skeleton)
            {
                if (joint._parent == Joint::kInvalidJointIndex)
                    return joint._self;
            }
            return -1;
        }
    }

    void SkeletonAnimationBinding::Resolve(const Guid &clip_id, const AnimationClip &clip, const Skeleton &skeleton)
    {
        EnsureSkeleton(skeleton);

        for (auto &binding : _clips)
        {
            if (binding._clip_id == clip_id)
            {
                binding._clip = &clip;
                return;
            }
        }
        _clips.push_back(ClipBinding{clip_id, &clip});
    }

    void SkeletonAnimationBinding::Resolve(const AnimationClip &clip, const Skeleton &skeleton)
    {
        Resolve(Guid::EmptyGuid(), clip, skeleton);
    }

    const AnimationClip *SkeletonAnimationBinding::FindClip(const Guid &clip_id) const
    {
        const ClipBinding *binding = FindBinding(clip_id);
        return binding != nullptr ? binding->_clip : nullptr;
    }

    void SkeletonAnimationBinding::Evaluate(const AnimationEvaluation &evaluation, const Skeleton &skeleton,
                                            SkeletonPose &out_pose)
    {
        out_pose = Evaluate(evaluation, skeleton)._pose;
    }

    AnimationEvaluateResult SkeletonAnimationBinding::Evaluate(const AnimationEvaluation &evaluation,
                                                               const Skeleton &skeleton)
    {
        AnimationEvaluateResult result;
        if (_joint_count != skeleton.JointNum())
            EnsureSkeleton(skeleton);
        result._pose = skeleton.GetBindPose();

        f32 accumulated_weight = 0.0f;
        for (u8 sample_index = 0u; sample_index < evaluation._sample_count; ++sample_index)
        {
            const auto &sample = evaluation._samples[sample_index];
            const ClipBinding *binding = FindBinding(sample._clip);
            if (binding == nullptr || binding->_clip == nullptr || sample._weight <= 0.0f)
                continue;

            SkeletonPose &sample_pose = _sample_poses[sample_index];
            sample_pose = skeleton.GetBindPose();
            const f32 current_time = sample._normalized_time ? sample._time * binding->_clip->Duration() : sample._time;
            const f32 previous_time = sample._normalized_time ? sample._previous_time * binding->_clip->Duration() :
                                                                  sample._previous_time;
            const RootMotionDelta sample_root_motion = SampleClip(
                *binding, skeleton, current_time, previous_time, sample._has_previous_time, sample._loop,
                sample._normalized_time, evaluation._root_motion_mode, sample_pose);

            const f32 next_weight = accumulated_weight + sample._weight;
            const f32 blend_weight = next_weight > 0.0f ? sample._weight / next_weight : 0.0f;
            if (accumulated_weight <= 0.0f)
            {
                result._pose = sample_pose;
                result._root_motion = sample_root_motion;
            }
            else
            {
                Pose::Blend(result._pose, result._pose, sample_pose, blend_weight, Joint::kInvalidJointIndex);
                result._root_motion._translation = result._root_motion._translation * (1.0f - blend_weight) +
                                                   sample_root_motion._translation * blend_weight;
                Quaternion target_rotation = sample_root_motion._rotation;
                if (Quaternion::Dot(result._root_motion._rotation, target_rotation) < 0.0f)
                    target_rotation = Quaternion(-target_rotation.x, -target_rotation.y, -target_rotation.z,
                                                 -target_rotation.w);
                result._root_motion._rotation = Quaternion::NormalizedQ(
                    result._root_motion._rotation * (1.0f - blend_weight) + target_rotation * blend_weight);
            }
            accumulated_weight = next_weight;
        }
        return result;
    }

    void SkeletonAnimationBinding::Clear()
    {
        _clips.clear();
        _joint_count = 0u;
        _skeleton = nullptr;
    }

    const SkeletonAnimationBinding::ClipBinding *SkeletonAnimationBinding::FindBinding(const Guid &clip_id) const
    {
        for (const auto &binding : _clips)
        {
            if (binding._clip_id == clip_id)
                return &binding;
        }
        return nullptr;
    }

    void SkeletonAnimationBinding::EnsureSkeleton(const Skeleton &skeleton)
    {
        if (_skeleton == &skeleton && _joint_count == skeleton.JointNum())
            return;
        _skeleton = &skeleton;
        _joint_count = skeleton.JointNum();
        for (auto &pose : _sample_poses)
            pose = skeleton.GetBindPose();
    }

    RootMotionDelta SkeletonAnimationBinding::SampleClip(const ClipBinding &binding, const Skeleton &skeleton,
                                                        f32 time, f32 previous_time, bool has_previous_time,
                                                        bool loop, bool normalized_time, ERootMotionMode mode,
                                                        SkeletonPose &out_pose) const
    {
        const AnimationClip &clip = *binding._clip;
        for (u32 track_index = 0u; track_index < clip.Size(); ++track_index)
        {
            const u16 joint_index = clip.GetIdAtIndex(track_index);
            if (joint_index >= out_pose.Size())
                continue;
            const TransformTrack &track = clip.GetTrackAtIndex(track_index);
            const Transform local = out_pose.GetLocalTransform(joint_index);
            out_pose.SetLocalTransform(joint_index, track.Evaluate(local, time, loop));
        }
        RootMotionDelta root_motion;
        const bool in_place = mode == ERootMotionMode::kInPlace;
        if (mode == ERootMotionMode::kDisabled || (!clip.GetRootMotionSettings()._enabled && !in_place))
            return root_motion;

        RootMotionSettings settings = clip.GetRootMotionSettings();
        settings._root_bone_index = FindRootBone(skeleton, settings);
        if (settings._root_bone_index < 0 || settings._root_bone_index >= skeleton.JointNum())
            return root_motion;

        if (has_previous_time && settings._enabled)
            root_motion = clip.ExtractRootMotion(previous_time, time, settings, loop);
        const bool lock_root = mode == ERootMotionMode::kApply || in_place;
        if (!lock_root)
            return root_motion;
        const Transform current = out_pose.GetLocalTransform(settings._root_bone_index);
        const Transform first = clip.SampleRootTransform(
            settings._root_bone_index, skeleton.GetBindPose().GetLocalTransform(settings._root_bone_index),
            clip.GetStartTime(), false);
        Transform locked = current;
        switch (settings._translation_mode)
        {
        case ERootMotionTranslationMode::kNone:
            break;
        case ERootMotionTranslationMode::kXZ:
            locked._position.x = first._position.x;
            locked._position.z = first._position.z;
            break;
        case ERootMotionTranslationMode::kXYZ:
            locked._position = first._position;
            break;
        }
        switch (settings._rotation_mode)
        {
        case ERootMotionRotationMode::kNone:
            break;
        case ERootMotionRotationMode::kYaw:
        {
            const Vector3f axis = Vector3f::kUp;
            const auto twist = [](const Quaternion &rotation, const Vector3f &up)
            {
                const Quaternion normalized = Quaternion::NormalizedQ(rotation);
                const Vector3f projected = up * DotProduct(Vector3f(normalized.x, normalized.y, normalized.z), up);
                const Quaternion result(projected.x, projected.y, projected.z, normalized.w);
                return Quaternion::LenSq(result) > Math::kFloatEpsilon ? Quaternion::NormalizedQ(result) :
                                                                         Quaternion::Identity();
            };
            const Quaternion current_yaw = twist(current._rotation, axis);
            const Quaternion first_yaw = twist(first._rotation, axis);
            // Quaternion::operator* evaluates as right * left in expression order.
            // Spell these in reverse so the mathematical order remains
            // inverse(current_yaw) * current and first_yaw * swing.
            const Quaternion swing = current._rotation * Quaternion::Inverse(current_yaw);
            locked._rotation = swing * first_yaw;
            break;
        }
        case ERootMotionRotationMode::kFull:
            locked._rotation = first._rotation;
            break;
        }
        out_pose.SetLocalTransform(settings._root_bone_index, locked);
        return mode == ERootMotionMode::kApply ? root_motion : RootMotionDelta{};
    }
}
