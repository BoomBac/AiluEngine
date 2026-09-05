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
        kApply
    };
}

#endif // __ROOT_MOTION_H__
