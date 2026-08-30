#include "Animation/SkeletonAsset.h"

#include "pch.h"

namespace Ailu
{
    void SkeletonAsset::Rebuild()
    {
        _skeleton.Rebuild();

        constexpr u64 kOffsetBasis = 1469598103934665603ull;
        constexpr u64 kPrime = 1099511628211ull;
        u64 hash = kOffsetBasis;
        for (const Joint &joint : _skeleton)
        {
            for (const char character : joint._name)
            {
                hash ^= static_cast<u8>(character);
                hash *= kPrime;
            }
            hash ^= joint._parent;
            hash *= kPrime;
        }
        _layout_hash = hash;
    }
}
