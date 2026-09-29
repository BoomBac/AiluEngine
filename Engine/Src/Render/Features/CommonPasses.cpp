#include "Render/Features/CommonPasses.h"
#include "Render/Features/SurfelGI.h"
#include "Framework/Common/Profiler.h"
#include "Framework/Common/Allocator.hpp"
#include "Framework/Common/ResourceMgr.h"
#include "Render/Buffer.h"
#include "Render/Camera.h"
#include "Render/CommandBuffer.h"
#include "Render/GraphicsContext.h"
#include "Render/Gizmo.h"
#include "Render/GraphicsPipelineStateObject.h"
#include "Render/Material.h"
#include "Render/RenderConstants.h"
#include "Render/RenderQueue.h"
#include "Render/RenderingData.h"
#include "UI/TextRenderer.h"
#include "pch.h"

#include "Framework/Common/EngineConfig.h"

#include "Render/Renderer.h"
#include "Render/FrameResource.h"
#include "Render/RenderGraph/RenderGraph.h"

#include <cmath>

/* 模版说明
0 = 天空，大气和体积云绘制时会与其进行相等判断
1 = 静态物体
2 = 动态物体，Camera MotionVector绘制时会与其进行小于判断
*/

namespace Ailu::Render
{
    namespace
    {
        const ShaderPropertyId kSurfelQueryEnabledPropertyId = Shader::PropertyID("_SurfelQueryEnabled");
        const ShaderPropertyId kSurfelPoolPropertyId = Shader::PropertyID("_SurfelPool");
        const ShaderPropertyId kSurfelLightingPropertyId = Shader::PropertyID("_SurfelLighting");
        const ShaderPropertyId kSurfelStatePropertyId = Shader::PropertyID("_SurfelState");
        const ShaderPropertyId kSurfelQueryGridCellsPropertyId = Shader::PropertyID("_SurfelQueryGridCells");
        const ShaderPropertyId kSurfelQueryGridIndicesPropertyId = Shader::PropertyID("_SurfelQueryGridIndices");
        const ShaderPropertyId kSurfelQueryDebugCountersPropertyId = Shader::PropertyID("_SurfelQueryDebugCounters");
        const ShaderPropertyId kSurfelGridResolutionPropertyId = Shader::PropertyID("_SurfelGridResolution");
        const ShaderPropertyId kSurfelCascadeCountPropertyId = Shader::PropertyID("_SurfelCascadeCount");
        const ShaderPropertyId kSurfelMaxCountPropertyId = Shader::PropertyID("_SurfelMaxCount");
        const ShaderPropertyId kSurfelBaseCellSizePropertyId = Shader::PropertyID("_SurfelBaseCellSize");
        const ShaderPropertyId kSurfelTanHalfFovPropertyId = Shader::PropertyID("_SurfelTanHalfFov");
        const ShaderPropertyId kSurfelTargetPixelRadiusPropertyId = Shader::PropertyID("_SurfelTargetPixelRadius");
        const ShaderPropertyId kSurfelQueryDebugEnabledPropertyId = Shader::PropertyID("_SurfelQueryDebugEnabled");

        struct QueuedDrawItem
        {
            VertexBuffer *_vertex_buffer = nullptr;
            IndexBuffer *_index_buffer = nullptr;
            Mesh *_mesh = nullptr;
            Material *_material = nullptr;
            CBufferPrimitiveDrawData _primitive_draw_data{};
            u16 _submesh_index = 0u;
            u16 _pass_index = 0u;
            u32 _instance_count = 1u;
            u32 _render_queue = Shader::kRenderQueueOpaque;
            uintptr_t _shader_ptr = 0u;
            uintptr_t _material_ptr = 0u;
            uintptr_t _mesh_ptr = 0u;
            ShaderVariantHash _variant_hash = 0u;
            u8 _blend_state_hash = 0u;
            u8 _raster_state_hash = 0u;
            u8 _depth_stencil_state_hash = 0u;
            u8 _topology_hash = 0u;
            u16 _stencil_ref = 0u;
            bool _force_alpha_test = false;
        };

        QueuedDrawItem MakeQueuedDrawItem(u32 render_queue, const RenderableObjectData &object, Material *material,
                                          u16 pass_index, bool force_alpha_test = false)
        {
            QueuedDrawItem item{};
            item._vertex_buffer = object._vertex_buffer;
            item._index_buffer = object._index_buffer;
            item._mesh = object._mesh;
            item._material = material;
            item._primitive_draw_data._primitive_base = object._primitive_index;
            item._submesh_index = object._submesh_index;
            item._pass_index = pass_index;
            item._instance_count = 1u;
            item._render_queue = render_queue;
            item._shader_ptr = reinterpret_cast<uintptr_t>(material->GetShader());
            item._material_ptr = reinterpret_cast<uintptr_t>(material);
            item._mesh_ptr = reinterpret_cast<uintptr_t>(object._mesh);
            item._variant_hash = material->ActiveVariantHash(pass_index);
            item._stencil_ref = (object._flags & kPrimitivePerObjectMotion) ? 1u : 0u;
            item._force_alpha_test = force_alpha_test;
            if (const auto *shader = material->GetActiveShader(); shader != nullptr)
            {
                auto raster_state = shader->PipelineRasterizerState(pass_index);
                raster_state._cull_mode = material->GetCullMode();
                raster_state.Hash(RasterizerState::_s_hash_obj.GenHash(raster_state));
                item._blend_state_hash = shader->PipelineBlendState(pass_index).Hash();
                item._raster_state_hash = raster_state.Hash();
                item._depth_stencil_state_hash = shader->PipelineDepthStencilState(pass_index).Hash();
                item._topology_hash = static_cast<u8>(shader->PipelineTopology(pass_index));
            }
            if (force_alpha_test)
            {
                auto keywords = material->ActiveKeywords(pass_index);
                keywords.insert("ALPHA_TEST");
                item._variant_hash = material->GetActiveShader()->ConstructVariantHash(pass_index, keywords);
            }
            return item;
        }

        void BindDDGILighting(Material *material, const RenderingData &data, Texture *irradiance_atlas,
                              Texture *distance_atlas)
        {
            const auto &ddgi = data._ddgi_lighting;
            material->SetTexture("_DDGI_IrradianceAtlas",
                                 irradiance_atlas != nullptr ? irradiance_atlas : Texture::s_p_default_black);
            material->SetTexture("_DDGI_DistanceAtlas",
                                 distance_atlas != nullptr ? distance_atlas : Texture::s_p_default_black);
            material->SetMatrix("_DDGI_LocalToWorld", ddgi._local_to_world);
            material->SetMatrix("_DDGI_WorldToLocal", ddgi._world_to_local);
            material->SetVector("_DDGI_ProbeSpacing",
                                Vector4f(ddgi._probe_spacing.x, ddgi._probe_spacing.y, ddgi._probe_spacing.z, 0.0f));
            material->SetVector("_DDGI_ProbeCount",
                                Vector4f(static_cast<f32>(ddgi._probe_count_x), static_cast<f32>(ddgi._probe_count_y),
                                         static_cast<f32>(ddgi._probe_count_z), 0.0f));
            material->SetVector("_DDGI_AtlasInfo",
                                Vector4f(static_cast<f32>(ddgi._atlas_width), static_cast<f32>(ddgi._atlas_height),
                                         static_cast<f32>(ddgi._atlas_tiles_x), static_cast<f32>(ddgi._atlas_tiles_y)));
            material->SetVector("_DDGI_DistanceAtlasInfo",
                                Vector4f(static_cast<f32>(ddgi._distance_atlas_width),
                                         static_cast<f32>(ddgi._distance_atlas_height),
                                         static_cast<f32>(ddgi._distance_atlas_tiles_x),
                                         static_cast<f32>(ddgi._distance_atlas_tiles_y)));
            material->SetFloat("_DDGI_UseVisibility", ddgi._use_visibility ? 1.0f : 0.0f);
            material->SetFloat("_DDGI_ProbeNormalBias", ddgi._probe_normal_bias);
            material->SetFloat("_DDGI_ProbeViewBias", ddgi._probe_view_bias);
            material->SetBuffer("_DDGI_ProbeData", ddgi._probe_data.get());
            material->SetFloat("_DDGI_UseProbeRelocation", ddgi._use_probe_relocation ? 1.0f : 0.0f);
            material->SetFloat("_DDGI_DebugMode", static_cast<f32>(ddgi._debug_mode));
            material->SetFloat("_DDGI_Enabled", ddgi._enabled ? 1.0f : 0.0f);
        }

        bool CompareQueuedDrawItem(const QueuedDrawItem &lhs, const QueuedDrawItem &rhs)
        {
            return std::tie(lhs._render_queue, lhs._shader_ptr, lhs._pass_index, lhs._variant_hash, lhs._material_ptr, lhs._mesh_ptr,
                            lhs._vertex_buffer, lhs._index_buffer, lhs._submesh_index, lhs._blend_state_hash,
                            lhs._raster_state_hash, lhs._depth_stencil_state_hash, lhs._topology_hash, lhs._stencil_ref,
                            lhs._force_alpha_test)
                 < std::tie(rhs._render_queue, rhs._shader_ptr, rhs._pass_index, rhs._variant_hash, rhs._material_ptr, rhs._mesh_ptr,
                            rhs._vertex_buffer, rhs._index_buffer, rhs._submesh_index, rhs._blend_state_hash,
                            rhs._raster_state_hash, rhs._depth_stencil_state_hash, rhs._topology_hash, rhs._stencil_ref,
                            rhs._force_alpha_test);
        }

        void EmitQueuedDraws(CommandBuffer *cmd, Vector<QueuedDrawItem> &items)
        {
            struct QueuedDrawBatch
            {
                u32 _item_index = 0u;
                u32 _instance_count = 0u;
                CBufferPrimitiveDrawData _primitive_draw_data{};
            };

            EngineConfig &config = Ailu::g_engine_config;
            const bool can_sort = std::all_of(items.begin(), items.end(), [](const QueuedDrawItem &item)
            {
                return item._render_queue < Shader::kRenderQueueTransparent;
            });
            if (config.EnableCpuStateBatchedSubmission && can_sort && items.size() > 1u)
                std::stable_sort(items.begin(), items.end(), CompareQueuedDrawItem);

            Vector<QueuedDrawBatch> batches;
            batches.reserve(items.size());
            for (u32 item_index = 0u; item_index < items.size();)
            {
                auto &item = items[item_index];
                u32 batch_end = item_index + 1u;
                while (batch_end < items.size() && CompareQueuedDrawItem(item, items[batch_end]) == false
                       && CompareQueuedDrawItem(items[batch_end], item) == false)
                    ++batch_end;
                const u32 instance_count = batch_end - item_index;
                CBufferPrimitiveDrawData draw_data = item._primitive_draw_data;
                bool is_contiguous = true;
                for (u32 index = item_index + 1u; index < batch_end; ++index)
                {
                    if (items[index]._primitive_draw_data._primitive_base != draw_data._primitive_base + index - item_index)
                    {
                        is_contiguous = false;
                        break;
                    }
                }
                if (!is_contiguous)
                {
                    auto allocation = FrameResource::Active()->AllocatePrimitiveIndices(instance_count);
                    for (u32 index = 0u; index < instance_count; ++index)
                        allocation._cpu_ptr[index] = items[item_index + index]._primitive_draw_data._primitive_base;
                    draw_data._instance_index_offset = allocation._offset;
                    draw_data._flags |= kPrimitiveDrawUseInstanceIndex;
                }
                batches.emplace_back(QueuedDrawBatch{item_index, instance_count, draw_data});
                item_index = batch_end;
            }

            // Draw snapshots capture a buffer data version.  Upload the complete indirection table before
            // constructing any draw command so every batch binds this frame's version rather than the prior slot's.
            FrameResource::Active()->UploadPrimitiveIndices();
            for (const QueuedDrawBatch &batch: batches)
            {
                auto &item = items[batch._item_index];
                bool was_alpha_test = false;
                float alpha_cutoff = 0.0f;
                if (item._force_alpha_test)
                {
                    was_alpha_test = item._material->ActiveKeywords(item._pass_index).contains("ALPHA_TEST");
                    alpha_cutoff = item._material->GetFloat("_AlphaCulloff");
                    item._material->EnableKeyword("ALPHA_TEST");
                    item._material->SetFloat("_AlphaCulloff", 0.5f);
                }
                item._material->GetShader()->_stencil_ref = item._stencil_ref;
                cmd->DrawSceneMesh(item._vertex_buffer, item._index_buffer, item._material, item._submesh_index, item._pass_index,
                                   batch._primitive_draw_data, batch._instance_count);
                if (item._force_alpha_test)
                {
                    item._material->SetFloat("_AlphaCulloff", alpha_cutoff);
                    if (!was_alpha_test)
                        item._material->DisableKeyword("ALPHA_TEST");
                }
            }
        }

        void EmitQueuedDraws(const Ref<CommandBuffer> &cmd, Vector<QueuedDrawItem> &items)
        {
            EmitQueuedDraws(cmd.get(), items);
        }

        void DrawShadowView(CommandBuffer *cmd, RenderTexture *shadow_map, const ShadowView &view)
        {
            if (cmd == nullptr || shadow_map == nullptr)
                return;
            const bool is_point_shadow = view._type == EShadowViewType::kPoint;
            const u16 dsv_view_index = is_point_shadow
                ? shadow_map->CalculateViewIndex(Texture::ETextureViewType::kDSV,
                                                 static_cast<ECubemapFace>(view._target_slice + 1u), 0u,
                                                 view._shadow_map_index)
                : shadow_map->CalculateViewIndex(Texture::ETextureViewType::kDSV, 0u, view._target_slice);
            cmd->SetRenderTarget(nullptr, shadow_map, 0u, dsv_view_index);
            cmd->ClearRenderTarget(kZFar, 0u);

            CBufferPerCameraData camera_data{};
            camera_data._MatrixVP = view._shadow_matrix;
            camera_data._CameraPos = Vector4f(view._light_world_position, 0.0f);
            if (is_point_shadow)
                camera_data._ZBufferParams = Vector4f(view._camera_near, view._camera_far, 0.0f, 0.0f);
            cmd->SetGlobalBuffer(RenderConstants::kCBufNamePerCamera, &camera_data, RenderConstants::kPerCameraDataSize);

            Vector<QueuedDrawItem> draw_items;
            for (const auto &[queue, objects] : view._cull_result)
            {
                for (const auto &object : objects)
                {
                    Material *shadow_material = object._material;
                    if (shadow_material == nullptr || shadow_material->GetActiveShader() == nullptr)
                        continue;
                    const i16 shadow_pass = shadow_material->GetActiveShader()->FindPass("ShadowCaster");
                    if (shadow_pass == -1)
                        continue;
                    if (is_point_shadow)
                        shadow_material->EnableKeyword("CAST_POINT_SHADOW");
                    else
                        shadow_material->DisableKeyword("CAST_POINT_SHADOW");
                    draw_items.emplace_back(MakeQueuedDrawItem(queue, object, shadow_material,
                                                               static_cast<u16>(shadow_pass),
                                                               shadow_material->SurfaceType() == ESurfaceType::kTransparent));
                }
            }
            EmitQueuedDraws(cmd, draw_items);
            if (is_point_shadow)
            {
                for (const auto &item : draw_items)
                    item._material->DisableKeyword("CAST_POINT_SHADOW");
            }
        }
    }

#pragma region ForwardPass
    //-------------------------------------------------------------OpaqueRenderPass-------------------------------------------------------------
    ForwardPass::ForwardPass() : RenderPass("TransparentPass")
    {
        shader_state_mat = MakeRef<Material>(ResourceMgr::Get().Get<Shader>(L"Shaders/hlsl/debug.alasset"), "ShaderStateDebug");
        _error_shader_pass_id = 0;
        _compiling_shader_pass_id = 1;
        _forward_lit_shader = ResourceMgr::Get().Get<Shader>(L"Shaders/hlsl/forwardlit.alasset");
        AL_ASSERT(_forward_lit_shader != nullptr);
        _brdf_lut = ResourceMgr::Get().Load<Texture2D>(L"Textures/ibl_brdf_lut.alasset");
        TextureDesc shadow_desc(1u, 1u, ERenderTargetFormat::kShadowMap);
        shadow_desc._array_size = 1u;
        shadow_desc._dimension = ETextureDimension::kTex2DArray;
        _dummy_main_light_shadow_map = RenderTexture::Create(shadow_desc, "_DummyForwardMainLightShadowMap");
        _dummy_add_light_shadow_maps = RenderTexture::Create(shadow_desc, "_DummyForwardAddLightShadowMaps");
        shadow_desc._dimension = ETextureDimension::kCubeArray;
        _dummy_point_light_shadow_maps = RenderTexture::Create(shadow_desc, "_DummyForwardPointLightShadowMaps");
        _event = static_cast<ERenderPassEvent>(static_cast<u16>(ERenderPassEvent::kBeforeTransparent) + 25u);
    }
    ForwardPass::~ForwardPass()
    {
    }

    void Ailu::Render::ForwardPass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData & rendering_data)
    {
        const bool use_shadow_maps = rendering_data._shadow_set != nullptr && rendering_data._shadow_set->_valid;
        graph.AddPass(_name, RDG::PassDesc(), [&](RDG::RenderGraphBuilder &builder)
                      { 
                          builder.Read(rendering_data._rg_handles._color_target, EResourceUsage::kWriteRTV);
                          builder.Read(rendering_data._rg_handles._depth_target, EResourceUsage::kDSV);
                          if (use_shadow_maps)
                          {
                              builder.Read(rendering_data._rg_handles._main_light_shadow_map);
                              builder.Read(rendering_data._rg_handles._addi_shadow_maps);
                              builder.Read(rendering_data._rg_handles._point_light_shadow_maps);
                          }
                          if (rendering_data._rg_handles._ao_tex.IsValid())
                              builder.Read(rendering_data._rg_handles._ao_tex);
                          if (rendering_data._rg_handles._environment_sh.IsValid())
                          builder.Read(rendering_data._rg_handles._environment_sh);
                          builder.Read(rendering_data._rg_handles._environment_diffuse);
                          builder.Read(rendering_data._rg_handles._environment_specular);
                          rendering_data._rg_handles._color_target = builder.Write(rendering_data._rg_handles._color_target);
                          rendering_data._rg_handles._depth_target = builder.Write(rendering_data._rg_handles._depth_target,EResourceUsage::kDSV);
                      }, 
        [this, use_shadow_maps](RDG::RenderGraph& graph,CommandBuffer* cmd, const RenderingData &rendering_data){
        auto &all_renderable = *rendering_data._cull_results;
        u32 lowerBound = Shader::kRenderQueueOpaque, upperBound = Shader::kRenderQueueEnd;
        auto filtered = all_renderable | std::views::filter([lowerBound, upperBound](const auto &kv)
                                                            { return kv.first >= lowerBound && kv.first <= upperBound; }) | std::views::values;
        u32 obj_num = 0;
        for (auto &r: filtered)
        {
            obj_num += (u32)r.size();
        }
        if (obj_num == 0)
            return;
        Texture *main_light_shadow_map = _dummy_main_light_shadow_map.get();
        Texture *add_light_shadow_maps = _dummy_add_light_shadow_maps.get();
        Texture *point_light_shadow_maps = _dummy_point_light_shadow_maps.get();
        if (use_shadow_maps)
        {
            main_light_shadow_map = graph.Resolve<Texture>(rendering_data._rg_handles._main_light_shadow_map);
            add_light_shadow_maps = graph.Resolve<Texture>(rendering_data._rg_handles._addi_shadow_maps);
            point_light_shadow_maps = graph.Resolve<Texture>(rendering_data._rg_handles._point_light_shadow_maps);
        }
        auto *occlusion_tex = rendering_data._rg_handles._ao_tex.IsValid()
            ? graph.Resolve<Texture>(rendering_data._rg_handles._ao_tex) : Texture::s_p_default_white;
        cmd->SetGlobalTexture("IBLLut", _brdf_lut.get());
        if (rendering_data._environment != nullptr)
        {
            cmd->SetGlobalTexture("RadianceTex", graph.Resolve<Texture>(rendering_data._rg_handles._environment_diffuse));
            cmd->SetGlobalTexture("PrefilterEnvTex", graph.Resolve<Texture>(rendering_data._rg_handles._environment_specular));
        }
        cmd->SetGlobalTexture("_OcclusionTex", occlusion_tex ? occlusion_tex : Texture::s_p_default_white);
        cmd->SetGlobalTexture(RenderResourceName::kMainLightShadowMap, main_light_shadow_map);
        cmd->SetGlobalTexture(RenderResourceName::kAddLightShadowMap, add_light_shadow_maps);
        cmd->SetGlobalTexture(RenderResourceName::kPointLightShadowMap, point_light_shadow_maps);
        cmd->SetRenderTargetLoadAction(rendering_data._rg_handles._color_target, ELoadStoreAction::kLoad);
        cmd->SetRenderTargetLoadAction(rendering_data._rg_handles._depth_target, ELoadStoreAction::kLoad);
        cmd->SetRenderTarget(rendering_data._rg_handles._color_target, rendering_data._rg_handles._depth_target);
            for (auto &it: all_renderable)
            {
                auto &[queue, objs] = it;
                if (queue >= Shader::kRenderQueueTransparent)
                {
                    for (auto &obj: objs)
                    {
                        if (obj._material == nullptr)
                            cmd->DrawSceneMesh(obj._vertex_buffer, obj._index_buffer, shader_state_mat.get(), obj._submesh_index,
                                               _error_shader_pass_id, CBufferPrimitiveDrawData{obj._primitive_index}, 1u);
                        else
                            cmd->DrawSceneMesh(obj._vertex_buffer, obj._index_buffer, obj._material, obj._submesh_index, 0u,
                                               CBufferPrimitiveDrawData{obj._primitive_index}, 1u);
                    }
                }
            } });
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
            obj_num += (u32) r.size();
        }
        if (obj_num == 0)
            return;
        auto cmd = CommandBufferPool::Get(_name);
        {
            PROFILE_BLOCK_GPU(cmd.get(), _name)
            cmd->SetGlobalTexture("IBLLut", _brdf_lut.get());
            if (rendering_data._environment != nullptr)
            {
                cmd->SetGlobalTexture("RadianceTex", rendering_data._environment->_diffuse_irradiance.get());
                cmd->SetGlobalTexture("PrefilterEnvTex", rendering_data._environment->_specular_prefilter.get());
            }
            cmd->SetRenderTarget(rendering_data._camera_color_target_handle, rendering_data._camera_depth_target_handle);
            for (auto &it: all_renderable)
            {
                auto &[queue, objs] = it;
                if (queue >= Shader::kRenderQueueTransparent)
                {
                    for (auto &obj: objs)
                    {
                        if (obj._material == nullptr)
                            cmd->DrawSceneMesh(obj._vertex_buffer, obj._index_buffer, shader_state_mat.get(), obj._submesh_index,
                                               _error_shader_pass_id, CBufferPrimitiveDrawData{obj._primitive_index}, 1u);
                        else
                            cmd->DrawSceneMesh(obj._vertex_buffer, obj._index_buffer, obj._material, obj._submesh_index, 0u,
                                               CBufferPrimitiveDrawData{obj._primitive_index}, 1u);
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
    ShadowCastPass::ShadowCastPass(ShadowSystem *shadow_system) : RenderPass("ShadowCastPass"), _shadow_system(shadow_system)
    {
        _event = static_cast<ERenderPassEvent>(static_cast<u16>(ERenderPassEvent::kBeforeShaodwMap) + 25u);
    }

    void Ailu::Render::ShadowCastPass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData & rendering_data)
    {
        graph.AddPass(_name, RDG::PassDesc(), [&, this](RDG::RenderGraphBuilder &builder)
        {
            if (rendering_data._render_shadow_maps && rendering_data._shadow_set != nullptr &&
                rendering_data._shadow_set->_valid)
            {
                rendering_data._rg_handles._main_light_shadow_map =
                    builder.Write(rendering_data._rg_handles._main_light_shadow_map, EResourceUsage::kDSV);
                rendering_data._rg_handles._addi_shadow_maps =
                    builder.Write(rendering_data._rg_handles._addi_shadow_maps, EResourceUsage::kDSV);
                rendering_data._rg_handles._point_light_shadow_maps =
                    builder.Write(rendering_data._rg_handles._point_light_shadow_maps, EResourceUsage::kDSV);
            }
            else
            {
                builder.Read(rendering_data._rg_handles._main_light_shadow_map);
                builder.Read(rendering_data._rg_handles._addi_shadow_maps);
                builder.Read(rendering_data._rg_handles._point_light_shadow_maps);
            }
        }, [this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
        {
            if (!data._render_shadow_maps || data._shadow_set == nullptr || !data._shadow_set->_valid)
                return;
            auto *directional_map = graph.Resolve<RenderTexture>(data._rg_handles._main_light_shadow_map);
            auto *additional_map = graph.Resolve<RenderTexture>(data._rg_handles._addi_shadow_maps);
            auto *point_map = graph.Resolve<RenderTexture>(data._rg_handles._point_light_shadow_maps);
            for (const ShadowView &view : data._shadow_set->_views)
            {
                RenderTexture *target = view._type == EShadowViewType::kDirectional ? directional_map
                    : view._type == EShadowViewType::kPoint ? point_map : additional_map;
                DrawShadowView(cmd, target, view);
            }
        });
    }

    void ShadowCastPass::BeginPass(GraphicsContext *context)
    {
    }
    void ShadowCastPass::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        if (_shadow_system == nullptr || !rendering_data._render_shadow_maps || rendering_data._shadow_set == nullptr ||
            !rendering_data._shadow_set->_valid)
            return;
        auto cmd = CommandBufferPool::Get(_name);
        cmd->Clear();
        {
            PROFILE_BLOCK_GPU(cmd.get(), _name)
            for (const ShadowView &view : rendering_data._shadow_set->_views)
            {
                RenderTexture *target = view._type == EShadowViewType::kDirectional
                    ? _shadow_system->GetDirectionalShadowMap(rendering_data._shadow_set->_directional._mode)
                    : view._type == EShadowViewType::kPoint ? _shadow_system->GetPointShadowMap()
                    : _shadow_system->GetAdditionalShadowMap();
                DrawShadowView(cmd.get(), target, view);
            }
        }
        Shader::SetGlobalTexture(RenderResourceName::kMainLightShadowMap,
                                 _shadow_system->GetDirectionalShadowMap(rendering_data._shadow_set->_directional._mode));
        Shader::SetGlobalTexture(RenderResourceName::kAddLightShadowMap, _shadow_system->GetAdditionalShadowMap());
        Shader::SetGlobalTexture(RenderResourceName::kPointLightShadowMap, _shadow_system->GetPointShadowMap());
        context->ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
        return;
    }

    void ShadowCastPass::EndPass(GraphicsContext *context)
    {
        RenderPass::EndPass(context);
    }
#pragma endregion

    //-------------------------------------------------------------ShadowCastPass-------------------------------------------------------------


#pragma region CubeMapGenPass
    //-------------------------------------------------------------CubeMapGenPass-------------------------------------------------------------

    CubeMapGenPass::CubeMapGenPass(Texture *src_tex, u16 size) : RenderPass("CubeMapGenPass")
    {
        Initialize(src_tex, nullptr, size);
    }

    CubeMapGenPass::CubeMapGenPass(Material *source_material, u16 size) : RenderPass("CubeMapGenPass")
    {
        Initialize(nullptr, source_material, size);
    }

    void CubeMapGenPass::Initialize(Texture *src_tex, Material *source_material, u16 size)
    {
        _is_procedural_source = source_material != nullptr;
        _is_src_cubemap = !_is_procedural_source && src_tex->Dimension() == ETextureDimension::kCube;
        _cubemap_rect = Rect{0, 0, size, size};
        _ibl_rect = Rect{0, 0, (u16) (size / 4), (u16) (size / 4)};
        const String source_name = _is_procedural_source ? "ProceduralSky" : src_tex->Name();
        float x = 0.f, y = 0.f, z = 0.f;
        Vector3f center = {x, y, z};
        Vector3f world_up = Vector3f::kUp;
        if (!_is_src_cubemap)
        {
            _src_cubemap = RenderTexture::Create(size, source_name + "_src_cubemap", ERenderTargetFormat::kDefaultHDR,
                                                 true, true, true);
        }
        else
            _input_src = src_tex;

        _prefilter_cubemap = RenderTexture::Create(size, source_name + "_prefilter_cubemap", ERenderTargetFormat::kDefaultHDR,
                                                   true, true, true);
        _prefilter_cubemap->_load_action = ELoadStoreAction::kNotCare;
        _radiance_map = RenderTexture::Create(size / 4, source_name + "_radiance", ERenderTargetFormat::kDefaultHDR, false);
        _radiance_map->_load_action = ELoadStoreAction::kNotCare;
        _p_gen_material = source_material != nullptr ? source_material : ResourceMgr::Get().Get<Material>(L"Runtime/Material/CubemapGen");
        if (!_is_procedural_source)
            _p_gen_material->SetTexture("env", src_tex);
        _p_filter_material = ResourceMgr::Get().Get<Material>(L"Runtime/Material/EnvmapFilter");
        Matrix4x4f view, proj;
        BuildPerspectiveFovLHMatrix(proj, 90 * k2Radius, 1.0f, 0.01f, 100.f);
        //BuildPerspectiveFovLHMatrix(proj, 2.0 * atan((f32)size / ((f32)size - 0.5)), 1.0, 1.0, 100000);
        float scale = 20.0f;
        MatrixScale(_world_mat, scale, scale, scale);
        _per_obj_cb = ConstantBuffer::Create(RenderConstants::kPerObjectDataSize);
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
            _per_camera_cb[i] = ConstantBuffer::Create(RenderConstants::kPerCameraDataSize);
            memcpy(_per_camera_cb[i]->GetData(), &_camera_data[i], RenderConstants::kPerCameraDataSize);
        }
        f32 mipmap_level = _prefilter_cubemap->MipmapLevel();
        for (f32 i = 0.0f; i < mipmap_level; i++)
        {
            u16 cur_mipmap_size = size >> (u16) i;
            _reflection_prefilter_mateirals.emplace_back(MakeRef<Material>(ResourceMgr::Get().Get<Shader>(L"Shaders/hlsl/filter_irradiance.alasset"), "ReflectionPrefilter"));
            _reflection_prefilter_mateirals.back()->SetFloat("_roughness", i / mipmap_level);
            _reflection_prefilter_mateirals.back()->SetFloat("_width", cur_mipmap_size);
            //_reflection_prefilter_mateirals.back()->SetTexture("SrcTex", ToWChar(src_texture_name));
            _reflection_prefilter_mateirals.back()->SetTexture("EnvMap", _src_cubemap.get());
        }
    }

    void Ailu::Render::CubeMapGenPass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
    {
        Texture *src_tex = _input_src ? _input_src : _src_cubemap.get();
        const bool source_uses_graph_handle = !_is_src_cubemap;
        if(!_is_src_cubemap)
        {
            graph.AddPass("GenCubeMap",RDG::PassDesc(), [&](RDG::RenderGraphBuilder &builder) { 
                _src_map_handle = builder.Import(_src_cubemap.get());
                _src_map_handle = builder.WriteRange(_src_map_handle, EResourceUsage::kWriteRTV, 0, 1, 0, 6);
                if (_is_procedural_source)
                {
                    builder.Read(rendering_data._rg_handles._sky_view_lut);
                    builder.Read(rendering_data._rg_handles._environment_transmittance_lut);
                }
            }, [this, src_tex](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data) {
                        //image tp cubemap
                auto *dst_cubemap = graph.Resolve<RenderTexture>(_src_map_handle);
                if (src_tex == nullptr || dst_cubemap == nullptr)
                    return;
                if (_is_procedural_source && data._environment != nullptr)
                {
                    _p_gen_material->SetTexture("_TexSkyViewLUT", graph.Resolve<Texture>(data._rg_handles._sky_view_lut));
                    _p_gen_material->SetTexture("_TexTransmittanceLUT",
                                                 graph.Resolve<Texture>(data._rg_handles._environment_transmittance_lut));
                    // The generated cubemap is the canonical environment. Runtime intensity and
                    // rotation are applied by each consumer when sampling it.
                    _p_gen_material->SetFloat("_EnvironmentIntensity", 1.0f);
                    _p_gen_material->SetFloat("_EnvironmentRotation", 0.0f);
                    _p_gen_material->SetVector("_SkyColor", data._environment->_sky_color);
                    _p_gen_material->SetVector("_GroundColor", data._environment->_ground_color);
                }
                for (u16 i = 0; i < 6; i++)
                {
                    u16 rt_index = dst_cubemap->CalculateViewIndex(Texture::ETextureViewType::kRTV, (ECubemapFace)(i + 1), 0, 0);
                    cmd->SetRenderTarget(dst_cubemap, rt_index);
                    cmd->ClearRenderTarget(Colors::kBlack);
                    cmd->SetGlobalBuffer(RenderConstants::kCBufNamePerCamera, _per_camera_cb[i].get());
                    cmd->DrawMesh(Mesh::s_cube.lock().get(), _p_gen_material, _per_obj_cb.get(), 0, 0, 1);
                }
            });
        }
        //gen radiance map
        graph.AddPass("RadianceGen", RDG::PassDesc(), [&](RDG::RenderGraphBuilder &builder)
                { 
        if (source_uses_graph_handle)
            builder.Read(_src_map_handle);
        _radiance_handle = builder.Import(_radiance_map.get());
        _radiance_handle = builder.Write(_radiance_handle); },
        [this, source_uses_graph_handle](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
        {
        Texture *src_tex = source_uses_graph_handle ? graph.Resolve<Texture>(_src_map_handle)
                                                    : (_input_src ? _input_src : _src_cubemap.get());
        auto *radiance_map = graph.Resolve<RenderTexture>(_radiance_handle);
        if (src_tex == nullptr || radiance_map == nullptr)
            return;
        _p_filter_material->SetTexture("EnvMap", src_tex);
        for (u16 i = 0; i < 6; i++)
        {
            u16 rt_index = radiance_map->CalculateViewIndex(Texture::ETextureViewType::kRTV, (ECubemapFace)(i + 1), 0, 0);
            cmd->SetRenderTarget(radiance_map, rt_index);
            //cmd->ClearRenderTarget(_radiance_map.get(), Colors::kBlack, rt_index);
            cmd->SetGlobalBuffer(RenderConstants::kCBufNamePerCamera, _per_camera_cb[i].get());
            cmd->DrawMesh(Mesh::s_cube.lock().get(), _p_filter_material, _per_obj_cb.get());
        }
        });
        //filter envmap
        graph.AddPass("EnvmapGen", RDG::PassDesc(), [&](RDG::RenderGraphBuilder &builder)
                      { 
        if (source_uses_graph_handle)
            builder.Read(_src_map_handle);
        _env_handle = builder.Import(_prefilter_cubemap.get());
        _env_handle = builder.Write(_env_handle); }, [this, source_uses_graph_handle](RDG::RenderGraph &graph,
                                                                                        CommandBuffer *cmd,
                                                                                        const RenderingData &data)
                      {
        Texture *src_tex = source_uses_graph_handle ? graph.Resolve<Texture>(_src_map_handle)
                                                    : (_input_src ? _input_src : _src_cubemap.get());
        auto *prefilter_map = graph.Resolve<RenderTexture>(_env_handle);
        if (src_tex == nullptr || prefilter_map == nullptr)
            return;
        const auto mipmap_level = prefilter_map->MipmapLevel();
        for (u16 i = 0; i < 6; i++)
        {
            //Rect r(0, 0, _prefilter_cubemap->Width(), _prefilter_cubemap->Height());
            for (u16 j = 0; j < mipmap_level; j++)
            {
                _reflection_prefilter_mateirals[j]->SetTexture("EnvMap", src_tex);
                //auto [w, h] = Texture::CalculateMipSize(_prefilter_cubemap->Width(), _prefilter_cubemap->Height(), j);
                //r.width = w;
                //r.height = h;
                u16 rt_index = prefilter_map->CalculateViewIndex(Texture::ETextureViewType::kRTV, (ECubemapFace)(i + 1), j, 0);
                //cmd->SetViewport(r);
                //cmd->SetScissorRect(r);
                cmd->SetRenderTarget(prefilter_map, rt_index);
                cmd->SetViewport(Rect{0, 0, static_cast<u16>(std::max<u32>(1u, _prefilter_cubemap->Width() >> j)),
                                      static_cast<u16>(std::max<u32>(1u, _prefilter_cubemap->Height() >> j))});
                cmd->SetGlobalBuffer(RenderConstants::kCBufNamePerCamera, _per_camera_cb[i].get());
                cmd->DrawMesh(Mesh::s_cube.lock().get(), _reflection_prefilter_mateirals[j].get(), _per_obj_cb.get(), 0, 1, 1);
            }
        }
        });
        if (source_uses_graph_handle)
            rendering_data._rg_handles._environment_sky = _src_map_handle;
    }
    void CubeMapGenPass::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        auto cmd = CommandBufferPool::Get("LightProbeGen");
        cmd->Clear();
        Texture *src_tex = _input_src ? _input_src : _src_cubemap.get();
        u16 mipmap_level = src_tex->MipmapLevel();
        if (!_is_src_cubemap)
        {
            if (_is_procedural_source && rendering_data._environment != nullptr)
            {
                _p_gen_material->SetTexture("_TexSkyViewLUT", rendering_data._environment->_sky_view_lut.get());
                _p_gen_material->SetTexture("_TexTransmittanceLUT", rendering_data._environment->_transmittance_lut.get());
                // Keep the baked cubemap canonical; runtime consumers own these transforms.
                _p_gen_material->SetFloat("_EnvironmentIntensity", 1.0f);
                _p_gen_material->SetFloat("_EnvironmentRotation", 0.0f);
                _p_gen_material->SetVector("_SkyColor", rendering_data._environment->_sky_color);
                _p_gen_material->SetVector("_GroundColor", rendering_data._environment->_ground_color);
            }
            //image tp cubemap
            for (u16 i = 0; i < 6; i++)
            {
                u16 rt_index = _src_cubemap->CalculateViewIndex(Texture::ETextureViewType::kRTV, (ECubemapFace)(i + 1), 0, 0);
                cmd->SetRenderTarget(_src_cubemap.get(), rt_index);
                cmd->ClearRenderTarget(Colors::kBlack);
                cmd->SetGlobalBuffer(RenderConstants::kCBufNamePerCamera, _per_camera_cb[i].get());
                cmd->DrawMesh(Mesh::s_cube.lock().get(), _p_gen_material, _per_obj_cb.get(), 0, 0, 1);
            }
            context->ExecuteCommandBuffer(cmd);
            cmd->Clear();
        }

        //gen radiance map
        _p_filter_material->SetTexture("EnvMap", src_tex);
        for (u16 i = 0; i < 6; i++)
        {
            u16 rt_index = _radiance_map->CalculateViewIndex(Texture::ETextureViewType::kRTV, (ECubemapFace)(i + 1), 0, 0);
            cmd->SetRenderTarget(_radiance_map.get(), rt_index);
            //cmd->ClearRenderTarget(_radiance_map.get(), Colors::kBlack, rt_index);
            cmd->SetGlobalBuffer(RenderConstants::kCBufNamePerCamera, _per_camera_cb[i].get());
            cmd->DrawMesh(Mesh::s_cube.lock().get(), _p_filter_material, _per_obj_cb.get());
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
                u16 rt_index = _prefilter_cubemap->CalculateViewIndex(Texture::ETextureViewType::kRTV, (ECubemapFace)(i + 1), j, 0);
                cmd->SetViewport(r);
                cmd->SetRenderTarget(_prefilter_cubemap.get(), rt_index);
                //cmd->ClearRenderTarget(_prefilter_cubemap.get(), Colors::kBlack, rt_index);
                cmd->SetGlobalBuffer(RenderConstants::kCBufNamePerCamera, _per_camera_cb[i].get());
                cmd->DrawMesh(Mesh::s_cube.lock().get(), _reflection_prefilter_mateirals[j].get(), _per_obj_cb.get(), 0, 1, 1);
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
        _event = static_cast<ERenderPassEvent>(static_cast<u16>(ERenderPassEvent::kBeforeGbuffer) + 25u);
    }

    void Ailu::Render::DeferredGeometryPass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
    {
        graph.AddPass(_name, RDG::PassDesc(), [&](RDG::RenderGraphBuilder &builder)
                      { 
                          rendering_data._rg_handles._gbuffers[0] = builder.Write(rendering_data._rg_handles._gbuffers[0]);
                          rendering_data._rg_handles._gbuffers[1] = builder.Write(rendering_data._rg_handles._gbuffers[1]);
                          rendering_data._rg_handles._gbuffers[2] = builder.Write(rendering_data._rg_handles._gbuffers[2]);
                          rendering_data._rg_handles._gbuffers[3] = builder.Write(rendering_data._rg_handles._gbuffers[3]);
                          rendering_data._rg_handles._depth_target = builder.Write(rendering_data._rg_handles._depth_target,EResourceUsage::kDSV);
                          if (rendering_data._rg_handles._environment_sh.IsValid())
                              builder.Read(rendering_data._rg_handles._environment_sh);
                      },
                      [this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &rendering_data)
                      {
                          cmd->SetRenderTargets(rendering_data._rg_handles._gbuffers, rendering_data._rg_handles._depth_target);
                        cmd->ClearRenderTarget(kZFar, 0u);
                        Vector<QueuedDrawItem> draw_items;
                        for (auto &it: *rendering_data._cull_results)
                        {
                            auto &[queue, objs] = it;
                            if (queue >= Shader::kRenderQueueTransparent)
                                break;
                            for (auto &obj: objs)
                            {
                                obj._material->GetShader()->_stencil_ref = (obj._flags & kPrimitivePerObjectMotion) ? 1 : 0;
                                draw_items.emplace_back(MakeQueuedDrawItem(queue, obj, obj._material, 0u));
                            }
                        }
                        EmitQueuedDraws(cmd, draw_items);
                      });
    }

    void DeferredGeometryPass::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        auto w = rendering_data._width, h = rendering_data._height;
        auto cmd = CommandBufferPool::Get("DeferredRenderPass");
        {
            PROFILE_BLOCK_GPU(cmd.get(), _name)
            cmd->SetRenderTargetLoadAction(rendering_data._camera_color_target_handle, ELoadStoreAction::kNotCare);
            cmd->SetRenderTargetLoadAction(rendering_data._camera_depth_target_handle, ELoadStoreAction::kClear);
            cmd->SetRenderTargets(rendering_data._gbuffers, rendering_data._camera_depth_target_handle);
            Vector<QueuedDrawItem> draw_items;
            for (auto &it: *rendering_data._cull_results)
            {
                auto &[queue, objs] = it;
                if (queue >= Shader::kRenderQueueTransparent)
                    break;
                for (auto &obj: objs)
                {
                    obj._material->GetShader()->_stencil_ref = (obj._flags & kPrimitivePerObjectMotion) ? 1 : 0;
                    draw_items.emplace_back(MakeQueuedDrawItem(queue, obj, obj._material, 0u));
                }
            }
            EmitQueuedDraws(cmd.get(), draw_items);
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
        _p_lighting_material = MakeRef<Material>(ResourceMgr::Get().Get<Shader>(L"Shaders/hlsl/deferred_lighting.alasset"), "DeferedGbufferLighting");
        _p_lighting_material->EnableKeyword("STATIC_DIFFUSE_PROBE_GPU");
        _brdf_lut = ResourceMgr::Get().Load<Texture2D>(L"Textures/ibl_brdf_lut.alasset");
        TextureDesc shadow_desc(1u, 1u, ERenderTargetFormat::kShadowMap);
        shadow_desc._array_size = 1u;
        shadow_desc._dimension = ETextureDimension::kTex2DArray;
        _dummy_main_light_shadow_map = RenderTexture::Create(shadow_desc, "_DummyMainLightShadowMap");
        _dummy_add_light_shadow_maps = RenderTexture::Create(shadow_desc, "_DummyAddLightShadowMaps");
        shadow_desc._dimension = ETextureDimension::kCubeArray;
        _dummy_point_light_shadow_map = RenderTexture::Create(shadow_desc, "_DummyPointLightShadowMap");

        TextureDesc volumetric_desc;
        volumetric_desc._width = 1u;
        volumetric_desc._height = 1u;
        volumetric_desc._depth = 1u;
        volumetric_desc._format = EALGFormat::kALGFormatR16G16B16A16_FLOAT;
        volumetric_desc._mip_num = 1u;
        _dummy_volumetric_light = Texture3D::Create(volumetric_desc);
        _dummy_volumetric_light->Name("_DummyVolumetricLight");
        _dummy_volumetric_light->SetPixel(0u, 0u, 0u, Colors::kBlack, 0u);
        _dummy_volumetric_light->Apply();
        _event = static_cast<ERenderPassEvent>(static_cast<u16>(ERenderPassEvent::kBeforeDeferedLighting) + 25u);
    }

    void Ailu::Render::DeferredLightingPass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
    {
        const bool use_shadow_maps = rendering_data._shadow_set != nullptr && rendering_data._shadow_set->_valid;
        const bool use_surfel_query = _surfel_gi != nullptr && _surfel_gi->_ready_for_current_view &&
                                      _surfel_gi->_is_active &&
                                      _surfel_gi->_has_active_component && _surfel_gi->_settings._enabled &&
                                      _surfel_gi->_settings._lighting_enabled &&
                                      _surfel_gi->_surfel_pool != nullptr &&
                                      _surfel_gi->_surfel_lighting != nullptr &&
                                      _surfel_gi->_surfel_state != nullptr &&
                                      _surfel_gi->_query_grid_cells != nullptr &&
                                      _surfel_gi->_query_grid_indices != nullptr &&
                                      _surfel_gi->_query_debug_counters != nullptr;
        RDG::RGHandle surfel_pool;
        RDG::RGHandle surfel_lighting;
        RDG::RGHandle surfel_state;
        RDG::RGHandle surfel_query_grid_cells;
        RDG::RGHandle surfel_query_grid_indices;
        RDG::RGHandle surfel_query_debug_counters;
        if (use_surfel_query)
        {
            surfel_pool = graph.Import(_surfel_gi->_surfel_pool.get());
            surfel_lighting = graph.Import(_surfel_gi->_surfel_lighting.get());
            surfel_state = graph.Import(_surfel_gi->_surfel_state.get());
            surfel_query_grid_cells = graph.Import(_surfel_gi->_query_grid_cells.get());
            surfel_query_grid_indices = graph.Import(_surfel_gi->_query_grid_indices.get());
            surfel_query_debug_counters = graph.Import(_surfel_gi->_query_debug_counters.get());
        }
        graph.AddPass(_name, RDG::PassDesc(), [&](RDG::RenderGraphBuilder &builder)
                      { 
                          builder.Read(rendering_data._rg_handles._gbuffers[0]);
                          builder.Read(rendering_data._rg_handles._gbuffers[1]);
                          builder.Read(rendering_data._rg_handles._gbuffers[2]);
                          builder.Read(rendering_data._rg_handles._gbuffers[3]);
                          builder.Read(rendering_data._rg_handles._depth_target);
                          if (use_shadow_maps)
                          {
                              builder.Read(rendering_data._rg_handles._main_light_shadow_map);
                              builder.Read(rendering_data._rg_handles._addi_shadow_maps);
                              builder.Read(rendering_data._rg_handles._point_light_shadow_maps);
                          }
                          if (rendering_data._rg_handles._ao_tex.IsValid())
                              builder.Read(rendering_data._rg_handles._ao_tex);
                          if (rendering_data._rg_handles._volumetric_fog_accum.IsValid())
                              builder.Read(rendering_data._rg_handles._volumetric_fog_accum);
                          builder.Read(rendering_data._rg_handles._environment_diffuse);
                          builder.Read(rendering_data._rg_handles._environment_specular);
                          if (rendering_data._ddgi_lighting._irradiance_atlas_handle.IsValid())
                              builder.Read(rendering_data._ddgi_lighting._irradiance_atlas_handle);
                          if (rendering_data._ddgi_lighting._distance_atlas_handle.IsValid())
                              builder.Read(rendering_data._ddgi_lighting._distance_atlas_handle);
                          if (rendering_data._ddgi_lighting._probe_data_handle.IsValid())
                              builder.Read(rendering_data._ddgi_lighting._probe_data_handle);
                          if (use_surfel_query)
                          {
                              builder.Read(surfel_pool, EResourceUsage::kReadSRV);
                              builder.Read(surfel_lighting, EResourceUsage::kReadSRV);
                              builder.Read(surfel_state, EResourceUsage::kReadSRV);
                              builder.Read(surfel_query_grid_cells, EResourceUsage::kReadSRV);
                              builder.Read(surfel_query_grid_indices, EResourceUsage::kReadSRV);
                              builder.Read(surfel_query_debug_counters, EResourceUsage::kWriteUAV);
                          }
                          rendering_data._rg_handles._color_target = builder.Write(rendering_data._rg_handles._color_target);
                      },
                          [this, use_surfel_query, surfel_pool, surfel_lighting, surfel_state,
                           surfel_query_grid_cells, surfel_query_grid_indices, surfel_query_debug_counters](RDG::RenderGraph &graph,
                                                                                 CommandBuffer *cmd,
                                                                                 const RenderingData &data)
                      {
                            _p_lighting_material->SetTexture("_GBuffer0", graph.Resolve<Texture>(data._rg_handles._gbuffers[0]));
                            _p_lighting_material->SetTexture("_GBuffer1", graph.Resolve<Texture>(data._rg_handles._gbuffers[1]));
                            _p_lighting_material->SetTexture("_GBuffer2", graph.Resolve<Texture>(data._rg_handles._gbuffers[2]));
                            _p_lighting_material->SetTexture("_GBuffer3", graph.Resolve<Texture>(data._rg_handles._gbuffers[3]));
                            _p_lighting_material->SetTexture("_CameraDepthTexture", graph.Resolve<Texture>(data._rg_handles._depth_target));
                            if (data._shadow_set != nullptr && data._shadow_set->_valid)
                            {
                                _p_lighting_material->SetTexture("_MainLightShadowMap",
                                                                  graph.Resolve<Texture>(data._rg_handles._main_light_shadow_map));
                                _p_lighting_material->SetTexture("_AddLightShadowMaps",
                                                                  graph.Resolve<Texture>(data._rg_handles._addi_shadow_maps));
                                _p_lighting_material->SetTexture("_PointLightShadowMaps",
                                                                  graph.Resolve<Texture>(data._rg_handles._point_light_shadow_maps));
                            }
                            else
                            {
                                _p_lighting_material->SetTexture("_MainLightShadowMap",
                                                                  _dummy_main_light_shadow_map.get());
                                _p_lighting_material->SetTexture("_AddLightShadowMaps",
                                                                  _dummy_add_light_shadow_maps.get());
                                _p_lighting_material->SetTexture("_PointLightShadowMaps",
                                                                  _dummy_point_light_shadow_map.get());
                            }
                            _p_lighting_material->SetTexture("IBLLut", _brdf_lut.get());
                            _p_lighting_material->SetTexture("RadianceTex",
                                                              graph.Resolve<Texture>(data._rg_handles._environment_diffuse));
                            _p_lighting_material->SetTexture("PrefilterEnvTex",
                                                              graph.Resolve<Texture>(data._rg_handles._environment_specular));
                            auto *occlusion_tex = data._rg_handles._ao_tex.IsValid()
                                ? graph.Resolve<Texture>(data._rg_handles._ao_tex) : nullptr;
                            _p_lighting_material->SetTexture("_OcclusionTex", occlusion_tex ? occlusion_tex : Texture::s_p_default_white);
                            auto *volumetric_light = data._rg_handles._volumetric_fog_accum.IsValid()
                                ? graph.Resolve<Texture>(data._rg_handles._volumetric_fog_accum) : nullptr;
                            _p_lighting_material->SetTexture("_VolumetricLightTexture",
                                                              volumetric_light ? volumetric_light
                                                                               : _dummy_volumetric_light.get());
                            Texture *ddgi_atlas = data._ddgi_lighting._irradiance_atlas_handle.IsValid()
                                ? graph.Resolve<Texture>(data._ddgi_lighting._irradiance_atlas_handle) : nullptr;
                            Texture *ddgi_distance_atlas = data._ddgi_lighting._distance_atlas_handle.IsValid()
                                ? graph.Resolve<Texture>(data._ddgi_lighting._distance_atlas_handle) : nullptr;
                            BindDDGILighting(_p_lighting_material.get(), data, ddgi_atlas, ddgi_distance_atlas);
                            GPUBuffer *ddgi_probe_data = data._ddgi_lighting._probe_data_handle.IsValid()
                                ? graph.Resolve<GPUBuffer>(data._ddgi_lighting._probe_data_handle) : nullptr;
                            _p_lighting_material->SetBuffer("_DDGI_ProbeData", ddgi_probe_data);
                            _p_lighting_material->SetInt(kSurfelQueryEnabledPropertyId, use_surfel_query ? 1 : 0);
                            if (use_surfel_query)
                            {
                                _p_lighting_material->SetBuffer(kSurfelPoolPropertyId, graph.Resolve<GPUBuffer>(surfel_pool));
                                _p_lighting_material->SetBuffer(kSurfelLightingPropertyId,
                                                               graph.Resolve<GPUBuffer>(surfel_lighting));
                                _p_lighting_material->SetBuffer(kSurfelStatePropertyId, graph.Resolve<GPUBuffer>(surfel_state));
                                _p_lighting_material->SetBuffer(kSurfelQueryGridCellsPropertyId,
                                                               graph.Resolve<GPUBuffer>(surfel_query_grid_cells));
                                _p_lighting_material->SetBuffer(kSurfelQueryGridIndicesPropertyId,
                                                               graph.Resolve<GPUBuffer>(surfel_query_grid_indices));
                                _p_lighting_material->SetBuffer(kSurfelQueryDebugCountersPropertyId,
                                                               graph.Resolve<GPUBuffer>(surfel_query_debug_counters));
                                _p_lighting_material->SetInt(kSurfelGridResolutionPropertyId,
                                    static_cast<i32>(_surfel_gi->_resource_grid_resolution));
                                _p_lighting_material->SetInt(kSurfelCascadeCountPropertyId,
                                    static_cast<i32>(_surfel_gi->_resource_cascade_count));
                                _p_lighting_material->SetInt(kSurfelMaxCountPropertyId,
                                    static_cast<i32>(_surfel_gi->_resource_max_surfel_count));
                                _p_lighting_material->SetFloat(kSurfelBaseCellSizePropertyId,
                                                               _surfel_gi->_settings._base_cell_size);
                                _p_lighting_material->SetFloat(kSurfelTanHalfFovPropertyId,
                                    std::tan(data._camera->FovH() * 3.14159265358979323846f / 360.0f));
                                _p_lighting_material->SetFloat(kSurfelTargetPixelRadiusPropertyId,
                                                               _surfel_gi->_settings._target_pixel_radius);
                                _p_lighting_material->SetInt(
                                    kSurfelQueryDebugEnabledPropertyId,
                                    (_surfel_gi->_settings._debug_draw_surfels ||
                                     _surfel_gi->_settings._debug_coverage || _surfel_gi->_settings._debug_draw_tile_grid)
                                        ? 1 : 0);
                            }
                            cmd->SetRenderTargetLoadAction(data._rg_handles._color_target, ELoadStoreAction::kNotCare);
                            cmd->SetRenderTarget(data._rg_handles._color_target);
                            cmd->DrawFullScreenQuad(_p_lighting_material.get());
                      }
        );
    }
    void DeferredLightingPass::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        _p_lighting_material->SetTexture("_GBuffer0", rendering_data._gbuffers[0]);
        _p_lighting_material->SetTexture("_GBuffer1", rendering_data._gbuffers[1]);
        _p_lighting_material->SetTexture("_GBuffer2", rendering_data._gbuffers[2]);
        _p_lighting_material->SetTexture("_GBuffer3", rendering_data._gbuffers[3]);
        _p_lighting_material->SetTexture("_CameraDepthTexture", rendering_data._camera_depth_tex_handle);
        _p_lighting_material->SetTexture("IBLLut", _brdf_lut.get());
        if (rendering_data._environment != nullptr)
        {
            _p_lighting_material->SetTexture("RadianceTex", rendering_data._environment->_diffuse_irradiance.get());
            _p_lighting_material->SetTexture("PrefilterEnvTex", rendering_data._environment->_specular_prefilter.get());
        }
        BindDDGILighting(_p_lighting_material.get(), rendering_data,
                         rendering_data._ddgi_lighting._irradiance_atlas_texture.get(),
                         rendering_data._ddgi_lighting._distance_atlas_texture.get());
        const bool use_surfel_query = _surfel_gi != nullptr && _surfel_gi->_ready_for_current_view &&
                                      _surfel_gi->_is_active &&
                                      _surfel_gi->_has_active_component && _surfel_gi->_settings._enabled &&
                                      _surfel_gi->_settings._lighting_enabled &&
                                      _surfel_gi->_surfel_pool != nullptr &&
                                      _surfel_gi->_surfel_lighting != nullptr &&
                                      _surfel_gi->_surfel_state != nullptr &&
                                      _surfel_gi->_query_grid_cells != nullptr &&
                                      _surfel_gi->_query_grid_indices != nullptr &&
                                      _surfel_gi->_query_debug_counters != nullptr;
        _p_lighting_material->SetInt(kSurfelQueryEnabledPropertyId, use_surfel_query ? 1 : 0);
        if (use_surfel_query)
        {
            _p_lighting_material->SetBuffer(kSurfelPoolPropertyId, _surfel_gi->_surfel_pool.get());
            _p_lighting_material->SetBuffer(kSurfelLightingPropertyId, _surfel_gi->_surfel_lighting.get());
            _p_lighting_material->SetBuffer(kSurfelStatePropertyId, _surfel_gi->_surfel_state.get());
            _p_lighting_material->SetBuffer(kSurfelQueryGridCellsPropertyId, _surfel_gi->_query_grid_cells.get());
            _p_lighting_material->SetBuffer(kSurfelQueryGridIndicesPropertyId, _surfel_gi->_query_grid_indices.get());
            _p_lighting_material->SetBuffer(kSurfelQueryDebugCountersPropertyId,
                                            _surfel_gi->_query_debug_counters.get());
            _p_lighting_material->SetInt(kSurfelGridResolutionPropertyId,
                static_cast<i32>(_surfel_gi->_resource_grid_resolution));
            _p_lighting_material->SetInt(kSurfelCascadeCountPropertyId,
                static_cast<i32>(_surfel_gi->_resource_cascade_count));
            _p_lighting_material->SetInt(kSurfelMaxCountPropertyId,
                                         static_cast<i32>(_surfel_gi->_resource_max_surfel_count));
            _p_lighting_material->SetFloat(kSurfelBaseCellSizePropertyId, _surfel_gi->_settings._base_cell_size);
            _p_lighting_material->SetFloat(kSurfelTanHalfFovPropertyId,
                                           std::tan(rendering_data._camera->FovH() *
                                                    3.14159265358979323846f / 360.0f));
            _p_lighting_material->SetFloat(kSurfelTargetPixelRadiusPropertyId,
                                           _surfel_gi->_settings._target_pixel_radius);
            _p_lighting_material->SetInt(kSurfelQueryDebugEnabledPropertyId,
                                         (_surfel_gi->_settings._debug_draw_surfels ||
                                          _surfel_gi->_settings._debug_coverage ||
                                          _surfel_gi->_settings._debug_draw_tile_grid) ? 1 : 0);
        }
        auto cmd = CommandBufferPool::Get("DeferredLightingPass");
        {
            PROFILE_BLOCK_GPU(cmd.get(), _name)
            cmd->SetRenderTargetLoadAction(rendering_data._camera_color_target_handle, ELoadStoreAction::kNotCare);
            cmd->SetRenderTargetLoadAction(rendering_data._camera_depth_target_handle, ELoadStoreAction::kNotCare);
            cmd->SetRenderTarget(rendering_data._camera_color_target_handle, rendering_data._camera_depth_target_handle);
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

#pragma region SkyPass
    //-------------------------------------------------------------SkyboxPass-------------------------------------------------------------
    SkyPass::SkyPass() : RenderPass("SkyPass")
    {
        _p_skybox_material = MakeRef<Material>(ResourceMgr::Get().Get<Shader>(L"Shaders/hlsl/skybox.alasset"), "Skybox");
        _p_procedural_material = _p_skybox_material;
        auto texture_sky_shader = ResourceMgr::Get().GetRef<Shader>(L"Shaders/hlsl/texture_sky.alasset");
        if (texture_sky_shader == nullptr)
            texture_sky_shader = ResourceMgr::Get().Load<Shader>(L"Shaders/hlsl/texture_sky.alasset");
        _p_texture_material = MakeRef<Material>(texture_sky_shader.get(), "TextureSky");
        _p_skybox_material->SetCullMode(ECullMode::kFront);
        _p_texture_material->SetCullMode(ECullMode::kFront);
        Matrix4x4f world_mat;
        MatrixScale(world_mat, 1000000.f, 1000000.f, 1000000.f);
        _p_cbuffer = ConstantBuffer::Create(RenderConstants::kPerObjectDataSize);
        _p_cbuffer->SetData(reinterpret_cast<u8 *>(&world_mat), sizeof(Matrix4x4f));
        _event = static_cast<ERenderPassEvent>(static_cast<u16>(ERenderPassEvent::kBeforeSkybox) + 25u);
    }

    void Ailu::Render::SkyPass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
    {
        if (rendering_data._environment == nullptr || !rendering_data._environment->_render_sky)
            return;
        if (rendering_data._environment != nullptr && rendering_data._environment->_is_texture_source)
        {
            graph.AddPass("DrawTextureSky", RDG::PassDesc(), [&](RDG::RenderGraphBuilder &builder)
            {
                builder.Read(rendering_data._rg_handles._environment_sky);
                builder.Read(rendering_data._rg_handles._color_target, EResourceUsage::kWriteRTV);
                builder.Read(rendering_data._rg_handles._depth_target, EResourceUsage::kDSV);
                rendering_data._rg_handles._color_target = builder.Write(rendering_data._rg_handles._color_target);
                rendering_data._rg_handles._depth_target = builder.Write(rendering_data._rg_handles._depth_target, EResourceUsage::kDSV);
            }, [this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
            {
                _p_texture_material->SetTexture("_EnvironmentSky", graph.Resolve<Texture>(data._rg_handles._environment_sky));
                _p_texture_material->SetFloat("_EnvironmentIntensity", data._environment->_intensity);
                _p_texture_material->SetFloat("_EnvironmentRotation", data._environment->_rotation);
                cmd->SetRenderTarget(data._rg_handles._color_target, data._rg_handles._depth_target);
                if (_is_clear)
                {
                    cmd->ClearRenderTarget(Colors::kBlack);
                    cmd->ClearRenderTarget(kZFar, 0u);
                }
                cmd->DrawMesh(Mesh::s_sphere.lock().get(), _p_texture_material.get(), _p_cbuffer.get(), 0, 1);
            });
            return;
        }
        graph.AddPass("DrawSkyBox", RDG::PassDesc(), [&](RDG::RenderGraphBuilder &builder)
                      { 
                          builder.Read(rendering_data._rg_handles._sky_view_lut);
                          builder.Read(rendering_data._rg_handles._environment_transmittance_lut);
                          builder.Read(rendering_data._rg_handles._color_target, EResourceUsage::kWriteRTV);
                          builder.Read(rendering_data._rg_handles._depth_target, EResourceUsage::kDSV);
                          rendering_data._rg_handles._color_target = builder.Write(rendering_data._rg_handles._color_target);
                          rendering_data._rg_handles._depth_target = builder.Write(rendering_data._rg_handles._depth_target,EResourceUsage::kDSV);
                      }, [this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &rendering_data)
                      {
                        _p_skybox_material->SetTexture(
                            "_TexSkyViewLUT", graph.Resolve<RenderTexture>(rendering_data._rg_handles._sky_view_lut));
                        _p_skybox_material->SetTexture("_TexTransmittanceLUT",
                                                       graph.Resolve<Texture>(rendering_data._rg_handles._environment_transmittance_lut));
                        _p_skybox_material->SetFloat("_EnvironmentIntensity", rendering_data._environment->_intensity);
                        _p_skybox_material->SetFloat("_EnvironmentRotation", rendering_data._environment->_rotation);
                        _p_skybox_material->SetVector("_SkyColor", rendering_data._environment->_sky_color);
                        _p_skybox_material->SetVector("_GroundColor", rendering_data._environment->_ground_color);
                        cmd->SetRenderTargetLoadAction(rendering_data._rg_handles._color_target, ELoadStoreAction::kNotCare);
                        cmd->SetRenderTargetLoadAction(rendering_data._rg_handles._depth_target, ELoadStoreAction::kNotCare);
                        cmd->SetRenderTarget(rendering_data._rg_handles._color_target, rendering_data._rg_handles._depth_target);
                        if (_is_clear)
                        {
                            cmd->ClearRenderTarget(Colors::kBlack);
                            cmd->ClearRenderTarget(kZFar, 0u);
                        }
                        cmd->DrawMesh(Mesh::s_sphere.lock().get(), _p_skybox_material.get(), _p_cbuffer.get(), 0, 1);
        });
    }
    void SkyPass::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        if (rendering_data._environment == nullptr || !rendering_data._environment->_render_sky)
            return;
        auto cmd = CommandBufferPool::Get("SkyboxPass");
        cmd->Clear();
        {
            PROFILE_BLOCK_GPU(cmd.get(), _name)
            if (rendering_data._environment != nullptr && rendering_data._environment->_is_texture_source)
            {
                _p_texture_material->SetTexture("_EnvironmentSky", rendering_data._environment->_sky_cubemap.get());
                _p_texture_material->SetFloat("_EnvironmentIntensity", rendering_data._environment->_intensity);
                _p_texture_material->SetFloat("_EnvironmentRotation", rendering_data._environment->_rotation);
                cmd->SetRenderTarget(rendering_data._camera_color_target_handle, rendering_data._camera_depth_target_handle);
                if (_is_clear)
                {
                    cmd->ClearRenderTarget(Colors::kBlack);
                    cmd->ClearRenderTarget(kZFar, 0u);
                }
            cmd->DrawMesh(Mesh::s_sphere.lock().get(), _p_texture_material.get(), _p_cbuffer.get(), 0, 1);
                context->ExecuteCommandBuffer(cmd);
                CommandBufferPool::Release(cmd);
                return;
            }
            _p_skybox_material->SetTexture("_TexSkyViewLUT", rendering_data._environment->_sky_view_lut.get());
            _p_skybox_material->SetTexture("_TexTransmittanceLUT", rendering_data._environment->_transmittance_lut.get());
            _p_skybox_material->SetFloat("_EnvironmentIntensity", rendering_data._environment->_intensity);
            _p_skybox_material->SetFloat("_EnvironmentRotation", rendering_data._environment->_rotation);
            _p_skybox_material->SetVector("_SkyColor", rendering_data._environment->_sky_color);
            _p_skybox_material->SetVector("_GroundColor", rendering_data._environment->_ground_color);
            cmd->SetRenderTargetLoadAction(rendering_data._camera_color_target_handle, ELoadStoreAction::kNotCare);
            cmd->SetRenderTargetLoadAction(rendering_data._camera_depth_target_handle, ELoadStoreAction::kNotCare);
            cmd->SetRenderTarget(rendering_data._camera_color_target_handle, rendering_data._camera_depth_target_handle);
            if (_is_clear)
            {
                cmd->ClearRenderTarget(Colors::kBlack);
                cmd->ClearRenderTarget(kZFar, 0u);
            }
            cmd->DrawMesh(Mesh::s_sphere.lock().get(), _p_skybox_material.get(), _p_cbuffer.get(), 0, 1);
        }
        context->ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
    }
    void SkyPass::BeginPass(GraphicsContext *context)
    {
    }
    void SkyPass::EndPass(GraphicsContext *context)
    {
        RenderPass::EndPass(context);
    }
#pragma endregion
    //-------------------------------------------------------------SkyPass-------------------------------------------------------------

#pragma region GizmoPass
    //-------------------------------------------------------------GizmoPass-------------------------------------------------------------
    GizmoPass::GizmoPass() : RenderPass("GizmoPass")
    {
        _event = ERenderPassEvent::kAfterPostprocess;
    }
    static void GetScreenAxis(const RenderingData &rendering_data, Vector4f *out) 
    {
        auto &cam = rendering_data._camera;
        const static f32 s_axis_length = 34.f;
        const static f32 s_axis_margin = 58.f;
        Vector2f origin((f32) rendering_data._width - s_axis_margin, (f32) rendering_data._height - s_axis_margin);

        auto GetAxisEnd = [&](const Vector3f &world_axis)
        {
            Vector2f axis_dir(DotProduct(world_axis, cam->Right()), DotProduct(world_axis, cam->Up()));
            return origin + axis_dir * s_axis_length;
        };

        Vector2f y_axis_end = GetAxisEnd(Vector3f::kUp);
        Vector2f x_axis_end = GetAxisEnd(Vector3f::kRight);
        Vector2f z_axis_end = GetAxisEnd(Vector3f::kForward);
        out[0] = Vector4f(origin.x, origin.y, y_axis_end.x, y_axis_end.y);
        out[1] = Vector4f(origin.x, origin.y, x_axis_end.x, x_axis_end.y);
        out[2] = Vector4f(origin.x, origin.y, z_axis_end.x, z_axis_end.y);
    }

    static Matrix4x4f GetGridPlaneMatrix(const RenderingData &rendering_data)
    {
        Matrix4x4f scale = MatrixScale(1000.0f, 1000.0f, 1000.0f);
        if (rendering_data._camera == nullptr || rendering_data._camera->Type() != ECameraType::kOrthographic)
            return scale;

        Vector3f forward = rendering_data._camera->Forward();
        Vector3f abs_forward(std::abs(forward.x), std::abs(forward.y), std::abs(forward.z));
        Vector3f grid_position = rendering_data._camera->Position() + forward * std::max(1.0f, rendering_data._camera->Near() + 0.01f);
        if (abs_forward.x >= abs_forward.y && abs_forward.x >= abs_forward.z)
            return scale * MatrixRotationZ(k2Radius * 90.0f) * MatrixTranslation(grid_position);
        if (abs_forward.z >= abs_forward.x && abs_forward.z >= abs_forward.y)
            return scale * MatrixRotationX(k2Radius * 90.0f) * MatrixTranslation(grid_position);
        return scale * MatrixTranslation(grid_position);
    }

    static bool Is2DGridCamera(const RenderingData &rendering_data)
    {
        return rendering_data._camera != nullptr && rendering_data._camera->Type() == ECameraType::kOrthographic;
    }

    static void SetGridPlaneAxisMode(Material *material, const RenderingData &rendering_data)
    {
        if (material == nullptr)
            return;

        f32 axis_mode = 0.0f;// XZ
        if (Is2DGridCamera(rendering_data))
        {
            Vector3f forward = rendering_data._camera->Forward();
            Vector3f abs_forward(std::abs(forward.x), std::abs(forward.y), std::abs(forward.z));
            if (abs_forward.z >= abs_forward.x && abs_forward.z >= abs_forward.y)
                axis_mode = 1.0f;// XY
            else if (abs_forward.x >= abs_forward.y && abs_forward.x >= abs_forward.z)
                axis_mode = 2.0f;// YZ
        }
        material->SetVector("_grid_axis_mode", Vector4f(axis_mode, 0.0f, 0.0f, 0.0f));
    }
    using SceneManagement::SceneMgr;

    void Ailu::Render::GizmoPass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
    {
        static auto mat_point_light = ResourceMgr::Get().Get<Material>(L"Runtime/Material/PointLightBillboard");
        static auto mat_directional_light = ResourceMgr::Get().Get<Material>(L"Runtime/Material/DirectionalLightBillboard");
        static auto mat_spot_light = ResourceMgr::Get().Get<Material>(L"Runtime/Material/SpotLightBillboard");
        static auto mat_area_light = ResourceMgr::Get().Get<Material>(L"Runtime/Material/AreaLightBillboard");
        static auto mat_camera = ResourceMgr::Get().Get<Material>(L"Runtime/Material/CameraBillboard");
        static auto mat_gird_plane = ResourceMgr::Get().Get<Material>(L"Runtime/Material/GridPlane");
        static auto mat_lightprobe = ResourceMgr::Get().Get<Material>(L"Runtime/Material/LightProbeBillboard");
        graph.AddPass(_name, RDG::PassDesc(), [&](RDG::RenderGraphBuilder &builder)
                { 
                    // Gizmo 叠加在后处理结果之上，不再读写场景颜色。
                    RDG::RGHandle &color_target = rendering_data.FinalColorHandle();
                    builder.Read(color_target, EResourceUsage::kWriteRTV);
                    builder.Read(rendering_data._rg_handles._depth_target, EResourceUsage::kDSV);
                    color_target = builder.Write(color_target);
                    rendering_data._rg_handles._depth_target = builder.Write(rendering_data._rg_handles._depth_target,EResourceUsage::kDSV);
                },[this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &rendering_data)
                {
                    Vector4f axis[3];
                    GetScreenAxis(rendering_data, axis);
                    Gizmo::DrawLine(axis[0].xy, axis[0].zw, Colors::kGreen);
                    Gizmo::DrawLine(axis[1].xy, axis[1].zw, Colors::kRed);
                    Gizmo::DrawLine(axis[2].xy, axis[2].zw, Colors::kBlue);
                    bool is_2d_grid = Is2DGridCamera(rendering_data);
                    if (is_2d_grid)
                        cmd->SetRenderTarget(rendering_data.FinalColorHandle());
                    else
                        cmd->SetRenderTarget(rendering_data.FinalColorHandle(), rendering_data._rg_handles._depth_target);

                    Matrix4x4f grid_plane_pos = GetGridPlaneMatrix(rendering_data);
                    SetGridPlaneAxisMode(mat_gird_plane, rendering_data);
                    cmd->DrawMesh(Mesh::s_plane.lock().get(), mat_gird_plane, grid_plane_pos);
                    if (is_2d_grid)
                        cmd->SetRenderTarget(rendering_data.FinalColorHandle(), rendering_data._rg_handles._depth_target);
                    u16 entity_index = 0;
                    for (auto &light_comp: SceneMgr::Get().ActiveScene()->GetRegister().View<ECS::LightComponent>())
                    {
                        const auto &t = SceneMgr::Get().ActiveScene()->GetRegister().GetComponent<ECS::LightComponent, ECS::TransformComponent>(entity_index++);
                        auto world_pos = t->GetPosition();
                        auto m = MatrixTranslation(world_pos);
                        f32 scale = 2.0f;
                        m = MatrixScale(scale, scale, scale) * m;
                        switch (light_comp._type)
                        {
                            case ECS::ELightType::kDirectional:
                            {
                                cmd->DrawMesh(Mesh::s_quad.lock().get(), mat_directional_light, m);
                            }
                            break;
                            case ECS::ELightType::kPoint:
                            {
                                cmd->DrawMesh(Mesh::s_quad.lock().get(), mat_point_light, m);
                            }
                            break;
                            case ECS::ELightType::kSpot:
                            {
                                cmd->DrawMesh(Mesh::s_quad.lock().get(), mat_spot_light, m);
                            }
                            break;
                            case ECS::ELightType::kArea:
                            {
                                cmd->DrawMesh(Mesh::s_quad.lock().get(), mat_area_light, m);
                            }
                            break;
                        }
                    }
                    entity_index = 0;
                    for (auto &light_comp: SceneMgr::Get().ActiveScene()->GetRegister().View<ECS::CLightProbe>())
                    {
                        const auto &t = SceneMgr::Get().ActiveScene()->GetRegister().GetComponent<ECS::CLightProbe, ECS::TransformComponent>(entity_index++);
                        auto world_pos = t->GetPosition();
                        auto m = MatrixTranslation(world_pos);
                        f32 scale = 2.0f;
                        m = MatrixScale(scale, scale, scale) * m;
                        cmd->DrawMesh(Mesh::s_quad.lock().get(), mat_lightprobe, m);
                    }
                    entity_index = 0;
                    for (auto &light_comp: SceneMgr::Get().ActiveScene()->GetRegister().View<ECS::CCamera>())
                    {
                        const auto &t = SceneMgr::Get().ActiveScene()->GetRegister().GetComponent<ECS::CCamera, ECS::TransformComponent>(entity_index++);
                        auto world_pos = t->GetPosition();
                        auto m = MatrixTranslation(world_pos);
                        f32 scale = 2.0f;
                        m = MatrixScale(scale, scale, scale) * m;
                        cmd->DrawMesh(Mesh::s_quad.lock().get(), mat_camera, m);
                    }
                    Gizmo::Submit(cmd, &rendering_data); 
                });
    }

    void GizmoPass::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        auto cmd = CommandBufferPool::Get("GizmoPass");
        static auto mat_point_light = ResourceMgr::Get().Get<Material>(L"Runtime/Material/PointLightBillboard");
        static auto mat_directional_light = ResourceMgr::Get().Get<Material>(L"Runtime/Material/DirectionalLightBillboard");
        static auto mat_spot_light = ResourceMgr::Get().Get<Material>(L"Runtime/Material/SpotLightBillboard");
        static auto mat_area_light = ResourceMgr::Get().Get<Material>(L"Runtime/Material/AreaLightBillboard");
        static auto mat_camera = ResourceMgr::Get().Get<Material>(L"Runtime/Material/CameraBillboard");
        static auto mat_gird_plane = ResourceMgr::Get().Get<Material>(L"Runtime/Material/GridPlane");
        static auto mat_lightprobe = ResourceMgr::Get().Get<Material>(L"Runtime/Material/LightProbeBillboard");
        cmd->Clear();
        {
            Vector4f axis[3];
            GetScreenAxis(rendering_data, axis);
            Gizmo::DrawLine(axis[0].xy, axis[0].zw, Colors::kGreen);
            Gizmo::DrawLine(axis[1].xy, axis[1].zw, Colors::kRed);
            Gizmo::DrawLine(axis[2].xy, axis[2].zw, Colors::kBlue);

            //cmd->SetViewProjectionMatrix(rendering_data._camera->GetView(), rendering_data._camera->GetProj());
            PROFILE_BLOCK_GPU(cmd.get(), _name)
            cmd->SetViewport(rendering_data._viewport);
            cmd->SetScissorRect(rendering_data._scissor_rect);
            bool is_2d_grid = Is2DGridCamera(rendering_data);
            if (is_2d_grid)
                cmd->SetRenderTarget(rendering_data._camera_color_target_handle);
            else
                cmd->SetRenderTarget(rendering_data._camera_color_target_handle, rendering_data._camera_depth_target_handle);

            Matrix4x4f grid_plane_pos = GetGridPlaneMatrix(rendering_data);
            SetGridPlaneAxisMode(mat_gird_plane, rendering_data);
            cmd->DrawMesh(Mesh::s_plane.lock().get(), mat_gird_plane, grid_plane_pos);
            if (is_2d_grid)
                cmd->SetRenderTarget(rendering_data._camera_color_target_handle, rendering_data._camera_depth_target_handle);
            u16 entity_index = 0;
            for (auto &light_comp: SceneMgr::Get().ActiveScene()->GetRegister().View<ECS::LightComponent>())
            {
                const auto &t = SceneMgr::Get().ActiveScene()->GetRegister().GetComponent<ECS::LightComponent, ECS::TransformComponent>(entity_index++);
                auto world_pos = t->GetPosition();
                auto m = MatrixTranslation(world_pos);
                f32 scale = 2.0f;
                m = MatrixScale(scale, scale, scale) * m;
                switch (light_comp._type)
                {
                    case ECS::ELightType::kDirectional:
                    {
                        cmd->DrawMesh(Mesh::s_quad.lock().get(), mat_directional_light, m);
                    }
                    break;
                    case ECS::ELightType::kPoint:
                    {
                        cmd->DrawMesh(Mesh::s_quad.lock().get(), mat_point_light, m);
                    }
                    break;
                    case ECS::ELightType::kSpot:
                    {
                        cmd->DrawMesh(Mesh::s_quad.lock().get(), mat_spot_light, m);
                    }
                    break;
                    case ECS::ELightType::kArea:
                    {
                        cmd->DrawMesh(Mesh::s_quad.lock().get(), mat_area_light, m);
                    }
                    break;
                }
            }
            entity_index = 0;
            for (auto &light_comp: SceneMgr::Get().ActiveScene()->GetRegister().View<ECS::CLightProbe>())
            {
                const auto &t = SceneMgr::Get().ActiveScene()->GetRegister().GetComponent<ECS::CLightProbe, ECS::TransformComponent>(entity_index++);
                auto world_pos = t->GetPosition();
                auto m = MatrixTranslation(world_pos);
                f32 scale = 2.0f;
                m = MatrixScale(scale, scale, scale) * m;
                cmd->DrawMesh(Mesh::s_quad.lock().get(), mat_lightprobe, m);
            }
            entity_index = 0;
            for (auto &light_comp: SceneMgr::Get().ActiveScene()->GetRegister().View<ECS::CCamera>())
            {
                const auto &t = SceneMgr::Get().ActiveScene()->GetRegister().GetComponent<ECS::CCamera, ECS::TransformComponent>(entity_index++);
                auto world_pos = t->GetPosition();
                auto m = MatrixTranslation(world_pos);
                f32 scale = 2.0f;
                m = MatrixScale(scale, scale, scale) * m;
                cmd->DrawMesh(Mesh::s_quad.lock().get(), mat_camera, m);
            }
            Gizmo::Submit(cmd.get(), &rendering_data);
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
        _p_blit_mat = ResourceMgr::Get().Get<Material>(L"Runtime/Material/Blit");
        _p_obj_cb = ConstantBuffer::Create(256);
        memcpy(_p_obj_cb->GetData(), &BuildIdentityMatrix(), sizeof(Matrix4x4f));
        _p_quad_mesh = ResourceMgr::Get().Get<Mesh>(L"Runtime/Mesh/FullScreenQuad");
        _event = ERenderPassEvent::kAfterTransparent;
    }
    CopyColorPass::~CopyColorPass()
    {
    }
    void Ailu::Render::CopyColorPass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
    {
        graph.AddPass(_name, RDG::PassDesc(), [&](RDG::RenderGraphBuilder &builder)
        { 
            builder.Read(rendering_data._rg_handles._color_target);
            rendering_data._rg_handles._color_tex = builder.Write(rendering_data._rg_handles._color_tex);
        }, [this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
        { 
            cmd->Blit(data._rg_handles._color_target, data._rg_handles._color_tex);
        });
    }
    void CopyColorPass::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        auto cmd = CommandBufferPool::Get("CopyColor");
        cmd->Clear();
        {
            PROFILE_BLOCK_GPU(cmd.get(), _name)
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
        _p_blit_mat->SetTexture("_BlitSourceTex", nullptr);
    }
#pragma endregion

#pragma region CopyDepthPass
    //-------------------------------------------------------------CopyColorPass-------------------------------------------------------------

    //-------------------------------------------------------------CopyDepthPass-------------------------------------------------------------
    CopyDepthPass::CopyDepthPass() : RenderPass("CopyDepthPass")
    {
        _p_blit_mat = ResourceMgr::Get().Get<Material>(L"Runtime/Material/Blit");
        _event = ERenderPassEvent::kAfterGbuffer;
    }
    CopyDepthPass::~CopyDepthPass()
    {
    }
    void Ailu::Render::CopyDepthPass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
    {
        graph.AddPass(_name, RDG::PassDesc(), [&](RDG::RenderGraphBuilder &builder)
        { 
            builder.Read(rendering_data._rg_handles._depth_target);
            rendering_data._rg_handles._depth_tex = builder.Write(rendering_data._rg_handles._depth_tex);
        }, [this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
        { 
            cmd->Blit(data._rg_handles._depth_target, data._rg_handles._depth_tex); 
        });
    }
    void CopyDepthPass::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        _depth_tex_handle = rendering_data._camera_depth_tex_handle;
        auto cmd = CommandBufferPool::Get("CopyDepth");
        cmd->Clear();
        {
            PROFILE_BLOCK_GPU(cmd.get(), _name)
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
        _p_blit_mat->SetTexture("_BlitSourceTex", nullptr);
        Shader::SetGlobalTexture("_CameraDepthTexture", _depth_tex_handle);
        ComputeShader::SetGlobalTexture("_CameraDepthTexture", _depth_tex_handle);
    }
#pragma endregion
//-------------------------------------------------------------CopyDepthPass-------------------------------------------------------------

//-------------------------------------------------------------WireFramePass-------------------------------------------------------------
#pragma region WireFramePass
    WireFramePass::WireFramePass() : RenderPass("WireFramePass")
    {

        _wireframe_mat = ResourceMgr::Get().GetRef<Material>(L"Runtime/Material/Wireframe");
        _event = ERenderPassEvent::kAfterPostprocess;
    }
    WireFramePass::~WireFramePass()
    {
    }
    void Ailu::Render::WireFramePass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
    {
        graph.AddPass(_name, RDG::PassDesc(), [&](RDG::RenderGraphBuilder &builder)
                      { 
                    builder.Read(rendering_data._rg_handles._color_target, EResourceUsage::kWriteRTV);
                    builder.Read(rendering_data._rg_handles._depth_target, EResourceUsage::kDSV);
                    rendering_data._rg_handles._color_target = builder.Write(rendering_data._rg_handles._color_target);
                    rendering_data._rg_handles._depth_target = builder.Write(rendering_data._rg_handles._depth_target,EResourceUsage::kDSV);
                      }, [this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &rendering_data)
                      {
                          auto &all_renderable = *rendering_data._cull_results;
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
                                        ResourceMgr::Get().RegisterResource(shader_name_w, wireframe_shader);
                                        _wireframe_shaders.insert(shader_name_w);
                                    }
                                    auto mat = MakeRef<Material>(*obj._material);
                                    mat->ChangeShader(ResourceMgr::Get().Get<Shader>(shader_name_w));
                                    _wireframe_mats[obj._material->Name()] = mat;
                                }
                                cmd->DrawSceneMesh(obj._vertex_buffer, obj._index_buffer, _wireframe_mats[obj._material->Name()].get(),
                                                   obj._submesh_index, 0u, CBufferPrimitiveDrawData{obj._primitive_index}, 1u);
                            }
                        }
        });
    }
    void WireFramePass::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        auto &all_renderable = *rendering_data._cull_results;
        auto cmd = CommandBufferPool::Get(_name);
        {
            PROFILE_BLOCK_GPU(cmd.get(), _name)
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
                            ResourceMgr::Get().RegisterResource(shader_name_w, wireframe_shader);
                            _wireframe_shaders.insert(shader_name_w);
                        }
                        auto mat = MakeRef<Material>(*obj._material);
                        mat->ChangeShader(ResourceMgr::Get().Get<Shader>(shader_name_w));
                        _wireframe_mats[obj._material->Name()] = mat;
                    }
                    cmd->DrawSceneMesh(obj._vertex_buffer, obj._index_buffer, _wireframe_mats[obj._material->Name()].get(),
                                       obj._submesh_index, 0u, CBufferPrimitiveDrawData{obj._primitive_index}, 1u);
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
        _ui_default_shader = ResourceMgr::Get().LoadAssetRef<Shader>(L"Shaders/hlsl/default_ui.alasset");
        _ui_default_mat = MakeRef<Material>(_ui_default_shader.get(), "DefaultUIMaterial");
        _ui_default_mat->SetTexture("_MainTex", Texture::s_p_default_white);
        _ui_default_mat->SetVector("_Color", Colors::kWhite);
        Vector<VertexBufferLayoutDesc> desc_list;
        desc_list.push_back({EVertexSemantic::kPosition, EShaderDateType::kFloat3, 0});
        desc_list.push_back({EVertexSemantic::kTexcoord0, EShaderDateType::kFloat2, 1});
        _obj_cb = ConstantBuffer::Create(RenderConstants::kPerObjectDataSize);
        _vbuf = VertexBuffer::Create(desc_list, "ui_vbuf");
        _ibuf = IndexBuffer::Create(nullptr, vertex_count, "ui_ibuf", true);
        f32 box_w = 180.f, box_h = 30.f;
        Vector3f *vertices = AL_ALLOC_TAG(EMemoryTag::kTemporary, Vector3f, 4);
        vertices[0] = {-box_w * 0.5f, box_h * 0.5f, 0.0f};
        vertices[1] = {box_w * 0.5f, box_h * 0.5f, 0.0f};
        vertices[2] = {-box_w * 0.5f, -box_h * 0.5f, 0.0f};
        vertices[3] = {box_w * 0.5f, -box_h * 0.5f, 0.0f};
        u32 *indices = AL_ALLOC_TAG(EMemoryTag::kTemporary, u32, 6);
        indices[0] = 0; indices[1] = 1; indices[2] = 2; indices[3] = 1; indices[4] = 3; indices[5] = 2;
        Vector2f *uv0 = AL_ALLOC_TAG(EMemoryTag::kTemporary, Vector2f, 4);
        uv0[0] = {0.f, 0.f}; uv0[1] = {1.f, 0.f}; uv0[2] = {0.f, 1.f}; uv0[3] = {1.f, 1.f};
        _vbuf->SetStream(nullptr, vertex_count * sizeof(Vector3f), 0, true);
        _vbuf->SetStream(nullptr, vertex_count * sizeof(Vector2f), 1, true);
        _ibuf->SetData((u8 *) indices, 6 * sizeof(u32));
        AL_FREE(vertices);
        AL_FREE(indices);
        AL_FREE(uv0);
    }
    GUIPass::~GUIPass()
    {
    }
    void Ailu::Render::GUIPass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
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
        _motion_vector_mat = MakeRef<Material>(ResourceMgr::Get().Get<Shader>(L"Shaders/hlsl/motion_vector.alasset"), "Runtime/MotionVector");
    }

    MotionVectorPass::~MotionVectorPass()
    {
    }

    void Ailu::Render::MotionVectorPass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
    {
        graph.AddPass("StaticMotion", RDG::PassDesc(), [&](RDG::RenderGraphBuilder &builder)
                      { 
                          builder.Read(rendering_data._rg_handles._depth_tex);
                          rendering_data._rg_handles._motion_vector_tex = builder.Write(rendering_data._rg_handles._motion_vector_tex);
                      }, [this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &rendering_data)
                      {
                          cmd->SetRenderTarget(rendering_data._rg_handles._motion_vector_tex);
                          _motion_vector_mat->SetTexture("_CameraDepthTexture", graph.Resolve<Texture>(rendering_data._rg_handles._depth_tex));
                          cmd->DrawFullScreenQuad(_motion_vector_mat.get(), 0); 
                      });
        graph.AddPass("DynamicMotion", RDG::PassDesc(), [&](RDG::RenderGraphBuilder &builder)
                      { 
                          builder.Read(rendering_data._rg_handles._motion_vector_tex);
                          rendering_data._rg_handles._motion_vector_tex = builder.Write(rendering_data._rg_handles._motion_vector_tex);
                          rendering_data._rg_handles._motion_vector_depth = builder.Write(rendering_data._rg_handles._motion_vector_depth,EResourceUsage::kDSV);
                      }, [this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &rendering_data)
                      {
                          cmd->SetRenderTarget(rendering_data._rg_handles._motion_vector_tex,rendering_data._rg_handles._motion_vector_depth);
                          for (auto &it: *rendering_data._cull_results)
                          {
                              auto &[queue, objs] = it;
                              for (auto &obj: objs)
                              {
                                  if ((obj._flags & kPrimitivePerObjectMotion) == 0u)
                                      continue;
                                  //obj._material->GetShader()->_stencil_ref = ConstantBuffer::As<CBufferPerObjectData>(obj_cb)->_MotionVectorParam.x? 1 : 0;
                                  i16 mv_pass = obj._material->GetActiveShader()->FindPass("MotionVector");
                                  if (mv_pass != -1)
                                      cmd->DrawSceneMesh(obj._vertex_buffer, obj._index_buffer, obj._material, obj._submesh_index,
                                                         mv_pass, CBufferPrimitiveDrawData{obj._primitive_index}, 1u);
                              }
                          }
                      });
    }

    void MotionVectorPass::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        auto cmd = CommandBufferPool::Get(_name);
        RTHandle motion_vector_rt = cmd->GetTempRT(rendering_data._width, rendering_data._height, "_MotionVectorTexture", ERenderTargetFormat::kRGHalf, false, false, false);
        RTHandle motion_vector_depth = cmd->GetTempRT(rendering_data._width, rendering_data._height, "_MotionVectorDepth", ERenderTargetFormat::kDepth, false, false, false);
        cmd->Clear();
        {
            PROFILE_BLOCK_GPU(cmd.get(), "CameraMotionVector")
            //camera motion vector
            cmd->SetRenderTarget(motion_vector_rt, motion_vector_depth);
            //cmd->ClearRenderTarget(motion_vector_rt, motion_vector_depth,Colors::kBlack,kZFar);
            _motion_vector_mat->SetTexture("_CameraDepthTexture", rendering_data._camera_depth_tex_handle);
            cmd->DrawFullScreenQuad(_motion_vector_mat.get(), 0);
            //object motion vector
            for (auto &it: *rendering_data._cull_results)
            {
                auto &[queue, objs] = it;
                for (auto &obj: objs)
                {
                    if ((obj._flags & kPrimitivePerObjectMotion) == 0u)
                        continue;
                    //obj._material->GetShader()->_stencil_ref = ConstantBuffer::As<CBufferPerObjectData>(obj_cb)->_MotionVectorParam.x? 1 : 0;
                    i16 mv_pass = obj._material->GetActiveShader()->FindPass("MotionVector");
                    if (mv_pass != -1)
                        cmd->DrawSceneMesh(obj._vertex_buffer, obj._index_buffer, obj._material, obj._submesh_index,
                                           mv_pass, CBufferPrimitiveDrawData{obj._primitive_index}, 1u);
                }
            }
        }
        cmd->ReleaseTempRT(motion_vector_depth);
        cmd->ReleaseTempRT(motion_vector_rt);
        context->ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
        Shader::SetGlobalTexture("_MotionVectorTexture", motion_vector_rt);
        ComputeShader::SetGlobalTexture("_MotionVectorTexture", motion_vector_rt);
        //f32 aspect = (f32)rendering_data._width / (f32)rendering_data._height;
        //Gizmo::DrawTexture(Rect(0,0,256,static_cast<u16>(256 / aspect)),g_pRenderTexturePool->Get(motion_vector_rt));
    }

    void MotionVectorPass::BeginPass(GraphicsContext *context)
    {
    }

    void MotionVectorPass::EndPass(GraphicsContext *context)
    {
        RenderPass::EndPass(context);
    }
#pragma endregion

#pragma region HZB
    HZBPass::HZBPass() : RenderPass("HZB")
    {
        _hzb_gen = ResourceMgr::Get().LoadAssetRef<ComputeShader>(L"Shaders/hlsl/Compute/hzb.alasset");
        _hzb_kernel = _hzb_gen->FindKernel("CSMain");
        _event = ERenderPassEvent::kAfterGbuffer;
    }
    HZBPass::~HZBPass()
    {
    }
    void Ailu::Render::HZBPass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
    {
        u16 w = (rendering_data._width + 1) >> 1;
        u16 h = (rendering_data._height + 1) >> 1;
        u16 mip = Texture::MaxMipmapCount(w, h);
        u16 first_dispatch_mip_num = std::min<u16>(4, mip);
        graph.AddPass("HZB0", RDG::PassDesc{RDG::EPassType::kCompute}, [&](RDG::RenderGraphBuilder &builder)
                          { 
                              builder.Read(rendering_data._rg_handles._depth_tex);
                          rendering_data._rg_handles._hzb = builder.WriteRange(rendering_data._rg_handles._hzb, EResourceUsage::kWriteUAV,
                                                                               0u, first_dispatch_mip_num);
                      }, [=](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &rendering_data)
                      {
                            auto *depth_tex = graph.Resolve<Texture>(rendering_data._rg_handles._depth_tex);
                            auto *hzb_tex = graph.Resolve<Texture>(rendering_data._rg_handles._hzb);
                            _hzb_gen->SetTexture(_hzb_kernel, "_DepthInput", depth_tex);
                            {
                                _hzb_gen->SetInt("NumMipLevels", first_dispatch_mip_num);
                                _hzb_gen->SetTexture(_hzb_kernel, "_HZ_Buffer_Mip1", hzb_tex, ECubemapFace::kUnknown, 0);
                                _hzb_gen->SetTexture(_hzb_kernel, "_HZ_Buffer_Mip2", hzb_tex, ECubemapFace::kUnknown, 1);
                                _hzb_gen->SetTexture(_hzb_kernel, "_HZ_Buffer_Mip3", hzb_tex, ECubemapFace::kUnknown, 2);
                                _hzb_gen->SetTexture(_hzb_kernel, "_HZ_Buffer_Mip4", hzb_tex, ECubemapFace::kUnknown, 3);
                                auto [x, y, z] = _hzb_gen->CalculateDispatchNum(_hzb_kernel, w, h, 1);
                                cmd->Dispatch(_hzb_gen.get(), _hzb_kernel, x, y);
                            }
                      });
        if (u16 second_dispatch_mip_num = mip - first_dispatch_mip_num; second_dispatch_mip_num > 0)
        {
            graph.AddPass("HZB1", RDG::PassDesc{RDG::EPassType::kCompute}, [&](RDG::RenderGraphBuilder &builder)
                          { 
                              builder.ReadRange(rendering_data._rg_handles._hzb, EResourceUsage::kReadSRV, first_dispatch_mip_num - 1u, 1u);
                              rendering_data._rg_handles._hzb = builder.WriteRange(rendering_data._rg_handles._hzb, EResourceUsage::kWriteUAV,
                                                                                   first_dispatch_mip_num, std::min<u16>(4u, second_dispatch_mip_num));
                          }, [=](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &rendering_data)
                          {
                                _hzb_gen->SetInt("NumMipLevels", second_dispatch_mip_num);
                                auto *hzb_tex = graph.Resolve<Texture>(rendering_data._rg_handles._hzb);
                                _hzb_gen->SetTexture(_hzb_kernel, "_DepthInput", hzb_tex, ECubemapFace::kUnknown, 3);
                                _hzb_gen->SetTexture(_hzb_kernel, "_HZ_Buffer_Mip1", hzb_tex, ECubemapFace::kUnknown, 4);
                                _hzb_gen->SetTexture(_hzb_kernel, "_HZ_Buffer_Mip2", hzb_tex, ECubemapFace::kUnknown, 5);
                                _hzb_gen->SetTexture(_hzb_kernel, "_HZ_Buffer_Mip3", hzb_tex, ECubemapFace::kUnknown, 6);
                                _hzb_gen->SetTexture(_hzb_kernel, "_HZ_Buffer_Mip4", hzb_tex, ECubemapFace::kUnknown, 7);
                                //u16 base_w = w, base_h = h;
                                auto [base_w, base_h] = Texture::CalculateMipSize(w, h, 4);
                                auto [x, y, z] = _hzb_gen->CalculateDispatchNum(_hzb_kernel, base_w, base_h, 1);
                                cmd->Dispatch(_hzb_gen.get(), _hzb_kernel, x, y);
                          });
        }

    }

    void HZBPass::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        auto cmd = CommandBufferPool::Get(_name);
        u16 w = (rendering_data._width + 1) >> 1;
        u16 h = (rendering_data._height + 1) >> 1;
        RTHandle hzb_rt = cmd->GetTempRT(w, h, "_HZB", ERenderTargetFormat::kRFloat, true, false, true);
        cmd->Clear();
        {
            PROFILE_BLOCK_GPU(cmd.get(), "HZB")
            _hzb_gen->SetTexture(_hzb_kernel, "_DepthInput", rendering_data._camera_depth_target_handle);
            u16 mip = Texture::MaxMipmapCount(w, h);
            u16 first_dispatch_mip_num = std::min<u16>(4, mip);
            {
                _hzb_gen->SetInt("NumMipLevels", first_dispatch_mip_num);
                _hzb_gen->SetTexture(_hzb_kernel, "_HZ_Buffer_Mip1", hzb_rt, ECubemapFace::kUnknown, 0);
                _hzb_gen->SetTexture(_hzb_kernel, "_HZ_Buffer_Mip2", hzb_rt, ECubemapFace::kUnknown, 1);
                _hzb_gen->SetTexture(_hzb_kernel, "_HZ_Buffer_Mip3", hzb_rt, ECubemapFace::kUnknown, 2);
                _hzb_gen->SetTexture(_hzb_kernel, "_HZ_Buffer_Mip4", hzb_rt, ECubemapFace::kUnknown, 3);
                auto [x, y, z] = _hzb_gen->CalculateDispatchNum(_hzb_kernel, w, h, 1);
                cmd->Dispatch(_hzb_gen.get(), _hzb_kernel, x, y);
            }
            if (u16 second_dispatch_mip_num = mip - first_dispatch_mip_num; second_dispatch_mip_num > 0)
            {
                _hzb_gen->SetInt("NumMipLevels", second_dispatch_mip_num);
                _hzb_gen->SetTexture(_hzb_kernel, "_DepthInput", hzb_rt, ECubemapFace::kUnknown, 3);
                _hzb_gen->SetTexture(_hzb_kernel, "_HZ_Buffer_Mip1", hzb_rt, ECubemapFace::kUnknown, 4);
                _hzb_gen->SetTexture(_hzb_kernel, "_HZ_Buffer_Mip2", hzb_rt, ECubemapFace::kUnknown, 5);
                _hzb_gen->SetTexture(_hzb_kernel, "_HZ_Buffer_Mip3", hzb_rt, ECubemapFace::kUnknown, 6);
                _hzb_gen->SetTexture(_hzb_kernel, "_HZ_Buffer_Mip4", hzb_rt, ECubemapFace::kUnknown, 7);
                //u16 base_w = w, base_h = h;
                auto [base_w, base_h] = Texture::CalculateMipSize(w, h, 4);
                auto [x, y, z] = _hzb_gen->CalculateDispatchNum(_hzb_kernel, base_w, base_h, 1);
                cmd->Dispatch(_hzb_gen.get(), _hzb_kernel, x, y);
            }
        }
        cmd->ReleaseTempRT(hzb_rt);
        context->ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
        Shader::SetGlobalTexture("_CameraHiZTexture", hzb_rt);
        ComputeShader::SetGlobalTexture("_CameraHiZTexture", hzb_rt);
    }
    void HZBPass::BeginPass(GraphicsContext *context) {}
    void HZBPass::EndPass(GraphicsContext *context) {}
#pragma endregion

#pragma region DepthOnlyPass

    DepthOnlyPass::DepthOnlyPass() : RenderPass("DepthOnlyPass")
    {
        _event = ERenderPassEvent::kBeforeGbuffer;
    }

    void DepthOnlyPass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData & rendering_data)
    {
        graph.AddPass(_name,RDG::PassDesc(),[&,this](RDG::RenderGraphBuilder &builder)
        {
            rendering_data._rg_handles._depth_target = builder.Write(rendering_data._rg_handles._depth_target,EResourceUsage::kDSV);
        },
        [this](RDG::RenderGraph& graph, CommandBuffer *cmd, const RenderingData &data){
            cmd->SetRenderTarget(nullptr,graph.Resolve<RenderTexture>(data._rg_handles._depth_target));
            cmd->ClearRenderTarget(kZFar, 0u);
            Vector<QueuedDrawItem> draw_items;
            for (auto &it: *data._cull_results)
            {
                auto &[queue, objs] = it;
                for (auto &obj: objs)
                {
                    if (i16 shadow_pass = obj._material->GetActiveShader()->FindPass("DepthOnly"); shadow_pass != -1)
                    {
                        draw_items.emplace_back(MakeQueuedDrawItem(queue, obj, obj._material, (u16) shadow_pass));
                    }
                }
            }
            EmitQueuedDraws(cmd, draw_items);
        });
    }

#pragma endregion
}// namespace Ailu::Render
