#pragma once
#ifndef __ROOT_MOTION_H__
#define __ROOT_MOTION_H__

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Math/Transform.h"

namespace Ailu
{
    struct AILU_API RootMotionDelta
    {
        Vector3f _translation = Vector3f::kZero;
        Quaternion _rotation = Quaternion::Identity();

        bool IsIdentity() const
        {
            return _translation == Vector3f::kZero && Quaternion::IsSameOrientation(_rotation, Quaternion::Identity());
        }
    };

    enum class ERootMotionMode : u8
    {
        kDisabled,
        kExtractOnly,
        kApply,
        kInPlace
    };

    inline constexpr u8 kRootMotionModeCount = 4u;

    inline bool IsValidRootMotionMode(ERootMotionMode mode)
    {
        return static_cast<u8>(mode) < kRootMotionModeCount;
    }

    inline ERootMotionMode DeserializeRootMotionMode(u8 value)
    {
        return value < kRootMotionModeCount ? static_cast<ERootMotionMode>(value) : ERootMotionMode::kDisabled;
    }

    inline u8 SerializeRootMotionMode(ERootMotionMode mode)
    {
        return IsValidRootMotionMode(mode) ? static_cast<u8>(mode) : static_cast<u8>(ERootMotionMode::kDisabled);
    }
}

#endif // __ROOT_MOTION_H__
