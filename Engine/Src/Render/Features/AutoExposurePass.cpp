#include "Render/Features/AutoExposurePass.h"

#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/TimeMgr.h"
#include "Render/Camera.h"
#include "Render/CommandBuffer.h"
#include "Render/GraphicsContext.h"
#include "Render/RenderGraph/RenderGraph.h"
#include "Render/RenderingData.h"
#include "Render/Shader.h"

#include <algorithm>
#include <cmath>

namespace Ailu::Render
{
    namespace
    {
        constexpr u32 kHistogramBinCount = 64u;
        constexpr u32 kHistogramGroupSize = 16u;
        constexpr u32 kHistogramSampleStep = 4u;
        constexpr f32 kLogLuminanceMinimum = -12.0f;
        constexpr f32 kLogLuminanceMaximum = 16.0f;
        constexpr f32 kDefaultExposureEv = 0.0f;
        constexpr u64 kHistoryIdleFrameLimit = 120u;

        bool IsAutomaticMode(EExposureMode mode)
        {
            return mode == EExposureMode::kAuto || mode == EExposureMode::kAperturePriority ||
                   mode == EExposureMode::kShutterPriority || mode == EExposureMode::kAutoISO;
        }

        Ref<RenderTexture> CreateExposureHistory(const String &name)
        {
            TextureDesc desc(1u, 1u, ERenderTargetFormat::kRFloat);
            desc._is_color_target = false;
            desc._is_random_access = true;
            desc._is_linear = true;
            desc._fixed_size = true;
            return RenderTexture::Create(desc, name);
        }
    }

    AutoExposurePass::AutoExposurePass()
    {
        _shader = ResourceMgr::Get().LoadAssetRef<ComputeShader>(L"Shaders/hlsl/Compute/auto_exposure.alasset");
        if (_shader != nullptr)
        {
            _clear_histogram_kernel = _shader->FindKernel("ClearHistogram");
            _build_histogram_kernel = _shader->FindKernel("BuildHistogram");
            _reduce_exposure_kernel = _shader->FindKernel("ReduceAndAdaptExposure");
        }
    }

    RenderTexture *AutoExposurePass::GetPreExposureTexture(const Camera &camera) const
    {
        const auto state_it = _camera_states.find(camera.HashCode());
        const u64 frame_index = g_pGfxContext != nullptr ? g_pGfxContext->GetFrameCount() : 0u;
        if (state_it == _camera_states.end() || !state_it->second._has_rendered ||
            !state_it->second._history_valid ||
            state_it->second._camera_identity != &camera || frame_index != state_it->second._last_render_frame + 1u)
            return nullptr;
        const CameraExposureState &state = state_it->second;
        return (state._write_a ? state._history_b : state._history_a).get();
    }

    RenderTexture *AutoExposurePass::GetPreviousPreExposureTexture(const Camera &camera) const
    {
        const auto state_it = _camera_states.find(camera.HashCode());
        const u64 frame_index = g_pGfxContext != nullptr ? g_pGfxContext->GetFrameCount() : 0u;
        if (state_it == _camera_states.end() || !state_it->second._has_rendered ||
            !state_it->second._previous_pre_exposure_valid ||
            state_it->second._camera_identity != &camera || frame_index != state_it->second._last_render_frame + 1u)
            return nullptr;
        const CameraExposureState &state = state_it->second;
        return (state._write_a ? state._history_a : state._history_b).get();
    }

    RDG::RGHandle AutoExposurePass::Record(RDG::RenderGraph &graph, RDG::RGHandle source,
                                           const RenderingData &rendering_data, const Camera &camera,
                                           const AutoExposureSettings &settings)
    {
        if (!_shader || _clear_histogram_kernel == kInvalidComputeShaderKernelId ||
            _build_histogram_kernel == kInvalidComputeShaderKernelId ||
            _reduce_exposure_kernel == kInvalidComputeShaderKernelId || !source.IsValid())
            return {};

        const TextureDesc source_desc = graph.GetTextureDesc(source);
        if (source_desc._width == 0u || source_desc._height == 0u)
            return {};

        const u64 frame_index = g_pGfxContext != nullptr ? g_pGfxContext->GetFrameCount() : 0u;
        const u64 camera_hash = camera.HashCode();
        const u64 camera_reset_revision = camera.GetTemporalHistoryResetRevision();
        for (auto it = _camera_states.begin(); it != _camera_states.end();)
        {
            const CameraExposureState &state = it->second;
            if (state._has_rendered && frame_index > state._last_render_frame &&
                frame_index - state._last_render_frame > kHistoryIdleFrameLimit)
                it = _camera_states.erase(it);
            else
                ++it;
        }

        CameraExposureState &state = _camera_states[camera_hash];
        if (!state._history_a || !state._history_b)
        {
            state._history_a = CreateExposureHistory("AutoExposureHistoryA");
            state._history_b = CreateExposureHistory("AutoExposureHistoryB");
            state._history_valid = false;
            state._has_rendered = false;
        }
        if (!state._history_a || !state._history_b)
            return {};

        const bool pre_exposure_valid = state._has_rendered && state._history_valid &&
                                        state._camera_identity == &camera &&
                                        frame_index == state._last_render_frame + 1u;
        const bool skipped_or_duplicate_frame = state._has_rendered &&
            (frame_index <= state._last_render_frame || frame_index != state._last_render_frame + 1u);
        const bool reset_history = !state._has_rendered || state._camera_identity != &camera ||
                                   state._camera_reset_revision != camera_reset_revision || skipped_or_duplicate_frame;
        const bool history_valid = state._history_valid && !reset_history;
        state._camera_identity = &camera;
        state._camera_reset_revision = camera_reset_revision;
        state._history_valid = history_valid;

        Ref<RenderTexture> history_read_texture = state._write_a ? state._history_b : state._history_a;
        Ref<RenderTexture> history_write_texture = state._write_a ? state._history_a : state._history_b;
        const RDG::RGHandle history_read = graph.Import(history_read_texture.get());
        const RDG::RGHandle history_write = graph.Import(history_write_texture.get());
        const i32 metering_mode = static_cast<u8>(settings._metering_mode) <=
            static_cast<u8>(EExposureMeteringMode::kCenterWeighted)
            ? static_cast<i32>(settings._metering_mode) : static_cast<i32>(EExposureMeteringMode::kAverage);

        BufferDesc histogram_desc;
        histogram_desc._element_num = kHistogramBinCount;
        histogram_desc._element_size = sizeof(u32);
        histogram_desc._size = kHistogramBinCount * sizeof(u32);
        histogram_desc._target = EGPUBufferTarget::kStructured;
        histogram_desc._is_random_write = true;
        histogram_desc._is_create_srv = true;
        histogram_desc._is_create_uav = true;
        const RDG::RGHandle histogram = graph.CreateResource(histogram_desc, "AutoExposureHistogram");
        auto histogram_cleared = std::make_shared<RDG::RGHandle>();
        auto histogram_built = std::make_shared<RDG::RGHandle>();
        auto exposure_output = std::make_shared<RDG::RGHandle>();

        RDG::PassDesc compute_pass;
        compute_pass._type = RDG::EPassType::kCompute;
        graph.AddPass("AutoExposureClearHistogram", compute_pass,
            [histogram, histogram_cleared](RDG::RenderGraphBuilder &builder)
            {
                *histogram_cleared = builder.Write(histogram, EResourceUsage::kWriteUAV);
            },
            [this, histogram_cleared](RDG::RenderGraph &pass_graph, CommandBuffer *cmd, const RenderingData &)
            {
                GPUBuffer *buffer = pass_graph.Resolve<GPUBuffer>(*histogram_cleared);
                if (buffer == nullptr)
                    return;
                _shader->SetBuffer(_clear_histogram_kernel, "_Histogram", buffer);
                cmd->Dispatch(_shader.get(), _clear_histogram_kernel, 1u, 1u, 1u);
            });

        const u32 sample_width = (source_desc._width + kHistogramSampleStep - 1u) / kHistogramSampleStep;
        const u32 sample_height = (source_desc._height + kHistogramSampleStep - 1u) / kHistogramSampleStep;
        const u32 dispatch_x = (sample_width + kHistogramGroupSize - 1u) / kHistogramGroupSize;
        const u32 dispatch_y = (sample_height + kHistogramGroupSize - 1u) / kHistogramGroupSize;
        graph.AddPass("AutoExposureBuildHistogram", compute_pass,
            [source, history_read, histogram_cleared, histogram_built](RDG::RenderGraphBuilder &builder)
            {
                builder.Read(source, EResourceUsage::kReadSRV);
                builder.Read(history_read, EResourceUsage::kReadSRV);
                *histogram_built = builder.WriteAfter(*histogram_cleared, EResourceUsage::kWriteUAV);
            },
            [this, source, history_read, histogram_built, source_desc, dispatch_x, dispatch_y,
             pre_exposure_valid, metering_mode]
            (RDG::RenderGraph &pass_graph, CommandBuffer *cmd, const RenderingData &)
            {
                Texture *source_texture = pass_graph.Resolve<Texture>(source);
                GPUBuffer *histogram_buffer = pass_graph.Resolve<GPUBuffer>(*histogram_built);
                if (source_texture == nullptr || histogram_buffer == nullptr)
                    return;

                _shader->SetTexture(_build_histogram_kernel, "_SourceTex", source_texture);
                _shader->SetTexture(_build_histogram_kernel, "_PreviousExposureEV",
                                    pass_graph.Resolve<Texture>(history_read));
                _shader->SetBuffer(_build_histogram_kernel, "_Histogram", histogram_buffer);
                _shader->SetVector("_SourceSize", Vector4f(static_cast<f32>(source_desc._width),
                                                            static_cast<f32>(source_desc._height), 0.0f, 0.0f));
                _shader->SetInt("_HistoryValid", pre_exposure_valid ? 1 : 0);
                _shader->SetVector("_LogRange", Vector4f(kLogLuminanceMinimum, kLogLuminanceMaximum,
                                                          0.0f, 0.0f));
                _shader->SetInt("_MeteringMode", metering_mode);
                cmd->Dispatch(_shader.get(), _build_histogram_kernel, static_cast<u16>(dispatch_x),
                              static_cast<u16>(dispatch_y), 1u);
            });

        const PhysicalCameraSettings &camera_settings = camera.PhysicalSettings();
        const f32 delta_time = std::clamp(std::isfinite(TimeMgr::s_delta_time) ? TimeMgr::s_delta_time : 0.0f,
                                          0.0f, 0.25f);
        const f32 low_percentile = std::clamp(std::isfinite(settings._low_percentile)
            ? settings._low_percentile : 5.0f, 0.0f, 99.0f);
        const f32 high_percentile = std::clamp(std::isfinite(settings._high_percentile)
            ? settings._high_percentile : 95.0f, low_percentile + 0.1f, 100.0f);
        const f32 adaptation_speed = std::clamp(std::isfinite(settings._adaptation_speed)
            ? settings._adaptation_speed : 3.0f, 0.0f, 100.0f);
        const auto resolve_adaptation_speed = [adaptation_speed](f32 speed)
        {
            return std::isfinite(speed) && speed >= 0.0f ? std::clamp(speed, 0.0f, 100.0f) : adaptation_speed;
        };
        const f32 speed_brighten = resolve_adaptation_speed(settings._adaptation_speed_brighten);
        const f32 speed_darken = resolve_adaptation_speed(settings._adaptation_speed_darken);
        const f32 minimum_ev = std::clamp(std::isfinite(settings._minimum_ev) ? settings._minimum_ev : -12.0f,
                                          -24.0f, 23.9f);
        const f32 maximum_ev = std::clamp(std::isfinite(settings._maximum_ev) ? settings._maximum_ev : 16.0f,
                                          minimum_ev + 0.1f, 24.0f);
        const Vector4f camera_params(camera_settings._aperture_f_number, camera_settings._shutter_seconds,
                                     camera_settings._iso, camera_settings._exposure_compensation_ev);
        const Vector4f control_limits(1.0e-6f, 3600.0f, 0.1f, 128.0f);
        const Vector4f exposure_limits(minimum_ev, maximum_ev, 0.0f, 0.0f);
        const Vector4f photometric_calibration(ResolveSceneLuminanceToCdM2(settings), 0.0f, 0.0f, 0.0f);
        const bool exposure_valid = history_valid;
        const EExposureMode exposure_mode = camera_settings._exposure_mode;
        graph.AddPass("AutoExposureReduceAndAdapt", compute_pass,
            [histogram_built, history_read, history_write, exposure_output]
            (RDG::RenderGraphBuilder &builder)
            {
                builder.Read(*histogram_built, EResourceUsage::kReadSRV);
                builder.Read(history_read, EResourceUsage::kReadSRV);
                *exposure_output = builder.Write(history_write, EResourceUsage::kWriteUAV);
            },
            [this, histogram_built, history_read, exposure_output, camera_hash, frame_index,
             camera_reset_revision, camera_params, control_limits, exposure_limits, photometric_calibration,
             low_percentile, high_percentile, speed_brighten, speed_darken, delta_time, exposure_valid,
             exposure_mode, metering_mode, pre_exposure_valid]
            (RDG::RenderGraph &pass_graph, CommandBuffer *cmd, const RenderingData &)
            {
                GPUBuffer *histogram_buffer = pass_graph.Resolve<GPUBuffer>(*histogram_built);
                Texture *history_texture = pass_graph.Resolve<Texture>(history_read);
                Texture *output_texture = pass_graph.Resolve<Texture>(*exposure_output);
                if (histogram_buffer == nullptr || history_texture == nullptr || output_texture == nullptr)
                    return;

                _shader->SetBuffer(_reduce_exposure_kernel, "_Histogram", histogram_buffer);
                _shader->SetTexture(_reduce_exposure_kernel, "_PreviousExposureEV", history_texture);
                _shader->SetTexture(_reduce_exposure_kernel, "_OutputExposureEV", output_texture);
                _shader->SetVector("_Percentiles", Vector4f(low_percentile * 0.01f,
                                                             high_percentile * 0.01f, 0.0f, 0.0f));
                _shader->SetVector("_Adaptation", Vector4f(delta_time, speed_brighten, speed_darken, 0.0f));
                _shader->SetVector("_CameraParams", camera_params);
                _shader->SetVector("_ControlLimits", control_limits);
                _shader->SetVector("_ExposureLimits", exposure_limits);
                _shader->SetVector("_PhotometricCalibration", photometric_calibration);
                _shader->SetInt("_ExposureMode", static_cast<i32>(exposure_mode));
                _shader->SetInt("_HistoryValid", exposure_valid ? 1 : 0);
                _shader->SetInt("_MeteringMode", metering_mode);
                cmd->Dispatch(_shader.get(), _reduce_exposure_kernel, 1u, 1u, 1u);

                auto state_it = _camera_states.find(camera_hash);
                if (state_it == _camera_states.end())
                    return;
                CameraExposureState &state = state_it->second;
                state._history_valid = true;
                state._previous_pre_exposure_valid = pre_exposure_valid;
                state._write_a = !state._write_a;
                state._has_rendered = true;
                state._last_render_frame = frame_index;
                state._camera_reset_revision = camera_reset_revision;
            });

        (void) rendering_data;
        return *exposure_output;
    }
}
