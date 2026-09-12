#include "RHI/DX12/DXRSample.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/TimeMgr.h"
#include "RHI/DX12/D3DBuffer.h"
#include "RHI/DX12/D3DCommandBuffer.h"
#include "RHI/DX12/D3DShaderCompiler.h"
#include "RHI/DX12/dxhelper.h"
#include "RHI/DX12/dxrhelper.h"
#include "Render/Camera.h"
#include "Render/GraphicsContext.h"
#include "Render/RayTracing/SceneRayTracingProxy.h"
#include "Render/Shader.h"
#include "Render/ShaderInterop.h"
#include "Render/Texture.h"
#include "Scene/Scene.h"
#include "pch.h"

using namespace Ailu::Render;
namespace Ailu::RHI::DX12
{
    Ref<ConstantBuffer> g_rayGenData;
    Ref<ConstantBuffer> g_perSceneData;
    Ref<ConstantBuffer> g_perCamData;
    CBufferPerSceneData g_sceneData;
    CBufferPerCameraData g_camData;

    DXRSample::DXRSample(ID3D12Device5 *device, ID3D12CommandQueue *cmd_queue) : m_dxrDevice(device), m_commandQueue(cmd_queue)
    {
    }

    DXRSample::~DXRSample()
    {
    }

    void DXRSample::Init(u16 w, u16 h)
    {
        g_rayGenData = ConstantBuffer::Create(sizeof(RayGenConstantBuffer), "RayGenData");
        g_perSceneData = ConstantBuffer::Create(sizeof(CBufferPerSceneData), "PerSceneData");
        g_perCamData = ConstantBuffer::Create(sizeof(CBufferPerCameraData), "PerCamData");

        m_rayGenCB.viewport = {-1.0f, -1.0f, 1.0f, 1.0f};
        f32 border = 0.1f;
        f32 m_aspectRatio = static_cast<f32>(w) / static_cast<f32>(h);
        m_rayGenCB.stencil =
                {
                        -1 + border, -1 + border * m_aspectRatio,
                        1.0f - border, 1 - border * m_aspectRatio};
        m_raytracing_shader = RayTracingShader::Create(ResourceMgr::GetResSysPath(L"Shaders/hlsl/DXR/Raytracing.hlsl"), "DXR_Sample_RayTracingShader");
        if (m_raytracing_shader == nullptr)
        {
            LOG_ERROR("DXRSample: failed to create raytracing shader.");
            return;
        }
        m_sceneProxy = MakeScope<SceneRayTracingProxy>();
    }

    void DXRSample::MakesureOutput(u16 w, u16 h)
    {
        if (m_uav_output == nullptr || m_uav_output->Width() != w || m_uav_output->Height() != h)
        {
            if (m_uav_output)
                m_uav_output.reset();
            TextureDesc tex_desc;
            tex_desc._width = w;
            tex_desc._height = h;
            tex_desc._format = EALGFormat::kALGFormatR16G16B16A16_FLOAT;
            tex_desc._is_random_access = true;
            tex_desc._mip_num = 0;
            m_uav_output = Texture2D::Create(tex_desc);
            m_uav_output->ApplySync();
        }
    }

    static u64 s_frame = 0u;
    void DXRSample::Render(RHICommandBuffer *cmd, u16 w, u16 h)
    {
        if (s_frame < 5u)
        {
            s_frame++;
            return;
        }
        if (s_frame == 5u)
        {
            LOG_INFO("Start init DXR Sample");
            Init(w, h);
        }
        s_frame++;
        auto *active_scene = SceneManagement::SceneMgr::Get().ActiveScene();
        if (m_raytracing_shader == nullptr || m_sceneProxy == nullptr || active_scene == nullptr)
            return;
        m_sceneProxy->Sync(active_scene);

        auto *raytracing_scene = m_sceneProxy->GetScene();
        if (raytracing_scene == nullptr || !m_sceneProxy->HasRenderableScene() || !raytracing_scene->IsReady())
            return;

        MakesureOutput(w, h);
        auto cam = *Camera::sCurrent;
        f32 f = cam.Far(), n = cam.Near();
        u32 pixel_width = cam.OutputSize().x, pixel_height = cam.OutputSize().y;
        g_camData._ScreenParams = Vector4f(1.0f / (f32) pixel_width, 1.0f / (f32) pixel_height, (f32) pixel_width, (f32) pixel_height);
        Camera::CalculateZBUfferAndProjParams(cam, g_camData._ZBufferParams, g_camData._ProjectionParams);
        g_camData._CameraPos = cam.Position();
        g_camData._MatrixV = cam.GetView();
        g_camData._MatrixP = cam.GetProj();
        g_camData._MatrixVP = cam.GetViewProj();
        g_camData._MatrixVP_NoJitter = cam.GetViewProj();
        g_camData._MatrixIVP = MatrixInverse(g_camData._MatrixVP);

        g_rayGenData->SetData(reinterpret_cast<u8 *>(&m_rayGenCB), sizeof(RayGenConstantBuffer));
        g_perSceneData->SetData(reinterpret_cast<u8 *>(&g_sceneData), sizeof(CBufferPerSceneData));
        g_perCamData->SetData(reinterpret_cast<u8 *>(&g_camData), sizeof(CBufferPerCameraData));
        m_raytracing_shader->SetScene(raytracing_scene);
        m_raytracing_shader->SetBuffer("VertexBuffer", m_sceneProxy->GetVertexData());
        m_raytracing_shader->SetBuffer("NormalBuffer", m_sceneProxy->GetNormalData());
        m_raytracing_shader->SetBuffer("IndexBuffer", m_sceneProxy->GetIndexData());
        m_raytracing_shader->SetBuffer("InstanceGeometryBuffer", m_sceneProxy->GetPrimitiveData());
        m_raytracing_shader->SetBuffer("g_rayGenCB", g_rayGenData.get());
        m_raytracing_shader->SetBuffer("g_perSceneData", g_perSceneData.get());
        m_raytracing_shader->SetBuffer("g_perCamData", g_perCamData.get());
        m_raytracing_shader->SetTexture("RenderTarget", m_uav_output.get());
        m_raytracing_shader->PushState(raytracing_scene);
        m_raytracing_shader->DispatchRays(cmd, w, h, 1u);
    }
}// namespace Ailu::RHI::DX12
