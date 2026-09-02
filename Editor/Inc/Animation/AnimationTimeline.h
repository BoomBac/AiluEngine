#pragma once

#include "Animation/Clip.h"
#include "Animation/Skeleton.h"
#include "Timeline/TimelineView.h"

namespace Ailu::Editor
{
    class AnimationTimeline final : public TimelineView
    {
        DECLARE_DELEGATE(on_bone_selected, u16);

    public:
        AnimationTimeline();

        void SetClip(AnimationClip *clip);
        void SetSkeleton(const Skeleton *skeleton);
        void SetSelectedBone(u16 joint_index);
        AnimationClip *GetClip() const { return _clip; }
        const Skeleton *GetSkeleton() const { return _skeleton; }

    private:
        void RefreshTracks();

        AnimationClip *_clip = nullptr;
        const Skeleton *_skeleton = nullptr;
    };
}
