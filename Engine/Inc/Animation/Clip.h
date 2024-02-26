#pragma once
#ifndef __ANIM_CLIP_H__
#define __ANIM_CLIP_H__
#include <map>
#include "Skeleton.h"

namespace Ailu
{
	class AnimationClip
	{
		DECLARE_PRIVATE_PROPERTY(sk, CurSkeletion, Skeleton)
		DECLARE_PRIVATE_PROPERTY(frame_count, FrameCount, u32)
		DECLARE_PRIVATE_PROPERTY(name, Name, String)
		DECLARE_PROTECTED_PROPERTY(origin_path, OriginPath, String)
		DECLARE_PRIVATE_PROPERTY_RO(duration, Duration, f32)
	public:
		AnimationClip();
		AnimationClip(Skeleton& sk, u32 frame_count,f32 duration);
		void AddKeyFrame(u16 joint_index,const Transform& local_pose);
		void Bake();
		~AnimationClip() = default;
		//no lerp
		const Matrix4x4f& Sample(u16 joint_index,u32 time) const;
		//for gizmo draw only
		const Matrix4x4f& SampleDebug(u16 joint_index,u32 time) const;
	private:
		Vector<Vector<Transform>> _local_pose;
		Vector<Vector<Matrix4x4f>> _local_pose_mat;
		Vector<Vector<Matrix4x4f>> _local_pose_mat_debug;
	};

	class AnimationClipLibrary
	{
	public:
		static void AddClip(const String& name, AnimationClip* clip);
		static void AddClip(AnimationClip* clip);
		static Ref<AnimationClip> GetClip(const String& name);
		static auto Begin() { return s_clips.begin(); }
		static auto End() { return s_clips.end(); }
	private:
		inline static std::map<String, Ref<AnimationClip>> s_clips{};
	};
}


#endif // !__ANIM_CLIP_H__

