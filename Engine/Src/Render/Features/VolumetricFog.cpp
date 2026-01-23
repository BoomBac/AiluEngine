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
        }
        VolumetricFogPass::~VolumetricFogPass()
        {
        }
        void VolumetricFogPass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
        {
            graph.AddPass("VolumetricFog_LightInject", RDG::PassDesc(RDG::EPassType::kCompute), [&, this](RDG::RenderGraphBuilder &builder)
            {
                builder.Read(rendering_data._rg_handles._main_light_shadow_map);
            }, 
            [this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
            {
                const Camera* cam = Camera::sSelected? Camera::sSelected : data._camera;
                _volumetric_fog->SetMatrix("_matrix_iv",MatrixInverse(cam->GetView()));
                _volumetric_fog->SetMatrix("_matrix_p",cam->GetProjNoJitter());
                _volumetric_fog->SetFloat("_cam_near", cam->Near());
                _volumetric_fog->SetFloat("_cam_far", cam->Far());
                auto kernel = _volumetric_fog->FindKernel("light_injection");
                _volumetric_fog->SetTexture("_FogTexture", _fog_texture);
                //_volumetric_fog->SetTexture("_MainLightShadowMap", graph.Resolve<Texture>(data._rg_handles._main_light_shadow_map));
                auto [x,y,z] = _volumetric_fog->CalculateDispatchNum(kernel, 64u, 64u, 64u);
                cmd->Dispatch(_volumetric_fog, kernel, x, y, z);
                Shader::SetGlobalTexture("_VolumetricLightTexture", _fog_texture);
            });
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
            desc._width = 64;
            desc._height = 64;
            desc._depth = 64;
            desc._format = EALGFormat::kALGFormatR16G16B16A16_FLOAT;
            desc._is_random_access = true;
            _fog_texture = Texture3D::Create(desc);
            _fog_texture->Name("VolumetricFogTexture");
            _fog_texture->Apply();
            _fog_texture->CreateView();
            _volumetric_fog_cs = g_pResourceMgr->Load<ComputeShader>(L"Shaders/volumetric_light.alasset");
            _volumetric_fog_pass->_fog_texture = _fog_texture.get();
            _volumetric_fog_pass->_volumetric_fog = _volumetric_fog_cs.get();
        }
        VolumetricFog::~VolumetricFog()
        {

        }
        void VolumetricFog::AddRenderPasses(Renderer &renderer, const RenderingData &rendering_data)
        {
            renderer.EnqueuePass(_volumetric_fog_pass.get());
        }
        #pragma endregion
    }// namespace Render
}// namespace Ailu
