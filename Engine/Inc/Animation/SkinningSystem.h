#pragma once
#ifndef __SKINNING_SYSTEM_H__
#define __SKINNING_SYSTEM_H__

#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/SmartPtr.h"
#include "Framework/Math/ALMath.hpp"

#include <future>

namespace Ailu::Render
{
    class SkeletonMesh;
}

namespace Ailu::ECS
{
    class AILU_API SkinningSystem
    {
    public:
        static constexpr u32 kDefaultVerticesPerTask = 2000u;

        void Submit(Render::SkeletonMesh *mesh, const Vector<Matrix4x4f> &pose_palette,
                    u32 vertices_per_task = kDefaultVerticesPerTask);
        void WaitFor() const;
        void Clear();

    private:
        Vector<Ref<std::future<void>>> _tasks;
    };
}

#endif // __SKINNING_SYSTEM_H__
