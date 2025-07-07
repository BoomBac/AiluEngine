#include "Render/Features/TemporalAA.h"
#include "Render/CommandBuffer.h"
#include "pch.h"

#include "Framework/Common/Application.h"
#include "Framework/Common/Profiler.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/Camera.h"
#include "Render/Renderer.h"

namespace Ailu::Render
{
    TemporalAA::TemporalAA() : RenderFeature("TemporalAA")
    {
        _prepare_pass._event = ERenderPassEvent::kBeforeGbuffer;
        _execute_pass._event = ERenderPassEvent::kBeforePostprocess;
        _taa = MakeRef<Material>(g_pResourceMgr->Get<Shader>(L"Shaders/hlsl/taa.hlsl"), "Runtime/TAA");
    };

    TemporalAA::~TemporalAA()
    {
    }
    void Ailu::Render::TemporalAA::AddRenderPasses(Renderer &renderer, const RenderingData & rendering_data)
    {
        static bool first_time = false;
        const Camera& camera = *rendering_data._camera;
        auto hash = camera.HashCode();
        const auto& proj = camera.GetProj();
        const auto& view = camera.GetView();
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
            matrix[2][0] += jitter.x * _jitter_scale;
            matrix[2][1] += jitter.y * _jitter_scale;
        }
        //viewProj = matrix * view;
        _prepare_pass.Setup(matrix);
        //renderer.EnqueuePass(&_prepare_pass);
        jitter.x = (offsetX - 0.5f) / rendering_data._width;
        jitter.y = (offsetY - 0.5f) / rendering_data._height;
        jitter *= _jitter_scale;
        Vector4f quality = {_clamp_quality, _history_quality, _motion_quality, 0.0f};
        Vector4f params = Vector4f::kZero;
        params.x = _variance_clip_scale;
        params.y = _sharpness;
        params.z = _history_factor;
        _execute_pass.Setup(haltonSequence.prevViewProj, viewProj, _taa.get(), (i32)hash,jitter,params,quality);
        renderer.EnqueuePass(&_execute_pass);

        haltonSequence.prevViewProj = viewProj;
        haltonSequence.frameCount = Application::Application::Get().GetFrameCount();
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
//        auto cmd = CommandBufferPool::Get("TAAPreparePass");
//        cmd->SetViewProjectionMatrix(rendering_data._camera->GetView(), _jitter_matrix);
//        CommandBufferPool::Release(cmd);
    }
    void TAAPreparePass::BeginPass(GraphicsContext *context)
    {
    }
    void TAAPreparePass::EndPass(GraphicsContext *context)
    {
    }


    TAAExecutePass::TAAExecutePass() : RenderPass("TAAExecutePass")
    {
        _origin_camera_cbuf = std::unique_ptr<ConstantBuffer>(ConstantBuffer::Create(RenderConstants::kPerCameraDataSize));
        _taa_gen = g_pResourceMgr->GetRef<ComputeShader>(L"Shaders/taa.alasset");
    }
    void TAAExecutePass::Setup(Matrix4x4f pre_matrix, Matrix4x4f cur_matrix, Material *taa_mat, int camera_hash, Vector2f jitter,Vector4f params,Vector4f quality)
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
        _jitter.xy = _jitter.zw;
        _jitter.zw = jitter;
        _params = params;
        _quality = quality;
    }

    TAAExecutePass::~TAAExecutePass()
    {
    }
    void TAAExecutePass::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        auto cmd = CommandBufferPool::Get("Temporal AA");
        auto &cur_info = _infos[_cur_camera_hash];
        auto &camera_data = rendering_data._camera_data;
        CBufferPerCameraData camera_cb;
        camera_cb._MatrixV = rendering_data._camera->GetView();
        camera_cb._MatrixP = rendering_data._camera->GetProj();
        camera_cb._MatrixVP = camera_cb._MatrixV * camera_cb._MatrixP;
        camera_cb._MatrixIVP = MatrixInverse(camera_cb._MatrixVP);
        camera_cb._CameraPos = rendering_data._camera->Position();
        memcpy(_origin_camera_cbuf->GetData(), &camera_cb, RenderConstants::kPerCameraDataSize);
        rendering_data._camera->GetProj();
        bool is_compute = true;
        {
            PROFILE_BLOCK_GPU(cmd.get(),TemporaAA)
            //GpuProfileBlock b(cmd.get(), cmd->Name());
            if (cur_info._first_tick || cur_info._target_a->Width() != camera_data._camera_color_target_desc._width || cur_info._target_a->Height() != camera_data._camera_color_target_desc._height)
            {
                if (cur_info._target_a)
                    cur_info._target_a->Release();
                if (cur_info._target_b)
                    cur_info._target_b->Release();
                auto desc = camera_data._camera_color_target_desc;
                desc._random_access = true;
                cur_info._target_a = RenderTexture::Create(desc, "_TemporalAA_TargetA");
                cur_info._target_b = RenderTexture::Create(desc, "_TemporalAA_TargetB");
                cmd->Blit(rendering_data._camera_color_target_handle, cur_info._is_cur_a? cur_info._target_a.get() : cur_info._target_b.get());
                cur_info._first_tick = false;
            }
            auto cur_target = cur_info._is_cur_a? cur_info._target_a.get() : cur_info._target_b.get();
            auto history_target = cur_info._is_cur_a? cur_info._target_b.get() : cur_info._target_a.get();
            if (is_compute)
            {
                _taa_gen->SetVector("_SourceTex_TexelSize",Vector4f(1.0f / rendering_data._width, 1.0f / rendering_data._height,(f32) rendering_data._width, (f32) rendering_data._height));
                //_taa_gen->SetVector("_Params",_params);
                _taa_gen->SetVector("_Jitter",_jitter);
                _taa_gen->SetTexture("_CurrentColor",rendering_data._camera_opaque_tex_handle);
                _taa_gen->SetTexture("_HistoryColor",history_target);
                _taa_gen->SetTexture("_CurrentTarget",cur_target);
                _taa_gen->SetFloat("_ClampQuality",_quality.x);
                _taa_gen->SetFloat("_HistoryQuality",_quality.y);
                _taa_gen->SetFloat("_MotionQuality",_quality.z);
                _taa_gen->SetFloat("_VarianceClampScale", _params.x);
                _taa_gen->SetFloat("_Sharpness", _params.y);
                _taa_gen->SetFloat("_HistoryFactor", _params.z);
                auto kernel = _taa_gen->FindKernel("CSMain");
                auto [x,y,z] = _taa_gen->CalculateDispatchNum(kernel,rendering_data._width,rendering_data._height,1);
                cmd->Dispatch(_taa_gen.get(),kernel,x,y);
                //cmd->Blit(cur_target, rendering_data._camera_color_target_handle);
                rendering_data._postprocess_input = cur_target;
            }
            else
            {
                _taa_material->SetMatrix("_MatrixVP_CurFrame", _cur_vp_matrix);
                _taa_material->SetMatrix("_MatrixVP_PreFrame", cur_info._pre_matrix);
                _taa_material->SetVector("_TAAParams", _params);
            
                _taa_material->SetVector("_CameraDepthTexture_TexelSize", cur_target->TexelSize());
                _taa_material->SetVector("_CurFrameColor_TexelSize", cur_target->TexelSize());
                cmd->SetGlobalTexture("_HistoryFrameColor", history_target);
                cmd->SetGlobalTexture("_CurFrameColor", rendering_data._camera_opaque_tex_handle);
                cmd->SetGlobalTexture("_CameraDepthTexture", rendering_data._camera_depth_tex_handle);
                cmd->SetRenderTarget(rendering_data._camera_color_target_handle);
                cmd->DrawFullScreenQuad(_taa_material);
                cmd->Blit(rendering_data._camera_color_target_handle, history_target);
                cmd->Blit(rendering_data._camera_color_target_handle, rendering_data._camera_opaque_tex_handle);
                //cmd->SetViewProjectionMatrix(rendering_data._camera->GetView(), rendering_data._camera->GetProj());
            }
            //cmd->Blit(cur_target, rendering_data._camera_color_target_handle);
            //rendering_data._postprocess_input = cur_target;
        }

        context->ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
    }
    void TAAExecutePass::BeginPass(GraphicsContext *context)
    {
    }
    void TAAExecutePass::EndPass(GraphicsContext *context)
    {
        auto &cur_info = _infos[_cur_camera_hash];
        cur_info._is_cur_a = !cur_info._is_cur_a;
    }
}// namespace Ailu
