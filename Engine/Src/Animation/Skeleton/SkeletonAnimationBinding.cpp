#include "Animation/Skeleton/SkeletonAnimationBinding.h"

#include <algorithm>

namespace Ailu
{
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
        if (_joint_count != skeleton.JointNum())
            EnsureSkeleton(skeleton);
        out_pose = skeleton.GetBindPose();

        f32 accumulated_weight = 0.0f;
        for (u8 sample_index = 0u; sample_index < evaluation._sample_count; ++sample_index)
        {
            const auto &sample = evaluation._samples[sample_index];
            const ClipBinding *binding = FindBinding(sample._clip);
            if (binding == nullptr || binding->_clip == nullptr || sample._weight <= 0.0f)
                continue;

            SkeletonPose &sample_pose = _sample_poses[sample_index];
            sample_pose = skeleton.GetBindPose();
            SampleClip(*binding, sample._time, sample._loop, sample._normalized_time, sample_pose);

            const f32 next_weight = accumulated_weight + sample._weight;
            const f32 blend_weight = next_weight > 0.0f ? sample._weight / next_weight : 0.0f;
            if (accumulated_weight <= 0.0f)
                out_pose = sample_pose;
            else
                Pose::Blend(out_pose, out_pose, sample_pose, blend_weight, Joint::kInvalidJointIndex);
            accumulated_weight = next_weight;
        }
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

    void SkeletonAnimationBinding::SampleClip(const ClipBinding &binding, f32 time, bool loop, bool normalized_time,
                                              SkeletonPose &out_pose) const
    {
        const AnimationClip &clip = *binding._clip;
        if (normalized_time)
            time *= clip.Duration();
        for (u32 track_index = 0u; track_index < clip.Size(); ++track_index)
        {
            const u16 joint_index = clip.GetIdAtIndex(track_index);
            if (joint_index >= out_pose.Size())
                continue;
            const TransformTrack &track = clip.GetTrackAtIndex(track_index);
            const Transform local = out_pose.GetLocalTransform(joint_index);
            out_pose.SetLocalTransform(joint_index, track.Evaluate(local, time, loop));
        }
    }
}
