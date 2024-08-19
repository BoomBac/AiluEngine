#include "Objects/LightProbeComponent.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/CommandBuffer.h"
#include "Render/Gizmo.h"
#include "Render/GraphicsContext.h"
#include "Render/RenderPipeline.h"
#include "pch.h"

#include "Render/Pass/RenderPass.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "Ext/stb/stb_image_write.h"

namespace Ailu
{
    static Scope<RenderTexture> s_default_cubemap = nullptr;
    Ailu::LightProbeComponent::LightProbeComponent()
    {
        static bool init = false;
        if (!init)
        {
            s_default_cubemap = std::move(RenderTexture::Create(4, "DefaultCubemap", ERenderTargetFormat::kDefault, false, false, false));
        }
        _cubemap = RenderTexture::Create(512, "light probe", ERenderTargetFormat::kRGBAHalf, true, true, true);
        _cubemap->CreateView();
        _pass = MakeScope<CubeMapGenPass>(_cubemap.get());
        //_pass = MakeScope<CubeMapGenPass>(g_pResourceMgr->Get<Texture2D>(L"Textures/small_cave_1k.alasset"));
        _obj_cbuf = std::unique_ptr<IConstantBuffer>(IConstantBuffer::Create(256));
        _debug_mat = MakeRef<Material>(g_pResourceMgr->Get<Shader>(L"Shaders/hlsl/cubemap_debug.hlsl"), "cube_map_debug");
        _debug_mat->SetTexture("_SrcCubeMap", s_default_cubemap.get());
    }
    void LightProbeComponent::Tick(const float &delta_time)
    {
        if (_update_every_tick)
        {
            auto &transf = dynamic_cast<SceneActor *>(_p_onwer)->GetTransform();
            Camera cam;
            cam.Position(transf._position);
            cam.Near(0.01f);
            cam.Far(_size);
            cam._layer_mask = 0;
            cam._layer_mask |= ERenderLayer::kDefault;
            cam.TargetTexture(_cubemap.get());

            g_pGfxContext->GetPipeline()->GetRenderer()->SubmitTaskPass(_pass.get());
            g_pGfxContext->GetPipeline()->GetRenderer()->Render(cam, *g_pSceneMgr->_p_current);
            //_debug_mat->SetTexture("_SrcCubeMap",_pass->_prefilter_cubemap.get());
            _debug_mat->SetTexture("_SrcCubeMap", _pass->_src_cubemap.get());
        }
        if (_src_type == 0)
        {
            _debug_mat->DisableKeyword("DEBUG_REFLECT");
            //_debug_mat->SetTexture("_SrcCubeMap", _pass->_src_cubemap.get());
            _debug_mat->SetTexture("_SrcCubeMap", _cubemap.get());
        }
        else if (_src_type == 1)
        {
            _debug_mat->DisableKeyword("DEBUG_REFLECT");
            _debug_mat->SetTexture("_SrcCubeMap", _pass->_radiance_map.get());
        }
        else if (_src_type == 2)
        {
            _debug_mat->EnableKeyword("DEBUG_REFLECT");
            _debug_mat->SetTexture("_SrcCubeMap", _pass->_prefilter_cubemap.get());
        }
        else {}
        _debug_mat->SetFloat("_mipmap", _mipmap);
        Shader::SetGlobalTexture("SkyBox", _cubemap.get());
        Shader::SetGlobalTexture("RadianceTex", _pass->_radiance_map.get());
        Shader::SetGlobalTexture("PrefilterEnvTex", _pass->_prefilter_cubemap.get());
    }

    void LightProbeComponent::Serialize(std::basic_ostream<char, std::char_traits<char>> &os, String indent)
    {
    }

    void *LightProbeComponent::DeserializeImpl(Queue<std::tuple<String, String>> &formated_str)
    {
        return nullptr;
    }
    void LightProbeComponent::OnGizmo()
    {
        Component::OnGizmo();
        Gizmo::DrawCube(dynamic_cast<SceneActor *>(_p_onwer)->GetTransform()._position, _size);
    }

    void LightProbeComponent::UpdateProbe()
    {
        auto &transf = dynamic_cast<SceneActor *>(_p_onwer)->GetTransform();
        Camera cam;
        cam.Position(transf._position);
        cam.Near(0.01f);
        cam.Far(_size);
        cam._layer_mask = 0;
        cam._layer_mask |= ERenderLayer::kDefault;
        cam.TargetTexture(_cubemap.get());

        g_pGfxContext->GetPipeline()->GetRenderer()->SubmitTaskPass(_pass.get());
        g_pGfxContext->GetPipeline()->GetRenderer()->Render(cam, *g_pSceneMgr->_p_current);
        RenderTexture *out_tex = _pass->_prefilter_cubemap.get();
        for (u16 j = 1; j <= 6; j++)
        {
            for (u16 i = 0; i < 1; i++)
            {
                auto face = (ECubemapFace::ECubemapFace) j;
                const f32 *data = (f32 *) out_tex->ReadBack(i, 0, face);
                String asset_path = std::format("ScreenGrab/{}_{}_{}.exr", out_tex->Name(), ECubemapFace::ToString(face), i);
                auto [w, h] = out_tex->CurMipmapSize(i);
                stbi_write_hdr(ToChar(ResourceMgr::GetResSysPath(ToWChar(asset_path))).c_str(), w, h, 4, data);
                delete[] data;
            }
        }
    }
}// namespace Ailu
