#include "Render/Features/VolumetricFog.h"
#include "Render/Renderer.h"
#include "Render/GraphicsContext.h"
#include "Render/Material.h"
#include "Render/CommandBuffer.h"
#include "Render/RenderGraph/RenderGraph.h"
#include "Render/ShaderInterop.h"
#include "Framework/Common/ResourceMgr.h"
#include <cmath>
#include "Scene/Scene.h"
#include "Scene/Component.h"
#include "Render/Gizmo.h"

namespace Ailu
{
    namespace Render
    {
        namespace
        {
            bool MatrixNearlyEqual(const Matrix4x4f &lhs, const Matrix4x4f &rhs, f32 epsilon)
            {
                for (u32 row = 0; row < 4; ++row)
                {
                    for (u32 column = 0; column < 4; ++column)
                    {
                        if (std::abs(lhs[row][column] - rhs[row][column]) > epsilon)
                            return false;
                    }
                }
                return true;
            }

            bool IsCameraCut(const VolumetricFogHistory &history, const Camera &camera, f32 fog_far)
            {
                const f32 cut_distance = std::max(1.0f, fog_far * 0.1f);
                const Vector3f position_delta = camera.Position() - history._prev_position;
                return std::sqrt(DotProduct(position_delta, position_delta)) > cut_distance ||
                       DotProduct(camera.Forward(), history._prev_forward) < 0.5f;
            }

            Ref<Texture3D> CreateLightTexture(const Vector3UInt &voxel_num, const String &name)
            {
                TextureDesc desc;
                desc._width = voxel_num.x;
                desc._height = voxel_num.y;
                desc._depth = voxel_num.z;
                desc._format = EALGFormat::kALGFormatR16G16B16A16_FLOAT;
                desc._is_random_access = true;
                desc._mip_num = 0;
                auto texture = Texture3D::Create(desc);
                texture->Name(name);
                texture->Apply();
                return texture;
            }

            Ref<Texture3D> CreateAccumTexture(const Vector3UInt &voxel_num)
            {
                TextureDesc desc;
                desc._width = voxel_num.x;
                desc._height = voxel_num.y;
                desc._depth = voxel_num.z;
                desc._format = EALGFormat::kALGFormatR16G16B16A16_FLOAT;
                desc._is_random_access = true;
                desc._mip_num = 0;
                auto texture = Texture3D::Create(desc);
                texture->Name("VolumetricFogAccumTexture");
                texture->Apply();
                return texture;
            }
        }

        #pragma region VolumetricFogPass
        VolumetricFogPass::VolumetricFogPass()
        {
            _event = ERenderPassEvent::kAfterDeferedLighting;
            _debug_material = MakeRef<Material>(ResourceMgr::Get().Load<Shader>(L"Shaders/hlsl/voxel_drawer.alasset").get(), "VolumeRayDebugLineMat");
            _debug_material->SetVector("_GridNum", Vector4Int(8,8,8,0));
            _debug_material->SetFloat("_DebugExtinction", 0.0f);
        }
        VolumetricFogPass::~VolumetricFogPass()
        {
        }
        void VolumetricFogPass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
        {
            //static RDG::RGHandle s_max_z_handle;
            //static Vector2f maxz_sample_uv_scale;
            // graph.AddPass("MaxZ", RDG::PassDesc(RDG::EPassType::kCompute), [&, this](RDG::RenderGraphBuilder &builder)
            // {
            //     TextureDesc desc = TextureDesc(_voxel_num.x,_voxel_num.y,ERenderTargetFormat::kRFloat);
            //     desc._is_random_access = true;
            //     s_max_z_handle = builder.AllocTexture(desc, "VolumetricFog_MaxZ");
            //     builder.Read(rendering_data._rg_handles._depth_tex);
            //     s_max_z_handle = builder.Write(s_max_z_handle);
            // }, 
            // [this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
            // {
            //     u32 sample_width = (data._width + _voxel_num.x - 1) / _voxel_num.x;
            //     u32 sample_height = (data._height + _voxel_num.y - 1) / _voxel_num.y;
            //     maxz_sample_uv_scale.x = (data._width / (f32)_voxel_num.x) / (f32)sample_width;
            //     maxz_sample_uv_scale.y = (data._height / (f32)_voxel_num.y) / (f32)sample_height;
            //     _max_z_cs->SetVector("_radius", Vector2f((f32)sample_width, (f32)sample_height));
            //     auto kernel = _max_z_cs->FindKernel("MaxZ");
            //     //_max_z_cs->SetTexture("_VolumetricLight", _cur_light_texture);
            //     _max_z_cs->SetTexture("_MaxZ_Texture", graph.Resolve<Texture>(s_max_z_handle));
            //     _max_z_cs->SetTexture("_CameraDepthTexture", graph.Resolve<Texture>(data._rg_handles._depth_tex));
            //     auto [x,y,z] = _max_z_cs->CalculateDispatchNum(kernel, _voxel_num.x,_voxel_num.y, 0);
            //     cmd->Dispatch(_max_z_cs, kernel, x, y, 1);
            // });
            const auto shadow_handle = rendering_data._rg_handles._main_light_shadow_map;
            const auto spot_shadow_handle = rendering_data._rg_handles._addi_shadow_maps;
            const auto point_shadow_handle = rendering_data._rg_handles._point_light_shadow_maps;
            const auto environment_sh_handle = rendering_data._rg_handles._environment_sh;
            const auto volume_handle = graph.Import(_local_fog_buffer.get());
            graph.AddPass("VolumetricFog_LightInject", RDG::PassDesc(RDG::EPassType::kCompute),
                          [&, this, shadow_handle, spot_shadow_handle, point_shadow_handle, environment_sh_handle,
                           volume_handle](RDG::RenderGraphBuilder &builder)
            {
                if (shadow_handle.IsValid())
                    builder.Read(shadow_handle);
                if (environment_sh_handle.IsValid())
                    builder.Read(environment_sh_handle);
                builder.Read(volume_handle);
                if (spot_shadow_handle.IsValid())
                    builder.Read(spot_shadow_handle);
                if (point_shadow_handle.IsValid())
                    builder.Read(point_shadow_handle);
                _history_light_handle = builder.Import(_history_light_texture);
                builder.Read(_history_light_handle);
                _medium_handle = builder.Import(_medium_texture);
                _medium_handle = builder.Write(_medium_handle, EResourceUsage::kWriteUAV);
                _inject_handle = builder.Import(_cur_light_texture);
                _inject_handle = builder.Write(_inject_handle,EResourceUsage::kWriteUAV);
            }, 
            [this, shadow_handle, spot_shadow_handle, point_shadow_handle, environment_sh_handle, volume_handle]
            (RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
            {
                auto *cur_light = graph.Resolve<Texture3D>(_inject_handle);
                auto *history_light = graph.Resolve<Texture3D>(_history_light_handle);
                auto *medium_texture = graph.Resolve<Texture3D>(_medium_handle);
                auto *main_light_shadow_map = shadow_handle.IsValid()
                                                  ? graph.Resolve<Texture>(shadow_handle)
                                                  : nullptr;
                if (cur_light == nullptr || history_light == nullptr || medium_texture == nullptr ||
                    (shadow_handle.IsValid() && main_light_shadow_map == nullptr))
                    return;
                auto *volumes = graph.Resolve<GPUBuffer>(volume_handle);
                if (volumes == nullptr || !volumes->IsReady())
                    return;
                _volumetric_fog->SetBuffer(_light_injection_kernel, "_LocalFogVolumes", volumes);
                _volumetric_fog->SetInt("_local_fog_count", _local_fog_count);
                const Camera *cam = data._camera;
                auto *scene_data = ConstantBuffer::As<CBufferPerSceneData>(data._p_per_scene_cbuf);
                u64 lighting_signature = 14695981039346656037ull;
                const auto hash_bytes = [&lighting_signature](const void *value, size_t size)
                {
                    const auto *bytes = static_cast<const u8 *>(value);
                    for (size_t index = 0u; index < size; ++index)
                        lighting_signature = (lighting_signature ^ bytes[index]) * 1099511628211ull;
                };
                hash_bytes(&_local_lights_enabled, sizeof(_local_lights_enabled));
                hash_bytes(&_max_local_lights, sizeof(_max_local_lights));
                if (scene_data != nullptr)
                {
                    const u32 point_count = std::min(u32(RenderConstants::kMaxPointLightNum),
                                                     u32(scene_data->_ActiveLightCount.y));
                    const u32 spot_count = std::min(4u, u32(scene_data->_ActiveLightCount.z));
                    hash_bytes(&point_count, sizeof(point_count));
                    hash_bytes(&spot_count, sizeof(spot_count));
                    hash_bytes(scene_data->_PointLights, point_count * sizeof(ShaderDirectionalAndPointLightData));
                    hash_bytes(scene_data->_SpotLights, spot_count * sizeof(ShaderSpotlLightData));
                    const bool overflow = _local_lights_enabled && point_count + spot_count > _max_local_lights;
                    if (overflow && !_local_light_overflow)
                        LOG_WARNING("[VolumetricFog] {} punctual lights exceed cap {}; truncating",
                            point_count + spot_count, _max_local_lights);
                    _local_light_overflow = overflow;
                }
                if (_history_state != nullptr && _history_state->_local_lighting_signature != lighting_signature)
                {
                    _history_valid = false;
                    _history_state->_local_lighting_signature = lighting_signature;
                }
                auto *environment_sh = environment_sh_handle.IsValid()
                                           ? graph.Resolve<GPUBuffer>(environment_sh_handle)
                                           : nullptr;
                const f32 fog_far = std::max(cam->Near() * 1.001f, std::min(cam->Far(), _fog_distance));
                _volumetric_fog->SetMatrix("_matrix_iv",MatrixInverse(cam->GetView()));
                _volumetric_fog->SetMatrix("_matrix_ip",MatrixInverse(cam->GetProjNoJitter()));
                const VolumetricFogHistory empty_history;
                const auto &history = _history_state != nullptr ? *_history_state : empty_history;
                _volumetric_fog->SetMatrix("_matrix_pre_v", history._prev_view);
                _volumetric_fog->SetMatrix("_matrix_pre_p", history._prev_proj);
                _volumetric_fog->SetFloat("_cam_near", cam->Near());
                _volumetric_fog->SetFloat("_cam_far", fog_far);
                _volumetric_fog->SetFloat("_prev_cam_near", history._prev_near);
                _volumetric_fog->SetFloat("_prev_cam_far", history._prev_far);
                _volumetric_fog->SetBool("_temporal_reprojection", _temporal_reprojection && _history_valid);
                _volumetric_fog->SetBool("_has_main_light_shadow", shadow_handle.IsValid());
                _volumetric_fog->SetBool("_has_spot_light_shadow", spot_shadow_handle.IsValid());
                _volumetric_fog->SetBool("_has_point_light_shadow", point_shadow_handle.IsValid());
                if (point_shadow_handle.IsValid())
                    _volumetric_fog->SetTexture(_light_injection_kernel, "_PointLightShadowMaps",
                        graph.Resolve<Texture>(point_shadow_handle));
                if (spot_shadow_handle.IsValid())
                    _volumetric_fog->SetTexture(_light_injection_kernel, "_AddLightShadowMaps",
                        graph.Resolve<Texture>(spot_shadow_handle));
                _volumetric_fog->SetBool("_has_environment_sh", environment_sh != nullptr);
                _volumetric_fog->SetFloat("_environment_intensity", _environment_intensity);
                _volumetric_fog->SetVector("_cam_pos", cam->Position());
                //_volumetric_fog->SetVector("_zmax_uv_scale", maxz_sample_uv_scale);
                _volumetric_fog->SetTexture(_light_injection_kernel, "_VolumetricLight", cur_light);
                _volumetric_fog->SetTexture(_light_injection_kernel, "_History_VolumetricLight", history_light);
                if (shadow_handle.IsValid())
                    _volumetric_fog->SetTexture(_light_injection_kernel, RenderResourceName::kMainLightShadowMap,
                                                 main_light_shadow_map);
                if (environment_sh != nullptr)
                    _volumetric_fog->SetBuffer(_light_injection_kernel, "g_environment_sh", environment_sh);
                auto [x,y,z] = _volumetric_fog->CalculateDispatchNum(_light_injection_kernel, cur_light->Width(),
                                                                      cur_light->Height(), cur_light->Depth());
                _volumetric_fog->SetTexture(_light_injection_kernel, "_FogMedium", medium_texture);
                cmd->Dispatch(_volumetric_fog.get(), _light_injection_kernel, x, y, z);
                cmd->UavBarrier(medium_texture);
                cmd->UavBarrier(cur_light);
                _volumetric_fog->SetTexture(_local_light_kernel, "_FogMedium", medium_texture);
                _volumetric_fog->SetTexture(_local_light_kernel, "_VolumetricLight", cur_light);
                _volumetric_fog->SetTexture(_local_light_kernel, "_BlueNoise",
                    ResourceMgr::Get().Get<Texture2D>(EnginePath::kEngineTexturePathW + L"blue_noise.alasset"));
                if (spot_shadow_handle.IsValid())
                    _volumetric_fog->SetTexture(_local_light_kernel, "_AddLightShadowMaps",
                        graph.Resolve<Texture>(spot_shadow_handle));
                if (point_shadow_handle.IsValid())
                    _volumetric_fog->SetTexture(_local_light_kernel, "_PointLightShadowMaps",
                        graph.Resolve<Texture>(point_shadow_handle));
                _local_light_count = 0u;
                _local_light_froxel_count = 0u;
                cmd->BeginProfiler("VolumetricFog_LocalLights");
                if (_local_lights_enabled && scene_data != nullptr)
                {
                    for (u32 light_type = 0u; light_type < 2u; ++light_type)
                    {
                        const u32 capacity = light_type == 0u ? RenderConstants::kMaxPointLightNum
                                                            : RenderConstants::kMaxSpotLightNum;
                        const u32 count = std::min(capacity, u32(scene_data->_ActiveLightCount[light_type + 1u]));
                        for (u32 light_index = 0u; light_index < count && _local_light_count < _max_local_lights;
                             ++light_index)
                        {
                            const auto position = light_type == 0u ? scene_data->_PointLights[light_index]._LightPos
                                : scene_data->_SpotLights[light_index]._LightPos;
                            const f32 range = light_type == 0u ? scene_data->_PointLights[light_index]._LightParam0
                                : scene_data->_SpotLights[light_index]._Rdius;
                            const auto bounds = _bounded_local_lights
                                ? CalculateFroxelBounds(position, range, cam->GetView(), cam->GetProjNoJitter(),
                                    cam->Near(), fog_far, _voxel_num)
                                : FroxelBounds{Vector3UInt(0u, 0u, 0u), _voxel_num};
                            ++_local_light_count;
                            _local_light_froxel_count += bounds.Count();
                            if (bounds.IsEmpty())
                                continue;
                            const auto size = bounds._max - bounds._min;
                            _volumetric_fog->SetInt("_local_light_index", light_index);
                            _volumetric_fog->SetInt("_local_light_type", light_type);
                            _volumetric_fog->SetInts("_bounds_min",
                                {i32(bounds._min.x), i32(bounds._min.y), i32(bounds._min.z), 0});
                            _volumetric_fog->SetInts("_bounds_size", {i32(size.x), i32(size.y), i32(size.z), 0});
                            const auto [lx, ly, lz] = _volumetric_fog->CalculateDispatchNum(_local_light_kernel,
                                size.x, size.y, size.z);
                            cmd->Dispatch(_volumetric_fog.get(), _local_light_kernel, lx, ly, lz);
                            // UAV RMW is ordered between lights; dispatch snapshots retain each light's constants.
                            cmd->UavBarrier(cur_light);
                        }
                    }
                }
                cmd->EndProfiler();
                if (_temporal_reprojection && _history_valid)
                {
                    _volumetric_fog->SetTexture(_temporal_kernel, "_VolumetricLight", cur_light);
                    _volumetric_fog->SetTexture(_temporal_kernel, "_History_VolumetricLight", history_light);
                    const auto [tx, ty, tz] = _volumetric_fog->CalculateDispatchNum(_temporal_kernel,
                        _voxel_num.x, _voxel_num.y, _voxel_num.z);
                    cmd->Dispatch(_volumetric_fog.get(), _temporal_kernel, tx, ty, tz);
                    cmd->UavBarrier(cur_light);
                }
                if (_history_state != nullptr)
                {
                    _history_state->_prev_proj = cam->GetProjNoJitter();
                    _history_state->_prev_view = cam->GetView();
                    _history_state->_prev_position = cam->Position();
                    _history_state->_prev_forward = cam->Forward();
                    _history_state->_prev_near = cam->Near();
                    _history_state->_prev_far = fog_far;
                    _history_state->_valid = true;
                    _history_state->_is_cur_a = !_history_state->_is_cur_a;
                }
            });
            graph.AddPass("VolumetricFog_LightIntegration", RDG::PassDesc(RDG::EPassType::kCompute), [&, this](RDG::RenderGraphBuilder &builder)
            {
                builder.Read(_inject_handle, EResourceUsage::kWriteUAV);
                //builder.Read(s_max_z_handle);
                _accum_handle = builder.Write(rendering_data._rg_handles._volumetric_fog_accum, EResourceUsage::kWriteUAV);
                rendering_data._rg_handles._volumetric_fog_accum = _accum_handle;
            },
            [this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
            {
                auto *cur_light = graph.Resolve<Texture3D>(_inject_handle);
                auto *accum_tex = graph.Resolve<Texture3D>(_accum_handle);
                if (cur_light == nullptr || accum_tex == nullptr)
                    return;
                // const Camera* cam = Camera::sSelected? Camera::sSelected : data._camera;
                // _volumetric_fog->SetMatrix("_matrix_iv",MatrixInverse(cam->GetView()));
                // _volumetric_fog->SetMatrix("_matrix_ip",MatrixInverse(cam->GetProjNoJitter()));
                // _volumetric_fog->SetFloat("_cam_near", cam->Near());
                // _volumetric_fog->SetFloat("_cam_far", cam->Far());
                // _volumetric_fog->SetVector("_cam_pos", cam->Position());
                _volumetric_fog->SetTexture(_light_integration_kernel, "_VolumetricLight", cur_light);
                _volumetric_fog->SetTexture(_light_integration_kernel, "_FogAccum", accum_tex);
                //_volumetric_fog->SetTexture("_MaxZ_Texture", graph.Resolve<Texture>(s_max_z_handle));
                auto [x,y,z] = _volumetric_fog->CalculateDispatchNum(_light_integration_kernel, cur_light->Width(), cur_light->Height(), cur_light->Depth());
                cmd->Dispatch(_volumetric_fog.get(), _light_integration_kernel, x, y, 1);
            });
            if (_debug_voxel_pos || _debug_extinction)
            {
                graph.AddPass("Debug Ray", RDG::PassDesc(), [&, this](RDG::RenderGraphBuilder &builder)
                            {
                    builder.Read(rendering_data._rg_handles._color_target, EResourceUsage::kWriteRTV);
                    builder.Read(rendering_data._rg_handles._depth_target, EResourceUsage::kDSV);
                    builder.Read(_inject_handle);
                    rendering_data._rg_handles._color_target = builder.Write(rendering_data._rg_handles._color_target);
                    rendering_data._rg_handles._depth_target = builder.Write(rendering_data._rg_handles._depth_target, EResourceUsage::kDSV);
                }, 
                [this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
                            { 
                    auto *cur_light = graph.Resolve<Texture3D>(_inject_handle);
                    if (cur_light == nullptr)
                        return;
                    auto w = cur_light->Width();
                    auto h = cur_light->Height();
                    auto d = cur_light->Depth();
                    _debug_material->SetVector("_GridNum", Vector4Int(w,h,d,1));
                    _debug_material->SetFloat("_DebugExtinction", _debug_extinction ? 1.0f : 0.0f);
                    _debug_material->SetTexture("_VoxelSrc", cur_light);
                    cmd->SetRenderTarget(data._rg_handles._color_target,data._rg_handles._depth_target);
                    cmd->DrawProcedural(_debug_material.get(), 1u, 2u,w*h*d);
                });
            }

        }
        void VolumetricFogPass::BeginPass(GraphicsContext *context)
        {
        }
        void VolumetricFogPass::EndPass(GraphicsContext *context)
        {
        }
        #pragma endregion

        VolumetricFogCompositePass::VolumetricFogCompositePass() : RenderPass("VolumetricFog_Composite")
        {
            _event = ERenderPassEvent::kAfterSkybox;
            auto shader = ResourceMgr::Get().Load<Shader>(L"Shaders/hlsl/volumetric_fog_composite.alasset");
            _composite_material = MakeRef<Material>(shader.get(), "VolumetricFogComposite");
        }

        void VolumetricFogCompositePass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
        {
            if (!rendering_data._rg_handles._volumetric_fog_accum.IsValid())
                return;
            const auto fog_handle = rendering_data._rg_handles._volumetric_fog_accum;
            const auto depth_handle = rendering_data._rg_handles._depth_tex;
            graph.AddPass(_name, RDG::PassDesc(), [&](RDG::RenderGraphBuilder &builder)
            {
                builder.Read(fog_handle);
                builder.Read(depth_handle);
                if (rendering_data._rg_handles._pre_exposure_ev.IsValid())
                    builder.Read(rendering_data._rg_handles._pre_exposure_ev, EResourceUsage::kReadSRV);
                // Keep the existing sky/opaque color as the blend destination without sampling it as an SRV.
                rendering_data._rg_handles._color_target = builder.WriteAfter(rendering_data._rg_handles._color_target,
                                                                             EResourceUsage::kWriteRTV);
            }, [this, fog_handle, depth_handle](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
            {
                _composite_material->SetTexture("_VolumetricFogAccum", graph.Resolve<Texture>(fog_handle));
                _composite_material->SetTexture("_CameraDepthTexture", graph.Resolve<Texture>(depth_handle));
                const auto *cam = data._camera;
                _composite_material->SetFloat("_fog_far", std::max(cam->Near() * 1.001f,
                                                                  std::min(cam->Far(), _fog_distance)));
                cmd->SetRenderTargetLoadAction(data._rg_handles._color_target, ELoadStoreAction::kLoad);
                cmd->SetRenderTarget(data._rg_handles._color_target);
                cmd->DrawFullScreenQuad(_composite_material.get());
            });
        }
        
        #pragma region VolumetricFog
        VolumetricFog::VolumetricFog() : RenderFeature("VolumetricFog")
        {
            BufferDesc volume_desc;
            volume_desc._target = EGPUBufferTarget::kStructured;
            volume_desc._element_size = sizeof(LocalFogVolumeData);
            volume_desc._element_num = kMaxLocalFogVolumes;
            volume_desc._is_random_write = false;
            volume_desc._is_create_uav = false;
            _local_fog_buffer = GPUBuffer::Create(volume_desc, "VolumetricFogLocalVolumes");
            _volumetric_fog_pass = MakeScope<VolumetricFogPass>();
            _composite_pass = MakeScope<VolumetricFogCompositePass>();
            _scratch_light_a = CreateLightTexture(_voxel_num, "VolumetricLightScratchA");
            _scratch_light_b = CreateLightTexture(_voxel_num, "VolumetricLightScratchB");
            _volumetric_fog_cs = ResourceMgr::Get().LoadAssetRef<ComputeShader>(L"Shaders/hlsl/Compute/volumetric_light.alasset");
            _max_z_cs = ResourceMgr::Get().LoadAssetRef<ComputeShader>(L"Shaders/hlsl/Compute/max_z.alasset");
            _accum_texture = CreateAccumTexture(_voxel_num);
            _medium_texture = CreateLightTexture(_voxel_num, "VolumetricFogMedium");
            _volumetric_fog_cs->SetFloat("_FogDensity", _fog_density);
            _volumetric_fog_cs->SetFloat("_fog_height", _fog_height);
            _volumetric_fog_cs->SetFloat("_height_falloff", _height_falloff);
            _volumetric_fog_cs->SetFloat("_temporal_current_weight", _blend_factor);
            _volumetric_fog_cs->SetBool("_temporal_reprojection", _temporal_reprojection);
            _volumetric_fog_cs->SetFloat("_intensity", _intensity);
            _volumetric_fog_cs->SetFloat("_ambient_intensity", _ambient_intensity);
            _volumetric_fog_cs->SetFloat("_g", _g);
            _volumetric_fog_pass->_volumetric_fog = _volumetric_fog_cs;
            _volumetric_fog_pass->_max_z_cs = _max_z_cs;
            _volumetric_fog_pass->_light_injection_kernel = _volumetric_fog_cs->FindKernel("LightInjection");
            _volumetric_fog_pass->_local_light_kernel = _volumetric_fog_cs->FindKernel("LocalLightInjection");
            _volumetric_fog_pass->_temporal_kernel = _volumetric_fog_cs->FindKernel("TemporalLighting");
            _volumetric_fog_pass->_light_integration_kernel = _volumetric_fog_cs->FindKernel("LightIntegration");
            const auto blue_noise_path = EnginePath::kEngineTexturePathW + L"blue_noise.alasset";
            auto *blue_noise = ResourceMgr::Get().Get<Texture2D>(blue_noise_path);
            _volumetric_fog_cs->SetTexture(_volumetric_fog_pass->_light_injection_kernel, "_BlueNoise", blue_noise);
            _volumetric_fog_pass->_voxel_num = _voxel_num;
        }
        
        VolumetricFog::~VolumetricFog()
        {

        }

        void VolumetricFog::UpdateLocalFogVolumes(const RenderingData &rendering_data)
        {
            Vector<LocalFogVolumeData> volumes;
            u32 enabled_count = 0u;
            if (rendering_data._scene != nullptr)
            {
                const auto &registry = rendering_data._scene->GetRegister();
                u32 volume_index = 0u;
                for (const auto &volume : registry.View<ECS::LocalFogVolumeComponent>())
                {
                    const auto entity = registry.GetEntity<ECS::LocalFogVolumeComponent>(volume_index++);
                    const auto *transform = registry.GetComponent<ECS::TransformComponent>(entity);
                    if (!registry.IsEntityEnabled(entity) ||
                        !registry.IsComponentEnabled<ECS::LocalFogVolumeComponent>(entity) ||
                        !volume._enabled || transform == nullptr)
                        continue;
                    const auto scale = transform->GetRenderScale();
                    if (std::abs(scale.x) < 1e-4f || std::abs(scale.y) < 1e-4f || std::abs(scale.z) < 1e-4f)
                        continue;
                    if (++enabled_count > kMaxLocalFogVolumes)
                        continue;
                    LocalFogVolumeData data;
                    const auto &local_to_world = transform->GetRenderWorldMatrix();
                    data._world_to_local = MatrixInverse(local_to_world);
                    if (_debug_local_fog_bounds)
                    {
                        const Color color(0.3f, 0.8f, 1.0f, 1.0f);
                        if (volume._shape == ECS::ELocalFogVolumeShape::kBox)
                        {
                            for (u32 corner = 0u; corner < 8u; ++corner)
                            {
                                const Vector3f local((corner & 1u) ? 0.5f : -0.5f,
                                    (corner & 2u) ? 0.5f : -0.5f, (corner & 4u) ? 0.5f : -0.5f);
                                for (u32 axis = 0u; axis < 3u; ++axis)
                                {
                                    if (corner & (1u << axis))
                                        continue;
                                    auto other = local;
                                    other[axis] = 0.5f;
                                    Gizmo::DrawLine(TransformCoord(local_to_world, local),
                                        TransformCoord(local_to_world, other), color);
                                }
                            }
                        }
                        else
                        {
                            for (u32 axis = 0u; axis < 3u; ++axis)
                            {
                                for (u32 segment = 0u; segment < 32u; ++segment)
                                {
                                    const f32 angle_a = segment * 6.2831853f / 32.0f;
                                    const f32 angle_b = (segment + 1u) * 6.2831853f / 32.0f;
                                    Vector3f a = Vector3f::kZero, b = Vector3f::kZero;
                                    a[(axis + 1u) % 3u] = std::cos(angle_a);
                                    a[(axis + 2u) % 3u] = std::sin(angle_a);
                                    b[(axis + 1u) % 3u] = std::cos(angle_b);
                                    b[(axis + 2u) % 3u] = std::sin(angle_b);
                                    Gizmo::DrawLine(TransformCoord(local_to_world, a),
                                        TransformCoord(local_to_world, b), color);
                                }
                            }
                        }
                    }
                    data._density = std::max(volume._density, 0.0f);
                    data._scattering_albedo = Vector3f(std::clamp(volume._scattering_albedo.x, 0.0f, 1.0f),
                        std::clamp(volume._scattering_albedo.y, 0.0f, 1.0f),
                        std::clamp(volume._scattering_albedo.z, 0.0f, 1.0f));
                    data._edge_fade = std::max(volume._edge_fade, 1e-4f);
                    data._shape = static_cast<u32>(volume._shape);
                    volumes.push_back(data);
                }
            }
            const bool medium_changed = volumes.size() != _previous_local_fog_volumes.size() ||
                (!volumes.empty() && std::memcmp(volumes.data(), _previous_local_fog_volumes.data(),
                                               volumes.size() * sizeof(LocalFogVolumeData)) != 0);
            if (medium_changed)
            {
                for (auto &entry : _histories)
                    entry.second._valid = false;
                _previous_local_fog_volumes = volumes;
            }
            const bool overflow = enabled_count > kMaxLocalFogVolumes;
            if (overflow && !_local_fog_overflow)
                LOG_WARNING("[VolumetricFog] Local volume count {} exceeds {}; truncating",
                    enabled_count, kMaxLocalFogVolumes);
            _local_fog_overflow = overflow;
            _volumetric_fog_pass->_local_fog_count = static_cast<u32>(volumes.size());
            _local_fog_volume_count = static_cast<u32>(volumes.size());
            // Keep a valid initialized binding even when there are no volumes.
            if (volumes.empty())
                volumes.push_back(LocalFogVolumeData{});
            _local_fog_buffer->SetData(volumes);
            _volumetric_fog_pass->_local_fog_buffer = _local_fog_buffer;
        }

        void VolumetricFog::UpdateFroxelResolution(u32 viewport_width, u32 viewport_height)
        {
            const u32 tile_size = std::clamp(_xy_tile_size, 1u, 32u);
            const u32 slice_count = std::clamp(_z_slice_count, 16u, 256u);
            const Vector3UInt voxel_num(
                std::max(1u, (viewport_width + tile_size - 1u) / tile_size),
                std::max(1u, (viewport_height + tile_size - 1u) / tile_size),
                slice_count);
            if (viewport_width == _resource_viewport_width && viewport_height == _resource_viewport_height &&
                tile_size == _resource_xy_tile_size && slice_count == _resource_z_slice_count &&
                voxel_num.x == _voxel_num.x && voxel_num.y == _voxel_num.y && voxel_num.z == _voxel_num.z)
                return;

            _voxel_num = voxel_num;
            _resource_viewport_width = viewport_width;
            _resource_viewport_height = viewport_height;
            _resource_xy_tile_size = tile_size;
            _resource_z_slice_count = slice_count;
            _accum_texture = CreateAccumTexture(_voxel_num);
            _medium_texture = CreateLightTexture(_voxel_num, "VolumetricFogMedium");
            _scratch_light_a = CreateLightTexture(_voxel_num, "VolumetricLightScratchA");
            _scratch_light_b = CreateLightTexture(_voxel_num, "VolumetricLightScratchB");
            _scratch_is_cur_a = true;
            _histories.clear();
            _last_camera_hash = 0u;
            _volumetric_fog_pass->_voxel_num = _voxel_num;
        }

        void VolumetricFog::AddRenderPasses(Renderer &renderer, const RenderingData &rendering_data)
        {
            const Camera *camera = rendering_data._camera;
            if (camera == nullptr)
                return;
            UpdateFroxelResolution(rendering_data._width, rendering_data._height);
            UpdateLocalFogVolumes(rendering_data);
            const std::array<f32, 13> fog_settings{_fog_density, _fog_height, _height_falloff, _intensity,
                _ambient_intensity, _g, _blend_factor, f32(_medium_debug_mode), f32(_max_local_lights),
                _local_light_shadows ? 1.0f : 0.0f, _jitter_enabled ? 1.0f : 0.0f,
                _local_light_intensity, _local_light_supersampling ? 1.0f : 0.0f};
            if (fog_settings != _previous_fog_settings)
            {
                for (auto &entry : _histories)
                    entry.second._valid = false;
                _previous_fog_settings = fog_settings;
            }
            _volumetric_fog_cs->SetFloat("_jitter_scale", _jitter_enabled ? .8f : 0.0f);
            _volumetric_fog_cs->SetFloat("_local_light_intensity", _local_light_intensity);
            _volumetric_fog_cs->SetBool("_local_light_supersampling", _local_light_supersampling);
            _volumetric_fog_cs->SetFloat("_FogDensity", _fog_density);
            _volumetric_fog_cs->SetFloat("_fog_height", _fog_height);
            _volumetric_fog_cs->SetFloat("_height_falloff", _height_falloff);
            _volumetric_fog_cs->SetFloat("_intensity", _intensity);
            _volumetric_fog_cs->SetFloat("_ambient_intensity", _ambient_intensity);
            _volumetric_fog_cs->SetFloat("_g", _g);
            _volumetric_fog_cs->SetFloat("_temporal_current_weight", _blend_factor);
            _volumetric_fog_cs->SetInt("_medium_debug_mode", _medium_debug_mode);
            _volumetric_fog_cs->SetBool("_local_lights_enabled", _local_lights_enabled);
            _volumetric_fog_cs->SetBool("_local_light_shadows", _local_light_shadows);
            const u64 camera_hash = camera->HashCode();
            const f32 fog_far = std::max(camera->Near() * 1.001f, std::min(camera->Far(), _fog_distance));
            if (renderer._is_use_render_graph && rendering_data._p_per_camera_cbuf != nullptr)
            {
                auto *camera_data = ConstantBuffer::As<CBufferPerCameraData>(rendering_data._p_per_camera_cbuf);
                camera_data->_volumetric_fog_far = fog_far;
                camera_data->_volumetric_fog_enabled = 1.0f;
            }
            const bool temporal_allowed = _temporal_reprojection && !renderer.IsRenderingCubemap();
            VolumetricFogHistory *history_state = nullptr;
            Texture3D *cur_light = nullptr;
            Texture3D *history_light = nullptr;
            bool history_valid = false;
            if (renderer.IsRenderingCubemap())
            {
                cur_light = _scratch_is_cur_a ? _scratch_light_a.get() : _scratch_light_b.get();
                history_light = _scratch_is_cur_a ? _scratch_light_b.get() : _scratch_light_a.get();
                _scratch_is_cur_a = !_scratch_is_cur_a;
            }
            else
            {
                auto &history = _histories[camera_hash];
                if (!history._light_a || !history._light_b)
                {
                    history._light_a = CreateLightTexture(_voxel_num, std::format("VolumetricLight_{}_A", camera_hash));
                    history._light_b = CreateLightTexture(_voxel_num, std::format("VolumetricLight_{}_B", camera_hash));
                    history._valid = false;
                }
                const bool camera_switched = _last_camera_hash != 0u && _last_camera_hash != camera_hash;
                const bool projection_changed = history._valid &&
                    (!MatrixNearlyEqual(camera->GetProjNoJitter(), history._prev_proj, 1e-4f) ||
                     std::abs(camera->Near() - history._prev_near) > 1e-4f ||
                     std::abs(fog_far - history._prev_far) > 1e-4f);
                const bool history_invalid = !history._valid || camera_switched || projection_changed ||
                    IsCameraCut(history, *camera, fog_far) || (temporal_allowed && !history._temporal_was_enabled);
                if (history_invalid)
                    history._valid = false;
                history_valid = temporal_allowed && history._valid;
                history_state = &history;
                cur_light = history._is_cur_a ? history._light_a.get() : history._light_b.get();
                history_light = history._is_cur_a ? history._light_b.get() : history._light_a.get();
                history._temporal_was_enabled = temporal_allowed;
                _last_camera_hash = camera_hash;
            }
            if (!cur_light->IsReady() || !history_light->IsReady() || !_medium_texture->IsReady() ||
                !_accum_texture->IsReady() || !_local_fog_buffer->IsReady())
            {
                if (rendering_data._p_per_camera_cbuf != nullptr)
                {
                    auto *camera_data = ConstantBuffer::As<CBufferPerCameraData>(rendering_data._p_per_camera_cbuf);
                    camera_data->_volumetric_fog_enabled = 0.0f;
                }
                return;
            }
            if (_debug_local_light_bounds && rendering_data._p_per_scene_cbuf != nullptr)
            {
                const auto *lights = ConstantBuffer::As<CBufferPerSceneData>(rendering_data._p_per_scene_cbuf);
                for (u32 type = 0u; type < 2u; ++type)
                {
                    const u32 capacity = type == 0u ? RenderConstants::kMaxPointLightNum
                                                    : RenderConstants::kMaxSpotLightNum;
                    const u32 count = std::min(capacity, u32(lights->_ActiveLightCount[type + 1u]));
                    for (u32 index = 0u; index < count; ++index)
                    {
                        const Vector3f position = type == 0u ? lights->_PointLights[index]._LightPos
                            : lights->_SpotLights[index]._LightPos;
                        const f32 range = type == 0u ? lights->_PointLights[index]._LightParam0
                            : lights->_SpotLights[index]._Rdius;
                        const auto bounds = CalculateFroxelBounds(position, range, camera->GetView(),
                            camera->GetProjNoJitter(), camera->Near(), fog_far, _voxel_num);
                        if (bounds.IsEmpty())
                            continue;
                        Vector3f corners[8];
                        const auto inverse_projection = MatrixInverse(camera->GetProjNoJitter());
                        const auto inverse_view = MatrixInverse(camera->GetView());
                        for (u32 corner = 0u; corner < 8u; ++corner)
                        {
                            const f32 x = f32((corner & 1u) ? bounds._max.x : bounds._min.x) / _voxel_num.x;
                            const f32 y = f32((corner & 2u) ? bounds._max.y : bounds._min.y) / _voxel_num.y;
                            const f32 z = f32((corner & 4u) ? bounds._max.z : bounds._min.z) / _voxel_num.z;
                            auto ray = TransformVector(inverse_projection, Vector4f(x * 2 - 1, y * 2 - 1, 1, 1));
                            ray /= ray.w;
                            const f32 depth = camera->Near() * std::pow(fog_far / camera->Near(), z);
                            corners[corner] = TransformCoord(inverse_view, Vector3f(ray.xyz) * (depth / ray.z));
                        }
                        for (u32 corner = 0u; corner < 8u; ++corner)
                            for (u32 axis = 0u; axis < 3u; ++axis)
                                if (!(corner & (1u << axis)))
                                    Gizmo::DrawLine(corners[corner], corners[corner | (1u << axis)],
                                        Color(1.0f, .6f, .1f, 1.0f));
                    }
                }
            }
            _volumetric_fog_pass->_debug_voxel_pos = _debug_voxel_pos;
            _volumetric_fog_pass->_debug_extinction = _debug_extinction || _medium_debug_mode == 3u;
            _volumetric_fog_pass->_fog_distance = _fog_distance;
            _volumetric_fog_pass->_temporal_reprojection = temporal_allowed;
            _volumetric_fog_pass->_history_valid = history_valid;
            _volumetric_fog_pass->_history_state = history_state;
            _volumetric_fog_pass->_environment_intensity =
                rendering_data._environment != nullptr && rendering_data._environment->_enable_ibl
                    ? rendering_data._environment->_intensity
                    : 0.0f;
            _composite_pass->_fog_distance = _fog_distance;
            _volumetric_fog_pass->_cur_light_texture = cur_light;
            _volumetric_fog_pass->_history_light_texture = history_light;
            _volumetric_fog_pass->_accum_texture = _accum_texture.get();
            _volumetric_fog_pass->_medium_texture = _medium_texture.get();
            _volumetric_fog_pass->_local_lights_enabled = _local_lights_enabled;
            _volumetric_fog_pass->_bounded_local_lights = _bounded_local_lights;
            _volumetric_fog_pass->_max_local_lights = std::min(_max_local_lights, 8u);
            _debug_light_texture = cur_light;
            renderer.EnqueuePass(_volumetric_fog_pass.get());
            renderer.EnqueuePass(_composite_pass.get());
        }
        u32 VolumetricFog::GetLocalLightCount() const { return _volumetric_fog_pass->_local_light_count; }
        u64 VolumetricFog::GetLocalLightFroxelCount() const { return _volumetric_fog_pass->_local_light_froxel_count; }

        void VolumetricFog::OnPropertyChanged(const PropertyInfo &prop)
        {
            if(prop.Name() == "_fog_density")
            {
                _volumetric_fog_cs->SetFloat("_FogDensity", _fog_density);
            }
            else if (prop.Name() == "_fog_height")
            {
                _volumetric_fog_cs->SetFloat("_fog_height", _fog_height);
            }
            else if (prop.Name() == "_height_falloff")
            {
                _volumetric_fog_cs->SetFloat("_height_falloff", _height_falloff);
            }
            else if(prop.Name() == "_blend_factor")
            {
                _volumetric_fog_cs->SetFloat("_temporal_current_weight", _blend_factor);
            }
            else if (prop.Name() == "_temporal_reprojection")
            {
                _volumetric_fog_cs->SetBool("_temporal_reprojection", _temporal_reprojection);
                if (!_temporal_reprojection)
                {
                    for (auto &entry : _histories)
                    {
                        auto &history = entry.second;
                        history._valid = false;
                        history._temporal_was_enabled = false;
                    }
                }
            }
            else if (prop.Name() == "_g")
            {
                _volumetric_fog_cs->SetFloat("_g", _g);
            }
            else if (prop.Name() == "_intensity")
            {
                _volumetric_fog_cs->SetFloat("_intensity", _intensity);
            }
            else if (prop.Name() == "_ambient_intensity")
            {
                _volumetric_fog_cs->SetFloat("_ambient_intensity", _ambient_intensity);
            }
        }
        #pragma endregion
    }// namespace Render
}// namespace Ailu
