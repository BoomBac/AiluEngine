#include "Animation/Clip.h"
#include "Animation/Skeleton.h"
#include "pch.h"

#include <algorithm>
#include <cmath>

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
        _root_motion = source._root_motion;
    }

    namespace
    {
        Quaternion ExtractTwist(const Quaternion &rotation, const Vector3f &axis)
        {
            const Quaternion normalized = Quaternion::NormalizedQ(rotation);
            const Vector3f projected = axis * DotProduct(Vector3f(normalized.x, normalized.y, normalized.z), axis);
            const Quaternion twist(projected.x, projected.y, projected.z, normalized.w);
            if (Quaternion::LenSq(twist) > Math::kFloatEpsilon)
                return Quaternion::NormalizedQ(twist);
            return Quaternion::Identity();
        }

        RootMotionDelta FilterRootMotion(const Transform &previous, const Transform &current,
                                         ERootMotionTranslationMode translation_mode,
                                         ERootMotionRotationMode rotation_mode)
        {
            const Transform relative = Transform::Combine(Transform::LocalInverse(previous), current);
            RootMotionDelta delta;
            delta._translation = relative._position;
            if (translation_mode == ERootMotionTranslationMode::kNone)
                delta._translation = Vector3f::kZero;
            else if (translation_mode == ERootMotionTranslationMode::kXZ)
                delta._translation.y = 0.0f;

            if (rotation_mode == ERootMotionRotationMode::kFull)
                delta._rotation = Quaternion::NormalizedQ(relative._rotation);
            else if (rotation_mode == ERootMotionRotationMode::kYaw)
                delta._rotation = ExtractTwist(relative._rotation, Vector3f::kUp);
            else
                delta._rotation = Quaternion::Identity();
            return delta;
        }

        void AccumulateRootMotion(RootMotionDelta &out_delta, const RootMotionDelta &delta)
        {
            if (out_delta._rotation == Quaternion::Identity())
                out_delta._translation += delta._translation;
            else
                out_delta._translation += out_delta._rotation * delta._translation;
            // Quaternion::operator* is the engine's standard composition operator; operator^ is
            // a legacy non-standard operation and is not suitable for accumulating deltas here.
            out_delta._rotation = Quaternion::NormalizedQ(out_delta._rotation * delta._rotation);
        }
    }

    Transform AnimationClip::SampleRootTransform(i32 root_bone_index, const Transform &reference, f32 time,
                                                  bool looping) const
    {
        if (root_bone_index < 0)
            return reference;
        for (u32 track_index = 0u; track_index < _tracks.size(); ++track_index)
        {
            const TransformTrack &track = _tracks[track_index];
            if (track.GetId() == static_cast<u16>(root_bone_index))
                return track.Evaluate(reference, time, looping);
        }
        return reference;
    }

    RootMotionDelta AnimationClip::ExtractRootMotion(f32 previous_time, f32 current_time,
                                                      const RootMotionSettings &settings) const
    {
        return ExtractRootMotion(previous_time, current_time, settings, _is_looping);
    }

    RootMotionDelta AnimationClip::ExtractRootMotion(f32 previous_time, f32 current_time,
                                                      const RootMotionSettings &settings, bool looping) const
    {
        RootMotionDelta result;
        if (!settings._enabled || settings._root_bone_index < 0 || _duration <= Math::kFloatEpsilon ||
            std::abs(previous_time - current_time) <= Math::kFloatEpsilon)
            return result;

        const Transform reference;
        const auto segment = [&](f32 begin, f32 end)
        {
            const Transform previous = SampleRootTransform(settings._root_bone_index, reference, begin, false);
            const Transform current = SampleRootTransform(settings._root_bone_index, reference, end, false);
            return FilterRootMotion(previous, current, settings._translation_mode, settings._rotation_mode);
        };
        if (!looping)
            return segment(previous_time, current_time);

        const f32 duration = _duration;
        const f32 previous_cycle = std::floor(previous_time / duration);
        f32 current_cycle = std::floor(current_time / duration);
        // Runtime callers commonly pass wrapped local times (for example 0.98 -> 0.02),
        // so the decreasing-time case also represents one loop boundary.
        if (current_time < previous_time && current_cycle <= previous_cycle)
            current_cycle = previous_cycle + 1.0f;
        const auto local_time = [duration](f32 time)
        {
            f32 result = std::fmod(time, duration);
            if (result < 0.0f)
                result += duration;
            return result;
        };
        if (previous_cycle == current_cycle)
            return segment(local_time(previous_time), local_time(current_time));

        AccumulateRootMotion(result, segment(local_time(previous_time), duration));
        const RootMotionDelta full_cycle = segment(0.0f, duration);
        for (i64 cycle = static_cast<i64>(previous_cycle + 1.0f); cycle < static_cast<i64>(current_cycle); ++cycle)
            AccumulateRootMotion(result, full_cycle);
        AccumulateRootMotion(result, segment(0.0f, local_time(current_time)));
        return result;
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
