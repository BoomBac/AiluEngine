#pragma once
#ifndef __ANIM_CLIP_H__
#define __ANIM_CLIP_H__
#include <map>
#include "Objects/Object.h"
#include "TransformTrack.h"
#include "SpriteAnimationTrack.h"
#include "AnimationEvent.h"
#include "RootMotion.h"
#include "generated/Clip.gen.h"
namespace Ailu
{
    class Skeleton;

    AENUM()
    enum class ERootMotionTranslationMode : u8
    {
        kNone,
        kXZ,
        kXYZ
    };

    AENUM()
    enum class ERootMotionRotationMode : u8
    {
        kNone,
        kYaw,
        kFull
    };

    ASTRUCT()
    struct AILU_API RootMotionSettings
    {
        GENERATED_BODY()

        APROPERTY()
        bool _enabled = false;
        APROPERTY()
        String _root_bone_name;
        APROPERTY()
        ERootMotionTranslationMode _translation_mode = ERootMotionTranslationMode::kXZ;
        APROPERTY()
        ERootMotionRotationMode _rotation_mode = ERootMotionRotationMode::kYaw;

        i32 _root_bone_index = -1;
    };

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
        [[nodiscard]] const Guid &SkeletonGuid() const { return _skeleton_guid; }
        void SkeletonGuid(const Guid &guid) { _skeleton_guid = guid; }
        bool RemapToSkeleton(const Skeleton &source, const Skeleton &target);
        [[nodiscard]] const Guid &PreviewMeshGuid() const { return _preview_mesh_guid; }
        void PreviewMeshGuid(const Guid &guid) { _preview_mesh_guid = guid; }
        [[nodiscard]] f32 GetStartTime() const { return _start_time; }
        void StartTime(f32 start_time) { _start_time = start_time; }
        [[nodiscard]] f32 GetEndTime() const { return _end_time; }
        void EndTime(f32 end_time) { _end_time = end_time; }
        [[nodiscard]] const RootMotionSettings &GetRootMotionSettings() const { return _root_motion; }
        RootMotionSettings &GetRootMotionSettings() { return _root_motion; }
        void SetRootMotionSettings(const RootMotionSettings &settings) { _root_motion = settings; }
        Transform SampleRootTransform(i32 root_bone_index, const Transform &reference, f32 time, bool looping) const;
        RootMotionDelta ExtractRootMotion(f32 previous_time, f32 current_time,
                                          const RootMotionSettings &settings) const;
        RootMotionDelta ExtractRootMotion(f32 previous_time, f32 current_time, const RootMotionSettings &settings,
                                          bool looping) const;
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
        Guid _skeleton_guid = Guid::EmptyGuid();
        Guid _preview_mesh_guid = Guid::EmptyGuid();
        RootMotionSettings _root_motion;
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

