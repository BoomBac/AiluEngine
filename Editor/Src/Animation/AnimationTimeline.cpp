#include "Animation/AnimationTimeline.h"

#include <algorithm>
#include <format>

namespace Ailu::Editor
{
    AnimationTimeline::AnimationTimeline() : TimelineView()
    {
        Name("AnimationTimeline");
        _on_track_selected += [this](u32 track_index)
        {
            if (track_index > 0u && track_index - 1u < (_clip != nullptr ? _clip->Size() : 0u))
                _on_bone_selected_delegate.Invoke(_clip->GetIdAtIndex(track_index - 1u));
            else
                _on_bone_selected_delegate.Invoke(Joint::kInvalidJointIndex);
        };
    }

    void AnimationTimeline::SetClip(AnimationClip *clip)
    {
        _clip = clip;
        RefreshTracks();
    }

    void AnimationTimeline::SetSkeleton(const Skeleton *skeleton)
    {
        _skeleton = skeleton;
        RefreshTracks();
    }

    void AnimationTimeline::RefreshTracks()
    {
        Vector<TimelineTrack> tracks;
        if (_clip == nullptr)
        {
            SetTracks(tracks);
            SetTimeRange(0.0f, 1.0f);
            SetSnapInterval(0.0f);
            return;
        }

        const f32 duration = std::max(_clip->Duration(), 0.001f);
        const f32 frame_rate = _clip->FrameRate() > 0.0f ? _clip->FrameRate() :
                               (_clip->FrameDuration() > 0.0f ? 1.0f / _clip->FrameDuration() : 30.0f);
        const f32 frame_interval = 1.0f / frame_rate;
        SetTimeRange(0.0f, duration);
        SetSnapInterval(frame_interval);

        TimelineTrack event_track;
        event_track._name = "Events";
        event_track._marker_color = Color(0.98f, 0.66f, 0.22f, 1.0f);
        event_track._is_event_track = true;
        for (const AnimationEvent &event : _clip->Events())
            event_track._markers.push_back(std::clamp(event._time, 0.0f, duration));
        tracks.push_back(std::move(event_track));

        for (u32 track_index = 0u; track_index < _clip->Size(); ++track_index)
        {
            const u16 joint_index = _clip->GetIdAtIndex(track_index);
            TimelineTrack track;
            track._name = std::format("Joint {}", joint_index);
            if (_skeleton != nullptr && joint_index < _skeleton->JointNum())
                track._name = (*_skeleton)[joint_index]._name;
            track._marker_color = Color(0.25f, 0.70f, 0.98f, 1.0f);
            const u32 frame_count = _clip->GetTrackAtIndex(track_index).GetPositionTrack().Size();
            for (u32 frame_index = 0u; frame_index < frame_count; ++frame_index)
                track._markers.push_back(std::min(frame_index * frame_interval, duration));
            tracks.push_back(std::move(track));
        }
        SetTracks(tracks);
    }
}
