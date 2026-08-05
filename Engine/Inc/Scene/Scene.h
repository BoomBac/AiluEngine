#pragma once
#ifndef __SCENE_H__
#define __SCENE_H__
#include <unordered_set>
#include "Component.h"
#include "Entity.h"
#include "Framework/Math/Geometry.h"
#include "Framework/Math/Guid.h"
#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/Containers/Map.h"
#include "Framework/Common/NonCopyable.h"
#include "Objects/Serialize.h"
#include "generated/Scene.gen.h"


namespace Ailu
{
    namespace SceneManagement
    {
        struct LightingData
        {
            f32 _indirect_lighting_intensity = 0.25f;
        };
        class ISceneCommand;
        ACLASS()
        class AILU_API Scene final : public Object
        {
            GENERATED_BODY()
            friend class SceneMgr;
        public:
            Scene() = default;
            explicit Scene(const String &name);
            ECS::Entity AddObject(String name = "");
            ECS::Entity AddObject(Ref<Mesh> mesh, Ref<Material> mat);
            ECS::Entity AddObject(Ref<Mesh> mesh, const Vector<Ref<Material>>& mats);
            ECS::Entity AddObject(String name, const Guid &requested_guid);
            ECS::Entity DuplicateEntity(ECS::Entity e);
            void RemoveObject(ECS::Entity entity);

            // --- Hierarchy API (new) ---
            bool IsValidEntity(ECS::Entity entity) const;
            bool IsDescendantOf(ECS::Entity entity, ECS::Entity potential_ancestor) const;
            bool Reparent(ECS::Entity child, ECS::Entity new_parent, bool keep_world_transform = true);
            bool Detach(ECS::Entity child, bool keep_world_transform = true);
            bool RenameEntity(ECS::Entity entity, const String& new_name);
            u64 StructureRevision() const { return _structure_revision; }

            // --- Entity GUID identity API ---
            ECS::Entity FindEntity(const Guid &guid) const;
            const Guid &GetEntityGuid(ECS::Entity entity) const;
            bool HasEntityGuid(const Guid &guid) const;
            bool ValidateEntityGuidIndex() const;
            // 修复身份数据：为缺失/空 PersistentIdComponent 的 Entity 生成 GUID 并重建索引。
            // 仅用于保存前修复异常数据，不修改正常 Entity 的 GUID。
            bool EnsureValidEntityIdentities();

            // 场景自身的 Asset GUID（从场景文件 _header._guid 解析），用于跨场景持久引用校验。
            const Guid &AssetGuid() const { return _asset_guid; }
            void SetAssetGuid(const Guid &guid) { _asset_guid = guid; }

            // --- Compatibility wrappers ---
            void Attach(ECS::Entity current, ECS::Entity parent) { Reparent(current, parent); }
            void Detach(ECS::Entity current) { Detach(current, true); }
            void MarkDirty() { _dirty = true; };
            void EnqueueSceneCommand(ISceneCommand *command, bool undo = false);
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
            u32 GetTriangleBufferOffset(ECS::Entity entity, u16 submesh_index) const 
            {
                const u64 key = (static_cast<u64>(entity) << 32u) | static_cast<u64>(submesh_index);
                if (_mesh_bvh_node_triangle_offset.contains(key))
                {
                    return _mesh_bvh_node_triangle_offset.at(key);
                }
                return 0;
            };
            Vector2UInt GetBVHNodeRange(ECS::Entity entity, u16 submesh_index) const
            {
                const u64 key = (static_cast<u64>(entity) << 32u) | static_cast<u64>(submesh_index);
                if (_bvh_nodes_range.contains(key))
                {
                    return _bvh_nodes_range.at(key);
                }
                return Vector2UInt::kZero;
            };
            [[nodiscard]] std::span<const BVHNode> GetBVHNodes() const { return _tlas_nodes; };
            Render::GPUBuffer *GetBLASBuffer() const { return &*_blas_buffer; };
            Render::GPUBuffer *GetTLASBuffer() const { return &*_tlas_buffer; };
            u32 GetTLASNodeCount() const { return (u32) _tlas_nodes.size(); };
            u32 GetBLASNodeCount() const { return _blas_node_count; };
        private:
            void BeginUpdate();
            void BeginLateUpdate();
            void FixedUpdate(f32 fixed_delta_time);
            void UpdateFixedScripts(f32 fixed_delta_time);
            void LateUpdate(f32 delta_time, f32 render_alpha);
            void UpdateScripts(f32 delta_time);
            void UpdateLateScripts(f32 delta_time, f32 render_alpha);
            void UpdateRenderTransforms(f32 render_alpha);
            void UpdateBounds();
            void UpdateCameras();
            void UpdateAccelerationStructures();
            void UpdateGpuSceneIfNeeded();
            void EndUpdate();
            void DeletePendingEntities();
            void Clear();
            void Update(f32 dt);
            void RebuildBVHTree();
            void UpdateGpuScene();
            void ProcessSceneCommands();

            // --- Hierarchy helpers ---
            void TouchStructure();
            bool UnlinkFromParent(ECS::Entity entity);
            bool LinkAsLastChild(ECS::Entity entity, ECS::Entity parent);
            void CollectSubtreePostOrder(ECS::Entity root, Vector<ECS::Entity>& result) const;

            // --- Entity GUID identity helpers ---
            Guid GenerateUniqueEntityGuid() const;
            bool RegisterEntityGuid(ECS::Entity entity, const Guid &guid);
            void UnregisterEntityGuid(ECS::Entity entity);
            void RebuildEntityGuidIndex();
            ECS::Entity CreateEntityInternal(String name, const Guid &requested_guid);

        private:
            struct QueuedSceneCommand
            {
                ISceneCommand *_command = nullptr;
                bool _undo = false;
            };

        private:
            bool _dirty = true;
            u64 _structure_revision = 1u;
            u16 _total_renderable_count = 0u;
            ECS::Register _register;
            std::unordered_set<ECS::Entity> _pending_delete_entities;
            Vector<QueuedSceneCommand> _pending_scene_commands;
            Ref<Render::GPUBuffer> _scene_mesh_data;
            Ref<Render::GPUBuffer> _blas_buffer;
            Ref<Render::GPUBuffer> _tlas_buffer;
            u32 _triangle_count = 0u;
            u32 _blas_node_count = 0u;
            HashMap<u64, Vector2UInt> _bvh_nodes_range;
            HashMap<u64, u32> _mesh_bvh_node_triangle_offset;//按 entity/submesh 记录三角形数据在 buffer 中的偏移
            Vector<BVHNode> _tlas_nodes;
            HashMap<Guid, ECS::Entity, GuidHasher> _guid_to_entity;
            Guid _asset_guid;
        };

        class AILU_API SceneMgr : public NonCopyable
        {
        public:
            static SceneMgr& Get();
            static void Init();
            static void Shutdown();
        public:
            SceneMgr();
            ~SceneMgr();
            void FixedUpdate(f32 fixed_delta_time);
            void Update(f32 delta_time);
            void LateUpdate(f32 delta_time, f32 render_alpha);
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
}// namespace Ailu
#endif// !SCENE_H__
