#include "Render/RenderPipeline.h"
#include "Framework/Common/Application.h"
#include "Framework/Common/JobSystem.h"
#include "Framework/Common/Profiler.h"
#include "Render/CommandBuffer.h"
#include "UI/UIRenderer.h"
#include "pch.h"

#ifdef _PIX_DEBUG
#include "Ext/pix/Include/WinPixEventRuntime/pix3.h"
#endif// _PIX_DEBUG

namespace Ailu::Render
{
#pragma region FramePacket
    void ViewEntity::FormCamera(const Camera &cam, ViewEntity &entry)
    {
        entry._position = cam.Position();
        entry._layer_mask = cam._layer_mask;
        entry._frustum = cam.GetViewFrustum();
        entry._cam_hash = cam.HashCode();
        auto cam_cb_data = &entry._camera_data;
        u32 pixel_width = cam.Rect().x, pixel_height = cam.Rect().y;
        cam_cb_data->_ScreenParams = Vector4f(1.0f / (f32) pixel_width, 1.0f / (f32) pixel_height, (f32) pixel_width, (f32) pixel_height);
        Camera::CalculateZBUfferAndProjParams(cam, cam_cb_data->_ZBufferParams, cam_cb_data->_ProjectionParams);
        cam_cb_data->_CameraPos = cam.Position();
        cam_cb_data->_MatrixV = cam.GetView();
        cam_cb_data->_MatrixP = cam.GetProj();
        cam_cb_data->_MatrixVP = cam_cb_data->_MatrixV * cam_cb_data->_MatrixP;
        cam_cb_data->_MatrixVP_NoJitter = cam.GetViewProj();
        cam_cb_data->_MatrixIVP = MatrixInverse(cam_cb_data->_MatrixVP);
        cam_cb_data->_MatrixVP_Pre = cam.GetPrevViewProj();
        auto &jitter = cam.GetJitter();
        cam_cb_data->_matrix_jitter_x = jitter.x;
        cam_cb_data->_matrix_jitter_y = jitter.y;
        cam_cb_data->_uv_jitter_x = jitter.z;
        cam_cb_data->_uv_jitter_y = jitter.w;
        cam.GetCornerInWorld(cam_cb_data->_LT, cam_cb_data->_LB, cam_cb_data->_RT, cam_cb_data->_RB);
    }
    void ViewEntity::PushRenderNode(const RenderNode &node)
    {
        _render_nodes[_render_node_count++ % RenderConstants::kMaxRenderObjectCount] = node;
    }
    void ViewEntity::SortRenderNode()
    {
        if (_render_node_count > 1)
        {
            std::sort(_render_nodes.begin(), _render_nodes.begin() + _render_node_count, [](const RenderNode &a, const RenderNode &b)
                      { return a._sqr_distance < b._sqr_distance; });
        }
    }
    void FramePacket::CopyCameraConstBuffer(const ViewEntity &view)
    {
        if (!_cam_cb.contains(view._cam_hash))
        {
            _cam_cb[view._cam_hash] = ConstantBuffer::Create(RenderConstants::kPerCameraDataSize, std::format("CameraCB_{}", view._cam_hash));
            LOG_INFO("Create CameraCB: {}", view._cam_hash);
        }
        memcpy(_cam_cb[view._cam_hash]->GetData(), &view._camera_data, RenderConstants::kPerCameraDataSize);
    }
    FramePacket::~FramePacket()
    {
        for (auto &it: _cam_cb)
            DESTORY_PTR(it.second);
    }
#pragma endregion

#pragma region RenderPipeline
    RenderPipeline::RenderPipeline()
    {
        Init();
    }
    void RenderPipeline::Init()
    {
        TIMER_BLOCK("RenderPipeline::Init()");
        _renderers.push_back(MakeScope<Renderer>());
        _renderers.back()->_p_cur_pipeline = this;
        _cameras.emplace_back(Camera::sCurrent);
        _frame_res_manager = &FrameResourceManager::Get();
        _cur_frame_packet = nullptr;
        _cur_frame_res = nullptr;
    }
    void RenderPipeline::Destory()
    {
    }

    void RenderPipeline::Setup()
    {
        _cur_frame_packet = &_frame_packets[Application::Application::Get().GetFrameCount() % _frame_packets.size()];
        _cur_frame_res = &_frame_res[Application::Application::Get().GetFrameCount() % _frame_res.size()];
        ProcessPendingRenderObjects();
        {
            PROFILE_BLOCK_CPU(CollectView)
            CollectViews(*g_pSceneMgr->ActiveScene());
        }
         auto update_render_obj_job = JobSystem::Get().CreateJob("UpdateRenderObject",&RenderPipeline::UpdateRenderObject,this, 0u, (u32)_render_objs.size());
         auto update_vis_obj_job = JobSystem::Get().CreateJob("UpdateVisibilityObject", &RenderPipeline::UpdateVisibilityObject, this, 0u, (u32)_visiblity_objs.size());
         JobSystem::Get().AddDependency(update_render_obj_job,update_vis_obj_job);
         JobSystem::Get().Dispatch(update_render_obj_job);
        _targets.clear();
        _cameras.clear();
        _cameras.emplace_back(Camera::sCurrent);
        for (auto &r: _renderers)
            r->SetupFrameResource(&_frame_res[(Application::Application::Get().GetFrameCount() - 1) % _frame_res.size()], _cur_frame_res);
    }
    void RenderPipeline::Render()
    {
        /*
        Setup();
        for (auto cam: _cameras)
        {
            cam->SetRenderer(_renderers[0].get());
#ifdef _PIX_DEBUG
            PIXBeginEvent(cam->HashCode(), L"DeferedRenderer");
            RenderSingleCamera(*cam, *_renderers[0].get());
            PIXEndEvent();
#else
            RenderSingleCamera(*cam, *_renderers[0].get());
#endif
            _targets.push_back(_renderers[0]->TargetTexture());
        }
        RenderTexture::ResetRenderTarget();
        */
        {
            Application::Get().NotifyRender();
            Application::Get().WaitForRender();
            PROFILE_BLOCK_CPU(UIRender)
            auto cmd = CommandBufferPool::Get("UI");
            UI::UIRenderer::Get()->Render(cmd.get());
            GraphicsContext::Get().ExecuteCommandBuffer(cmd);
            CommandBufferPool::Release(cmd);
        }
    }
    RenderTexture *RenderPipeline::GetTarget(u16 index)
    {
        if (index < _targets.size())
            return _targets[index];
        return nullptr;
    }
    void RenderPipeline::RenderSingleCamera(const Camera &cam, Renderer &renderer)
    {
        renderer.Render(cam, *g_pSceneMgr->ActiveScene());
    }
    void RenderPipeline::FrameCleanUp()
    {
        g_pRenderTexturePool->RelesaeUnusedRT();
        for (auto &r: _renderers)
            r->FrameCleanup();
        if (_cur_frame_packet)
            _cur_frame_packet->ClearView();
        _is_need_wait_for_render_thread = true;
        _frame_res_manager->Tick();
        //LOG_INFO("FrameCleanup");
    }
    void RenderPipeline::OnRenderObjectSubmeshCountChanged(u32 render_obj_id, u16 old_mesh_count, u32 new_mesh_count)
    {
        std::lock_guard<std::mutex> lock(_submesh_changed_lock);
        if (old_mesh_count < new_mesh_count)
        {
            for (u16 i = old_mesh_count; i < new_mesh_count; i++)
                _visiblity_objs.push_back(VisibilityObject(render_obj_id, i, new_mesh_count));
        }
        else
        {
            auto remove_start = std::remove_if(_visiblity_objs.begin(), _visiblity_objs.end(), [&](const VisibilityObject &obj) -> bool
                                               { return obj._submesh >= new_mesh_count; });
            _visiblity_objs.erase(remove_start, _visiblity_objs.end());
        }
        for (auto &it: _visiblity_objs)
        {
            if (it._render_obj_handle == render_obj_id)
                it._submesh_count = new_mesh_count;
        }
    }
    void RenderPipeline::UpdateRenderObject(u32 start, u32 end)
    {
        PROFILE_BLOCK_CPU(UpdateRenderObject)
        const auto &r = g_pSceneMgr->ActiveScene()->GetRegister();
        for (u32 i = start; i < end; i++)
        {
            auto &obj = _render_objs[i];
            const ECS::TransformComponent *transf = r.GetComponent<ECS::TransformComponent>(obj._game_obj_handle);
            obj._world_matrix = transf->_transform._world_matrix;
            if (!obj._is_dynamic)
                continue;
            Mesh *new_mesh = nullptr;
            if (obj._is_skin)
            {
                auto comp = r.GetComponent<ECS::CSkeletonMesh>(obj._game_obj_handle);
                new_mesh = comp->_p_mesh.get();
                obj._motion_type = comp->_motion_vector_type;
            }
            else
            {
                auto comp = r.GetComponent<ECS::StaticMeshComponent>(obj._game_obj_handle);
                new_mesh = comp->_p_mesh.get();
                obj._motion_type = comp->_motion_vector_type;
            }
            if (new_mesh != obj._mesh)
            {
                OnRenderObjectSubmeshCountChanged(obj._id, obj._mesh->SubmeshCount(), new_mesh->SubmeshCount());
            }
            obj._mesh = new_mesh;
            const Vector<Ref<Material>> &new_materisl = obj._is_skin ? r.GetComponent<ECS::CSkeletonMesh>(obj._game_obj_handle)->_p_mats 
                : r.GetComponent<ECS::StaticMeshComponent>(obj._game_obj_handle)->_p_mats;
            obj._materials.resize(new_materisl.size());
            for (u16 i = 0; i < new_materisl.size(); i++)
                obj._materials[i] = new_materisl[i] ? new_materisl[i].get() : nullptr;
        }
    }
    void RenderPipeline::UpdateVisibilityObject(u32 start, u32 end)
    {
        // PROFILE_BLOCK_CPU(UpdateVisibilityObject)
        // for (u32 i = start; i < end; i++)
        // {
        //     auto &obj = _visiblity_objs[i];
        //     const auto &render_obj = _render_objs[_render_obj_lut[obj._render_obj_handle]];
        //     const auto &raw_bound = render_obj._mesh->BoundBox();
        //     obj._bounding_box = raw_bound[obj._submesh + 1] * render_obj._world_matrix;
        // }
        // static Vector<WaitHandle> s_cull_task_handle(FramePacket::kMaxViewNum);
        // u32 cur_view_count = 0u;
        // for(u32 i = 0; i < _cur_frame_packet->GetViewNum(); ++i)
        // {
        //     auto& view = _cur_frame_packet->GetView(i);
        //     auto job = JobSystem::Get().CreateJob(std::format("CullView_{}",i), &RenderPipeline::CullView, this,view);
        //     //auto handle  = JobSystem::Get().Dispatch(job);
        //     //JobSystem::Get().Wait(handle);
        //     s_cull_task_handle[cur_view_count++] = std::move(JobSystem::Get().Dispatch(job));

        // }
        // for(u32 i = 0; i < _cur_frame_packet->GetViewNum(); ++i)
        // {
        //     JobSystem::Get().Wait(s_cull_task_handle[i]);
        // }
    }
    void RenderPipeline::OnAddRenderObject(ECS::Entity e)
    {
        _pending_add_render_objs.push(e);
    }
    void RenderPipeline::CollectViews(const Scene&s)
    {
        const auto &r = s.GetRegister();
        for (auto &cam: r.View<ECS::CCamera>())
        {
            auto &view_entry = _cur_frame_packet->FetchViewEntity();
            ViewEntity::FormCamera(cam._camera, view_entry);
            _cur_frame_packet->CopyCameraConstBuffer(view_entry);
        }
        for (auto &light: r.View<ECS::LightComponent>())
        {
            for (u16 i = 0; i < light._shadow._shaodwcam_num; ++i)
            {
                auto &view_entry = _cur_frame_packet->FetchViewEntity();
                ViewEntity::FormCamera(light._shadow_cameras[i], view_entry);
                _cur_frame_packet->CopyCameraConstBuffer(view_entry);
            }
        }
    }
    void RenderPipeline::ProcessPendingRenderObjects()
    {
        const auto &r = g_pSceneMgr->ActiveScene()->GetRegister();
        while (!_pending_add_render_objs.empty())
        {
            auto e = _pending_add_render_objs.front();
            _pending_add_render_objs.pop();
            if (auto comp = r.GetComponent<ECS::StaticMeshComponent>(e); comp != nullptr)
            {
                _render_objs.emplace_back(e, true, false, comp->_p_mesh ? comp->_p_mesh.get() : nullptr, comp->_p_mats);
            }
            else if (auto comp = r.GetComponent<ECS::CSkeletonMesh>(e); comp != nullptr)
            {
                auto p = &(comp->_p_mats);
                _render_objs.emplace_back(e, true, true, comp->_p_mesh ? comp->_p_mesh.get() : nullptr, comp->_p_mats);
            }
            _render_obj_lut[(u32)e] = (u32)(_render_objs.size() - 1);
            // std::sort(_render_objs.begin(),_render_objs.end());
            // _dynamic_obj_start_pos = (u32)std::distance(_render_objs.begin(),std::find(_render_objs.begin(),_render_objs.end(),
            //     [](const RenderObject& obj)->bool{return obj._is_dynamic;}));
            auto &obj = _render_objs.back();
            _visiblity_objs.emplace_back(obj._id, 0u, obj._mesh ? obj._mesh->SubmeshCount() : 0u);
        }
    }
    void RenderPipeline::CullView(ViewEntity &view_entity)
    {
        CPUProfileBlock  block(std::format("CullView_{}",view_entity._cam_hash));
        const auto &cam_vf = view_entity._frustum;
        for (auto &vis_obj: _visiblity_objs)
        {
            if (ViewFrustum::Conatin(cam_vf, vis_obj._bounding_box) && vis_obj._layer_mask & view_entity._layer_mask)
            {
                if (RenderObject *render_obj = GetRenderObject(vis_obj._render_obj_handle); render_obj != nullptr)
                {
                    RenderNode node;
                    node._game_obj_handle = (u32)GetRenderObject(vis_obj._render_obj_handle)->_game_obj_handle;
                    node._sqr_distance = SqrMagnitude(render_obj->_world_pos - view_entity._position);
                    node._render_obj_handle = vis_obj._render_obj_handle;
                    node._submesh = vis_obj._submesh;
                    node._mesh = render_obj->_mesh;
                    node._mat = render_obj->_materials[vis_obj._submesh];
                    node._object_data._MatrixWorld = render_obj->_world_matrix;
                    node._object_data._MatrixInvWorld = MatrixInverse(render_obj->_world_matrix);
                    node._object_data._MatrixWorld_Pre = render_obj->_prev_world_matrix;
                    node._object_data._ObjectID = (i32)node._game_obj_handle;
                    node._object_data._MotionVectorParam.x = render_obj->_motion_type == ECS::EMotionVectorType::kPerObject ? 1.0f : 0.0f;//dynamic object
                    node._object_data._MotionVectorParam.y = render_obj->_motion_type == ECS::EMotionVectorType::kForceZero ? 1.0f : 0.0f;//force off
                    view_entity.PushRenderNode(node);
                }
            }
        }
        view_entity.SortRenderNode();
    }
    RenderObject *RenderPipeline::GetRenderObject(u32 render_obj_handle)
    {
        if (_render_obj_lut.contains(render_obj_handle))
            return &_render_objs[_render_obj_lut[render_obj_handle]];
        return nullptr;
    }

#pragma endregion

}// namespace Ailu
