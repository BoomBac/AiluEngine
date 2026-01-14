#pragma once
#ifndef __SCENE_H__
#define __SCENE_H__
#include "Component.h"
#include "Entity.hpp"
#include "Framework/Math/Geometry.h"
#include "GlobalMarco.h"
#include "Objects/Serialize.h"


namespace Ailu
{
    namespace SceneManagement
    {
        struct LightingData
        {
            f32 _indirect_lighting_intensity = 0.25f;
        };

        class AILU_API Scene final : public Object, public IPersistentable
        {
            friend class SceneMgr;
        public:
            void Serialize(Archive &arch) final;
            void Deserialize(Archive &arch) final;
            Scene(const String &name);
            ECS::Entity AddObject(String name = "");
            ECS::Entity AddObject(Ref<Mesh> mesh, Ref<Material> mat);
            ECS::Entity AddObject(Ref<Mesh> mesh, const Vector<Ref<Material>>& mats);
            ECS::Entity DuplicateEntity(ECS::Entity e);
            void RemoveObject(ECS::Entity entity);
            void Attach(ECS::Entity current, ECS::Entity parent);
            void Detach(ECS::Entity current);
            void MarkDirty() { _dirty = true; };
            const Vector<ECS::Entity> &EntityView() const;
            ECS::Entity Pick(const Ray &ray);
            LightingData _light_data;
            auto GetAllStaticRenderable() const { return _register.View<ECS::StaticMeshComponent>(); };
            auto GetAllSkinedRenderable() const { return _register.View<ECS::CSkeletonMesh>(); };
            const ECS::Register &GetRegister() const { return _register; }
            ECS::Register &GetRegister() { return _register; }
            u32 EntityNum() const { return _register.EntityNum(); }

            String AcquireName() const { return std::format("new_object_{}", _register.EntityNum()); };
            Render::GPUBuffer *GetSceneMeshDataBuffer() const{ return &*_scene_mesh_data; };
            u32 TriangleCount() const { return _triangle_count; };
            //指示实际三角形数据在buffer中的偏移，计算时读取到node.start + offset来索引三角形
            u32 GetTriangleBufferOffset(ECS::Entity entity) const 
            {
                if (_mesh_bvh_node_triangle_offset.contains(entity))
                {
                    return _mesh_bvh_node_triangle_offset.at(entity);
                }
                return 0;
            };
            Vector2UInt GetBVHNodeRange(ECS::Entity entity) const
            {
                if (_bvh_nodes_range.contains(entity))
                {
                    return _bvh_nodes_range.at(entity);
                }
                return Vector2UInt::kZero;
            };
            [[nodiscard]] std::span<const BVHNode> GetBVHNodes() const { return _tlas_nodes; };
            Render::GPUBuffer *GetBLASBuffer() const { return &*_blas_buffer; };
            Render::GPUBuffer *GetTLASBuffer() const { return &*_tlas_buffer; };
            u32 GetTLASNodeCount() const { return (u32) _tlas_nodes.size(); };
            u32 GetBLASNodeCount() const { return _blas_node_count; };
        private:
            void DeletePendingEntities();
            void Clear();
            void Update(f32 dt);
            void RebuildBVHTree();
        private:
            bool _dirty = true;
            u16 _total_renderable_count = 0u;
            ECS::Register _register;
            Queue<ECS::Entity> _pending_delete_entities;
            Ref<Render::GPUBuffer> _scene_mesh_data;
            Ref<Render::GPUBuffer> _blas_buffer;
            Ref<Render::GPUBuffer> _tlas_buffer;
            u32 _triangle_count = 0u;
            u32 _blas_node_count = 0u;
            HashMap<ECS::Entity, Vector2UInt> _bvh_nodes_range;
            HashMap<ECS::Entity, u32> _mesh_bvh_node_triangle_offset;//指示实际三角形数据在buffer中的偏移，计算时读取到node.start + offset来索引三角形
            Vector<BVHNode> _tlas_nodes;
        };

        class AILU_API SceneMgr : public IRuntimeModule
        {
        public:
            DISALLOW_COPY_AND_ASSIGN(SceneMgr)
            SceneMgr() = default;
            int Initialize();
            void Finalize();
            void Tick(f32 delta_time);

            void MarkCurSceneDirty() { _p_current->MarkDirty(); };
            Ref<Scene> Create(String name);
            Ref<Scene> OpenScene(const WString &scene_path);
            Scene *ActiveScene() { return _p_current; }
            void EnterPlayMode();
            void ExitPlayMode();
            void EnterSimulateMode();
            void ExitSimulateMode();
        private:
            Map<WString, Ref<Scene>> _all_scene;
            u16 _scene_index = 0u;
            Scene *_p_current = nullptr;
            Scene *_runtime_scene = nullptr;
            Scene *_runtime_scene_src = nullptr;
            Vector<Transform> _transform_cache;
        };
    }
    extern AILU_API SceneManagement::SceneMgr *g_pSceneMgr;
}// namespace Ailu
#endif// !SCENE_H__
