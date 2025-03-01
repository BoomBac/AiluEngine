#include "Render/PickPass.h"
#include "Common/Selection.h"
#include "Framework/Common/Profiler.h"
#include "Render/CommandBuffer.h"
#include <Framework/Common/ResourceMgr.h>
#include <Render/Gizmo.h>
#include <Render/Renderer.h>


namespace Ailu
{
    namespace Editor
    {
        PickPass::PickPass() : RenderPass("PickPass")
        {
            _pick_gen = MakeScope<Material>(g_pResourceMgr->Get<Shader>(L"Shaders/hlsl/pick_buffer.hlsl"), "Runtime/PickGen");
            _select_gen = MakeScope<Material>(g_pResourceMgr->Get<Shader>(L"Shaders/hlsl/select_buffer.hlsl"), "Runtime/SelectGen");
            _editor_outline = MakeScope<Material>(g_pResourceMgr->Get<Shader>(L"Shaders/hlsl/editor_outline.hlsl"), "Runtime/EditorOutline");
            _event = (ERenderPassEvent::ERenderPassEvent)(ERenderPassEvent::kAfterPostprocess - 1);//before gizmo pass
        }
        PickPass::~PickPass()
        {
        }
        void PickPass::Setup(RenderTexture *pick_buf, RenderTexture *pick_buf_depth)
        {
            _color = pick_buf;
            _depth = pick_buf_depth;
        }
        void PickPass::Execute(GraphicsContext *context, RenderingData &rendering_data)
        {
            static auto mat_point_light = g_pResourceMgr->Get<Material>(L"Runtime/Material/PointLightBillboard");
            static auto mat_directional_light = g_pResourceMgr->Get<Material>(L"Runtime/Material/DirectionalLightBillboard");
            static auto mat_spot_light = g_pResourceMgr->Get<Material>(L"Runtime/Material/SpotLightBillboard");
            static auto mat_area_light = g_pResourceMgr->Get<Material>(L"Runtime/Material/AreaLightBillboard");
            static auto mat_camera = g_pResourceMgr->Get<Material>(L"Runtime/Material/CameraBillboard");
            static auto mat_gird_plane = g_pResourceMgr->Get<Material>(L"Runtime/Material/GridPlane");
            static auto mat_lightprobe = g_pResourceMgr->Get<Material>(L"Runtime/Material/LightProbeBillboard");

            auto cmd = CommandBufferPool::Get(_name);
            {
                PROFILE_BLOCK_GPU(cmd.get(), PickPass);
                ECS::Register &r = g_pSceneMgr->ActiveScene()->GetRegister();
                cmd->SetRenderTarget(rendering_data._camera_color_target_handle, rendering_data._camera_depth_target_handle);
                if (auto &selected = Selection::SelectedEntities(); selected.size() > 0)
                {
                    for (auto entity: selected)
                    {
                        const auto t = r.GetComponent<TransformComponent>(entity)->_transform;
                        if (auto comp = r.GetComponent<LightComponent>(entity); comp != nullptr)
                        {
                            DrawLightGizmo(t, comp);
                            continue;
                        }
                        else if (auto comp = r.GetComponent<CLightProbe>(entity); comp != nullptr)
                        {
                            //Gizmo::DrawCube(t._position, comp->_size);
                            Vector3f ext = Vector3f(comp->_size) * 0.5f;
                            Gizmo::DrawAABB(t._position - ext,t._position + ext);
                            cmd->DrawRenderer(Mesh::s_p_shpere.lock().get(), comp->_debug_material, t._world_matrix, 0, 0, 1);
                        }
                        else if (auto comp = r.GetComponent<CCamera>(entity); comp != nullptr)
                        {
                            Camera::DrawGizmo(&comp->_camera,Colors::kWhite);
                        }
                    }
                }
                //write to pick buffer
                cmd->SetRenderTarget(_color, _depth);
                cmd->ClearRenderTarget(_color, _depth, Colors::kBlack, kZFar);
                for (const auto &queue_data: *rendering_data._cull_results)
                {
                    auto &[queue, objs] = queue_data;
                    for (auto &obj: objs)
                    {
                        cmd->DrawRenderer(obj._mesh, _pick_gen.get(), (*rendering_data._p_per_object_cbuf)[obj._scene_id], obj._submesh_index, 0, obj._instance_count);
                    }
                }
                u16 entity_index = 0;
                for (auto &light_comp: r.View<LightComponent>())
                {
                    const auto &t = r.GetComponent<LightComponent, TransformComponent>(entity_index);
                    auto world_pos = t->_transform._position;
                    CBufferPerObjectData obj_data;
                    obj_data._ObjectID = r.GetEntity<LightComponent>(entity_index);
                    obj_data._MatrixWorld = MatrixTranslation(world_pos);
                    f32 scale = 2.0f;
                    obj_data._MatrixWorld = MatrixScale(scale, scale, scale) * obj_data._MatrixWorld;
                    switch (light_comp._type)
                    {
                        case ELightType::kDirectional:
                        {
                            cmd->DrawRenderer(Mesh::s_p_quad.lock().get(), mat_directional_light, obj_data, 0, 1, 1);
                        }
                        break;
                        case ELightType::kPoint:
                        {
                            cmd->DrawRenderer(Mesh::s_p_quad.lock().get(), mat_point_light, obj_data, 0, 1, 1);
                        }
                        break;
                        case ELightType::kSpot:
                        {
                            cmd->DrawRenderer(Mesh::s_p_quad.lock().get(), mat_spot_light, obj_data, 0, 1, 1);
                        }
                        break;
                        case ELightType::kArea:
                        {
                            cmd->DrawRenderer(Mesh::s_p_quad.lock().get(), mat_area_light, obj_data, 0, 1, 1);
                        }
                        break;
                    }
                    ++entity_index;
                }
                entity_index = 0;
                for (auto &light_comp: g_pSceneMgr->ActiveScene()->GetRegister().View<CLightProbe>())
                {
                    const auto &t = r.GetComponent<CLightProbe, TransformComponent>(entity_index);
                    auto world_pos = t->_transform._position;
                    CBufferPerObjectData obj_data;
                    obj_data._ObjectID = r.GetEntity<CLightProbe>(entity_index);
                    obj_data._MatrixWorld = MatrixTranslation(world_pos);
                    f32 scale = 2.0f;
                    obj_data._MatrixWorld = MatrixScale(scale, scale, scale) * obj_data._MatrixWorld;
                    cmd->DrawRenderer(Mesh::s_p_quad.lock().get(), mat_lightprobe, obj_data, 0, 1, 1);
                }
                entity_index = 0;
                for (auto &light_comp: g_pSceneMgr->ActiveScene()->GetRegister().View<CCamera>())
                {
                    const auto &t = g_pSceneMgr->ActiveScene()->GetRegister().GetComponent<CCamera, TransformComponent>(entity_index);
                    auto world_pos = t->_transform._position;
                    CBufferPerObjectData obj_data;
                    obj_data._ObjectID = r.GetEntity<CCamera>(entity_index);
                    obj_data._MatrixWorld = MatrixTranslation(world_pos);
                    f32 scale = 2.0f;
                    obj_data._MatrixWorld = MatrixScale(scale, scale, scale) * obj_data._MatrixWorld;
                    cmd->DrawRenderer(Mesh::s_p_quad.lock().get(), mat_camera, obj_data, 0, 1, 1);
                    ++entity_index;
                }
                //write to select buffer and draw debug outline
                if (auto &selected = Selection::SelectedEntities(); selected.size() > 0)
                {
                    RTHandle select_buf = cmd->GetTempRT(_color->Width(), _color->Height(), "select_buffer", ERenderTargetFormat::kDefault, false, false, false);
                    RTHandle select_buf_blur_temp = cmd->GetTempRT(_color->Width(), _color->Height(), "select_buf_blur_temp", ERenderTargetFormat::kDefault, false, false, false);
                    cmd->SetRenderTarget(select_buf, rendering_data._camera_depth_target_handle);
                    cmd->ClearRenderTarget(select_buf, Colors::kBlack);
                    cmd->ClearRenderTarget(select_buf_blur_temp, Colors::kBlack);
                    for (auto entity: selected)
                    {
                        const auto &t = r.GetComponent<TransformComponent>(entity)->_transform;
                        if (auto comp = r.GetComponent<StaticMeshComponent>(entity); comp != nullptr)
                        {
                            cmd->DrawRenderer(comp->_p_mesh.get(), _select_gen.get(), t._world_matrix, 0, 0, 1);
                            cmd->DrawRenderer(comp->_p_mesh.get(), _select_gen.get(), t._world_matrix, 0, 1, 1);
                        }
                        else if (auto comp = r.GetComponent<CSkeletonMesh>(entity); comp != nullptr)
                        {
                            cmd->DrawRenderer(comp->_p_mesh.get(), _select_gen.get(), t._world_matrix, 0, 0, 1);
                            cmd->DrawRenderer(comp->_p_mesh.get(), _select_gen.get(), t._world_matrix, 0, 1, 1);
                            Gizmo::DrawAABB(comp->_transformed_aabbs[0],Colors::kGreen);
                        }
                        if (auto c = r.GetComponent<CCollider>(entity))
                        {
                            DebugDrawer::DebugWireframe(*c,t);
                        }
                    }
                    _editor_outline->SetTexture("_SelectBuffer", select_buf);
                    _editor_outline->SetVector("_SelectBuffer_TexelSize", Vector4f(1.0f / _color->Width(), 1.0f / _color->Height(), (f32) _color->Width(), (f32) _color->Height()));
                    //generate outline
                    cmd->SetRenderTarget(select_buf_blur_temp);
                    cmd->DrawFullScreenQuad(_editor_outline.get());
                    //blur outline
                    _editor_outline->SetTexture("_SelectBuffer", select_buf_blur_temp);
                    cmd->SetRenderTarget(select_buf);
                    cmd->DrawFullScreenQuad(_editor_outline.get(),1);
                    //apply to color target
                    _editor_outline->SetTexture("_SelectBuffer", select_buf);
                    cmd->SetRenderTarget(rendering_data._camera_color_target_handle);
                    cmd->DrawFullScreenQuad(_editor_outline.get(),2);

                    cmd->ReleaseTempRT(select_buf);
                    cmd->ReleaseTempRT(select_buf_blur_temp);
                }
            }
            context->ExecuteCommandBuffer(cmd);
            CommandBufferPool::Release(cmd);
        }
        void PickPass::DrawLightGizmo(const Transform &transf, LightComponent *comp)
        {
            switch (comp->_type)
            {
                case Ailu::ELightType::kDirectional:
                {
                    Vector3f light_to = comp->_light._light_dir.xyz;
                    light_to.x *= 2.0f;
                    light_to.y *= 2.0f;
                    light_to.z *= 2.0f;
                    Vector3f points[5]{
                        Vector3f(0.0f, 0.0f, 0.0f),
                        Vector3f(-1.0f, 0.0f, 1.0f),
                        Vector3f(1.0f, 0.0f, 1.0f),
                        Vector3f(1.0f, 0.0f, -1.0f),
                        Vector3f(-1.0f, 0.0f, -1.0f)
                    };
                    auto mat = Transform::ToMatrix(transf);
                    for (u16 i = 0; i < 5; ++i)
                    {
                        TransformCoord(points[i], mat);
                        Gizmo::DrawLine(points[i], points[i] + light_to, comp->_light._light_color);
                    }
                    //Gizmo::DrawLine(transf._position, transf._position + light_to, comp->_light._light_color);
                    Gizmo::DrawCircle(transf._position, 0.5f, 24, comp->_light._light_color, Quaternion::ToMat4f(transf._rotation));
                    return;
                }
                case Ailu::ELightType::kPoint:
                {
                    Gizmo::DrawCircle(transf._position, comp->_light._light_param.x, 24, comp->_light._light_color, MatrixRotationX(ToRadius(90.0f)));
                    Gizmo::DrawCircle(transf._position, comp->_light._light_param.x, 24, comp->_light._light_color, MatrixRotationZ(ToRadius(90.0f)));
                    Gizmo::DrawCircle(transf._position, comp->_light._light_param.x, 24, comp->_light._light_color);
                    return;
                }
                case Ailu::ELightType::kSpot:
                {
                    float angleIncrement = 90.0;
                    auto rot_mat = Quaternion::ToMat4f(transf._rotation);
                    Vector3f light_to = comp->_light._light_dir.xyz;
                    light_to *= comp->_light._light_param.x;
                    {
                        Vector3f inner_center = light_to + transf._position;
                        float inner_radius = tan(ToRadius(comp->_light._light_param.y / 2.0f)) * comp->_light._light_param.x;
                        Gizmo::DrawLine(transf._position, transf._position + light_to * 10.f, Colors::kYellow);
                        Gizmo::DrawCircle(inner_center, inner_radius, 24, comp->_light._light_color, rot_mat);
                        for (int i = 0; i < 4; ++i)
                        {
                            float angle1 = ToRadius(angleIncrement * static_cast<float>(i));
                            float angle2 = ToRadius(angleIncrement * static_cast<float>(i + 1));
                            Vector3f point1(inner_center.x + inner_radius * cos(angle1), inner_center.y, inner_center.z + inner_radius * sin(angle1));
                            Vector3f point2(inner_center.x + inner_radius * cos(angle2), inner_center.y, inner_center.z + inner_radius * sin(angle2));
                            point1 -= inner_center;
                            point2 -= inner_center;
                            point1 = transf._rotation * point1;
                            point2 = transf._rotation * point2;
                            //TransformCoord(point1, rot_mat);
                            //TransformCoord(point2, rot_mat);
                            point1 += inner_center;
                            point2 += inner_center;
                            Gizmo::DrawLine(transf._position, point2, comp->_light._light_color);
                        }
                    }
                    light_to *= 0.9f;
                    {
                        Vector3f outer_center = light_to + transf._position;
                        float outer_radius = tan(ToRadius(comp->_light._light_param.z / 2.0f)) * comp->_light._light_param.x;
                        Gizmo::DrawCircle(outer_center, outer_radius, 24, comp->_light._light_color, rot_mat);
                        for (int i = 0; i < 4; ++i)
                        {
                            float angle1 = ToRadius(angleIncrement * static_cast<float>(i));
                            float angle2 = ToRadius(angleIncrement * static_cast<float>(i + 1));
                            Vector3f point1(outer_center.x + outer_radius * cos(angle1), outer_center.y, outer_center.z + outer_radius * sin(angle1));
                            Vector3f point2(outer_center.x + outer_radius * cos(angle2), outer_center.y, outer_center.z + outer_radius * sin(angle2));
                            point1 -= outer_center;
                            point2 -= outer_center;
                            TransformCoord(point1, rot_mat);
                            TransformCoord(point2, rot_mat);
                            point1 += outer_center;
                            point2 += outer_center;
                            Gizmo::DrawLine(transf._position, point2, comp->_light._light_color);
                        }
                    }
                    return;
                }
                case ELightType::kArea:
                {
                    Vector3f light_to = comp->_light._light_dir.xyz;
                    light_to.x *= comp->_light._light_param.x;
                    light_to.y *= comp->_light._light_param.x;
                    light_to.z *= comp->_light._light_param.x;
                    Gizmo::DrawLine(transf._position, transf._position + light_to, comp->_light._light_color);
                    if (comp->_light._light_param.w == 0.0f)
                    {
                        Vector3f points[4];
                        f32 w = comp->_light._light_param.y * 0.5f,h = comp->_light._light_param.z * 0.5f;
                        points[0].x = -w;
                        points[0].z = h;
                        points[1].x = w;
                        points[1].z = h;
                        points[2].x = w;
                        points[2].z = -h;
                        points[3].x = -w;
                        points[3].z = -h;
                        auto mat = Transform::ToMatrix(transf);
                        TransformCoord(points[0], mat);
                        TransformCoord(points[1], mat);
                        TransformCoord(points[2], mat);
                        TransformCoord(points[3], mat);
                        Gizmo::DrawLine(points[0], points[1], comp->_light._light_color);
                        Gizmo::DrawLine(points[1], points[2], comp->_light._light_color);
                        Gizmo::DrawLine(points[2], points[3], comp->_light._light_color);
                        Gizmo::DrawLine(points[3], points[0], comp->_light._light_color);
                    }
                    else
                    {
                        Gizmo::DrawCircle(transf._position, comp->_light._light_param.y, 24, comp->_light._light_color, Quaternion::ToMat4f(transf._rotation));
                    }
                }
            }
        }
        //-----------------------------------------------------------------------PickFeature----------------------------------------------------------------------------
        PickFeature::PickFeature() : RenderFeature("Pick")
        {
            _read_pickbuf = ComputeShader::Create(ResourceMgr::GetResSysPath(L"Shaders/hlsl/Compute/pick_buffer_reader.hlsl"));
            GPUBufferDesc desc{};
            desc._size = 256;
            desc._is_readable = true;
            desc._format = EALGFormat::kALGFormatR32_UINT;
            desc._is_random_write = true;
            _readback_buf = IGPUBuffer::Create(desc);
        }
        PickFeature::~PickFeature()
        {
            DESTORY_PTR(_readback_buf);
        }
        void PickFeature::AddRenderPasses(Renderer &renderer, RenderingData &rendering_data)
        {
            if (rendering_data._camera->_is_scene_camera)
            {
                if (_pick_buf == nullptr || _pick_buf->Width() != rendering_data._width || _pick_buf->Height() != rendering_data._height)
                {
                    _pick_buf = RenderTexture::Create(rendering_data._width, rendering_data._height, "pick_buffer", ERenderTargetFormat::kRUint, false, false, false);
                    _pick_buf_depth = RenderTexture::Create(rendering_data._width, rendering_data._height, "pick_buffer_depth", ERenderTargetFormat::kDepth, false, false, false);
                }
                if (_is_active)
                {
                    _pick.Setup(_pick_buf.get(), _pick_buf_depth.get());
                    renderer.EnqueuePass(&_pick);
                }
            }
        }
        u32 PickFeature::GetPickID(u16 x, u16 y) const
        {
            if (_is_active)
            {
                auto cmd = CommandBufferPool::Get("ReadbackPickBuffer");
                auto kernel = _read_pickbuf->FindKernel("cs_main");
                _read_pickbuf->SetTexture("_PickBuffer", _pick_buf.get());
                _read_pickbuf->SetBuffer("_PickResult", _readback_buf);
                _read_pickbuf->SetVector("pixel_pos", Vector4f((f32) x, (f32) y, 0.0f, 0.0f));
                cmd->Dispatch(_read_pickbuf.get(), kernel, 1, 1, 1);
                g_pGfxContext->ExecuteAndWaitCommandBuffer(cmd);
                CommandBufferPool::Release(cmd);
                u8 data[256];
                _readback_buf->ReadBack(data, 256);
                return *reinterpret_cast<u32 *>(data);
            }
            else
            {
                LOG_WARNING("Pick feature not active!");
                return -1;
            }
        }
        //-----------------------------------------------------------------------PickFeature----------------------------------------------------------------------------
    };//namespace Editor
};// namespace Ailu