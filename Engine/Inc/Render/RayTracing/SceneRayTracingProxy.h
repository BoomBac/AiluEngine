#pragma once
#ifndef __SCENE_RAY_TRACING_PROXY_H__
#define __SCENE_RAY_TRACING_PROXY_H__

#include "Render/Buffer.h"
#include "Render/RayTracing/RayTracingGeometry.h"
#include "Render/RayTracing/RayTracingScene.h"
#include "Render/ShaderInterop.h"
#include "Scene/Scene.h"

namespace Ailu::Render
{
    class Mesh;

    class AILU_API SceneRayTracingProxy
    {
    public:
        SceneRayTracingProxy();
        void Sync(const SceneManagement::Scene *scene, Vector<PrimitiveData> *primitive_data = nullptr,
                  GPUBuffer *primitive_buffer = nullptr, const Vector<ScenePrimitive> *scene_primitives = nullptr);
        void SyncLightCache(const SceneManagement::Scene *scene);

        RayTracingScene *GetScene() const { return FrontSlot()._scene.get(); }
        GPUBuffer *GetVertexData() const { return FrontSlot()._vertex_data.get(); }
        GPUBuffer *GetNormalData() const { return FrontSlot()._normal_data.get(); }
        GPUBuffer *GetIndexData() const { return FrontSlot()._indices_data.get(); }
        GPUBuffer *GetUVData() const { return FrontSlot()._uv_data.get(); }
        GPUBuffer *GetTangentData() const { return FrontSlot()._tangent_data.get(); }
        GPUBuffer *GetPrimitiveData() const
        {
            return _scene_primitive_buffer;
        }
        GPUBuffer *GetMaterialData() const { return _material_buffer.get(); }
        GPUBuffer *GetUnifiedLightData() const { return _unified_light_buffer.get(); }
        ConstantBuffer *GetUnifiedLightConfig() const { return _unified_light_config.get(); }
        u32 GetUnifiedLightCount() const { return _unified_light_config_cpu._light_count; }
        bool HasRenderableScene() const
        {
            return _scene_primitive_buffer != nullptr && FrontSlot()._scene != nullptr && FrontSlot()._scene->InstanceCount() > 0u;
        }

    private:
        struct CachedGeometry
        {
            Vector<Ref<RayTracingGeometry>> _geometries;
            VertexBuffer *_vertex_buffer = nullptr;
        };

        struct SceneInstanceKey
        {
            ECS::Entity _entity = ECS::kInvalidEntity;
            Mesh *_mesh = nullptr;
            VertexBuffer *_vertex_buffer = nullptr;
            u16 _material_class = 0u;
        };

        struct GeometryRange
        {
            u32 _triangle_offset = 0u;
            u32 _vertex_offset = 0u;
        };

        struct RTInstanceRef
        {
            u32 _primitive_index = 0u;
            Matrix4x4f _transform = {};
        };

        struct ProxyBufferSlot
        {
            HashMap<Mesh *, Vector<GeometryRange>> _mesh_ranges;
            Vector<SceneInstanceKey> _instance_keys;
            Vector<RTInstanceRef> _instances;
            Ref<RayTracingScene> _scene;
            Ref<GPUBuffer> _vertex_data;
            Ref<GPUBuffer> _normal_data;
            Ref<GPUBuffer> _uv_data;
            Ref<GPUBuffer> _tangent_data;
            Ref<GPUBuffer> _indices_data;
        };

        Vector<SceneInstanceKey> CollectInstanceKeys(const SceneManagement::Scene &scene, Vector<Matrix4x4f> &transforms, Vector<Material*> &materials) const;
        bool NeedsStructureRebuild(const ProxyBufferSlot &slot, const Vector<SceneInstanceKey> &instance_keys) const;
        Vector<Ref<RayTracingGeometry>> AcquireGeometry(Mesh *mesh, VertexBuffer *vertex_buffer);
        void StartRebuild(const Vector<SceneInstanceKey> &instance_keys, const Vector<Matrix4x4f> &transforms);
        bool Rebuild(ProxyBufferSlot &slot, const Vector<SceneInstanceKey> &instance_keys, const Vector<Matrix4x4f> &transforms);
        void UpdateTransforms(ProxyBufferSlot &slot, const Vector<Matrix4x4f> &transforms);
        bool RebuildPackedBuffers(ProxyBufferSlot &slot, const Vector<SceneInstanceKey> &instance_keys);
        bool IsSlotReady(ProxyBufferSlot &slot);
        void TrySwapBuffers();
        void ClearSlot(ProxyBufferSlot &slot);
        void UpdateMaterials(const SceneManagement::Scene &scene);
        void UpdateUnifiedLights(const SceneManagement::Scene &scene);

        ProxyBufferSlot &FrontSlot() { return _slots[_front_slot]; }
        const ProxyBufferSlot &FrontSlot() const { return _slots[_front_slot]; }
        ProxyBufferSlot &BackSlot() { return _slots[1u - _front_slot]; }
        const ProxyBufferSlot &BackSlot() const { return _slots[1u - _front_slot]; }

    private:
        HashMap<Mesh *, CachedGeometry> _mesh_geometry_cache;
        ProxyBufferSlot _slots[2];
        u32 _front_slot = 0u;
        bool _has_pending_swap = false;
        Vector<Matrix4x4f> _transforms;
        Vector<Material*> _materials;
        Vector<MaterialData> _material_data_cache;
        Map<u64, u32> _material_data_lut;
        Ref<GPUBuffer> _material_buffer;
        Vector<UnifiedLightData> _unified_light_data_cache;
        Ref<GPUBuffer> _unified_light_buffer;
        Ref<ConstantBuffer> _unified_light_config;
        UnifiedLightBufferConfig _unified_light_config_cpu = {};
        Vector<PrimitiveData> *_scene_primitive_data = nullptr;
        GPUBuffer *_scene_primitive_buffer = nullptr;
        const Vector<ScenePrimitive> *_scene_primitives = nullptr;
    };
}

#endif// __SCENE_RAY_TRACING_PROXY_H__
