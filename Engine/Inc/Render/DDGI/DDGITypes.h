#pragma once
#ifndef __DDGI_TYPES_H__
#define __DDGI_TYPES_H__

#include "Framework/Math/Matrix.hpp"
#include "Framework/Math/Vector.hpp"
#include "Scene/Entity.h"

namespace Ailu::Render
{
    using Math::Matrix4x4f;
    using Math::Vector3UInt;
    using Math::Vector3f;

    struct DDGIVolumeRenderData
    {
        ECS::Entity _entity = ECS::kInvalidEntity;
        Matrix4x4f _local_to_world = Matrix4x4f::Identity();
        Vector3UInt _probe_count = Vector3UInt(1u, 1u, 1u);
        Vector3f _probe_spacing = Vector3f::kOne;
    };

    struct DDGIVolumeRuntime
    {
        ECS::Entity _entity = ECS::kInvalidEntity;
        DDGIVolumeRenderData _desc;
        bool _seen_this_frame = false;
    };
}

#endif // __DDGI_TYPES_H__
