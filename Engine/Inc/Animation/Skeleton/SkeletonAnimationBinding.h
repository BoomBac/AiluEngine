#pragma once
#ifndef __SKELETON_ANIMATION_BINDING_H__
#define __SKELETON_ANIMATION_BINDING_H__

#include "Animation/AnimationEvaluation.h"
#include "Animation/Clip.h"
#include "Animation/Skeleton/SkeletonPose.h"
#include "Animation/Skeleton.h"

namespace Ailu
{
    class AILU_API SkeletonAnimationBinding
    {
    public:
        void Resolve(const Guid &clip_id, const AnimationClip &clip, const Skeleton &skeleton);
        void Resolve(const AnimationClip &clip, const Skeleton &skeleton);

        const AnimationClip *FindClip(const Guid &clip_id) const;

        void Evaluate(const AnimationEvaluation &evaluation, const Skeleton &skeleton, SkeletonPose &out_pose);
        void Clear();

    private:
        struct ClipBinding
        {
            Guid _clip_id = Guid::EmptyGuid();
            const AnimationClip *_clip = nullptr;
        };

        const ClipBinding *FindBinding(const Guid &clip_id) const;
        void EnsureSkeleton(const Skeleton &skeleton);
        void SampleClip(const ClipBinding &binding, f32 time, SkeletonPose &out_pose) const;

        Vector<ClipBinding> _clips;
        Array<SkeletonPose, 4> _sample_poses;
        u32 _joint_count = 0u;
        const Skeleton *_skeleton = nullptr;
    };
}

#endif // __SKELETON_ANIMATION_BINDING_H__
