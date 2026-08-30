#pragma once
#ifndef __ANIM_CLIP_H__
#define __ANIM_CLIP_H__
#include <map>
#include "Objects/Object.h"
#include "TransformTrack.h"
#include "SpriteAnimationTrack.h"
#include "AnimationEvent.h"
#include "generated/Clip.gen.h"
namespace Ailu
{
    ACLASS()
    class AILU_API AnimationClip : public Object
	{
        GENERATED_BODY()
	public:
        AnimationClip();
        ~AnimationClip() override = default;
        void CopyFrom(const AnimationClip &source);
        //特定轨道索引的关节标识
        u16 GetIdAtIndex(u32 index) const;
        const TransformTrack& GetTrackAtIndex(u32 index) const { return _tracks[index]; }
        const SpriteAnimationTrack &SpriteTrack() const { return _sprite_track; }
        SpriteAnimationTrack &SpriteTrack() { return _sprite_track; }
        const Vector<AnimationEvent> &Events() const { return _events; }
        Vector<AnimationEvent> &Events() { return _events; }
        void AddEvent(AnimationEvent event);
        void SetIdAtIndex(u32 index, u32 id);
        //包含的关节数量
        [[nodiscard]] u32 Size() const;
        TransformTrack& operator[](u16 joint);
        //由anim loader调用，
        void RecalculateDuration();
		[[nodiscard]] u32 FrameCount() const { return _frame_count; }
        void FrameCount(u32 frame_count) { _frame_count = frame_count; }
		//total duration in seconds
		[[nodiscard]] f32 Duration() const { return _duration; }
        void Duration(f32 duration) { _duration = duration; }
		[[nodiscard]] f32 FrameRate() const { return _frame_rate; }
        void FrameRate(f32 frame_rate) { _frame_rate = frame_rate; }
        [[nodiscard]] f32 FrameDuration() const { return _frame_duration;}
        void FrameDuration(f32 frame_duration) { _frame_duration = frame_duration; }
        [[nodiscard]] bool IsLooping() const { return _is_looping; }
        void IsLooping(bool is_looping) { _is_looping = is_looping; };
        [[nodiscard]] const Guid &PreviewMeshGuid() const { return _preview_mesh_guid; }
        void PreviewMeshGuid(const Guid &guid) { _preview_mesh_guid = guid; }
        [[nodiscard]] f32 GetStartTime() const { return _start_time; }
        void StartTime(f32 start_time) { _start_time = start_time; }
        [[nodiscard]] f32 GetEndTime() const { return _end_time; }
        void EndTime(f32 end_time) { _end_time = end_time; }
	private:
        f32 _start_time;
        f32 _end_time;
		u32 _frame_count;
		f32 _duration;
        f32 _frame_rate;
        f32 _frame_duration;
        bool _is_looping;
        Vector<TransformTrack> _tracks;
        SpriteAnimationTrack _sprite_track;
        Vector<AnimationEvent> _events;
        Guid _preview_mesh_guid = Guid::EmptyGuid();
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

