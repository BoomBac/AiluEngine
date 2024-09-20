#include "Render/Pass/TAAPass.h"
#include "Render/CommandBuffer.h"
#include "pch.h"

#include "Framework/Common/Application.h"
#include "Framework/Common/Profiler.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/Camera.h"
#include "Render/Renderer.h"

namespace Ailu
{
    TAAFeature::TAAFeature() : RenderFeature("TAAFeature")
    {
        _prepare_pass._event = ERenderPassEvent::kBeforeGbuffer;
        _execute_pass._event = ERenderPassEvent::kBeforePostprocess;
        _taa = MakeRef<Material>(g_pResourceMgr->Get<Shader>(L"Shaders/hlsl/taa.hlsl"), "Runtime/TAA");
    };

    TAAFeature::~TAAFeature()
    {
    }
    void TAAFeature::AddRenderPasses(Renderer *renderer, RenderingData &rendering_data)
    {
        static bool first_time = false;
        auto camera = *rendering_data._camera;
        auto hash = camera.HashCode();
        auto proj = camera.GetProjection();
        auto view = camera.GetView();
        //auto viewProj = proj * view;
        auto viewProj = view * proj;
        HaltonSequence haltonSequence;
        if (!_halton_sequence.contains(hash))
        {
            haltonSequence = HaltonSequence(1024);
        }
        else
        {
            haltonSequence = _halton_sequence[hash];
        }
        f32 offsetX, offsetY;
        haltonSequence.Get(offsetX, offsetY);
        auto matrix = proj;
        auto descriptor = rendering_data._width;
        Vector2f jitter = Vector2f::kZero;
        if (camera.Type() == ECameraType::kOrthographic)
        {
            matrix[0][3] -= (offsetX * 2 - 1) / rendering_data._width;
            matrix[1][3] -= (offsetY * 2 - 1) / rendering_data._height;
        }
        else
        {
            jitter.x = (offsetX * 2 - 1) / rendering_data._width;
            jitter.y = (offsetY * 2 - 1) / rendering_data._height;
            matrix[2][0] += jitter.x;
            matrix[2][1] += jitter.y;
        }
        //viewProj = matrix * view;
        _prepare_pass.Setup(matrix);
        renderer->EnqueuePass(&_prepare_pass);
        jitter.x = (offsetX - 0.5f) / rendering_data._width;
        jitter.y = (offsetY - 0.5f) / rendering_data._height;
        _execute_pass.Setup(haltonSequence.prevViewProj, viewProj, _taa.get(), hash, jitter);
        renderer->EnqueuePass(&_execute_pass);

        haltonSequence.prevViewProj = viewProj;
        haltonSequence.frameCount = Application::s_frame_count;
        _halton_sequence[hash] = haltonSequence;
    }
    TAAPreparePass::TAAPreparePass() : RenderPass("TAAPreparePass")
    {
    }


    void TAAPreparePass::Setup(Matrix4x4f jitter)
    {
        _jitter_matrix = jitter;
    }
    TAAPreparePass::~TAAPreparePass()
    {
    }
    void TAAPreparePass::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        auto cmd = CommandBufferPool::Get("TAAPreparePass");
        cmd->SetViewProjectionMatrix(rendering_data._camera->GetView(), _jitter_matrix);
        CommandBufferPool::Release(cmd);
    }
    void TAAPreparePass::BeginPass(GraphicsContext *context)
    {
    }
    void TAAPreparePass::EndPass(GraphicsContext *context)
    {
    }


    TAAExecutePass::TAAExecutePass() : RenderPass("TAAExecutePass")
    {
    }
    void Ailu::TAAExecutePass::Setup(Matrix4x4f pre_matrix, Matrix4x4f cur_matrix, Material *taa_mat, int camera_hash, Vector2f jitter)
    {
        if (_infos.contains(camera_hash))
            _infos[camera_hash]._pre_matrix = pre_matrix;
        else
        {
            TAAInfo info{pre_matrix, true, nullptr};
            _infos[camera_hash] = info;
        }
        _taa_material = taa_mat;
        _cur_camera_hash = camera_hash;
        _cur_vp_matrix = cur_matrix;
        _jitter = jitter;
    }

    TAAExecutePass::~TAAExecutePass()
    {
    }
    void TAAExecutePass::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        auto cmd = CommandBufferPool::Get("Temporal AA");
        auto &cur_info = _infos[_cur_camera_hash];
        auto &camera_data = rendering_data._camera_data;
        {
            ProfileBlock b(cmd.get(), cmd->GetName());
            if (cur_info._first_tick || cur_info._pre_camera_color->Width() != camera_data._camera_color_target_desc._width || cur_info._pre_camera_color->Height() != camera_data._camera_color_target_desc._height)
            {
                cur_info._pre_camera_color = RenderTexture::Create(camera_data._camera_color_target_desc, "_HistoryColor");
                cmd->Blit(rendering_data._camera_color_target_handle, cur_info._pre_camera_color.get());
                cur_info._first_tick = false;
            }
            if (_taa_material)
            {
                _taa_material->SetMatrix("_MatrixVP_CurFrame", _cur_vp_matrix);
                _taa_material->SetMatrix("_MatrixVP_PreFrame", cur_info._pre_matrix);
                _taa_material->SetVector("_TAAParams", Vector4f(_jitter.x, _jitter.y, 0.05f, 0.0f));

                _taa_material->SetVector("_CameraDepthTexture_TexelSize", Vector4f(1.0f / rendering_data._width, 1.0f / rendering_data._height, (f32) rendering_data._width, (f32) rendering_data._height));
                _taa_material->SetVector("_CurFrameColor_TexelSize", Vector4f(1.0f / rendering_data._width, 1.0f / rendering_data._height, (f32) rendering_data._width, (f32) rendering_data._height));
                cmd->SetGlobalTexture("_HistoryFrameColor", cur_info._pre_camera_color.get());
                cmd->SetGlobalTexture("_CurFrameColor", rendering_data._camera_opaque_tex_handle);
                cmd->SetGlobalTexture("_CameraDepthTexture", rendering_data._camera_depth_tex_handle);
                cmd->SetRenderTarget(rendering_data._camera_color_target_handle);
                cmd->DrawFullScreenQuad(_taa_material);
                cmd->Blit(rendering_data._camera_color_target_handle, cur_info._pre_camera_color.get());
                cmd->Blit(rendering_data._camera_color_target_handle, rendering_data._camera_opaque_tex_handle);
                cmd->SetViewProjectionMatrix(rendering_data._camera->GetView(), rendering_data._camera->GetProjection());
            }
        }

        context->ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
    }
    void TAAExecutePass::BeginPass(GraphicsContext *context)
    {
    }
    void TAAExecutePass::EndPass(GraphicsContext *context)
    {
        
    }
}// namespace Ailu
