#pragma once
#ifndef __ANIM_CLIP_H__
#define __ANIM_CLIP_H__
#include <map>
#include "Skeleton.h"
#include "Objects/Object.h"
#include "Objects/Serialize.h"
#include "TransformTrack.h"
#include "Pose.h"
namespace Ailu
{
    class AILU_API AnimationClip : public Object, public IPersistentable
	{
	public:
		AnimationClip();
		~AnimationClip() override = default;
        //特定轨道索引的关节标识
        u16 GetIdAtIndex(u32 index) const;
        void SetIdAtIndex(u32 index, u32 id);
        //包含的关节数量
        [[nodiscard]] u32 Size() const;
        f32 Sample(Pose& pose,f32 time);
        TransformTrack& operator[](u16 joint);
        //由anim loader调用，
        void RecalculateDuration();
        void Serialize(Archive &arch) final;
        void Deserialize(Archive &arch) final;

		[[nodiscard]] u32 FrameCount() const { return _frame_count; }
        void FrameCount(u32 frame_count) { _frame_count = frame_count; }
		//total duration in seconds
		[[nodiscard]] f32 Duration() const { return _duration; }
        void Duration(f32 duration) { _duration = duration; }
		[[nodiscard]] f32 FrameRate() const { return _frame_rate; }
        void FrameRate(f32 frame_rate) { _frame_rate = frame_rate; }
        [[nodiscard]] f32 FrameDuration() const { return _frame_duration;}
        [[nodiscard]] bool IsLooping() const { return _is_looping; }
        void IsLooping(bool is_looping) { _is_looping = is_looping; };
        [[nodiscard]] f32 GetStartTime() const { return _start_time; }
        [[nodiscard]] f32 GetEndTime() const { return _end_time; }
        [[nodiscard]] f32 GetNormalizedTime(f32 in_time) const;
    private:
        f32 AdjustTimeToFitRange(f32 in_time) const;
	private:
        f32 _start_time;
        f32 _end_time;
		u32 _frame_count;
		f32 _duration;
        f32 _frame_rate;
        f32 _frame_duration;
        bool _is_looping;
        Vector<TransformTrack> _tracks;
	};

    class AILU_API AnimationClipLibrary
	{
	public:
        static void AddClip(const String &name, Ref<AnimationClip> clip);
        static void AddClip(Ref<AnimationClip> clip);
		static Ref<AnimationClip> GetClip(const String& name);
		static auto Begin() { return s_clips.begin(); }
		static auto End() { return s_clips.end(); }
	private:
		inline static std::map<String, Ref<AnimationClip>> s_clips{};
	};
}


#endif // !__ANIM_CLIP_H__

