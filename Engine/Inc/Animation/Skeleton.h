#pragma once
#ifndef __SKELETON__
#define __SKELETON__
#include "Framework/Math/ALMath.hpp"
#include "Framework/Math/Transform.h"
#include "Pose.h"
#include "Objects/Serialize.h"

namespace Ailu
{
    struct Joint
    {
        static constexpr u16 kInvalidJointIndex = 0xffff;
        String _name;
        u16 _parent = kInvalidJointIndex;
        u16 _self = kInvalidJointIndex;
        Vector<u16> _children;
        Matrix4x4f _inv_bind_pos = Matrix4x4f::Identity();
    };
    Archive &operator>>(Archive &ar, Joint &c);
    Archive &operator<<(Archive &ar, const Joint &c);

    class AILU_API Skeleton
    {
        friend Archive &operator<<(Archive &ar, const Skeleton &sk);
        friend Archive &operator>>(Archive &ar, Skeleton &sk);
    public:
        static i32 GetJointIndexByName(const Skeleton &sk, const String &name);
        Skeleton(const String &name);
        Skeleton();
        ~Skeleton();
        const u32 JointNum() const;
        void AddJoint(const Joint &joint);
        void SetBindPoseLocalTransform(u32 index, const Transform &transform);
        void GetBindMatrixPalette(Vector<Matrix4x4f> &out) const;
        Joint &GetJoint(u32 index);
        Joint &GetJoint(const String &name);
        void Clear();
        void Rebuild();
        const bool operator==(const Skeleton &other) const;
        Joint &operator[](u32 index);
        const Joint &operator[](u32 index) const;
        //大部分情况下，bind和rest姿势是一致的
        [[nodiscard]] const Pose &GetBindPose() const;
        [[nodiscard]] const Pose &GetRestPose() const;
        // 迭代器
        auto begin() {return _joints.begin();};
        auto end() {return _joints.end();};
        auto begin() const {return _joints.cbegin();};
        auto end() const {return _joints.cend();};
    private:
        String _name;
        Vector<Joint> _joints;
        Pose _bind_pose;
    };
    Archive &operator<<(Archive &ar, const Skeleton &sk);
    Archive &operator>>(Archive &ar, Skeleton &sk);
}// namespace Ailu


#endif// !SKELETON__
