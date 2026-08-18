#include "Animation/Clip.h"
#include "pch.h"

namespace Ailu
{
    AnimationClip::AnimationClip() : Object("animation_clip"), _frame_count(0), _duration(0.f), _frame_rate(30u), _frame_duration(0.f), _start_time(0.f), _end_time(0.f), _is_looping(true)
    {
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
    f32 AnimationClip::Sample(Pose &pose, f32 time)
    {
        AL_ASSERT(pose.Size() != 0);
        if (_duration == 0.f)
            return 0.f;
        time = AdjustTimeToFitRange(time);
        u32 size = (u32)_tracks.size();
        for (u32 i = 0; i < size; i++)
        {
            u16 joint_index = _tracks[i].GetId();
            Transform local = pose.GetLocalTransform(joint_index);
            Transform animated = _tracks[i].Evaluate(local, time, _is_looping);
            pose.SetLocalTransform(joint_index, animated);
        }
        return time;
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
    f32 AnimationClip::AdjustTimeToFitRange(f32 in_time) const
    {
        if (_is_looping)
        {
            f32 duration = _end_time - _start_time;
            if (duration <= 0) { 0.0f; }
            in_time = fmodf(in_time - _start_time, _end_time - _start_time);
            if (in_time < 0.0f)
            {
                in_time += _end_time - _start_time;
            }
            in_time += _start_time;
        }
        else
        {
            in_time = std::clamp(in_time, _start_time, _end_time);
        }
        return in_time;
    }
    f32 AnimationClip::GetNormalizedTime(f32 in_time) const
    {
        in_time = AdjustTimeToFitRange(in_time);
        return in_time / _duration;
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
