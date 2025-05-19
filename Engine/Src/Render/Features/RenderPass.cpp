#include "Render/Features/RenderFeature.h"
#include "Framework/Common/Profiler.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/Buffer.h"
#include "Render/CommandBuffer.h"
#include "Render/Gizmo.h"
#include "Render/GraphicsPipelineStateObject.h"
#include "Render/RenderConstants.h"
#include "Render/RenderQueue.h"
#include "Render/RenderingData.h"
#include "Render/TextRenderer.h"
#include "pch.h"

#include "Render/Renderer.h"
/* 模版说明
0 = 天空，大气和体积云绘制时会与其进行相等判断
1 = 静态物体
2 = 动态物体，Camera MotionVector绘制时会与其进行小于判断
*/

namespace Ailu::Render
{
    #pragma region RenderFeature
    //-------------------------------------------------------------RenderFeature-------------------------------------------------------------
    RenderFeature::RenderFeature(const String &name) : Object(name)
    {
    }


    RenderFeature::~RenderFeature()
    {
    }
    #pragma endregion
    //-------------------------------------------------------------RenderFeature-------------------------------------------------------------


    #pragma region RenderPass
    //-------------------------------------------------------------RenderPass-------------------------------------------------------------
    void RenderPass::EndPass(GraphicsContext *context)
    {
        RenderTexture::ResetRenderTarget();
    }
    #pragma endregion
    #pragma region ForwardPass
    //-------------------------------------------------------------OpaqueRenderPass-------------------------------------------------------------
    ForwardPass::ForwardPass() : RenderPass("TransparentPass")
    {
        shader_state_mat = MakeRef<Material>(g_pResourceMgr->Get<Shader>(L"Shaders/hlsl/debug.hlsl"), "ShaderStateDebug");
        _error_shader_pass_id = 0;
        _compiling_shader_pass_id = 1;
        _forward_lit_shader = g_pResourceMgr->Get<Shader>(L"Shaders/forwardlit.alasset");
        AL_ASSERT(_forward_lit_shader != nullptr);
        _event = (ERenderPassEvent::ERenderPassEvent)(ERenderPassEvent::kBeforeTransparent + 25u);
    }
    ForwardPass::~ForwardPass()
    {
    }
    void ForwardPass::BeginPass(GraphicsContext *context)
    {
        //for (auto& obj : RenderQueue::GetTransparentRenderables())
        //{
        //	if (!_transparent_replacement_materials.contains(obj.GetMaterial()->ID()))
        //	{
        //		auto mat = MakeRef<Material>(*(obj.GetMaterial()));
        //		mat->ChangeShader(_forward_lit_shader);
        //		_transparent_replacement_materials.emplace(std::make_pair(obj.GetMaterial()->ID(), mat));
        //	}
        //}
    }
    void ForwardPass::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        auto &all_renderable = *rendering_data._cull_results;
        u32 lowerBound = Shader::kRenderQueueOpaque, upperBound = Shader::kRenderQueueEnd;
        auto filtered = all_renderable | std::views::filter([lowerBound, upperBound](const auto &kv)
                                                            { return kv.first >= lowerBound && kv.first <= upperBound; }) |
                        std::views::values;
        u32 obj_num = 0;
        for (auto &r: filtered)
        {
            obj_num += (u32)r.size();
        }
        if (obj_num == 0)
            return;
        auto cmd = CommandBufferPool::Get(_name);
        {
            GpuProfileBlock b(cmd.get(), _name);
            cmd->SetRenderTarget(rendering_data._camera_color_target_handle, rendering_data._camera_depth_target_handle);
            for (auto &it: all_renderable)
            {
                auto &[queue, objs] = it;
                if (queue >= Shader::kRenderQueueTransparent)
                {
                    for (auto &obj: objs)
                    {
                        if (obj._material == nullptr)
                            cmd->DrawMesh(obj._mesh, shader_state_mat.get(), (*rendering_data._p_per_object_cbuf)[obj._scene_id], obj._submesh_index, _error_shader_pass_id, obj._instance_count);
                        else
                            cmd->DrawMesh(obj._mesh, obj._material, (*rendering_data._p_per_object_cbuf)[obj._scene_id], obj._submesh_index, 0, obj._instance_count);
                    }
                }
            }
        }
        context->ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
    }
    void ForwardPass::EndPass(GraphicsContext *context)
    {
        RenderPass::EndPass(context);
    }
    #pragma endregion

    #pragma region ShadowCastPass
    //-------------------------------------------------------------OpaqueRenderPass-------------------------------------------------------------

    //-------------------------------------------------------------ShadowCastPass-------------------------------------------------------------
    ShadowCastPass::ShadowCastPass() : RenderPass("ShadowCastPass")
    {
        auto shadow_map_size = QuailtySetting::s_cascade_shaodw_map_resolution;
        _p_mainlight_shadow_map = RenderTexture::Create(shadow_map_size, shadow_map_size, RenderConstants::kMaxCascadeShadowMapSplitNum, "MainLightShadowMap", ERenderTargetFormat::kShadowMap);
        _p_addlight_shadow_maps = RenderTexture::Create(shadow_map_size >> 1, shadow_map_size >> 1, RenderConstants::kMaxSpotLightNum + RenderConstants::kMaxAreaLightNum, "AddLightShadowMaps", ERenderTargetFormat::kShadowMap);
        _p_point_light_shadow_maps = RenderTexture::Create(shadow_map_size >> 1, "PointLightShadowMap", ERenderTargetFormat::kShadowMap, RenderConstants::kMaxPointLightNum);
        _event = (ERenderPassEvent::ERenderPassEvent)(ERenderPassEvent::kBeforeShaodwMap + 25u);
        _p_mainlight_shadow_map->_store_action = ELoadStoreAction::kClear;
        _p_addlight_shadow_maps->_store_action = ELoadStoreAction::kClear;
        _p_point_light_shadow_maps->_store_action = ELoadStoreAction::kClear;
    }

    void ShadowCastPass::BeginPass(GraphicsContext *context)
    {
    }
    void ShadowCastPass::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        auto cmd = CommandBufferPool::Get("MainLightShadowCastPass");
        cmd->Clear();
        {
            GpuProfileBlock profile(cmd.get(), _name);
            u32 obj_index = 0u;
            CBufferPerCameraData camera_data;
            //方向光阴影，只有一个
            if (rendering_data._cascade_shadow_data[0]._shadowmap_index >= 0)
            {
                for (int i = 0; i < QuailtySetting::s_cascade_shadow_map_count; i++)
                {
                    u16 dsv_rt_index = _p_mainlight_shadow_map->CalculateViewIndex(Texture::ETextureViewType::kDSV, 0, i);
                    cmd->SetRenderTarget(nullptr, _p_mainlight_shadow_map.get(), 0, dsv_rt_index);
                    cmd->ClearRenderTarget(kZFar,0u);
                    camera_data._MatrixVP = rendering_data._cascade_shadow_data[i]._shadow_matrix;
                    cmd->SetGlobalBuffer(RenderConstants::kCBufNamePerCamera, &camera_data, RenderConstants::kPerCameraDataSize);
                    for (auto &it: *rendering_data._cascade_shadow_data[i]._cull_results)
                    {
                        auto &[queue, objs] = it;
                        for (auto &obj: objs)
                        {
                            if (i16 shadow_pass = obj._material->GetShader()->FindPass("ShadowCaster"); shadow_pass != -1)
                            {
                                obj._material->DisableKeyword("CAST_POINT_SHADOW");
                                cmd->DrawMesh(obj._mesh, obj._material, (*rendering_data._p_per_object_cbuf)[obj._scene_id], obj._submesh_index, (u16) shadow_pass, obj._instance_count);
                            }
                        }
                    }
                }
            }
            //投灯阴影
            if (rendering_data._addi_shadow_num > 0)
            {
                for (int i = 0; i < RenderConstants::kMaxSpotLightNum; i++)
                {
                    auto &shadow_data = rendering_data._spot_shadow_data[i];
                    if (shadow_data._shadowmap_index < 0)
                        continue;
                    u16 dsv_rt_index = _p_addlight_shadow_maps->CalculateViewIndex(Texture::ETextureViewType::kDSV, 0, shadow_data._shadowmap_index);
                    cmd->SetRenderTarget(nullptr, _p_addlight_shadow_maps.get(), 0, dsv_rt_index);
                    cmd->ClearRenderTarget(kZFar,0u);
                    camera_data._MatrixVP = rendering_data._spot_shadow_data[i]._shadow_matrix;
                    cmd->SetGlobalBuffer(RenderConstants::kCBufNamePerCamera, &camera_data, RenderConstants::kPerCameraDataSize);
                    for (auto &it: *rendering_data._spot_shadow_data[i]._cull_results)
                    {
                        auto &[queue, objs] = it;
                        for (auto &obj: objs)
                        {
                            if (i16 shadow_pass = obj._material->GetShader()->FindPass("ShadowCaster"); shadow_pass != -1)
                            {
                                obj._material->DisableKeyword("CAST_POINT_SHADOW");
                                cmd->DrawMesh(obj._mesh, obj._material, (*rendering_data._p_per_object_cbuf)[obj._scene_id], obj._submesh_index, (u16) shadow_pass, obj._instance_count);
                            }
                        }
                    }
                    obj_index = 0;
                }
                for (int i = 0; i < RenderConstants::kMaxAreaLightNum; i++)
                {
                    auto &shadow_data = rendering_data._area_shadow_data[i];
                    if (shadow_data._shadowmap_index < 0)
                        continue;
                    u16 dsv_rt_index = _p_addlight_shadow_maps->CalculateViewIndex(Texture::ETextureViewType::kDSV, 0, shadow_data._shadowmap_index);
                    cmd->SetRenderTarget(nullptr, _p_addlight_shadow_maps.get(), 0, dsv_rt_index);
                    cmd->ClearRenderTarget(kZFar,0u);
                    camera_data._MatrixVP = rendering_data._area_shadow_data[i]._shadow_matrix;
                    cmd->SetGlobalBuffer(RenderConstants::kCBufNamePerCamera, &camera_data, RenderConstants::kPerCameraDataSize);
                    for (auto &it: *rendering_data._area_shadow_data[i]._cull_results)
                    {
                        auto &[queue, objs] = it;
                        for (auto &obj: objs)
                        {
                            if (i16 shadow_pass = obj._material->GetShader()->FindPass("ShadowCaster"); shadow_pass != -1)
                            {
                                obj._material->DisableKeyword("CAST_POINT_SHADOW");
                                cmd->DrawMesh(obj._mesh, obj._material, (*rendering_data._p_per_object_cbuf)[obj._scene_id], obj._submesh_index, (u16) shadow_pass, obj._instance_count);
                            }
                        }
                    }
                    obj_index = 0;
                }
            }
            //点光阴影
            if (rendering_data._addi_point_shadow_num > 0)
            {
                static const u32 pointshadow_mat_start = RenderConstants::kMaxCascadeShadowMapSplitNum + RenderConstants::kMaxSpotLightNum;
                for (int i = 0; i < RenderConstants::kMaxPointLightNum; i++)
                {
                    auto &shadow_data = rendering_data._point_shadow_data[i];
                    if (shadow_data._shadowmap_index < 0)
                        continue;
                    for (int j = 0; j < 6; j++)
                    {
                        //j + i * 6 定位到cubearray
                        u16 per_cube_slice_index = j + shadow_data._shadowmap_index * 6;
                        u16 dsv_rt_index = _p_point_light_shadow_maps->CalculateViewIndex(Texture::ETextureViewType::kDSV, (ECubemapFace::ECubemapFace)(j + 1), 0, shadow_data._shadowmap_index);
                        cmd->SetRenderTarget(nullptr, _p_point_light_shadow_maps.get(), 0, dsv_rt_index);
                        cmd->ClearRenderTarget(kZFar,0u);
                        Vector4f light_pos = rendering_data._point_shadow_data[shadow_data._shadowmap_index]._light_world_pos;
                        camera_data._CameraPos.xyz = light_pos.xyz;
                        light_pos.x = rendering_data._point_shadow_data[shadow_data._shadowmap_index]._camera_near;
                        light_pos.y = rendering_data._point_shadow_data[shadow_data._shadowmap_index]._camera_far;
                        camera_data._MatrixVP = rendering_data._point_shadow_data[shadow_data._shadowmap_index]._shadow_matrices[j];
                        camera_data._ZBufferParams = light_pos;
                        cmd->SetGlobalBuffer(RenderConstants::kCBufNamePerCamera, &camera_data, RenderConstants::kPerCameraDataSize);
                        for (auto &it: *rendering_data._point_shadow_data[shadow_data._shadowmap_index]._cull_results[j])
                        {
                            auto &[queue, objs] = it;
                            for (auto &obj: objs)
                            {
                                if (i16 shadow_pass = obj._material->GetShader()->FindPass("ShadowCaster"); shadow_pass != -1)
                                {
                                    obj._material->EnableKeyword("CAST_POINT_SHADOW");
                                    cmd->DrawMesh(obj._mesh, obj._material, (*rendering_data._p_per_object_cbuf)[obj._scene_id], obj._submesh_index, (u16) shadow_pass, obj._instance_count);
                                }
                            }
                        }
                        obj_index = 0;
                    }
                }
            }
        }
        Shader::SetGlobalTexture("MainLightShadowMap", _p_mainlight_shadow_map.get());
        Shader::SetGlobalTexture("AddLightShadowMaps", _p_addlight_shadow_maps.get());
        Shader::SetGlobalTexture("PointLightShadowMaps", _p_point_light_shadow_maps.get());
        context->ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
    }

    void ShadowCastPass::EndPass(GraphicsContext *context)
    {
        RenderPass::EndPass(context);
    }
    #pragma endregion

    //-------------------------------------------------------------ShadowCastPass-------------------------------------------------------------
    
    
    #pragma region CubeMapGenPass
    //-------------------------------------------------------------CubeMapGenPass-------------------------------------------------------------

    CubeMapGenPass::CubeMapGenPass(Texture *src_tex, u16 size) : RenderPass("CubeMapGenPass"),
                                                                 _cubemap_rect(Rect{0, 0, size, size}), _ibl_rect(Rect{0, 0, (u16) (size / 4), (u16) (size / 4)})
    {
        _is_src_cubemap = src_tex->Dimension() == ETextureDimension::kCube;
        float x = 0.f, y = 0.f, z = 0.f;
        Vector3f center = {x, y, z};
        Vector3f world_up = Vector3f::kUp;
        if (!_is_src_cubemap)
        {
            _src_cubemap = RenderTexture::Create(size, src_tex->Name() + "_src_cubemap", ERenderTargetFormat::kDefaultHDR, true, true, true);
        }
        else
            _input_src = src_tex;

        _prefilter_cubemap = RenderTexture::Create(size, src_tex->Name() + "_prefilter_cubemap", ERenderTargetFormat::kDefaultHDR, true, true, true);
        _prefilter_cubemap->_load_action = ELoadStoreAction::kNotCare;
        _radiance_map = RenderTexture::Create(size / 4, src_tex->Name() + "_radiance", ERenderTargetFormat::kDefaultHDR, false);
        _radiance_map->_load_action = ELoadStoreAction::kNotCare;
        _p_gen_material = g_pResourceMgr->Get<Material>(L"Runtime/Material/CubemapGen");
        _p_gen_material->SetTexture("env", src_tex);
        _p_filter_material = g_pResourceMgr->Get<Material>(L"Runtime/Material/EnvmapFilter");
        Matrix4x4f view, proj;
        BuildPerspectiveFovLHMatrix(proj, 90 * k2Radius, 1.0f, 0.01f, 100.f);
        //BuildPerspectiveFovLHMatrix(proj, 2.0 * atan((f32)size / ((f32)size - 0.5)), 1.0, 1.0, 100000);
        float scale = 20.0f;
        MatrixScale(_world_mat, scale, scale, scale);
        _per_obj_cb.reset(ConstantBuffer::Create(RenderConstants::kPerObjectDataSize));
        _per_obj_cb->SetData(reinterpret_cast<const u8 *>(&_world_mat), sizeof(Matrix4x4f));
        Vector3f targets[] =
                {
                        {x + 1.f, y, z},//+x
                        {x - 1.f, y, z},//-x
                        {x, y + 1.f, z},//+y
                        {x, y - 1.f, z},//-y
                        {x, y, z + 1.f},//+z
                        {x, y, z - 1.f} //-z
                };
        Vector3f ups[] =
                {
                        {0.f, 1.f, 0.f}, //+x
                        {0.f, 1.f, 0.f}, //-x
                        {0.f, 0.f, -1.f},//+y
                        {0.f, 0.f, 1.f}, //-y
                        {0.f, 1.f, 0.f}, //+z
                        {0.f, 1.f, 0.f}  //-z
                };
        for (u16 i = 0; i < 6; i++)
        {
            BuildViewMatrixLookToLH(view, center, targets[i], ups[i]);
            _camera_data[i]._MatrixVP = view * proj;
            _camera_data[i]._CameraPos = {0.0f, 0.f, 0.f, 0.f};
            _per_camera_cb[i].reset(ConstantBuffer::Create(RenderConstants::kPerCameraDataSize));
            memcpy(_per_camera_cb[i]->GetData(), &_camera_data[i], RenderConstants::kPerCameraDataSize);
        }
        f32 mipmap_level = _prefilter_cubemap->MipmapLevel();
        for (f32 i = 0.0f; i < mipmap_level; i++)
        {
            u16 cur_mipmap_size = size >> (u16) i;
            _reflection_prefilter_mateirals.emplace_back(MakeRef<Material>(g_pResourceMgr->Get<Shader>(L"Shaders/filter_irradiance.alasset"), "ReflectionPrefilter"));
            _reflection_prefilter_mateirals.back()->SetFloat("_roughness", i / mipmap_level);
            _reflection_prefilter_mateirals.back()->SetFloat("_width", cur_mipmap_size);
            //_reflection_prefilter_mateirals.back()->SetTexture("SrcTex", ToWChar(src_texture_name));
            _reflection_prefilter_mateirals.back()->SetTexture("EnvMap", _src_cubemap.get());
        }
    }

    void CubeMapGenPass::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        auto cmd = CommandBufferPool::Get("LightProbeGen");
        cmd->Clear();
        Texture *src_tex = _input_src ? _input_src : _src_cubemap.get();
        u16 mipmap_level = src_tex->MipmapLevel();
        if (!_is_src_cubemap)
        {
            //image tp cubemap
            for (u16 i = 0; i < 6; i++)
            {
                u16 rt_index = src_tex->CalculateViewIndex(Texture::ETextureViewType::kRTV, (ECubemapFace::ECubemapFace)(i + 1), 0, 0);
                cmd->SetRenderTarget(_src_cubemap.get(), rt_index);
                cmd->ClearRenderTarget(Colors::kBlack);
                cmd->SetGlobalBuffer(RenderConstants::kCBufNamePerCamera, _per_camera_cb[i].get());
                cmd->DrawMesh(Mesh::s_p_cube.lock().get(), _p_gen_material, _per_obj_cb.get(), 0, 0, 1);
            }
            context->ExecuteCommandBuffer(cmd);
            cmd->Clear();
            //实际上mipmap生成不使用传入的cmd，但是需要等待原始cubemap生成完毕
            _src_cubemap->GenerateMipmap();
        }
        else
        {
            dynamic_cast<RenderTexture *>(_input_src)->GenerateMipmap();
        }

        //gen radiance map
        _p_filter_material->SetTexture("EnvMap", src_tex);
        for (u16 i = 0; i < 6; i++)
        {
            u16 rt_index = _radiance_map->CalculateViewIndex(Texture::ETextureViewType::kRTV, (ECubemapFace::ECubemapFace)(i + 1), 0, 0);
            cmd->SetRenderTarget(_radiance_map.get(), rt_index);
            //cmd->ClearRenderTarget(_radiance_map.get(), Colors::kBlack, rt_index);
            cmd->SetGlobalBuffer(RenderConstants::kCBufNamePerCamera, _per_camera_cb[i].get());
            cmd->DrawMesh(Mesh::s_p_cube.lock().get(), _p_filter_material, _per_obj_cb.get());
        }
        //filter envmap
        mipmap_level = _prefilter_cubemap->MipmapLevel();
        for (u16 i = 0; i < 6; i++)
        {
            Rect r(0, 0, _prefilter_cubemap->Width(), _prefilter_cubemap->Height());
            for (u16 j = 0; j < mipmap_level; j++)
            {
                _reflection_prefilter_mateirals[j]->SetTexture("EnvMap", src_tex);
                auto [w, h] = Texture::CalculateMipSize(_prefilter_cubemap->Width(), _prefilter_cubemap->Height(), j);
                r.width = w;
                r.height = h;
                u16 rt_index = _prefilter_cubemap->CalculateViewIndex(Texture::ETextureViewType::kRTV, (ECubemapFace::ECubemapFace)(i + 1), j, 0);
                cmd->SetViewport(r);
                cmd->SetScissorRect(r);
                cmd->SetRenderTarget(_prefilter_cubemap.get(), rt_index);
                //cmd->ClearRenderTarget(_prefilter_cubemap.get(), Colors::kBlack, rt_index);
                cmd->SetGlobalBuffer(RenderConstants::kCBufNamePerCamera, _per_camera_cb[i].get());
                cmd->DrawMesh(Mesh::s_p_cube.lock().get(), _reflection_prefilter_mateirals[j].get(), _per_obj_cb.get(), 0, 1, 1);
            }
        }

        context->ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
    }

    void CubeMapGenPass::BeginPass(GraphicsContext *context)
    {
    }

    void CubeMapGenPass::EndPass(GraphicsContext *context)
    {
        RenderPass::EndPass(context);
    }
    #pragma endregion

    #pragma region DeferredGeometryPass
    //-------------------------------------------------------------CubeMapGenPass-------------------------------------------------------------

    //-------------------------------------------------------------DeferedGeometryPass-------------------------------------------------------------
    DeferredGeometryPass::DeferredGeometryPass() : RenderPass("DeferedGeometryPass")
    {
        _event = (ERenderPassEvent::ERenderPassEvent)(ERenderPassEvent::kBeforeGbuffer + 25u);
    }
    void DeferredGeometryPass::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        // if (rendering_data._cull_results->size() == 0)
        //     return;
        auto w = rendering_data._width, h = rendering_data._height;
        for (int i = 0; i < _rects.size(); i++)
            _rects[i] = Rect(0, 0, w, h);

        auto cmd = CommandBufferPool::Get("DeferredRenderPass");
        {
            GpuProfileBlock profile(cmd.get(), _name);
            cmd->SetRenderTargetLoadAction(rendering_data._camera_color_target_handle,ELoadStoreAction::kNotCare);
            cmd->SetRenderTargetLoadAction(rendering_data._camera_depth_target_handle,ELoadStoreAction::kClear);
            cmd->SetRenderTargets(rendering_data._gbuffers, rendering_data._camera_depth_target_handle);
            //cmd->ClearRenderTarget(rendering_data._gbuffers, rendering_data._camera_depth_target_handle, Colors::kBlack, kZFar);
            cmd->SetViewports({_rects[0], _rects[1], _rects[2], _rects[3]});
            cmd->SetScissorRects({_rects[0], _rects[1], _rects[2], _rects[3]});
            u32 obj_index = 0;
            for (auto &it: *rendering_data._cull_results)
            {
                auto &[queue, objs] = it;
                for (auto &obj: objs)
                {
                    auto obj_cb = (*rendering_data._p_per_object_cbuf)[obj._scene_id];
                    obj._material->GetShader()->_stencil_ref = ConstantBuffer::As<CBufferPerObjectData>(obj_cb)->_MotionVectorParam.x? 1 : 0;
                    cmd->DrawMesh(obj._mesh, obj._material, obj_cb, obj._submesh_index, 0, obj._instance_count);
                    ++obj_index;
                }
            }
        }
        context->ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
    }
    void DeferredGeometryPass::BeginPass(GraphicsContext *context)
    {
    }
    void DeferredGeometryPass::EndPass(GraphicsContext *context)
    {
        RenderPass::EndPass(context);
    }
    #pragma endregion

    #pragma region DeferedLightingPass
    //-------------------------------------------------------------DeferedGeometryPass-------------------------------------------------------------

    //-------------------------------------------------------------DeferedLightingPass-------------------------------------------------------------
    DeferredLightingPass::DeferredLightingPass() : RenderPass("DeferredLightingPass")
    {
        _p_lighting_material = MakeRef<Material>(g_pResourceMgr->Get<Shader>(L"Shaders/deferred_lighting.alasset"), "DeferedGbufferLighting");

        _brdf_lut = g_pResourceMgr->Load<Texture2D>(L"Textures/ibl_brdf_lut.alasset");
        // _brdf_lut->Apply();
        // _brdf_lut->Name("brdf_lut_tex");
        // _brdflut_gen = ComputeShader::Create(ResourceMgr::GetResSysPath(L"Shaders/hlsl/Compute/brdflut_gen.alcp"));
        // _brdflut_gen->SetTexture("_brdf_lut", _brdf_lut.get());
        // auto cmd = CommandBufferPool::Get();
        // cmd->Dispatch(_brdflut_gen.get(), _brdflut_gen->FindKernel("cs_main"), 8, 8, 1);
        // g_pGfxContext->ExecuteCommandBuffer(cmd);
        _event = (ERenderPassEvent::ERenderPassEvent)(ERenderPassEvent::kBeforeDeferedLighting + 25u);
    }
    void DeferredLightingPass::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        _p_lighting_material->SetTexture("_GBuffer0", rendering_data._gbuffers[0]);
        _p_lighting_material->SetTexture("_GBuffer1", rendering_data._gbuffers[1]);
        _p_lighting_material->SetTexture("_GBuffer2", rendering_data._gbuffers[2]);
        _p_lighting_material->SetTexture("_GBuffer3", rendering_data._gbuffers[3]);
        _p_lighting_material->SetTexture("_CameraDepthTexture", rendering_data._camera_depth_tex_handle);
        Shader::SetGlobalTexture("IBLLut", _brdf_lut.get());
        auto cmd = CommandBufferPool::Get("DeferredLightingPass");
        {
            GpuProfileBlock profile(cmd.get(), _name);
            cmd->SetRenderTargetLoadAction(rendering_data._camera_color_target_handle,ELoadStoreAction::kNotCare);
            cmd->SetRenderTargetLoadAction(rendering_data._camera_depth_target_handle,ELoadStoreAction::kNotCare);
            cmd->SetRenderTarget(rendering_data._camera_color_target_handle, rendering_data._camera_depth_target_handle);
            //cmd->ClearRenderTarget(rendering_data._camera_color_target_handle, Colors::kBlack);
            cmd->DrawFullScreenQuad(_p_lighting_material.get());
        }
        context->ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
    }
    void DeferredLightingPass::BeginPass(GraphicsContext *context)
    {
    }
    void DeferredLightingPass::EndPass(GraphicsContext *context)
    {
        RenderPass::EndPass(context);
    }
    #pragma endregion
    //-------------------------------------------------------------DeferedLightingPass-------------------------------------------------------------

    #pragma region SkyboxPass
    //-------------------------------------------------------------SkyboxPass-------------------------------------------------------------
    SkyboxPass::SkyboxPass() : RenderPass("SkyboxPass")
    {
        _p_lut_gen = ComputeShader::Create(ResourceMgr::GetResSysPath(L"Shaders/hlsl/Compute/atmosphere_lut_gen.hlsl"));
        //_p_skybox_material = MaterialLibrary::CreateMaterial(ShaderLibrary::Load("Shaders/skybox._pp.alasset"), "SkyboxPP");
        _p_skybox_material = MakeRef<Material>(g_pResourceMgr->Get<Shader>(L"Shaders/skybox.alasset"), "Skybox");
        Matrix4x4f world_mat;
        MatrixScale(world_mat, 1000000.f, 1000000.f, 1000000.f);
        _p_cbuffer.reset(ConstantBuffer::Create(RenderConstants::kPerObjectDataSize));
        _p_cbuffer->SetData(reinterpret_cast<u8 *>(&world_mat), sizeof(Matrix4x4f));
        _tlut = RenderTexture::Create(_transmittance_lut_size.x, _transmittance_lut_size.y, "_TransmittanceLUT", ERenderTargetFormat::kRGBAHalf, false, false, true);
        _ms_lut = RenderTexture::Create(_mult_scatter_lut_size.x, _mult_scatter_lut_size.y, "_MultScatterLUT", ERenderTargetFormat::kRGBAHalf, false, false, true);

        auto cmd = CommandBufferPool::Get("SkyLutGen");
        u16 transmittance_lut_gen_kernel = _p_lut_gen->FindKernel("TransmittanceGen");
        u16 mult_scatter_lut_gen_kernel = _p_lut_gen->FindKernel("MultiScattGen");
        _p_lut_gen->SetTexture("_TransmittanceLUT", _tlut.get());
        cmd->Dispatch(_p_lut_gen.get(), transmittance_lut_gen_kernel, _transmittance_lut_size.x / 16, _transmittance_lut_size.y / 16, 1);

        _p_lut_gen->SetTexture("_TexTransmittanceLUT", _tlut.get());
        _p_lut_gen->SetTexture("_MultScatterLUT", _ms_lut.get());
        cmd->Dispatch(_p_lut_gen.get(), mult_scatter_lut_gen_kernel, _mult_scatter_lut_size.x / 16, _mult_scatter_lut_size.y / 16, 1);
        g_pGfxContext->ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
        _event = (ERenderPassEvent::ERenderPassEvent)(ERenderPassEvent::kBeforeSkybox + 25u);
    }
    void SkyboxPass::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        u16 sky_lut_gen_kernel = _p_lut_gen->FindKernel("SkyLightGen");
        auto cmd = CommandBufferPool::Get("SkyboxPass");
        cmd->Clear();
        {
            GpuProfileBlock profile(cmd.get(), _name);
            //auto t_lut = cmd->GetTempRT(_transmittance_lut_size.x, _transmittance_lut_size.y, "_TransmittanceLUT", ERenderTargetFormat::kRGBAHalf, false, false, true);
            //auto ms_lut = cmd->GetTempRT(_mult_scatter_lut_size.x, _mult_scatter_lut_size.y, "_MultScatterLUT", ERenderTargetFormat::kRGBAHalf, false, false, true);
            auto sv_lut = cmd->GetTempRT(_sky_lut_size.x, _sky_lut_size.y, "_SkyLightLUT", ERenderTargetFormat::kRGBAHalf, false, false, true);


            _p_lut_gen->SetTexture("_TexTransmittanceLUT", _tlut.get());
            _p_lut_gen->SetTexture("_TexMultScatterLUT", _ms_lut.get());
            _p_lut_gen->SetTexture("_SkyLightLUT", sv_lut);
            _p_lut_gen->SetVector("_MainLightPosition", rendering_data._mainlight_world_position);
            cmd->Dispatch(_p_lut_gen.get(), sky_lut_gen_kernel, _sky_lut_size.x / 16, _sky_lut_size.y / 16, 1);


            _p_skybox_material->SetTexture("_TexSkyViewLUT", sv_lut);
            _p_skybox_material->SetTexture("_TexTransmittanceLUT", _tlut.get());
            cmd->SetRenderTargetLoadAction(rendering_data._camera_color_target_handle,ELoadStoreAction::kNotCare);
            cmd->SetRenderTargetLoadAction(rendering_data._camera_depth_target_handle,ELoadStoreAction::kNotCare);
            cmd->SetRenderTarget(rendering_data._camera_color_target_handle, rendering_data._camera_depth_target_handle);
            if (_is_clear)
            {
                cmd->ClearRenderTarget(Colors::kBlack);
                cmd->ClearRenderTarget(kZFar,0u);
            }
            cmd->DrawMesh(Mesh::s_p_shpere.lock().get(), _p_skybox_material.get(), _p_cbuffer.get(), 0, 1);
            ComputeShader::SetGlobalTexture("_TexSkyViewLUT",sv_lut);
            //cmd->ReleaseTempRT(t_lut);
            //cmd->ReleaseTempRT(ms_lut);
            cmd->ReleaseTempRT(sv_lut);
        }
        context->ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
    }
    void SkyboxPass::BeginPass(GraphicsContext *context)
    {
    }
    void SkyboxPass::EndPass(GraphicsContext *context)
    {
        RenderPass::EndPass(context);
    }
    #pragma endregion
    //-------------------------------------------------------------SkyboxPass-------------------------------------------------------------

    #pragma region GizmoPass
    //-------------------------------------------------------------GizmoPass-------------------------------------------------------------
    GizmoPass::GizmoPass() : RenderPass("GizmoPass")
    {
        for (int i = 0; i < 20; i++)
        {
            _p_cbuffers.push_back(std::unique_ptr<ConstantBuffer>(ConstantBuffer::Create(256)));
        }
        auto grid_plane_pos = MatrixScale(1000.0f, 1000.f, 1000.f);
        memcpy(_p_cbuffers[0]->GetData(), &grid_plane_pos, sizeof(Matrix4x4f));
        _event = ERenderPassEvent::kAfterPostprocess;
    }
    static Vector2f ViewportTransform(const Vector4f& clipPos, const Vector2f& viewportSize) {
        // 首先进行透视除法
        Vector2f ndcPos = { clipPos.x / clipPos.w, clipPos.y / clipPos.w };

        // 将 NDC 坐标从 [-1, 1] 映射到 [0, 1]
        //ndcPos.x = std::clamp(ndcPos.x * 0.5f + 0.5f, 0.0f, 1.0f);
        //ndcPos.y = std::clamp(ndcPos.y * 0.5f + 0.5f, 0.0f, 1.0f);

        // 映射到视口尺寸
        Vector2f screenPos;
        screenPos.x = ndcPos.x * viewportSize.x * 0.5f;
        screenPos.y = ndcPos.y * viewportSize.y * 0.5f;
        screenPos.x += viewportSize.x * 0.5f;
        screenPos.y += viewportSize.y * 0.5f;
        return screenPos;
    }

    void GizmoPass::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        auto cmd = CommandBufferPool::Get("GizmoPass");
        static auto mat_point_light = g_pResourceMgr->Get<Material>(L"Runtime/Material/PointLightBillboard");
        static auto mat_directional_light = g_pResourceMgr->Get<Material>(L"Runtime/Material/DirectionalLightBillboard");
        static auto mat_spot_light = g_pResourceMgr->Get<Material>(L"Runtime/Material/SpotLightBillboard");
        static auto mat_area_light = g_pResourceMgr->Get<Material>(L"Runtime/Material/AreaLightBillboard");
        static auto mat_camera = g_pResourceMgr->Get<Material>(L"Runtime/Material/CameraBillboard");
        static auto mat_gird_plane = g_pResourceMgr->Get<Material>(L"Runtime/Material/GridPlane");
        static auto mat_lightprobe = g_pResourceMgr->Get<Material>(L"Runtime/Material/LightProbeBillboard");
        cmd->Clear();
        {
            auto &cam = rendering_data._camera;
            // 获取相机参数
            float nearPlane = cam->Near() + 0.05f;
            float verticalFOV = cam->FovH() * k2Radius;// 单位为弧度
            float aspectRatio = cam->Aspect();

            // 计算半视野角度
            float halfHeight = nearPlane * tan(verticalFOV / 2.0f);
            float halfWidth = halfHeight * aspectRatio;

            // 获取相机的方向向量
            Vector3f cameraForward = cam->Forward();
            Vector3f cameraRight = cam->Right();
            Vector3f cameraUp = cam->Up();

            // 计算近裁剪面的左下角
            Vector3f nearCenter = cam->Position() + cameraForward * nearPlane;
            Vector3f bottomLeft = nearCenter - cameraRight * halfWidth * 0.95f - cameraUp * halfHeight * 0.95f;


            const static f32 s_axis_length = 30.f;
            Vector3f camera_target = cam->Position() + cam->Forward() * (cam->Near() + 50.f);
            Matrix4x4f vp = rendering_data._camera->GetView() * rendering_data._camera->GetProj();
            Vector2f half_size((f32)(rendering_data._width >> 1), (f32)(rendering_data._height >> 1));
            Vector2f viewport_size((f32)rendering_data._width, (f32)rendering_data._height);
            half_size -= s_axis_length;
            half_size = -half_size;
            //camera_target = Vector3f::kZero;
            Vector4f cpos_camera_target = {camera_target,1.0f};
            Vector4f cpos_y_axis = {camera_target + Vector3f::kUp* s_axis_length,1.0f};
            Vector4f cpos_x_axis = {camera_target + Vector3f::kRight* s_axis_length,1.0f};
            Vector4f cpos_z_axis = {camera_target + Vector3f::kForward* s_axis_length,1.0f};
            TransformVector(cpos_camera_target,vp);
            TransformVector(cpos_x_axis, vp);
            TransformVector(cpos_y_axis, vp);
            TransformVector(cpos_z_axis, vp);
            Vector2f screen_pos_x_axis= cpos_x_axis.xy;
            Vector2f screen_pos_y_axis= cpos_y_axis.xy;
            Vector2f screen_pos_z_axis= cpos_z_axis.xy;
            //screen_pos_x_axis = screen_pos_x_axis / Magnitude(screen_pos_x_axis) * s_axis_length;// + half_size;
            //screen_pos_y_axis = screen_pos_y_axis / Magnitude(screen_pos_y_axis) * s_axis_length;// + half_size;
            //screen_pos_z_axis = screen_pos_z_axis / Magnitude(screen_pos_z_axis) * s_axis_length;// + half_size;
            cpos_camera_target.xy +=  half_size;
            screen_pos_x_axis += half_size;
            screen_pos_y_axis += half_size;
            screen_pos_z_axis += half_size;
            Gizmo::DrawLine(Vector2f(cpos_camera_target.xy), screen_pos_y_axis, Colors::kGreen);
            Gizmo::DrawLine(Vector2f(cpos_camera_target.xy), screen_pos_x_axis, Colors::kRed);
            Gizmo::DrawLine(Vector2f(cpos_camera_target.xy), screen_pos_z_axis, Colors::kBlue);

            //cmd->SetViewProjectionMatrix(rendering_data._camera->GetView(), rendering_data._camera->GetProj());
            GpuProfileBlock profile(cmd.get(), _name);
            cmd->SetViewport(rendering_data._viewport);
            cmd->SetScissorRect(rendering_data._scissor_rect);
            cmd->SetRenderTarget(rendering_data._camera_color_target_handle, rendering_data._camera_depth_target_handle);

            cmd->DrawMesh(Mesh::s_p_plane.lock().get(), mat_gird_plane, _p_cbuffers[0].get(), 0, 0, 1);
            u16 index = 1;
            u16 entity_index = 0;
            for (auto &light_comp: g_pSceneMgr->ActiveScene()->GetRegister().View<ECS::LightComponent>())
            {
                if (index >= 20)
                    break;
                const auto &t = g_pSceneMgr->ActiveScene()->GetRegister().GetComponent<ECS::LightComponent, ECS::TransformComponent>(entity_index++);
                auto world_pos = t->_transform._position;
                auto m = MatrixTranslation(world_pos);
                f32 scale = 2.0f;
                m = MatrixScale(scale, scale, scale) * m;
                memcpy(_p_cbuffers[index]->GetData(), &m, sizeof(Matrix4x4f));
                switch (light_comp._type)
                {
                    case ECS::ELightType::kDirectional:
                    {
                        cmd->DrawMesh(Mesh::s_p_quad.lock().get(), mat_directional_light, m);
                    }
                    break;
                    case ECS::ELightType::kPoint:
                    {
                        cmd->DrawMesh(Mesh::s_p_quad.lock().get(), mat_point_light, m);
                    }
                    break;
                    case ECS::ELightType::kSpot:
                    {
                        cmd->DrawMesh(Mesh::s_p_quad.lock().get(), mat_spot_light, m);
                    }
                    break;
                    case ECS::ELightType::kArea:
                    {
                        cmd->DrawMesh(Mesh::s_p_quad.lock().get(), mat_area_light, m);
                    }
                    break;
                }
            }
            entity_index = 0;
            for (auto &light_comp: g_pSceneMgr->ActiveScene()->GetRegister().View<ECS::CLightProbe>())
            {
                if (index >= 20)
                    break;
                const auto &t = g_pSceneMgr->ActiveScene()->GetRegister().GetComponent<ECS::CLightProbe, ECS::TransformComponent>(entity_index++);
                auto world_pos = t->_transform._position;
                auto m = MatrixTranslation(world_pos);
                f32 scale = 2.0f;
                m = MatrixScale(scale, scale, scale) * m;
                memcpy(_p_cbuffers[index]->GetData(), &m, sizeof(Matrix4x4f));
                cmd->DrawMesh(Mesh::s_p_quad.lock().get(), mat_lightprobe, m);
            }
            entity_index = 0;
            for (auto &light_comp: g_pSceneMgr->ActiveScene()->GetRegister().View<ECS::CCamera>())
            {
                const auto &t = g_pSceneMgr->ActiveScene()->GetRegister().GetComponent<ECS::CCamera, ECS::TransformComponent>(entity_index++);
                auto world_pos = t->_transform._position;
                auto m = MatrixTranslation(world_pos);
                f32 scale = 2.0f;
                m = MatrixScale(scale, scale, scale) * m;
                memcpy(_p_cbuffers[index]->GetData(), &m, sizeof(Matrix4x4f));
                cmd->DrawMesh(Mesh::s_p_quad.lock().get(), mat_camera, _p_cbuffers[index++].get(), 0, 0, 1);
            }
            Gizmo::Submit(cmd.get(), rendering_data);
        }
        context->ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
    }
    void GizmoPass::BeginPass(GraphicsContext *context)
    {

    }
    void GizmoPass::EndPass(GraphicsContext *context)
    {
        RenderPass::EndPass(context);
    }
    #pragma endregion
    //-------------------------------------------------------------GizmoPass-------------------------------------------------------------

    //-------------------------------------------------------------CopyColorPass-------------------------------------------------------------
    #pragma region CopyColorPass
    CopyColorPass::CopyColorPass() : RenderPass("CopyColor")
    {
        _p_blit_mat = g_pResourceMgr->Get<Material>(L"Runtime/Material/Blit");
        _p_obj_cb = ConstantBuffer::Create(256);
        memcpy(_p_obj_cb->GetData(), &BuildIdentityMatrix(), sizeof(Matrix4x4f));
        _p_quad_mesh = g_pResourceMgr->Get<Mesh>(L"Runtime/Mesh/FullScreenQuad");
        _event = ERenderPassEvent::kAfterTransparent;
    }
    CopyColorPass::~CopyColorPass()
    {
    }
    void CopyColorPass::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        auto cmd = CommandBufferPool::Get("CopyColor");
        cmd->Clear();
        {
            GpuProfileBlock profile(cmd.get(), _name);
            cmd->Blit(rendering_data._camera_color_target_handle, rendering_data._camera_opaque_tex_handle);
        }
        context->ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
    }
    void CopyColorPass::BeginPass(GraphicsContext *context)
    {
    }
    void CopyColorPass::EndPass(GraphicsContext *context)
    {
        RenderPass::EndPass(context);
        _p_blit_mat->SetTexture("_SourceTex", nullptr);
    }
    #pragma endregion

    #pragma region CopyDepthPass
    //-------------------------------------------------------------CopyColorPass-------------------------------------------------------------

    //-------------------------------------------------------------CopyDepthPass-------------------------------------------------------------
    CopyDepthPass::CopyDepthPass() : RenderPass("CopyDepthPass")
    {
        _p_blit_mat = g_pResourceMgr->Get<Material>(L"Runtime/Material/Blit");
        _event = ERenderPassEvent::kAfterGbuffer;
    }
    CopyDepthPass::~CopyDepthPass()
    {
    }
    void CopyDepthPass::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        _depth_tex_handle = rendering_data._camera_depth_tex_handle;
        auto cmd = CommandBufferPool::Get("CopyDepth");
        cmd->Clear();
        {
            GpuProfileBlock profile(cmd.get(), _name);
            cmd->Blit(rendering_data._camera_depth_target_handle, rendering_data._camera_depth_tex_handle);
        }
        context->ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
    }
    void CopyDepthPass::BeginPass(GraphicsContext *context)
    {
    }
    void CopyDepthPass::EndPass(GraphicsContext *context)
    {
        RenderPass::EndPass(context);
        _p_blit_mat->SetTexture("_SourceTex", nullptr);
        Shader::SetGlobalTexture("_CameraDepthTexture",_depth_tex_handle);
        ComputeShader::SetGlobalTexture("_CameraDepthTexture",_depth_tex_handle);
    }
    #pragma endregion
    //-------------------------------------------------------------CopyDepthPass-------------------------------------------------------------

    //-------------------------------------------------------------WireFramePass-------------------------------------------------------------
    #pragma region WireFramePass
    WireFramePass::WireFramePass() : RenderPass("WireFramePass")
    {

        _wireframe_mat = g_pResourceMgr->GetRef<Material>(L"Runtime/Material/Wireframe");
        _event = ERenderPassEvent::kAfterPostprocess;
    }
    WireFramePass::~WireFramePass()
    {
    }
    void WireFramePass::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        auto &all_renderable = *rendering_data._cull_results;
        auto cmd = CommandBufferPool::Get(_name);
        {
            GpuProfileBlock b(cmd.get(), _name);
            cmd->SetRenderTarget(rendering_data._camera_color_target_handle, rendering_data._camera_depth_target_handle);
            for (auto &it: all_renderable)
            {
                auto &[queue, objs] = it;
                for (auto &obj: objs)
                {
                    if (!_wireframe_mats.contains(obj._material->Name()))
                    {
                        WString shader_name_w = ToWChar(obj._material->GetShader()->Name().c_str());
                        shader_name_w = std::format(L"Runtime/Shader/Wireframe_{}", shader_name_w);
                        if (!_wireframe_shaders.contains(shader_name_w))
                        {
                            auto wireframe_shader = Shader::Create(ResourceMgr::GetResSysPath(L"Shaders/hlsl/wireframe.hlsl"));
                            auto &pass = obj._material->GetShader()->GetPassInfo(0);
                            wireframe_shader->SetVertexShader(0, pass._vert_src_file, pass._vert_entry);
                            wireframe_shader->Compile();
                            g_pResourceMgr->RegisterResource(shader_name_w, wireframe_shader);
                            _wireframe_shaders.insert(shader_name_w);
                        }
                        auto mat = MakeRef<Material>(*obj._material);
                        mat->ChangeShader(g_pResourceMgr->Get<Shader>(shader_name_w));
                        _wireframe_mats[obj._material->Name()] = mat;
                    }
                    cmd->DrawMesh(obj._mesh, _wireframe_mats[obj._material->Name()].get(), (*rendering_data._p_per_object_cbuf)[obj._scene_id], obj._submesh_index, 0, obj._instance_count);
                }
            }
        }
        context->ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
    }

    void WireFramePass::BeginPass(GraphicsContext *context) {};
    void WireFramePass::EndPass(GraphicsContext *context) 
    {
        RenderPass::EndPass(context);
    };
    #pragma endregion
    //-------------------------------------------------------------WireFramePass-------------------------------------------------------------

    #pragma region GUIPass
    //-------------------------------------------------------------GUIPass------------------------------------------------------------------
    GUIPass::GUIPass() : RenderPass("GUIPass")
    {
        _event = ERenderPassEvent::kAfterPostprocess;
        const u16 vertex_count = 1000u;
        _ui_default_shader = g_pResourceMgr->GetRef<Shader>(L"Shaders/default_ui.alasset");
        _ui_default_mat = MakeRef<Material>(_ui_default_shader.get(), "DefaultUIMaterial");
        _ui_default_mat->SetTexture("_MainTex", Texture::s_p_default_white);
        _ui_default_mat->SetVector("_Color", Colors::kWhite);
        Vector<VertexBufferLayoutDesc> desc_list;
        desc_list.push_back({"POSITION", EShaderDateType::kFloat3, 0});
        desc_list.push_back({"TEXCOORD", EShaderDateType::kFloat2, 1});
        _obj_cb.reset(ConstantBuffer::Create(RenderConstants::kPerObjectDataSize));
        _vbuf.reset(VertexBuffer::Create(desc_list, "ui_vbuf"));
        _ibuf.reset(IndexBuffer::Create(nullptr, vertex_count, "ui_ibuf", true));
        f32 box_w = 180.f, box_h = 30.f;
        Vector3f *vertices = new Vector3f[4]{{-box_w * 0.5f, box_h * 0.5f, 0.0f},
                                             {box_w * 0.5f, box_h * 0.5f, 0.0f},
                                             {-box_w * 0.5f, -box_h * 0.5f, 0.0f},
                                             {box_w * 0.5f, -box_h * 0.5f, 0.0f}};
        u32 *indices = new u32[6]{0, 1, 2, 1, 3, 2};
        Vector2f *uv0 = new Vector2f[4]{{0.f, 0.f}, {1.f, 0.f}, {0.f, 1.f}, {1.f, 1.f}};
        _vbuf->SetStream(nullptr, vertex_count * sizeof(Vector3f), 0, true);
        _vbuf->SetStream(nullptr, vertex_count * sizeof(Vector2f), 1, true);
        _ibuf->SetData((u8 *) indices, 6 * sizeof(u32));
        delete[] vertices;
        delete[] indices;
        delete[] uv0;
    }
    GUIPass::~GUIPass()
    {
    }
    void GUIPass::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        auto cmd = CommandBufferPool::Get(_name);
        cmd->Clear();
        {
            //TextRenderer::Get()->Render(g_pRenderTexturePool->Get(rendering_data._camera_color_target_handle), cmd.get());
            //f32 w = (f32) rendering_data._width;
            //f32 h = (f32) rendering_data._height;
            //GpuProfileBlock profile(cmd.get(), _name);
            //Matrix4x4f view, proj;
            //f32 aspect = w / h;
            //f32 half_width = w * 0.5f, half_height = h * 0.5f;
            //BuildViewMatrixLookToLH(view,Vector3f(0.f,0.f,-50.f),Vector3f::kForward,Vector3f::kUp);
            //BuildOrthographicMatrix(proj,-half_width,half_width,half_height,-half_height,1.f,200.f);
            //CBufferPerCameraData cb_per_cam;
            //cb_per_cam._MatrixVP = view * proj;
            //cb_per_cam._ScreenParams = Vector4f( 1.0f / w, 1.0f / h,w, h);
            //CBufferPerObjectData per_obj_data;
            //per_obj_data._MatrixWorld = BuildIdentityMatrix();
            //memcpy(_obj_cb->GetData(), &per_obj_data, RenderConstants::kPerObjectDataSize);
            //cmd->SetGlobalBuffer(RenderConstants::kCBufNamePerCamera,&cb_per_cam,RenderConstants::kPerCameraDataSize);
            //cmd->SetRenderTarget(rendering_data._camera_color_target_handle, rendering_data._camera_depth_target_handle);
            //cmd->DrawIndexed(_vbuf.get(), _ibuf.get(), _obj_cb.get(), _ui_default_mat.get());
        }
        context->ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
    }
    void GUIPass::BeginPass(GraphicsContext *context)
    {
        RenderPass::BeginPass(context);
    }
    void GUIPass::EndPass(GraphicsContext *context)
    {
        RenderPass::EndPass(context);
    }
    #pragma endregion
    //-------------------------------------------------------------GUIPass------------------------------------------------------------------

    #pragma region MotionVectorPass
    MotionVectorPass::MotionVectorPass() : RenderPass("MotionVectorPass")
    {
        _event = ERenderPassEvent::kBeforeTransparent;
        _motion_vector_mat = MakeRef<Material>(g_pResourceMgr->Get<Shader>(L"Shaders/motion_vector.alasset"),"Runtime/MotionVector");
    }

    MotionVectorPass::~MotionVectorPass()
    {
    
    }

    void MotionVectorPass::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        auto cmd = CommandBufferPool::Get(_name);
        RTHandle motion_vector_rt = cmd->GetTempRT(rendering_data._width,rendering_data._height, "_MotionVectorTexture", ERenderTargetFormat::kRGHalf,false,false,false);
        RTHandle motion_vector_depth = cmd->GetTempRT(rendering_data._width,rendering_data._height, "_MotionVectorDepth", ERenderTargetFormat::kDepth,false,false,false);
        cmd->Clear();
        {
            PROFILE_BLOCK_GPU(cmd.get(), CameraMotionVector)
            //camera motion vector
            cmd->SetRenderTarget(motion_vector_rt, motion_vector_depth);
            //cmd->ClearRenderTarget(motion_vector_rt, motion_vector_depth,Colors::kBlack,kZFar);
            _motion_vector_mat->SetTexture("_CameraDepthTexture",rendering_data._camera_depth_tex_handle);
            cmd->DrawFullScreenQuad(_motion_vector_mat.get(),0);
            //object motion vector
            for (auto &it: *rendering_data._cull_results)
            {
                auto &[queue, objs] = it;
                for (auto &obj: objs)
                {
                    auto obj_cb = (*rendering_data._p_per_object_cbuf)[obj._scene_id];
                    if (ConstantBuffer::As<CBufferPerObjectData>(obj_cb)->_MotionVectorParam.x < 1)
                        continue;
                    //obj._material->GetShader()->_stencil_ref = ConstantBuffer::As<CBufferPerObjectData>(obj_cb)->_MotionVectorParam.x? 1 : 0;
                    i16 mv_pass = obj._material->GetShader()->FindPass("MotionVector");
                    if (mv_pass != -1)
                        cmd->DrawMesh(obj._mesh, obj._material, obj_cb, obj._submesh_index, mv_pass, obj._instance_count);
                }
            }
        }
        cmd->ReleaseTempRT(motion_vector_depth);
        cmd->ReleaseTempRT(motion_vector_rt);
        context->ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
        Shader::SetGlobalTexture("_MotionVectorTexture", motion_vector_rt);
        ComputeShader::SetGlobalTexture("_MotionVectorTexture", motion_vector_rt);
        f32 aspect = (f32)rendering_data._width / (f32)rendering_data._height;
        Gizmo::DrawTexture(Rect(0,0,256,static_cast<u16>(256 / aspect)),g_pRenderTexturePool->Get(motion_vector_rt));
    }

    void MotionVectorPass::BeginPass(GraphicsContext *context)
    {
    }

    void MotionVectorPass::EndPass(GraphicsContext *context)
    {
        RenderPass::EndPass(context);
    }
#pragma endregion
}// namespace Ailu