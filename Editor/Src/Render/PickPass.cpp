#include "Render/PickPass.h"
#include "Common/Selection.h"
#include "Framework/Common/Profiler.h"
#include "Render/CommandBuffer.h"
#include <Framework/Common/ResourceMgr.h>
#include <Render/Gizmo.h>
#include <Render/Renderer.h>
#include "Render/RenderGraph/RenderGraph.h"

namespace Ailu
{
    using namespace Editor;
    namespace Render
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
        void PickPass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
        {
            static auto mat_point_light = g_pResourceMgr->Get<Material>(L"Runtime/Material/PointLightBillboard");
            static auto mat_directional_light = g_pResourceMgr->Get<Material>(L"Runtime/Material/DirectionalLightBillboard");
            static auto mat_spot_light = g_pResourceMgr->Get<Material>(L"Runtime/Material/SpotLightBillboard");
            static auto mat_area_light = g_pResourceMgr->Get<Material>(L"Runtime/Material/AreaLightBillboard");
            static auto mat_camera = g_pResourceMgr->Get<Material>(L"Runtime/Material/CameraBillboard");
            static auto mat_gird_plane = g_pResourceMgr->Get<Material>(L"Runtime/Material/GridPlane");
            static auto mat_lightprobe = g_pResourceMgr->Get<Material>(L"Runtime/Material/LightProbeBillboard");
            static RDG::RGHandle color, depth;
            graph.AddPass("PickBuffer", RDG::PassDesc(), [&](RDG::RenderGraphBuilder &builder)
                          {
                            color = builder.Import(_color);
                            depth = builder.Import(_depth);
                            color = builder.Write(color);
                            depth = builder.Write(depth,EResourceUsage::kDSV);
                }, [this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &rendering_data)
                {
                 cmd->SetRenderTarget(color, depth);
                ECS::Register &r = g_pSceneMgr->ActiveScene()->GetRegister();
             for (const auto &queue_data: *rendering_data._cull_results)
                {
                    auto &[queue, objs] = queue_data;
                    for (auto &obj: objs)
                    {
                        cmd->DrawMesh(obj._mesh, _pick_gen.get(), (*rendering_data._p_per_object_cbuf)[obj._scene_id], obj._submesh_index, 0, obj._instance_count);
                    }
                }
                if (auto &selected = Editor::Selection::SelectedEntities(); selected.size() > 0)
                {
                    for (auto entity: selected)
                    {
                        const auto t = r.GetComponent<ECS::TransformComponent>(entity)->_transform;
                        if (auto comp = r.GetComponent<ECS::LightComponent>(entity); comp != nullptr)
                        {
                            DrawLightGizmo(t, comp);
                            continue;
                        }
                        else if (auto comp = r.GetComponent<ECS::CLightProbe>(entity); comp != nullptr)
                        {
                            //Gizmo::DrawCube(t._position, comp->_size);
                            Vector3f ext = Vector3f(comp->_size) * 0.5f;
                            Gizmo::DrawAABB(t._position - ext, t._position + ext);
                            cmd->DrawMesh(Mesh::s_sphere.lock().get(), comp->_debug_material, t._world_matrix, 0, 0, 1);
                        }
                        else if (auto comp = r.GetComponent<ECS::CCamera>(entity); comp != nullptr)
                        {
                            Camera::DrawGizmo(&comp->_camera, Colors::kWhite);
                        }
                    }
                }
                u16 entity_index = 0;
                for (auto &light_comp: r.View<ECS::LightComponent>())
                {
                    const auto &t = r.GetComponent<ECS::LightComponent, ECS::TransformComponent>(entity_index);
                    auto world_pos = t->_transform._position;
                    CBufferPerObjectData obj_data;
                    obj_data._ObjectID = (i32)r.GetEntity<ECS::LightComponent>(entity_index);
                    obj_data._MatrixWorld = MatrixTranslation(world_pos);
                    f32 scale = 2.0f;
                    obj_data._MatrixWorld = MatrixScale(scale, scale, scale) * obj_data._MatrixWorld;
                    switch (light_comp._type)
                    {
                        case ECS::ELightType::kDirectional:
                        {
                            cmd->DrawMesh(Mesh::s_quad.lock().get(), mat_directional_light, obj_data, 0, 1, 1);
                        }
                        break;
                        case ECS::ELightType::kPoint:
                        {
                            cmd->DrawMesh(Mesh::s_quad.lock().get(), mat_point_light, obj_data, 0, 1, 1);
                        }
                        break;
                        case ECS::ELightType::kSpot:
                        {
                            cmd->DrawMesh(Mesh::s_quad.lock().get(), mat_spot_light, obj_data, 0, 1, 1);
                        }
                        break;
                        case ECS::ELightType::kArea:
                        {
                            cmd->DrawMesh(Mesh::s_quad.lock().get(), mat_area_light, obj_data, 0, 1, 1);
                        }
                        break;
                    }
                    ++entity_index;
                }
                entity_index = 0;
                for (auto &light_comp: g_pSceneMgr->ActiveScene()->GetRegister().View<ECS::CLightProbe>())
                {
                    const auto &t = r.GetComponent<ECS::CLightProbe, ECS::TransformComponent>(entity_index);
                    auto world_pos = t->_transform._position;
                    CBufferPerObjectData obj_data;
                    obj_data._ObjectID = (i32)r.GetEntity<ECS::CLightProbe>(entity_index);
                    obj_data._MatrixWorld = MatrixTranslation(world_pos);
                    f32 scale = 2.0f;
                    obj_data._MatrixWorld = MatrixScale(scale, scale, scale) * obj_data._MatrixWorld;
                    cmd->DrawMesh(Mesh::s_quad.lock().get(), mat_lightprobe, obj_data, 0, 1, 1);
                }
                entity_index = 0;
                for (auto &light_comp: g_pSceneMgr->ActiveScene()->GetRegister().View<ECS::CCamera>())
                {
                    const auto &t = g_pSceneMgr->ActiveScene()->GetRegister().GetComponent<ECS::CCamera, ECS::TransformComponent>(entity_index);
                    auto world_pos = t->_transform._position;
                    CBufferPerObjectData obj_data;
                    obj_data._ObjectID = (i32)r.GetEntity<ECS::CCamera>(entity_index);
                    obj_data._MatrixWorld = MatrixTranslation(world_pos);
                    f32 scale = 2.0f;
                    obj_data._MatrixWorld = MatrixScale(scale, scale, scale) * obj_data._MatrixWorld;
                    cmd->DrawMesh(Mesh::s_quad.lock().get(), mat_camera, obj_data, 0, 1, 1);
                    ++entity_index;
                }
            });
            static RDG::RGHandle select_buf,select_buf_blur;
            if (Selection::SelectedEntities().size() == 0)
                return;
            graph.AddPass("SelectBuffer", RDG::PassDesc(), [&](RDG::RenderGraphBuilder &builder)
                          {
                    TextureDesc desc(_color->Width(), _color->Height(),ERenderTargetFormat::kDefault);
                    select_buf = builder.AllocTexture(desc,"select_buffer");
                    select_buf = builder.Write(select_buf); }, [this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &rendering_data)
                          {
                if (auto &selected = Selection::SelectedEntities(); selected.size() > 0)
                {
                    cmd->SetRenderTarget(select_buf);
                    ECS::Register &r = g_pSceneMgr->ActiveScene()->GetRegister();
                    for (auto entity: selected)
                    {
                        const auto &t = r.GetComponent<ECS::TransformComponent>(entity)->_transform;
                        if (auto comp = r.GetComponent<ECS::StaticMeshComponent>(entity); comp != nullptr)
                        {
                            cmd->DrawMesh(comp->_p_mesh.get(), _select_gen.get(), t._world_matrix, 0, 0, 1);
                            cmd->DrawMesh(comp->_p_mesh.get(), _select_gen.get(), t._world_matrix, 0, 1, 1);
                        }
                        else if (auto comp = r.GetComponent<ECS::CSkeletonMesh>(entity); comp != nullptr)
                        {
                            cmd->DrawMesh(comp->_p_mesh.get(), _select_gen.get(), t._world_matrix, 0, 0, 1);
                            cmd->DrawMesh(comp->_p_mesh.get(), _select_gen.get(), t._world_matrix, 0, 1, 1);
                            Gizmo::DrawAABB(comp->_transformed_aabbs[0], Colors::kGreen);
                        }
                        if (auto c = r.GetComponent<ECS::CCollider>(entity))
                        {
                            DebugDrawer::DebugWireframe(*c, t);
                        }
                    }
                } });
            _editor_outline->SetVector("_SelectBuffer_TexelSize", Vector4f(1.0f / _color->Width(), 1.0f / _color->Height(), (f32) _color->Width(), (f32) _color->Height()));

            graph.AddPass("GenerateOutline", RDG::PassDesc(), [&](RDG::RenderGraphBuilder &builder)
                          {
                    TextureDesc desc(_color->Width(), _color->Height(),ERenderTargetFormat::kDefault);
                    select_buf_blur = builder.AllocTexture(desc,"select_buffer_blur");
                    builder.Read(select_buf);
                    select_buf_blur = builder.Write(select_buf_blur);
                }, [this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &rendering_data) { 
                    _editor_outline->SetTexture("_SelectBuffer", graph.Resolve<Texture>(select_buf));
                    cmd->SetRenderTarget(select_buf_blur);
                    cmd->DrawFullScreenQuad(_editor_outline.get());
                });
            graph.AddPass("BlurOutline", RDG::PassDesc(), [&](RDG::RenderGraphBuilder &builder)
                          {
                    builder.Read(select_buf_blur);
                    select_buf = builder.Write(select_buf); }, [this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &rendering_data)
                          { 
                    _editor_outline->SetTexture("_SelectBuffer", graph.Resolve<Texture>(select_buf_blur));
                    cmd->SetRenderTarget(select_buf);
                    cmd->DrawFullScreenQuad(_editor_outline.get(),1); 
                });
            graph.AddPass("ApplyOutline", RDG::PassDesc(), [&](RDG::RenderGraphBuilder &builder)
                          {
                    builder.Read(select_buf);
                    builder.Read(rendering_data._rg_handles._color_target);
                    rendering_data._rg_handles._color_target = builder.Write(rendering_data._rg_handles._color_target); 
                }, [this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &rendering_data)
                          { 
                    _editor_outline->SetTexture("_SelectBuffer", graph.Resolve<Texture>(select_buf));
                    cmd->SetRenderTarget(rendering_data._rg_handles._color_target);
                    cmd->DrawFullScreenQuad(_editor_outline.get(),2); });
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
                if (auto &selected = Editor::Selection::SelectedEntities(); selected.size() > 0)
                {
                    for (auto entity: selected)
                    {
                        const auto t = r.GetComponent<ECS::TransformComponent>(entity)->_transform;
                        if (auto comp = r.GetComponent<ECS::LightComponent>(entity); comp != nullptr)
                        {
                            DrawLightGizmo(t, comp);
                            continue;
                        }
                        else if (auto comp = r.GetComponent<ECS::CLightProbe>(entity); comp != nullptr)
                        {
                            //Gizmo::DrawCube(t._position, comp->_size);
                            Vector3f ext = Vector3f(comp->_size) * 0.5f;
                            Gizmo::DrawAABB(t._position - ext,t._position + ext);
                            cmd->DrawMesh(Mesh::s_sphere.lock().get(), comp->_debug_material, t._world_matrix, 0, 0, 1);
                        }
                        else if (auto comp = r.GetComponent<ECS::CCamera>(entity); comp != nullptr)
                        {
                            Camera::DrawGizmo(&comp->_camera,Colors::kWhite);
                        }
                    }
                }
                //write to pick buffer
                cmd->SetRenderTarget(_color, _depth);
                //cmd->ClearRenderTarget(_color, _depth, Colors::kBlack, kZFar);
                for (const auto &queue_data: *rendering_data._cull_results)
                {
                    auto &[queue, objs] = queue_data;
                    for (auto &obj: objs)
                    {
                        cmd->DrawMesh(obj._mesh, _pick_gen.get(), (*rendering_data._p_per_object_cbuf)[obj._scene_id], obj._submesh_index, 0, obj._instance_count);
                    }
                }
                u16 entity_index = 0;
                for (auto &light_comp: r.View<ECS::LightComponent>())
                {
                    const auto &t = r.GetComponent<ECS::LightComponent, ECS::TransformComponent>(entity_index);
                    auto world_pos = t->_transform._position;
                    CBufferPerObjectData obj_data;
                    obj_data._ObjectID = (i32)r.GetEntity<ECS::LightComponent>(entity_index);
                    obj_data._MatrixWorld = MatrixTranslation(world_pos);
                    f32 scale = 2.0f;
                    obj_data._MatrixWorld = MatrixScale(scale, scale, scale) * obj_data._MatrixWorld;
                    switch (light_comp._type)
                    {
                        case ECS::ELightType::kDirectional:
                        {
                            cmd->DrawMesh(Mesh::s_quad.lock().get(), mat_directional_light, obj_data, 0, 1, 1);
                        }
                        break;
                        case ECS::ELightType::kPoint:
                        {
                            cmd->DrawMesh(Mesh::s_quad.lock().get(), mat_point_light, obj_data, 0, 1, 1);
                        }
                        break;
                        case ECS::ELightType::kSpot:
                        {
                            cmd->DrawMesh(Mesh::s_quad.lock().get(), mat_spot_light, obj_data, 0, 1, 1);
                        }
                        break;
                        case ECS::ELightType::kArea:
                        {
                            cmd->DrawMesh(Mesh::s_quad.lock().get(), mat_area_light, obj_data, 0, 1, 1);
                        }
                        break;
                    }
                    ++entity_index;
                }
                entity_index = 0;
                for (auto &light_comp: g_pSceneMgr->ActiveScene()->GetRegister().View<ECS::CLightProbe>())
                {
                    const auto &t = r.GetComponent<ECS::CLightProbe, ECS::TransformComponent>(entity_index);
                    auto world_pos = t->_transform._position;
                    CBufferPerObjectData obj_data;
                    obj_data._ObjectID = (i32)r.GetEntity<ECS::CLightProbe>(entity_index);
                    obj_data._MatrixWorld = MatrixTranslation(world_pos);
                    f32 scale = 2.0f;
                    obj_data._MatrixWorld = MatrixScale(scale, scale, scale) * obj_data._MatrixWorld;
                    cmd->DrawMesh(Mesh::s_quad.lock().get(), mat_lightprobe, obj_data, 0, 1, 1);
                }
                entity_index = 0;
                for (auto &light_comp: g_pSceneMgr->ActiveScene()->GetRegister().View<ECS::CCamera>())
                {
                    const auto &t = g_pSceneMgr->ActiveScene()->GetRegister().GetComponent<ECS::CCamera, ECS::TransformComponent>(entity_index);
                    auto world_pos = t->_transform._position;
                    CBufferPerObjectData obj_data;
                    obj_data._ObjectID = (i32)r.GetEntity<ECS::CCamera>(entity_index);
                    obj_data._MatrixWorld = MatrixTranslation(world_pos);
                    f32 scale = 2.0f;
                    obj_data._MatrixWorld = MatrixScale(scale, scale, scale) * obj_data._MatrixWorld;
                    cmd->DrawMesh(Mesh::s_quad.lock().get(), mat_camera, obj_data, 0, 1, 1);
                    ++entity_index;
                }
                //write to select buffer and draw debug outline
                if (auto &selected = Selection::SelectedEntities(); selected.size() > 0)
                {
                    RTHandle select_buf = cmd->GetTempRT(_color->Width(), _color->Height(), "select_buffer", ERenderTargetFormat::kDefault, false, false, false);
                    RTHandle select_buf_blur_temp = cmd->GetTempRT(_color->Width(), _color->Height(), "select_buf_blur_temp", ERenderTargetFormat::kDefault, false, false, false);
                    cmd->SetRenderTarget(select_buf, rendering_data._camera_depth_target_handle);
                    for (auto entity: selected)
                    {
                        const auto &t = r.GetComponent<ECS::TransformComponent>(entity)->_transform;
                        if (auto comp = r.GetComponent<ECS::StaticMeshComponent>(entity); comp != nullptr)
                        {
                            cmd->DrawMesh(comp->_p_mesh.get(), _select_gen.get(), t._world_matrix, 0, 0, 1);
                            cmd->DrawMesh(comp->_p_mesh.get(), _select_gen.get(), t._world_matrix, 0, 1, 1);
                        }
                        else if (auto comp = r.GetComponent<ECS::CSkeletonMesh>(entity); comp != nullptr)
                        {
                            cmd->DrawMesh(comp->_p_mesh.get(), _select_gen.get(), t._world_matrix, 0, 0, 1);
                            cmd->DrawMesh(comp->_p_mesh.get(), _select_gen.get(), t._world_matrix, 0, 1, 1);
                            Gizmo::DrawAABB(comp->_transformed_aabbs[0],Colors::kGreen);
                        }
                        if (auto c = r.GetComponent<ECS::CCollider>(entity))
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
        void PickPass::DrawLightGizmo(const Transform &transf, ECS::LightComponent *comp)
        {
            switch (comp->_type)
            {
                case Ailu::ECS::ELightType::kDirectional:
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
                case Ailu::ECS::ELightType::kPoint:
                {
                    Gizmo::DrawCircle(transf._position, comp->_light._light_param.x, 24, comp->_light._light_color, MatrixRotationX(ToRadius(90.0f)));
                    Gizmo::DrawCircle(transf._position, comp->_light._light_param.x, 24, comp->_light._light_color, MatrixRotationZ(ToRadius(90.0f)));
                    Gizmo::DrawCircle(transf._position, comp->_light._light_param.x, 24, comp->_light._light_color);
                    return;
                }
                case Ailu::ECS::ELightType::kSpot:
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
                case ECS::ELightType::kArea:
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
            BufferDesc desc{};
            desc._size = 256;
            desc._is_readable = true;
            desc._format = EALGFormat::kALGFormatR32_UINT;
            desc._is_random_write = true;
            desc._target = EGPUBufferTarget::kCopyDestination;
            _readback_buf = GPUBuffer::Create(desc);
        }
        PickFeature::~PickFeature()
        {

        }
        void Ailu::Render::PickFeature::AddRenderPasses(Renderer &renderer, const RenderingData &rendering_data)
        {
            if (rendering_data._camera->_is_scene_camera)
            {
                if (_pick_buf == nullptr || _pick_buf->Width() != rendering_data._width || _pick_buf->Height() != rendering_data._height)
                {
                    _pick_buf = RenderTexture::Create(rendering_data._width, rendering_data._height, "pick_buffer", ERenderTargetFormat::kRUint, false, false, false);
                    _pick_buf->_store_action = ELoadStoreAction::kClear;
                    _pick_buf_depth = RenderTexture::Create(rendering_data._width, rendering_data._height, "pick_buffer_depth", ERenderTargetFormat::kDepth, false, false, false);
                    _pick_buf_depth->_store_action = ELoadStoreAction::kClear;
                }
                if (_is_active)
                {
                    _pick.Setup(_pick_buf.get(), _pick_buf_depth.get());
                    renderer.EnqueuePass(&_pick);
                }
            }
        }
        void PickFeature::GetPickID(u16 x, u16 y, std::function<void(u32)> on_value_get) const
        {
            if (_is_active)
            {
                auto cmd = CommandBufferPool::Get("ReadbackPickBuffer");
                auto kernel = _read_pickbuf->FindKernel("cs_main");
                _read_pickbuf->SetTexture("_PickBuffer", _pick_buf.get());
                _read_pickbuf->SetBuffer("_PickResult", _readback_buf.get());
                _read_pickbuf->SetVector("pixel_pos", Vector4f((f32) x, (f32) y, 0.0f, 0.0f));
                cmd->Dispatch(_read_pickbuf.get(), kernel, 1, 1, 1);
                cmd->ReadbackBuffer(_readback_buf.get(), false, 4u, [on_value_get](const u8 *data, u32 size)
                                    { on_value_get(*(u32 *) data);
                    });
                GraphicsContext::Get().ExecuteCommandBuffer(cmd);
                CommandBufferPool::Release(cmd);
            }
            else
            {
                LOG_WARNING("Pick feature not active!");
            }
        }
        //-----------------------------------------------------------------------PickFeature----------------------------------------------------------------------------
    };//namespace Editor
};// namespace Ailu