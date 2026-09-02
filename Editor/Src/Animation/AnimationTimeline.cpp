#include "Animation/AnimationTimeline.h"

#include <algorithm>
#include <format>
#include <limits>

namespace Ailu::Editor
{
    namespace
    {
        constexpr u32 kInvalidSelectionId = std::numeric_limits<u32>::max();
    }
    AnimationTimeline::AnimationTimeline() : TimelineView()
    {
        Name("AnimationTimeline");
        _on_track_selected += [this](u32 track_index)
        {
            if (track_index < Tracks().size() && !Tracks()[track_index]._is_event_track &&
                Tracks()[track_index]._selection_id <= std::numeric_limits<u16>::max())
                _on_bone_selected_delegate.Invoke(static_cast<u16>(Tracks()[track_index]._selection_id));
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
    void AnimationTimeline::SetSelectedBone(u16 joint_index)
    {
        for (u32 track_index = 0u; track_index < Tracks().size(); ++track_index)
        {
            if (!Tracks()[track_index]._is_event_track && Tracks()[track_index]._selection_id == joint_index)
            {
                SetSelectedTrack(track_index);
                return;
            }
        }
        SetSelectedTrack(0u);
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
        event_track._selection_id = kInvalidSelectionId;
        for (const AnimationEvent &event : _clip->Events())
            event_track._markers.push_back(std::clamp(event._time, 0.0f, duration));
        tracks.push_back(std::move(event_track));

        for (u32 track_index = 0u; track_index < _clip->Size(); ++track_index)
        {
            const u16 joint_index = _clip->GetIdAtIndex(track_index);
            String joint_name = std::format("Joint {}", joint_index);
            if (_skeleton != nullptr && joint_index < _skeleton->JointNum())
                joint_name = (*_skeleton)[joint_index]._name;

            const TransformTrack &transform_track = _clip->GetTrackAtIndex(track_index);
            const auto add_transform_track = [&](const char *channel_name, const auto &channel_track,
                                                 const Color &marker_color)
            {
                TimelineTrack channel;
                channel._name = std::format("{} / {}", joint_name, channel_name);
                channel._marker_color = marker_color;
                channel._selection_id = joint_index;
                channel._markers.reserve(channel_track.Size());
                for (u32 frame_index = 0u; frame_index < channel_track.Size(); ++frame_index)
                    channel._markers.push_back(std::clamp(channel_track[frame_index]._time, 0.0f, duration));
                tracks.push_back(std::move(channel));
            };

            add_transform_track("Position", transform_track.GetPositionTrack(), Color(0.25f, 0.82f, 0.52f, 1.0f));
            add_transform_track("Rotation", transform_track.GetRotationTrack(), Color(0.98f, 0.72f, 0.25f, 1.0f));
            add_transform_track("Scale", transform_track.GetScaleTrack(), Color(0.72f, 0.48f, 0.98f, 1.0f));
        }
        SetTracks(tracks);
    }
}
