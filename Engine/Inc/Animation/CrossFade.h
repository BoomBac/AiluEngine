//
// Created by 22292 on 2024/10/14.
//

#ifndef AILU_CROSSFADE_H
#define AILU_CROSSFADE_H
#include "Clip.h"
#include "GlobalMarco.h"
#include "Pose.h"
namespace Ailu
{
    /*
     * 用于对淡入的动画进行采样
     */
    struct CrossFadeTarget
    {
        inline CrossFadeTarget()
            : mClip(nullptr), mTime(0.0f),
              mDuration(0.0f), mElapsed(0.0f) {}
        inline CrossFadeTarget(AnimationClip *target, const Pose &pose, f32 dur)
            : mClip(target), mTime(target->GetStartTime()),
              mPose(pose), mDuration(dur), mElapsed(0.0f) {}
        Pose mPose;
        AnimationClip *mClip;
        f32 mTime;
        f32 mDuration;
        f32 mElapsed;
    };

    /*
     * 管理当前播放和即将淡入的片段
     * */
    class CrossFadeController
    {
    public:
        CrossFadeController();
        explicit CrossFadeController(const Skeleton& skeleton);
        void SetSkeleton(const Skeleton& skeleton);
        //将动画片段作为该控制器的基础动画，由update来驱动
        void Play(AnimationClip* target);
        void FadeTo(AnimationClip* target, f32 fade_time);
        void Update(f32 dt);
        Pose& GetCurrentPose();
        AnimationClip* GetcurrentClip();
    private:
        Vector<CrossFadeTarget> mTargets;
        AnimationClip* mClip;
        f32 mTime;
        Pose mPose;
        Skeleton mSkeleton;
        bool mWasSkeletonSet;
    };

    namespace AnimBlending
    {
        /*
         * 生成用于add函数的第四个参数
         * */
        Pose MakeAdditivePose(Skeleton& skeleton, AnimationClip* clip);
        /*
         * 叠加混合，输出 = 输入 + （叠加姿态 - 基础叠加姿态）
         * */
        void Add(Pose& out,Pose& in_pose,Pose& add_pose,Pose& base_pose,int blend_root);
    }
}// namespace Ailu
#endif//AILU_CROSSFADE_H
