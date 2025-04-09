#pragma once
#ifndef __RENDER_PIPELINE__
#define __RENDER_PIPELINE__
#include <utility>

#include "Camera.h"
#include "FrameResource.h"
#include "Renderer.h"

namespace Ailu
{
    struct VisibilityObject
    {
        u32 _id;
        u32 _render_obj_handle;
        AABB _bounding_box;
        u16 _submesh;
        //如果其为0，那么该obj就进入销毁流程
        u16 _submesh_count;
        u32 _layer_mask;
        VisibilityObject(u32 render_obj_handle,u16 submesh,u16 submesh_count) : _id(s_global_id++),_render_obj_handle(render_obj_handle),
        _submesh(submesh),_submesh_count(submesh_count){}
    private:
        inline static u32 s_global_id = 0u;
    };
    struct RenderObject
    {
    public:
        inline const static u32 kDynamicMask = BIT(1);
        inline const static u32 kSkinMask    = BIT(2);
    public:
        u32 _id;
        ECS::Entity _game_obj_handle;
        bool _is_skin;
        bool _is_dynamic;
        ECS::EMotionVectorType _motion_type;
        Vector3f _world_pos = Vector3f::kZero;
        Mesh* _mesh;
        Vector<Material*> _materials;
        Matrix4x4f _world_matrix = Matrix4x4f::Identity();
        Matrix4x4f _prev_world_matrix = Matrix4x4f::Identity();
        bool operator<(const RenderObject& other) const 
        {
            return _type_mask < other._type_mask;
        }
        RenderObject(ECS::Entity entity,bool is_dynamic,bool is_skin,Mesh* mesh,const Vector<Ref<Material>>& mats) : _id(s_global_id++),_game_obj_handle(entity),_is_dynamic(is_dynamic),
        _is_skin(is_skin),_mesh(mesh)
        {
            if (is_dynamic) _type_mask |= kDynamicMask;
            if (is_skin) _type_mask |= kSkinMask;
            _materials.resize(mats.size());
            for(u16 i = 0; i < mats.size();i++)
                _materials[i] = mats[i].get();
        }
    private:
        u32 _type_mask = 0u;
        inline static u32 s_global_id = 0u;
    };

    struct RenderNode
    {
    public:
        u32 _game_obj_handle;
        u32 _render_obj_handle;
        u32 _submesh;
        f32 _sqr_distance;
        Material* _mat;
        Mesh* _mesh;
        CBufferPerObjectData _object_data;
    };
    
    struct ViewEntity
    {
    public:
        static void FormCamera(const Camera &cam,ViewEntity& entry);
        ViewEntity() : _cam_hash(0u), _position(0.0f), _layer_mask(0){}
        ViewEntity(u32 id, const Vector3f &pos, u32 layer, ViewFrustum frustum) : _cam_hash(id), _position(pos),
            _layer_mask(layer), _frustum(std::move(frustum)) {}
        void PushRenderNode(const RenderNode& node);
        void Clear() {_render_node_count = 0u;}
        void SortRenderNode();
        [[nodiscard]] u32 NodeNum() const {return _render_node_count;}
        auto begin() {return _render_nodes.begin();}
        auto end() {return _render_nodes.begin() + _render_node_count;}
    public:
        //相机的哈希值
        u32 _cam_hash;
        Vector3f _position;
        u32 _layer_mask;
        ViewFrustum _frustum;
        CBufferPerCameraData _camera_data;
    private:
        Array<RenderNode,RenderConstants::kMaxRenderObjectCount> _render_nodes;
        u32 _render_node_count = 0u;
    };

    class FramePacket
    {
    public:
        inline const static u32 kMaxViewNum = 64u;
    public:
        FramePacket() : _view_num(0u) {};
        ~FramePacket();
        ViewEntity&FetchViewEntity() {
            return _views[_view_num++ % kMaxViewNum];
        }
        ViewEntity&GetView(u32 index) {return _views[index];}
        void ClearView() {_view_num = 0;}
        [[nodiscard]] u32 GetViewNum() const {return _view_num;}
        void CopyCameraConstBuffer(const ViewEntity& view);
        auto begin() {return _views.begin();}
        auto end() {return _views.begin() + _view_num;}
    private:
        Array<ViewEntity,kMaxViewNum> _views;
        u32 _view_num;
        HashMap<u32, ConstantBuffer *> _cam_cb;
    };

    class AILU_API RenderPipeline
    {
        DISALLOW_COPY_AND_ASSIGN(RenderPipeline)
    public:
        using RenderEvent = std::function<void>();
        RenderPipeline();
        void Init();
        void Destory();
        void Render();
        virtual void Setup();
        Renderer *GetRenderer(u16 index = 0) { return index < _renderers.size() ? _renderers[index].get() : nullptr; };
        RenderTexture *GetTarget(u16 index = 0);
        void FrameCleanUp();
        FrameResource *CurFrameResource() { return _cur_frame_res; }
        //新添加网格组件时mesh不能为空
        void OnAddRenderObject(ECS::Entity e);
    public:
        bool _is_need_wait_for_render_thread = true;
    private:
        void RenderSingleCamera(const Camera &cam, Renderer &renderer);
        void UpdateRenderObject(u32 start,u32 end);
        void UpdateVisibilityObject(u32 start,u32 end);
        void OnRenderObjectSubmeshCountChanged(u32 render_obj_id,u16 old_mesh_count,u32 new_mesh_count);
        void ProcessPendingRenderObjects();
    private:
        std::mutex _submesh_changed_lock;
    protected:
        virtual void BeforeReslove() {};
        RenderObject* GetRenderObject(u32 render_obj_handle);
        void CollectViews(const Scene& s);
        void CullView(ViewEntity& view_entity);
        void Test(ViewEntity& view_entity) {};
    protected:
        Array<FramePacket,RenderConstants::kFrameCount + 1> _frame_packets;
        Array<FrameResource, RenderConstants::kFrameCount + 1> _frame_res;
        Vector<Camera *> _cameras;
        Vector<Scope<Renderer>> _renderers;
        Vector<RenderTexture *> _targets;
        FrameResource *_cur_frame_res;
        FramePacket *_cur_frame_packet;
        Vector<RenderObject> _render_objs;
        Map<u32,u32> _render_obj_lut;
        u32 _dynamic_obj_start_pos = 0u;
        Vector<VisibilityObject> _visiblity_objs;
        Queue<ECS::Entity> _pending_add_render_objs;
    };
}// namespace Ailu

#endif// !RENDER_PIPELINE__
