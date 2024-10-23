//
// Created by 22292 on 2024/10/22.
//
#pragma once
#ifndef __BLEND_SPACE_H__
#define __BLEND_SPACE_H__

#include "GlobalMarco.h"
#include "Clip.h"
#include "Pose.h"
#include "Skeleton.h"

namespace Ailu
{
	class AILU_API BlendSpace
	{
	public:
        struct ClipEntry
        {
            f32 _x, _y;
            AnimationClip *_clip;
        };
        BlendSpace();
        BlendSpace(Skeleton * sk,Vector2f x_range, Vector2f y_range,bool is_2d = false);
        ~BlendSpace();
        void SetSkeleton(Skeleton* sk);
        Skeleton* GetSkeleton() const;
        void AddClip(AnimationClip* clip, f32 x, f32 y);
        void RemoveClip(AnimationClip* clip);
        void Resize(Vector2f x_range, Vector2f y_range);
        void GetRange(Vector2f &x_range, Vector2f &y_range) const;
        void SetPosition(f32 x, f32 y);
        void GetPosition(f32 &x, f32 &y) const;
        void Update(f32 dt);
        Pose &GetCurrentPose();
        void SetClipPosition(AnimationClip* clip, f32 x, f32 y);
        Vector<ClipEntry> &GetClips() {return _clips;};
    private:
        void NormalizeRange(f32 &x, f32 &y) const;
    private:
        bool _is_2d;
        //index 0 is blend res
        Array<Pose, 5> _poses;
        f32 _time;
        Skeleton* _skeleton;
        Vector2f _x_range, _y_range;
        f32 _x, _y;
		Vector<ClipEntry> _clips;
	};
}

#endif// !BLEND_SPACE_H__
