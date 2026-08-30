#pragma once
#ifndef __SKELETON_ASSET_H__
#define __SKELETON_ASSET_H__

#include "Animation/Skeleton.h"
#include "Objects/Object.h"
#include "generated/SkeletonAsset.gen.h"

namespace Ailu
{
    ACLASS()
    class AILU_API SkeletonAsset : public Object
    {
        GENERATED_BODY()

    public:
        [[nodiscard]] const Skeleton &GetSkeleton() const { return _skeleton; }
        Skeleton &GetSkeletonMutable() { return _skeleton; }
        [[nodiscard]] u64 LayoutHash() const { return _layout_hash; }
        void Rebuild();

    private:
        Skeleton _skeleton;
        u64 _layout_hash = 0u;
    };
}

#endif // __SKELETON_ASSET_H__
