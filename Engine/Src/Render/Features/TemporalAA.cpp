#include "Render/Features/TemporalAA.h"
#include "Render/CommandBuffer.h"
#include "Render/GraphicsContext.h"
#include "pch.h"

#include "Framework/Common/Profiler.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/Camera.h"
#include "Render/Renderer.h"
#include "Render/RenderGraph/RenderGraph.h"

namespace Ailu::Render
{
    namespace
    {
        constexpr f32 kCameraCutDistance = 1.0f;
        constexpr f32 kCameraCutOrientationDot = 0.5f;
        constexpr f32 kProjectionCutFovDegrees = 1.0f;
        constexpr f32 kProjectionCutRelativeRange = 0.1f;

    }

    TemporalAA::TemporalAA() : RenderFeature("TemporalAA")
    {
        _execute_pass._event = ERenderPassEvent::kBeforePostprocess;
    }

    void TemporalAA::AddRenderPasses(Renderer &renderer, const RenderingData &rendering_data)
    {
        if (rendering_data._camera == nullptr)
            return;

        const Camera &camera = *rendering_data._camera;
        if (camera.IsTemporalUpscalingActive())
        {
            _execute_pass.InvalidateHistory(camera.HashCode());
            return;
        }
        const u64 camera_hash = camera.HashCode();
        if (camera._anti_aliasing != EAntiAliasing::kTAA)
        {
            _execute_pass.InvalidateHistory(camera_hash);
            return;
        }

        const Vector4f &camera_jitter = camera.GetJitter();
        const Vector2f current_uv_jitter(camera_jitter.z, camera_jitter.w);
        const Vector4f quality{_clamp_quality, _history_quality, _motion_quality, 0.0f};
        const Vector4f params{_variance_clip_scale, _sharpness, _history_factor, 0.0f};
        const u64 frame_index = g_pGfxContext != nullptr ? g_pGfxContext->GetFrameCount() : 0u;
        _execute_pass.Setup(camera, frame_index, rendering_data._width, rendering_data._height, current_uv_jitter,
                            rendering_data._camera_data._camera_color_target_desc, params, quality);
        if (renderer._is_use_render_graph)
            renderer.EnqueuePass(&_prepare_pass);
        renderer.EnqueuePass(&_execute_pass);
    }

    TAAReactivePreparePass::TAAReactivePreparePass() : RenderPass("TAAOpaqueSnapshot")
    {
        // Include Cloud composition at455, while keeping the snapshot before Sprite460 and transparency475.
        _event = static_cast<ERenderPassEvent>(static_cast<u16>(ERenderPassEvent::kAfterMotionVector) + 1u);
    }

    void TAAReactivePreparePass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
    {
        const auto source = rendering_data._rg_handles._color_target;
        if (!source.IsValid())
            return;
        TextureDesc desc = rendering_data._camera_data._camera_color_target_desc;
        desc._width = static_cast<u16>(rendering_data._width);
        desc._height = static_cast<u16>(rendering_data._height);
        const auto snapshot = graph.CreateResource(desc, "_TAAOpaqueSnapshot");
        auto output = std::make_shared<RDG::RGHandle>();
        graph.AddPass("TAAOpaqueSnapshot", {}, [source, snapshot, output](RDG::RenderGraphBuilder &builder)
        {
            builder.Read(source);
            *output = builder.Write(snapshot);
        }, [source, output](RDG::RenderGraph &pass_graph, CommandBuffer *cmd, const RenderingData &)
        {
            cmd->Blit(source, *output);
        });
        rendering_data._rg_handles._taa_opaque_color = *output;
    }

    TAAExecutePass::TAAExecutePass() : RenderPass("TAAExecutePass")
    {
        _taa_gen = ResourceMgr::Get().LoadAssetRef<ComputeShader>(L"Shaders/hlsl/Compute/taa.alasset");
        if (_taa_gen)
        {
            _taa_kernel = _taa_gen->FindKernel("CSMain");
            _stabilize_kernel = _taa_gen->FindKernel("CSStabilize");
        }
    }

    void TAAExecutePass::InvalidateHistory(u64 camera_hash)
    {
        auto it = _infos.find(camera_hash);
        if (it != _infos.end())
            it->second._history_valid = false;
    }

    void TAAExecutePass::InvalidateAllHistory()
    {
        for (auto &[camera_hash, history] : _infos)
            history._history_valid = false;
    }

    void TAAExecutePass::Setup(const Camera &camera, u64 frame_index, u32 width, u32 height,
                               Vector2f current_uv_jitter, Vector4f params, Vector4f quality)
    {
        const TextureDesc color_desc(static_cast<u16>(width), static_cast<u16>(height), ERenderTargetFormat::kDefaultHDR);
        Setup(camera, frame_index, width, height, current_uv_jitter, color_desc, params, quality);
    }

    void TAAExecutePass::Setup(const Camera &camera, u64 frame_index, u32 width, u32 height,
                               Vector2f current_uv_jitter, const TextureDesc &color_desc,
                               Vector4f params, Vector4f quality)
    {
        AL_ASSERT_MSG(camera._anti_aliasing == EAntiAliasing::kTAA, "TAA history setup requires a TAA camera.");
        AL_ASSERT_MSG(std::isfinite(current_uv_jitter.x) && std::isfinite(current_uv_jitter.y),
                      "TAA camera jitter must be finite.");
        _cur_camera_hash = camera.HashCode();
        _current_camera = &camera;
        _current_frame_index = frame_index;
        _current_uv_jitter = current_uv_jitter;
        _params = params;
        _quality = quality;
        _resolve_executed = false;

        TAAHistoryState &history = _infos[_cur_camera_hash];
        const Vector3f current_position = camera.Position();
        const Vector3f current_forward = camera.Forward();
        const Vector3f current_up = camera.Up();
        const bool changed_camera_lifetime = history._has_rendered && history._camera_identity != &camera;
        const bool changed_dimensions = history._width != width || history._height != height;
        const bool skipped_frame = history._has_rendered && frame_index != history._last_render_frame + 1u;
        const bool explicit_reset = history._camera_reset_revision != camera.GetTemporalHistoryResetRevision();
        const f32 near_delta = std::abs(camera.Near() - history._previous_near);
        const f32 far_delta = std::abs(camera.Far() - history._previous_far);
        const bool projection_discontinuity =
            static_cast<u8>(camera.Type()) != history._projection_type ||
            std::abs(camera.FovH() - history._previous_fov) > kProjectionCutFovDegrees ||
            std::abs(camera.Aspect() - history._previous_aspect) > 1.0e-3f ||
            near_delta > std::max(history._previous_near, 1.0e-3f) * kProjectionCutRelativeRange ||
            far_delta > std::max(history._previous_far, 1.0e-3f) * kProjectionCutRelativeRange;
        const bool camera_cut = history._has_rendered &&
            (Math::Magnitude(current_position - history._previous_position) > kCameraCutDistance ||
             Math::DotProduct(current_forward, history._previous_forward) < kCameraCutOrientationDot ||
             Math::DotProduct(current_up, history._previous_up) < kCameraCutOrientationDot ||
             projection_discontinuity);

        if (changed_camera_lifetime || changed_dimensions || skipped_frame || camera_cut || explicit_reset)
            history._history_valid = false;

        const bool texture_desc_mismatch = history._target_a &&
            (history._target_a->Width() != width || history._target_a->Height() != height ||
             history._format != color_desc._format);
        if (!history._target_a || !history._target_b || texture_desc_mismatch)
        {
            history._target_a = nullptr;
            history._target_b = nullptr;
            history._reactive_a = nullptr;
            history._reactive_b = nullptr;
            history._stable_target = nullptr;
            if (width > 0u && height > 0u)
            {
                TextureDesc desc = color_desc;
                desc._width = static_cast<u16>(width);
                desc._height = static_cast<u16>(height);
                desc._is_random_access = true;
                history._target_a = RenderTexture::Create(desc, "_TemporalAA_TargetA");
                history._target_b = RenderTexture::Create(desc, "_TemporalAA_TargetB");
                history._format = desc._format;
                desc._format = EALGFormat::kALGFormatR8_UNORM;
                desc._is_color_target = false;
                history._reactive_a = RenderTexture::Create(desc, "_TemporalAA_ReactiveA");
                history._reactive_b = RenderTexture::Create(desc, "_TemporalAA_ReactiveB");
            }
            history._history_valid = false;
        }

        AL_ASSERT_MSG(history._target_a == nullptr || history._target_b == nullptr ||
                          history._target_a.get() != history._target_b.get(),
                      "TAA history read and write textures must be distinct.");
        AL_ASSERT_MSG(!history._history_valid ||
                          (history._has_rendered && frame_index == history._last_render_frame + 1u),
                      "TAA history may only be sampled on a contiguous rendered frame.");
        AL_ASSERT_MSG(!history._target_a || !history._target_b ||
                          (history._target_a->Width() == width && history._target_a->Height() == height &&
                           history._target_b->Width() == width && history._target_b->Height() == height),
                      "TAA history dimensions must match the current render dimensions.");

        history._width = width;
        history._height = height;
        history._camera_identity = &camera;
        history._camera_reset_revision = camera.GetTemporalHistoryResetRevision();
    }

    void TAAExecutePass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
    {
        if (rendering_data._camera == nullptr || !_taa_gen || _taa_kernel == kInvalidComputeShaderKernelId ||
            _stabilize_kernel == kInvalidComputeShaderKernelId)
            return;

        const RDG::RGHandle current_color = rendering_data._rg_handles._color_tex;
        const RDG::RGHandle depth = rendering_data._rg_handles._depth_tex;
        const RDG::RGHandle motion = rendering_data._rg_handles._motion_vector_tex;
        AL_ASSERT_MSG(current_color.IsValid() && depth.IsValid() && motion.IsValid(),
                      "TAA RenderGraph inputs must be valid.");
        if (!current_color.IsValid() || !depth.IsValid() || !motion.IsValid())
            return;

        auto history_it = _infos.find(_cur_camera_hash);
        if (history_it == _infos.end() || !history_it->second._target_a || !history_it->second._target_b)
            return;

        TAAHistoryState &history = history_it->second;
        const bool can_reproject = history._history_valid;
        const Vector2f previous_uv_jitter = history._previous_uv_jitter;
        const Vector2f current_uv_jitter = _current_uv_jitter;
        const Vector4f jitter(previous_uv_jitter.x, previous_uv_jitter.y,
                              current_uv_jitter.x, current_uv_jitter.y);
        const u64 camera_hash = _cur_camera_hash;
        const u64 frame_index = _current_frame_index;
        const Camera *camera = _current_camera;
        if (camera == nullptr)
            return;
        const Vector4f params = _params;
        const Vector4f quality = _quality;
        const u32 width = rendering_data._width;
        const u32 height = rendering_data._height;
        const Vector3f camera_position = camera->Position();
        const Vector3f camera_forward = camera->Forward();
        const Vector3f camera_up = camera->Up();
        const f32 camera_fov = camera->FovH();
        const f32 camera_near = camera->Near();
        const f32 camera_far = camera->Far();
        const f32 camera_aspect = camera->Aspect();
        const u8 projection_type = static_cast<u8>(camera->Type());
        Ref<RenderTexture> output_texture = history._write_a ? history._target_a : history._target_b;
        Ref<RenderTexture> history_texture = history._write_a ? history._target_b : history._target_a;
        AL_ASSERT_MSG(output_texture.get() != history_texture.get(),
                      "TAA history read and write textures must be distinct.");
        const RDG::RGHandle history_read = graph.Import(history_texture.get());
        const RDG::RGHandle history_write = graph.Import(output_texture.get());
        const auto reactive_read =
            graph.Import(history._write_a ? history._reactive_b.get() : history._reactive_a.get());
        const auto reactive_write =
            graph.Import(history._write_a ? history._reactive_a.get() : history._reactive_b.get());
        const auto opaque_color = rendering_data._rg_handles._taa_opaque_color;
        auto taa_output = std::make_shared<RDG::RGHandle>();
        auto reactive_output = std::make_shared<RDG::RGHandle>();
        RDG::PassDesc pass_desc;
        pass_desc._type = RDG::EPassType::kCompute;

        graph.AddPass("TemporalAA", pass_desc,
            [current_color, depth, motion, history_read, history_write, taa_output, opaque_color,
             reactive_read, reactive_write, reactive_output](RDG::RenderGraphBuilder &builder)
            {
                builder.Read(current_color, EResourceUsage::kReadSRV);
                builder.Read(depth, EResourceUsage::kReadSRV);
                builder.Read(motion, EResourceUsage::kReadSRV);
                builder.Read(history_read, EResourceUsage::kReadSRV);
                builder.Read(reactive_read, EResourceUsage::kReadSRV);
                if (opaque_color.IsValid())
                    builder.Read(opaque_color, EResourceUsage::kReadSRV);
                *taa_output = builder.Write(history_write, EResourceUsage::kWriteUAV);
                *reactive_output = builder.Write(reactive_write, EResourceUsage::kWriteUAV);
            },
            [this, current_color, depth, motion, history_read, taa_output, camera_hash, frame_index,
             current_uv_jitter, jitter, params, quality, width, height, can_reproject, camera_position,
             camera_forward, camera_up, camera_fov, camera_near, camera_far, camera_aspect, projection_type,
             opaque_color, reactive_read, reactive_output]
            (RDG::RenderGraph &pass_graph, CommandBuffer *cmd, const RenderingData &)
            {
                Texture *current_color_texture = pass_graph.Resolve<Texture>(current_color);
                Texture *depth_texture = pass_graph.Resolve<Texture>(depth);
                Texture *motion_texture = pass_graph.Resolve<Texture>(motion);
                Texture *history_color_texture = pass_graph.Resolve<Texture>(history_read);
                Texture *output = pass_graph.Resolve<Texture>(*taa_output);
                Texture *previous_reactive = pass_graph.Resolve<Texture>(reactive_read);
                Texture *current_reactive = pass_graph.Resolve<Texture>(*reactive_output);
                if (current_color_texture == nullptr || depth_texture == nullptr || motion_texture == nullptr ||
                    history_color_texture == nullptr || output == nullptr ||
                    previous_reactive == nullptr || current_reactive == nullptr)
                    return;

                _taa_gen->SetVector("_SourceTex_TexelSize", Vector4f(1.0f / width, 1.0f / height,
                                                                     static_cast<f32>(width), static_cast<f32>(height)));
                _taa_gen->SetVector("_Jitter", jitter);
                _taa_gen->SetFloat("_HistoryValid", can_reproject ? 1.0f : 0.0f);
                _taa_gen->SetTexture(_taa_kernel, "_CurrentColor", current_color_texture);
                _taa_gen->SetTexture(_taa_kernel, "_HistoryColor", history_color_texture);
                _taa_gen->SetTexture(_taa_kernel, "_CurrentTarget", output);
                _taa_gen->SetFloat("_ReactiveValid", opaque_color.IsValid() ? 1.0f : 0.0f);
                _taa_gen->SetTexture(_taa_kernel, "_OpaqueColor", opaque_color.IsValid()
                    ? pass_graph.Resolve<Texture>(opaque_color) : current_color_texture);
                _taa_gen->SetTexture(_taa_kernel, "_PreviousReactive", previous_reactive);
                _taa_gen->SetTexture(_taa_kernel, "_CurrentReactive", current_reactive);
                _taa_gen->SetTexture(_taa_kernel, "_CameraDepthTexture", depth_texture);
                _taa_gen->SetTexture(_taa_kernel, "_MotionVectorTexture", motion_texture);
                _taa_gen->SetFloat("_ClampQuality", quality.x);
                _taa_gen->SetFloat("_HistoryQuality", quality.y);
                _taa_gen->SetFloat("_MotionQuality", quality.z);
                _taa_gen->SetFloat("_VarianceClampScale", params.x);
                _taa_gen->SetFloat("_Sharpness", params.y);
                _taa_gen->SetFloat("_HistoryFactor", params.z);

                const auto [dispatch_x, dispatch_y, dispatch_z] =
                    _taa_gen->CalculateDispatchNum(_taa_kernel, width, height, 1u);
                cmd->Dispatch(_taa_gen.get(), _taa_kernel, dispatch_x, dispatch_y);

                auto state_it = _infos.find(camera_hash);
                if (state_it == _infos.end())
                    return;
                TAAHistoryState &state = state_it->second;
                state._history_valid = true;
                state._previous_uv_jitter = current_uv_jitter;
                state._last_render_frame = frame_index;
                state._write_a = !state._write_a;
                state._has_rendered = true;
                state._previous_position = camera_position;
                state._previous_forward = camera_forward;
                state._previous_up = camera_up;
                state._previous_fov = camera_fov;
                state._previous_near = camera_near;
                state._previous_far = camera_far;
                state._previous_aspect = camera_aspect;
                state._projection_type = projection_type;
            });

        TextureDesc stable_desc = rendering_data._camera_data._camera_color_target_desc;
        stable_desc._width = static_cast<u16>(width);
        stable_desc._height = static_cast<u16>(height);
        stable_desc._format = history._format;
        stable_desc._is_random_access = true;
        // The stabilization dispatch fully initializes this target. Preserve its result when later bound as an RTV.
        stable_desc._load = ELoadStoreAction::kNotCare;
        const auto stable_target = graph.CreateResource(stable_desc, "_TemporalAA_Stable");
        const auto resolved_color = *taa_output;
        const auto resolved_reactive = *reactive_output;
        auto stable_output = std::make_shared<RDG::RGHandle>();
        graph.AddPass("TemporalAAStabilize", pass_desc,
            [resolved_color, resolved_reactive, stable_target, stable_output, reactive_read, depth, motion]
            (RDG::RenderGraphBuilder &builder)
            {
                builder.Read(resolved_color, EResourceUsage::kReadSRV);
                builder.Read(resolved_reactive, EResourceUsage::kReadSRV);
                builder.Read(reactive_read, EResourceUsage::kReadSRV);
                builder.Read(depth, EResourceUsage::kReadSRV);
                builder.Read(motion, EResourceUsage::kReadSRV);
                *stable_output = builder.Write(stable_target, EResourceUsage::kWriteUAV);
            },
            [this, resolved_color, resolved_reactive, stable_output, jitter, width, height, opaque_color,
             reactive_read, depth, motion, can_reproject]
            (RDG::RenderGraph &pass_graph, CommandBuffer *cmd, const RenderingData &)
            {
                _taa_gen->SetVector("_SourceTex_TexelSize", Vector4f(1.0f / width, 1.0f / height,
                    static_cast<f32>(width), static_cast<f32>(height)));
                _taa_gen->SetVector("_Jitter", jitter);
                _taa_gen->SetFloat("_ReactiveValid", opaque_color.IsValid() ? 1.0f : 0.0f);
                _taa_gen->SetFloat("_HistoryValid", can_reproject ? 1.0f : 0.0f);
                _taa_gen->SetTexture(_stabilize_kernel, "_ResolvedColor", pass_graph.Resolve<Texture>(resolved_color));
                _taa_gen->SetTexture(_stabilize_kernel, "_ResolvedReactive",
                    pass_graph.Resolve<Texture>(resolved_reactive));
                _taa_gen->SetTexture(_stabilize_kernel, "_PreviousReactive",
                    pass_graph.Resolve<Texture>(reactive_read));
                _taa_gen->SetTexture(_stabilize_kernel, "_CameraDepthTexture", pass_graph.Resolve<Texture>(depth));
                _taa_gen->SetTexture(_stabilize_kernel, "_MotionVectorTexture", pass_graph.Resolve<Texture>(motion));
                _taa_gen->SetTexture(_stabilize_kernel, "_StableTarget", pass_graph.Resolve<Texture>(*stable_output));
                const auto [dispatch_x, dispatch_y, dispatch_z] =
                    _taa_gen->CalculateDispatchNum(_stabilize_kernel, width, height, 1u);
                cmd->Dispatch(_taa_gen.get(), _stabilize_kernel, dispatch_x, dispatch_y);
            });
        rendering_data._rg_handles._taa_color = *stable_output;
        rendering_data._rg_handles._taa_history_color = *taa_output;
        rendering_data._rg_handles._taa_reactive = *reactive_output;
    }

    void TAAExecutePass::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        auto history_it = _infos.find(_cur_camera_hash);
        if (history_it == _infos.end() || !history_it->second._target_a || !history_it->second._target_b ||
            !_taa_gen || _taa_kernel == kInvalidComputeShaderKernelId ||
            _stabilize_kernel == kInvalidComputeShaderKernelId)
            return;

        TAAHistoryState &history = history_it->second;
        if (!history._stable_target)
        {
            TextureDesc desc(static_cast<u16>(history._width), static_cast<u16>(history._height),
                             ERenderTargetFormat::kDefaultHDR);
            desc._format = history._format;
            desc._is_random_access = true;
            desc._load = ELoadStoreAction::kNotCare;
            history._stable_target = RenderTexture::Create(desc, "_TemporalAA_Stable");
        }
        auto cmd = CommandBufferPool::Get("Temporal AA");
        RenderTexture *output = history._write_a ? history._target_a.get() : history._target_b.get();
        RenderTexture *history_texture = history._write_a ? history._target_b.get() : history._target_a.get();
        const bool can_reproject = history._history_valid && RTHandle::Valid(rendering_data._camera_depth_tex_handle) &&
                                   RTHandle::Valid(rendering_data._motion_vector_tex_handle);
        const Vector4f jitter(history._previous_uv_jitter.x, history._previous_uv_jitter.y,
                              _current_uv_jitter.x, _current_uv_jitter.y);

        PROFILE_BLOCK_GPU(cmd.get(), "TemporalAA")
        _taa_gen->SetVector("_SourceTex_TexelSize", Vector4f(1.0f / rendering_data._width,
                                                              1.0f / rendering_data._height,
                                                              static_cast<f32>(rendering_data._width),
                                                              static_cast<f32>(rendering_data._height)));
        _taa_gen->SetVector("_Jitter", jitter);
        _taa_gen->SetFloat("_HistoryValid", can_reproject ? 1.0f : 0.0f);
        _taa_gen->SetTexture(_taa_kernel, "_CurrentColor", rendering_data._camera_opaque_tex_handle);
        _taa_gen->SetTexture(_taa_kernel, "_HistoryColor", history_texture);
        _taa_gen->SetTexture(_taa_kernel, "_CurrentTarget", output);
        _taa_gen->SetFloat("_ReactiveValid", 0.0f);
        _taa_gen->SetTexture(_taa_kernel, "_OpaqueColor", rendering_data._camera_opaque_tex_handle);
        _taa_gen->SetTexture(_taa_kernel, "_PreviousReactive",
                             history._write_a ? history._reactive_b.get() : history._reactive_a.get());
        _taa_gen->SetTexture(_taa_kernel, "_CurrentReactive",
                             history._write_a ? history._reactive_a.get() : history._reactive_b.get());
        _taa_gen->SetTexture(_taa_kernel, "_CameraDepthTexture", rendering_data._camera_depth_tex_handle);
        if (RTHandle::Valid(rendering_data._motion_vector_tex_handle))
            _taa_gen->SetTexture(_taa_kernel, "_MotionVectorTexture", rendering_data._motion_vector_tex_handle);
        if (!can_reproject)
            history._history_valid = false;
        _taa_gen->SetFloat("_ClampQuality", _quality.x);
        _taa_gen->SetFloat("_HistoryQuality", _quality.y);
        _taa_gen->SetFloat("_MotionQuality", _quality.z);
        _taa_gen->SetFloat("_VarianceClampScale", _params.x);
        _taa_gen->SetFloat("_Sharpness", _params.y);
        _taa_gen->SetFloat("_HistoryFactor", _params.z);

        const auto [dispatch_x, dispatch_y, dispatch_z] =
            _taa_gen->CalculateDispatchNum(_taa_kernel, rendering_data._width, rendering_data._height, 1u);
        cmd->Dispatch(_taa_gen.get(), _taa_kernel, dispatch_x, dispatch_y);
        _taa_gen->SetTexture(_stabilize_kernel, "_ResolvedColor", output);
        _taa_gen->SetTexture(_stabilize_kernel, "_ResolvedReactive",
            history._write_a ? history._reactive_a.get() : history._reactive_b.get());
        _taa_gen->SetTexture(_stabilize_kernel, "_PreviousReactive",
            history._write_a ? history._reactive_b.get() : history._reactive_a.get());
        _taa_gen->SetTexture(_stabilize_kernel, "_CameraDepthTexture", rendering_data._camera_depth_tex_handle);
        if (RTHandle::Valid(rendering_data._motion_vector_tex_handle))
            _taa_gen->SetTexture(_stabilize_kernel, "_MotionVectorTexture", rendering_data._motion_vector_tex_handle);
        _taa_gen->SetTexture(_stabilize_kernel, "_StableTarget", history._stable_target.get());
        cmd->Dispatch(_taa_gen.get(), _stabilize_kernel, dispatch_x, dispatch_y);
        rendering_data._postprocess_input = history._stable_target.get();

        context->ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
        _resolve_executed = true;
    }

    void TAAExecutePass::EndPass(GraphicsContext *context)
    {
        if (!_resolve_executed)
            return;

        auto history_it = _infos.find(_cur_camera_hash);
        if (history_it == _infos.end())
            return;

        TAAHistoryState &history = history_it->second;
        history._history_valid = true;
        history._previous_uv_jitter = _current_uv_jitter;
        history._last_render_frame = _current_frame_index;
        history._write_a = !history._write_a;
        history._has_rendered = true;
        if (_current_camera != nullptr)
        {
            history._previous_position = _current_camera->Position();
            history._previous_forward = _current_camera->Forward();
            history._previous_up = _current_camera->Up();
            history._previous_fov = _current_camera->FovH();
            history._previous_near = _current_camera->Near();
            history._previous_far = _current_camera->Far();
            history._previous_aspect = _current_camera->Aspect();
            history._projection_type = static_cast<u8>(_current_camera->Type());
        }
        _resolve_executed = false;
    }
}
