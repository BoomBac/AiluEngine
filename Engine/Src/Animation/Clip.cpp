#include "Animation/Clip.h"
#include "Animation/Skeleton.h"
#include "pch.h"

namespace Ailu
{
    AnimationClip::AnimationClip() : Object("animation_clip"), _frame_count(0), _duration(0.f), _frame_rate(30u), _frame_duration(0.f), _start_time(0.f), _end_time(0.f), _is_looping(true)
    {
    }
    void AnimationClip::CopyFrom(const AnimationClip &source)
    {
        Name(source.Name());
        _start_time = source._start_time;
        _end_time = source._end_time;
        _frame_count = source._frame_count;
        _duration = source._duration;
        _frame_rate = source._frame_rate;
        _frame_duration = source._frame_duration;
        _is_looping = source._is_looping;
        _tracks = source._tracks;
        _sprite_track = source._sprite_track;
        _events = source._events;
        _skeleton_guid = source._skeleton_guid;
        _preview_mesh_guid = source._preview_mesh_guid;
    }

    bool AnimationClip::RemapToSkeleton(const Skeleton &source, const Skeleton &target)
    {
        if (source.JointNum() == 0u || target.JointNum() == 0u)
            return false;

        Vector<TransformTrack> remapped_tracks;
        remapped_tracks.reserve(_tracks.size());
        for (const TransformTrack &track : _tracks)
        {
            const u16 source_index = track.GetId();
            if (source_index >= source.JointNum())
                return false;

            const i32 target_index = Skeleton::GetJointIndexByName(target, source[source_index]._name);
            if (target_index < 0)
                continue;

            TransformTrack remapped_track = track;
            remapped_track.SetId(static_cast<u16>(target_index));
            remapped_tracks.emplace_back(std::move(remapped_track));
        }
        _tracks = std::move(remapped_tracks);
        return true;
    }
    u16 AnimationClip::GetIdAtIndex(u32 index) const
    {
        return _tracks[index].GetId();
    }
    void AnimationClip::SetIdAtIndex(u32 index, u32 id)
    {
        _tracks[index].SetId(id);
    }
    u32 AnimationClip::Size() const
    {
        return (u32) _tracks.size();
    }
    void AnimationClip::AddEvent(AnimationEvent event)
    {
        const auto it = std::lower_bound(_events.begin(), _events.end(), event._time,
                                         [](const AnimationEvent &item, f32 time) { return item._time < time; });
        _events.insert(it, std::move(event));
    }

    TransformTrack &AnimationClip::operator[](u16 joint)
    {
        for (u64 i = 0, s = _tracks.size(); i < s; ++i)
        {
            if (_tracks[i].GetId() == joint)
            {
                return _tracks[i];
            }
        }
        _tracks.emplace_back();
        _tracks[_tracks.size() - 1].SetId(joint);
        return _tracks[_tracks.size() - 1];
    }
    void AnimationClip::RecalculateDuration()
    {
        _start_time = 0.0f;
        _end_time = 0.0f;
        bool startSet = false;
        bool endSet = false;
        u32 tracksSize = (u32)_tracks.size();
        for (unsigned int i = 0; i < tracksSize; ++i)
        {
            if (_tracks[i].IsValid())
            {
                float startTime = _tracks[i].GetStartTime();
                float endTime = _tracks[i].GetEndTime();
                if (startTime < _start_time || !startSet)
                {
                    _start_time = startTime;
                    startSet = true;
                }
                if (endTime > _end_time || !endSet)
                {
                    _end_time = endTime;
                    endSet = true;
                }
            }
        }
        _duration = _end_time - _start_time;
    }
    void AnimationClipLibrary::AddClip(const String &name, Ref<AnimationClip> clip)
    {
        s_clips.insert(std::make_pair(name, clip));
    }
    void AnimationClipLibrary::AddClip(Ref<AnimationClip> clip)
    {
        s_clips.insert(std::make_pair(clip->Name(), clip));
    }
    Ref<AnimationClip> AnimationClipLibrary::GetClip(const String &name)
    {
        return s_clips.contains(name) ? s_clips[name] : nullptr;
    }
}// namespace Ailu
