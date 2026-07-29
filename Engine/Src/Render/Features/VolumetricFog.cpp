#include "Render/Features/VolumetricFog.h"
#include "Render/Renderer.h"
#include "Render/CommandBuffer.h"
#include "Render/RenderGraph/RenderGraph.h"
#include "Framework/Common/ResourceMgr.h"

namespace Ailu
{
    namespace Render
    {
        #pragma region VolumetricFogPass
        VolumetricFogPass::VolumetricFogPass()
        {
            _event = ERenderPassEvent::kAfterDeferedLighting;
            _debug_material = MakeRef<Material>(ResourceMgr::Get().Load<Shader>(L"Shaders/hlsl/voxel_drawer.alasset").get(), "VolumeRayDebugLineMat");
            _debug_material->SetVector("_GridNum", Vector4Int(8,8,8,0));
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
            graph.AddPass("VolumetricFog_LightInject", RDG::PassDesc(RDG::EPassType::kCompute), [&, this](RDG::RenderGraphBuilder &builder)
            {
                builder.Read(rendering_data._rg_handles._main_light_shadow_map);
                _history_light_handle = builder.Import(_history_light_texture);
                builder.Read(_history_light_handle);
                _inject_handle = builder.Import(_cur_light_texture);
                _inject_handle = builder.Write(_inject_handle,EResourceUsage::kWriteUAV);
            }, 
            [this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
            {
                auto *cur_light = graph.Resolve<Texture3D>(_inject_handle);
                auto *history_light = graph.Resolve<Texture3D>(_history_light_handle);
                if (cur_light == nullptr || history_light == nullptr)
                    return;
                const Camera* cam = Camera::sSelected? Camera::sSelected : data._camera;
                _volumetric_fog->SetMatrix("_matrix_iv",MatrixInverse(cam->GetView()));
                _volumetric_fog->SetMatrix("_matrix_ip",MatrixInverse(cam->GetProjNoJitter()));
                _volumetric_fog->SetMatrix("_matrix_pre_v",_matrix_prev_v);
                _volumetric_fog->SetMatrix("_matrix_pre_p",_matrix_prev_p);
                _volumetric_fog->SetFloat("_cam_near", cam->Near());
                _volumetric_fog->SetFloat("_cam_far", cam->Far());
                _volumetric_fog->SetVector("_cam_pos", cam->Position());
                //_volumetric_fog->SetVector("_zmax_uv_scale", maxz_sample_uv_scale);
                _volumetric_fog->SetTexture("_VolumetricLight", cur_light);
                _volumetric_fog->SetTexture("_History_VolumetricLight", history_light);
                auto [x,y,z] = _volumetric_fog->CalculateDispatchNum(_light_injection_kernel, cur_light->Width(), cur_light->Height(), cur_light->Depth());
                cmd->Dispatch(_volumetric_fog, _light_injection_kernel, x, y, z);
                _matrix_prev_p = cam->GetProjNoJitter();
                _matrix_prev_v = cam->GetView();
            });
            graph.AddPass("VolumetricFog_LightIntegration", RDG::PassDesc(RDG::EPassType::kCompute), [&, this](RDG::RenderGraphBuilder &builder)
            {
                builder.Read(_inject_handle);
                //builder.Read(s_max_z_handle);
                _accum_handle = builder.Import(_accum_texture);
                _accum_handle = builder.Write(_accum_handle, EResourceUsage::kWriteUAV);
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
                _volumetric_fog->SetTexture("_VolumetricLight", cur_light);
                _volumetric_fog->SetTexture("_FogAccum", accum_tex);
                //_volumetric_fog->SetTexture("_MaxZ_Texture", graph.Resolve<Texture>(s_max_z_handle));
                auto [x,y,z] = _volumetric_fog->CalculateDispatchNum(_light_integration_kernel, cur_light->Width(), cur_light->Height(), cur_light->Depth());
                cmd->Dispatch(_volumetric_fog, _light_integration_kernel, x, y, 1);
                Shader::SetGlobalTexture("_VolumetricLightTexture", accum_tex);
            });
            if (_debug_voxel_pos)
            {
                graph.AddPass("Debug Ray", RDG::PassDesc(), [&, this](RDG::RenderGraphBuilder &builder)
                            {
                    builder.Read(rendering_data._rg_handles._color_target);
                    builder.Read(rendering_data._rg_handles._depth_target);
                    rendering_data._rg_handles._color_target = builder.Write(rendering_data._rg_handles._color_target);
                    rendering_data._rg_handles._depth_target = builder.Write(rendering_data._rg_handles._depth_target);
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
        
        #pragma region VolumetricFog
        VolumetricFog::VolumetricFog() : RenderFeature("VolumetricFog")
        {
            _volumetric_fog_pass = MakeScope<VolumetricFogPass>();
            TextureDesc desc;
            desc._width = _voxel_num.x;
            desc._height = _voxel_num.y;
            desc._depth = _voxel_num.z;
            desc._format = EALGFormat::kALGFormatR16G16B16A16_FLOAT;
            desc._is_random_access = true;
            desc._mip_num = 0;
            _volumetric_light_a = Texture3D::Create(desc);
            _volumetric_light_a->Name("VolumetricLightTextureA");
            _volumetric_light_a->Apply();
            _volumetric_light_a->CreateView();
            _volumetric_light_b = Texture3D::Create(desc);
            _volumetric_light_b->Name("VolumetricLightTextureB");
            _volumetric_light_b->Apply();
            _volumetric_light_b->CreateView();
            _volumetric_fog_cs = ResourceMgr::Get().Load<ComputeShader>(L"Shaders/hlsl/Compute/volumetric_light.alasset");
            _max_z_cs = ResourceMgr::Get().Load<ComputeShader>(L"Shaders/hlsl/Compute/max_z.alasset");
            _accum_texture = Texture3D::Create(desc);
            _accum_texture->Name("VolumetricFogAccumTexture");
            _accum_texture->Apply();
            _accum_texture->CreateView();
            _volumetric_fog_cs->SetFloat("_FogDensity", _fog_density);
            _volumetric_fog_cs->SetFloat("_temporal_blend_factor", _blend_factor);
            _volumetric_fog_cs->SetBool("_temporal_reprojection", _temporal_reprojection);
            _volumetric_fog_cs->SetFloat("_intensity", _intensity);
            _volumetric_fog_cs->SetFloat("_g", _g);
            _volumetric_fog_cs->SetTexture("_BlueNoise", ResourceMgr::Get().Get<Texture2D>(EnginePath::kEngineTexturePathW + L"blue_noise.alasset"));
            _volumetric_fog_pass->_volumetric_fog = _volumetric_fog_cs.get();
            _volumetric_fog_pass->_max_z_cs = _max_z_cs.get();
            _volumetric_fog_pass->_light_injection_kernel = _volumetric_fog_cs->FindKernel("LightInjection");
            _volumetric_fog_pass->_light_integration_kernel = _volumetric_fog_cs->FindKernel("LightIntegration");
            _volumetric_fog_pass->_voxel_num = _voxel_num;
        }
        
        VolumetricFog::~VolumetricFog()
        {

        }
        void VolumetricFog::AddRenderPasses(Renderer &renderer, const RenderingData &rendering_data)
        {
            _volumetric_fog_pass->_debug_voxel_pos = _debug_voxel_pos;
            _volumetric_fog_pass->_cur_light_texture = _is_cur_a? _volumetric_light_a.get() : _volumetric_light_b.get();
            _volumetric_fog_pass->_history_light_texture = _is_cur_a? _volumetric_light_b.get() : _volumetric_light_a.get();
            _volumetric_fog_pass->_accum_texture = _accum_texture.get();
            _is_cur_a = !_is_cur_a;
            renderer.EnqueuePass(_volumetric_fog_pass.get());
        }
        void VolumetricFog::OnPropertyChanged(const PropertyInfo &prop)
        {
            if(prop.Name() == "_fog_density")
            {
                _volumetric_fog_cs->SetFloat("_FogDensity", _fog_density);
            }
            else if(prop.Name() == "_blend_factor")
            {
                _volumetric_fog_cs->SetFloat("_temporal_blend_factor", _blend_factor);
            }
            else if (prop.Name() == "_temporal_reprojection")
            {
                _volumetric_fog_cs->SetBool("_temporal_reprojection", _temporal_reprojection);
            }
            else if (prop.Name() == "_g")
            {
                _volumetric_fog_cs->SetFloat("_g", _g);
            }
            else if (prop.Name() == "_intensity")
            {
                _volumetric_fog_cs->SetFloat("_intensity", _intensity);
            }
        }
        #pragma endregion
    }// namespace Render
}// namespace Ailu
