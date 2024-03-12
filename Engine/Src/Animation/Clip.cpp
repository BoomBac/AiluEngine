#include "pch.h"
#include "Animation/Clip.h"

namespace Ailu
{
	AnimationClip::AnimationClip() : _frame_count(0), _duration(0.f), _name("animation_clip")
	{
	}
	AnimationClip::AnimationClip(Skeleton& sk, u32 frame_count, f32 duration) : _sk(sk), _frame_count(frame_count),_duration(duration)
	{
		_local_pose.resize(_sk.JointNum());
	}

	void AnimationClip::AddKeyFrame(u16 joint_index, const Transform& local_pose)
	{
		AL_ASSERT(_sk.JointNum() < joint_index, "joint index out of range");
		if (_local_pose.empty())
			_local_pose.resize(_sk.JointNum());
		_local_pose[joint_index].emplace_back(local_pose);
	}

	void AnimationClip::Bake()
	{
		u32 frame_count = static_cast<u32>(_local_pose[0].size());
		u16 joint_num = static_cast<u32>(_local_pose.size());
		_local_pose_mat.resize(joint_num);
		_local_pose_mat_debug.resize(joint_num);
		for (u32 i = 0; i < joint_num; i++)
		{
			_local_pose_mat[i].resize(frame_count);
			_local_pose_mat_debug[i].resize(frame_count);
		}
		for (u32 i = 0; i < frame_count; i++)
		{
			for (u32 j = 0; j < _local_pose.size(); j++)
			{
				auto& joint = _sk[j];
				if (!_local_pose[j].empty())
				{
					auto& local_pose = _local_pose[j][i];
					if (joint._parent == 65535)
					{
						joint._cur_pose = Transform::ToMatrix(local_pose);
					}
					else
					{
						joint._cur_pose = Transform::ToMatrix(local_pose) * _sk[joint._parent]._cur_pose;
					}
					_local_pose_mat[j][i] = joint._inv_bind_pos * joint._cur_pose * joint._node_inv_world_mat;
					_local_pose_mat_debug[j][i] = joint._cur_pose * joint._node_inv_world_mat;
				}
			}
		}
	}

	const Matrix4x4f& AnimationClip::Sample(u16 joint_index, u32 time) const
	{
		AL_ASSERT(_local_pose_mat.size() < joint_index, "joint index out of range");
		AL_ASSERT(_local_pose_mat[joint_index].size() < time, "time out of range");
		return _local_pose_mat[joint_index][time];
	}

	const Matrix4x4f& AnimationClip::SampleDebug(u16 joint_index, u32 time) const
	{
		AL_ASSERT(_local_pose_mat_debug.size() < joint_index, "joint index out of range");
		AL_ASSERT(_local_pose_mat_debug[joint_index].size() < time, "time out of range");
		return _local_pose_mat_debug[joint_index][time];
	}

	void AnimationClipLibrary::AddClip(const String& name, AnimationClip* clip)
	{
		auto clip_ptr = MakeRef<AnimationClip>();
		clip_ptr.reset(clip);
		s_clips.insert(std::make_pair(name, std::move(clip_ptr)));
	}
	void AnimationClipLibrary::AddClip(AnimationClip* clip)
	{
		auto clip_ptr = MakeRef<AnimationClip>();
		clip_ptr.reset(clip);
		s_clips.insert(std::make_pair(clip->Name(), std::move(clip_ptr)));
	}
	Ref<AnimationClip> AnimationClipLibrary::GetClip(const String& name)
	{
		return s_clips.contains(name) ? s_clips[name] : nullptr;
	}
}