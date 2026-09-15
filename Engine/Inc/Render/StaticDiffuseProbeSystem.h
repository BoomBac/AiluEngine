#pragma once
#ifndef __STATIC_DIFFUSE_PROBE_SYSTEM_H__
#define __STATIC_DIFFUSE_PROBE_SYSTEM_H__

#include "Framework/Common/NonCopyable.h"
#include "Framework/Core/Containers/Vector.h"
#include "Render/ShaderInterop.h"
#include "Render/StaticDiffuseProbeData.h"

namespace Ailu::SceneManagement
{
    class Scene;
}

namespace Ailu::Render
{
    class AILU_API StaticDiffuseProbeSystem : public NonCopyable
    {
    public:
        void Update(const SceneManagement::Scene &scene);
        u64 Revision() const { return _revision; }

        bool Sample(const Vector3f &world_position, SphericalHarmonicsL2 &out_sh, f32 &out_weight) const;
        void BuildGPUData(Vector<StaticDiffuseProbeGroupData> &out_groups,
                          Vector<StaticDiffuseProbeSHData> &out_probes) const;

    private:
        struct Group
        {
            Vector3f _position = Vector3f::kZero;
            Vector3f _size = Vector3f::kZero;
            Ref<StaticDiffuseProbeData> _data;
        };

        Vector<Group> _groups;
        u64 _revision = 0u;
    };
}

#endif // __STATIC_DIFFUSE_PROBE_SYSTEM_H__
