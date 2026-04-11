#pragma once
#ifndef __RAY_TRACING_SCENE_H__
#define __RAY_TRACING_SCENE_H__
#include "../GpuResource.h"
#include "Framework/Math/ALMath.hpp"
#include "RayTracingGeometry.h"

namespace Ailu::Render
{
    struct RayTracingInstance
    {
        RayTracingGeometry* _geometry;
        Matrix4x4f _transform;
        u32 _instance_id;          // 传给 shader
        u32 _hit_group_offset;     // SBT offset
        u8  _mask = 0xFF;
    };

    class RayTracingScene : public GpuResource
    {
    public:
        static Ref<RayTracingScene> Create();
        RayTracingScene();
        ~RayTracingScene();
        NativeHandle NativeResource() final;
        u64 AddInstance(const RayTracingInstance& instance);
        bool RemoveInstance(u64 instance_handle);
        void UpdateInstance(u64 instance_handle, const RayTracingInstance& instance);
        void UpdateInstance(u64 instance_handle, const Matrix4x4f& transform);
        const RayTracingInstance* GetInstance(u64 instance_handle) const;
        bool HasInstance(u64 instance_handle) const;
        u64 InstanceCount() const { return _instances.size(); }
        void ClearInstances();
        void Build();
        void Update();
    private:
        virtual void OnInstanceTransformChanged(u32 index) {};
        virtual void OnInstanceChanged(u32 index) {};
        virtual bool NeedRecreateBuildResource() const { return false; };
        virtual void PrepareBuild(bool is_rebuild) {};
    protected:
        Vector<RayTracingInstance> _instances;
        NativeHandle _native_resource;
        bool _is_need_rebuild = true;
        bool _is_need_refit = false;
    };
}
#endif// !__RAY_TRACING_SCENE_H__