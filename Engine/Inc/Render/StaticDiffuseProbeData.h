#pragma once
#ifndef __STATIC_DIFFUSE_PROBE_DATA_H__
#define __STATIC_DIFFUSE_PROBE_DATA_H__

#include "Framework/Core/ReflectionMacros.h"
#include "Framework/Math/Vector.hpp"
#include "Objects/Object.h"
#include "Render/SphericalHarmonics.h"
#include "generated/StaticDiffuseProbeData.gen.h"

namespace Ailu::Render
{
    ASTRUCT()
    struct AILU_API ProbeGridDesc
    {
        GENERATED_BODY()

        APROPERTY()
        Math::Vector3f _origin = Math::Vector3f::kZero;
        APROPERTY()
        Math::Vector3f _spacing = Math::Vector3f::kOne;
        APROPERTY()
        Math::Vector3Int _probe_count = Math::Vector3Int(1, 1, 1);

        u32 ProbeCount() const;
        u32 FlattenIndex(i32 x, i32 y, i32 z) const;
    };

    ACLASS()
    class AILU_API StaticDiffuseProbeData : public Object
    {
        GENERATED_BODY()

    public:
        StaticDiffuseProbeData() = default;
        explicit StaticDiffuseProbeData(const String &name) : Object(name) {}

        ProbeGridDesc _grid;
        Vector<SphericalHarmonicsL2> _probes;
        u64 _revision = 0u;
    };
}

#endif // __STATIC_DIFFUSE_PROBE_DATA_H__
