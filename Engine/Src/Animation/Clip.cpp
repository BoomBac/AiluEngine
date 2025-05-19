#include "Animation/Clip.h"
#include "pch.h"

namespace Ailu
{
    AnimationClip::AnimationClip() : Object("animation_clip"), _frame_count(0), _duration(0.f), _frame_rate(30u), _frame_duration(0.f), _start_time(0.f), _end_time(0.f), _is_looping(true)
    {
    }
    void AnimationClip::Serialize(Archive &arch)
    {
        if (_frame_duration == 0.f && _duration > 0.f)
            _frame_duration = _duration / (f32)_frame_count;
        arch << "_name:" << _name << std::endl;
        arch << "_frame_count:" << _frame_count << std::endl;
        arch << "_duration:" << _duration << std::endl;
        arch << "_frame_rate:" << _frame_rate << std::endl;
        arch << "_frame_duration:" << _frame_duration << std::endl;
        arch << "_is_looping:" << _is_looping << std::endl;
        arch << "_tracks:" << _tracks.size() << std::endl;
        IndentBlock id1(arch);
        for (u32 i = 0u; i < _tracks.size(); i++)
        {
            auto &track = _tracks[i];
            auto &pos_track = track.GetPositionTrack();
            auto &rot_track = track.GetRotationTrack();
            auto &scl_track = track.GetScaleTrack();
            arch.InsertIndent();
            arch << "_joint_index:" << _tracks[i].GetId() << std::endl;
            arch.InsertIndent();
            arch << "_frame_num:" << pos_track.Size() << std::endl;
            {
                for (u32 frame_index = 0; frame_index < pos_track.Size(); frame_index++)
                {
                    Transform t;
                    t._position = TrackHelpers::ToVector(pos_track[frame_index]);
                    t._rotation = TrackHelpers::ToQuaternion(rot_track[frame_index]);
                    t._scale = TrackHelpers::ToVector(scl_track[frame_index]);
                    arch << t;
                }
            }
        }
    }
    void AnimationClip::Deserialize(Archive &arch)
    {
        Array<String, 7> bufs;
        Map<String, String> kvs;
        for (u16 i = 0; i < bufs.size(); i++)
        {
            arch >> bufs[i];
            auto str_list = su::Split(bufs[i], ":");
            kvs[str_list[0]] = str_list[1];
        }
        AL_ASSERT(su::BeginWith(bufs[0], "_name"));
        _name = kvs["_name"];
        _frame_count = std::stoul(kvs["_frame_count"]);
        _duration = std::stof(kvs["_duration"]);
        _frame_rate = std::stof(kvs["_frame_rate"]);
        _frame_duration = std::stof(kvs["_frame_duration"]);
        _is_looping = kvs["_is_looping"] == "true";
        _tracks.resize(std::stoul(kvs["_tracks"]));
        if (_frame_duration == 0.f && _duration > 0.f)
            _frame_duration = _duration / (f32)_frame_count;
        for (u16 i = 0; i < _tracks.size(); i++)
        {
            arch >> bufs[0] >> bufs[1];
            auto kw1 = su::Split(bufs[0], ":");
            auto kw2 = su::Split(bufs[1], ":");
            AL_ASSERT(kw1[0] == "_joint_index");
            AL_ASSERT(kw2[0] == "_frame_num");
            u16 joint_index = (u16)std::stoul(kw1[1]);
            u16 _frame_num =  (u16)std::stoul(kw2[1]);
            _tracks[i].SetId(joint_index);
            f32 cur_time = 0.f;
            for (u16 frame_index = 0; frame_index < _frame_num; frame_index++)
            {
                Transform t;
                arch >> t;
                _tracks[i].Resize(_frame_num);
                auto pos_frame = TrackHelpers::FromVector(t._position);
                pos_frame._time = cur_time;
                auto rot_frame = TrackHelpers::FromQuaternion(t._rotation);
                rot_frame._time = cur_time;
                auto scale_frame = TrackHelpers::FromVector(t._scale);
                scale_frame._time = cur_time;
                _tracks[i].GetPositionTrack()[frame_index] = pos_frame;
                _tracks[i].GetRotationTrack()[frame_index] = rot_frame;
                _tracks[i].GetScaleTrack()[frame_index] = scale_frame;
                cur_time += _frame_duration;
            }
        }
        _start_time = 0.f;
        _end_time = _start_time + _duration;
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