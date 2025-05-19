//
// Created by 22292 on 2024/10/14.
//
#include "Animation/CrossFade.h"
#include "pch.h"

namespace Ailu
{

    CrossFadeController::CrossFadeController() : mClip(nullptr), mTime(0), mWasSkeletonSet(false)
    {
    }
    CrossFadeController::CrossFadeController(const Skeleton &skeleton) : mClip(nullptr), mTime(0)
    {
        SetSkeleton(skeleton);
    }
    void CrossFadeController::SetSkeleton(const Skeleton &skeleton)
    {
        mSkeleton = skeleton;
        mPose = mSkeleton.GetRestPose();
        mWasSkeletonSet = true;
    }
    void CrossFadeController::Play(AnimationClip *target)
    {
        mTargets.clear();
        mClip = target;
        mPose = mSkeleton.GetRestPose();
        mTime = target->GetStartTime();
    }
    void CrossFadeController::FadeTo(AnimationClip *target, f32 fade_time)
    {
        if (mClip == nullptr)
        {
            Play(target);
            return;
        }
        if (!mTargets.empty())
        {
            AnimationClip *clip = mTargets.back().mClip;
            if (clip == target)
                return;
        }
        else
        {
            if (mClip == target)
                return;
        }
        mTargets.emplace_back(target, mSkeleton.GetRestPose(), fade_time);
    }
    void CrossFadeController::Update(f32 dt)
    {
        if (mClip == nullptr || !mWasSkeletonSet)
            return;
        u32 numTargets = (u32)mTargets.size();
        //淡入淡出完毕的话，移除第一个对象，每帧移除一个
        for (unsigned int i = 0; i < numTargets; ++ i)
        {
            float duration = mTargets[i].mDuration;
            if (mTargets[i].mElapsed >= duration)
            {
                mClip = mTargets[i].mClip;
                mTime = mTargets[i].mTime;
                mPose = mTargets[i].mPose;
                mTargets.erase(mTargets.begin() + i);
                break;
            }
        }
        //混合列表内的所有动画
        numTargets = (u32)mTargets.size();
        mPose = mSkeleton.GetRestPose();
        mTime = mClip->Sample(mPose, mTime + dt);
        for (unsigned int i = 0; i < numTargets; ++ i)
        {
            CrossFadeTarget& target = mTargets[i];
            target.mTime = target.mClip->Sample(target.mPose, target.mTime + dt);
            target.mElapsed += dt;
            f32 t = target.mElapsed / target.mDuration;
            if (t > 1.0f) { t = 1.0f; }
            Pose::Blend(mPose, mPose, target.mPose, t, -1);
        }
    }
    Pose &CrossFadeController::GetCurrentPose()
    {
        return mPose;
    }
    AnimationClip *CrossFadeController::GetcurrentClip()
    {
        return mClip;
    }

    namespace AnimBlending
    {
        Pose MakeAdditivePose(Skeleton& skeleton, AnimationClip* clip)
        {
            Pose ret = skeleton.GetRestPose();
            clip->Sample(ret, clip->GetStartTime());
            return ret;
        }
        void Add(Pose& out,Pose& in_pose,Pose& add_pose,Pose& base_pose,int blend_root)
        {
            u32 numJoints = add_pose.Size();
            for (u32 i = 0; i < numJoints; ++ i)
            {
                Transform input = in_pose.GetLocalTransform(i);
                Transform additive = add_pose.GetLocalTransform(i);
                Transform additiveBase= base_pose.GetLocalTransform(i);
                if (blend_root != Joint::kInvalidJointIndex &&!Pose::IsInHierarchy(add_pose, blend_root, i))
                    continue;
                // outPose = inPose + (addPose - basePose)
                Transform t;
                t._position = input._position + (additive._position - additiveBase._position);
                t._rotation = Quaternion::NormalizedQ(input._rotation * (Quaternion::Inverse(additiveBase._rotation) * additive._rotation));
                t._scale = input._scale + (additive._scale - additiveBase._scale);
                out.SetLocalTransform(i, t);
            }
        }
    }
}// namespace Ailu