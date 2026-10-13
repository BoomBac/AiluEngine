#include "pch.h"

#include "RHI/DX12/StreamlineDx12Bridge.h"
#include "RHI/DX12/StreamlineRuntime.h"
#include "RHI/DX12/D3DContext.h"
#include "RHI/DX12/D3DCommandBuffer.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/SystemInfo.h"
#include "Framework/Script/ScriptSystem.h"
#include "Render/Camera.h"
#include "Render/GraphicsContext.h"
#include "Render/GpuResource.h"
#include "Render/StreamlineSR.h"
#include "Render/Texture.h"

#include <unordered_set>

#if AILU_WITH_STREAMLINE
#include <sl.h>
#include <sl_consts.h>
#include <sl_dlss.h>
#endif

namespace Ailu::RHI::DX12
{
    bool StreamlineDx12Bridge::GetNativeResource(Render::GpuResource* resource, ID3D12Resource** native_resource)
    {
        if (native_resource == nullptr)
            return false;
        *native_resource = nullptr;
        if (resource == nullptr)
            return false;

        const auto handle = resource->NativeResource();
        if (!handle || handle._type != Render::RendererAPI::ERenderAPI::kDirectX12)
            return false;

        auto* d3d_resource = handle.As<ID3D12Resource>();
        d3d_resource->AddRef();
        *native_resource = d3d_resource;
        return true;
    }

    bool StreamlineDx12Bridge::UpgradeDevice(StreamlineRuntime& runtime, ID3D12Device* native_device,
                                              ID3D12Device** proxy_device)
    {
        if (proxy_device == nullptr)
            return false;
        *proxy_device = nullptr;
        if (native_device == nullptr)
            return false;
        ID3D12Device* upgraded_device = native_device;
        const bool upgrade_result = runtime.UpgradeInterface(reinterpret_cast<void**>(&upgraded_device));
        if (!upgrade_result || upgraded_device == nullptr || upgraded_device == native_device)
        {
            if (upgraded_device != nullptr && upgraded_device != native_device)
                upgraded_device->Release();
            return false;
        }
        *proxy_device = upgraded_device;
        return true;
    }

    bool StreamlineDx12Bridge::UpgradeFactory(StreamlineRuntime& runtime, IDXGIFactory* native_factory,
                                               IDXGIFactory6** proxy_factory)
    {
        if (proxy_factory == nullptr)
            return false;
        *proxy_factory = nullptr;
        if (native_factory == nullptr)
            return false;
        IDXGIFactory* proxy_base = native_factory;
        const bool upgrade_result = runtime.UpgradeInterface(reinterpret_cast<void**>(&proxy_base));
        if (!upgrade_result || proxy_base == nullptr || proxy_base == native_factory)
        {
            if (proxy_base != nullptr && proxy_base != native_factory)
                proxy_base->Release();
            return false;
        }

        IDXGIFactory6* upgraded_factory = nullptr;
        const HRESULT query_hr = proxy_base->QueryInterface(IID_PPV_ARGS(&upgraded_factory));
        proxy_base->Release();
        if (FAILED(query_hr))
        {
            LOG_WARNING("Streamline: upgraded IDXGIFactory does not expose IDXGIFactory6 (hr=0x{:08X}).",
                        static_cast<std::uint32_t>(query_hr));
        }
        else
            *proxy_factory = upgraded_factory;
        return SUCCEEDED(query_hr);
    }

    bool StreamlineDx12Bridge::UpgradeSwapchain(StreamlineRuntime& runtime, IDXGISwapChain1* proxy_swapchain,
                                                 IDXGISwapChain1** native_swapchain)
    {
        if (native_swapchain == nullptr)
            return false;
        *native_swapchain = nullptr;
        if (proxy_swapchain == nullptr)
            return false;
        return runtime.GetNativeInterface(proxy_swapchain, reinterpret_cast<void**>(native_swapchain));
    }
}

namespace Ailu::Render
{
    namespace
    {
        std::mutex s_dlss_state_mutex;
        std::unordered_map<u64, ESuperResolutionQuality> s_dlss_failed_modes;
    }

    void StreamlineSR::ResolveCamera(Camera& camera)
    {
        const Vector2D<u32> configured_size = camera.ConfiguredRenderSize();
        const bool is_supported_view = camera._is_scene_camera || &camera == Camera::sMain || &camera == Camera::sScene;
        auto* context = dynamic_cast<RHI::DX12::D3DContext*>(g_pGfxContext);
        const bool available = context != nullptr && context->GetStreamlineRuntime().IsDlssSupported();
        if (camera.SuperResolutionMode() != ESuperResolutionMode::kDlss)
        {
            {
                std::lock_guard lock(s_dlss_state_mutex);
                s_dlss_failed_modes.erase(camera.HashCode());
            }
            camera.ResolveTemporalMode(camera._anti_aliasing == EAntiAliasing::kTAA ? EResolvedTemporalMode::kTAA
                                                                                   : EResolvedTemporalMode::kSpatial,
                                       configured_size, available);
            return;
        }

        const Vector2D<u32> output_size = camera.OutputSize();
        u32 mode = 0u;
        switch (camera.SuperResolutionQuality())
        {
        case ESuperResolutionQuality::kQuality: mode = 1u; break;
        case ESuperResolutionQuality::kBalanced: mode = 2u; break;
        case ESuperResolutionQuality::kPerformance: mode = 3u; break;
        case ESuperResolutionQuality::kUltraPerformance: mode = 3u; break;
        default: break;
        }

        u32 render_width = 0u;
        u32 render_height = 0u;
        bool failed_mode = false;
        {
            std::lock_guard lock(s_dlss_state_mutex);
            const auto failed = s_dlss_failed_modes.find(camera.HashCode());
            if (failed != s_dlss_failed_modes.end())
            {
                failed_mode = failed->second == camera.SuperResolutionQuality();
                if (!failed_mode)
                    s_dlss_failed_modes.erase(failed);
            }
        }
        const bool can_use_dlss = !failed_mode && available && is_supported_view && camera.TargetTexture() == nullptr &&
                                  camera._postprocess_enabled && !camera.IsCustomVP() && output_size.x > 0u &&
                                  output_size.y > 0u && context->GetStreamlineRuntime().GetDlssOptimalRenderSize(
                                      mode, output_size.x, output_size.y, render_width, render_height);
        if (can_use_dlss)
        {
            camera.ResolveTemporalMode(EResolvedTemporalMode::kDlssSR,
                                       Vector2D<u32>{render_width, render_height}, true);
            return;
        }

        camera.ResolveTemporalMode(camera._anti_aliasing == EAntiAliasing::kTAA ? EResolvedTemporalMode::kTAA
                                                                               : EResolvedTemporalMode::kSpatial,
                                   configured_size, available);
    }

    namespace
    {
#if AILU_WITH_FSR
        std::mutex s_fsr_failure_mutex;
        std::unordered_set<u64> s_fsr_failed_cameras;
#endif
    }

    void SuperResolution::ResolveCamera(Camera& camera)
    {
#if !AILU_WITH_FSR
        StreamlineSR::ResolveCamera(camera);
        return;
#else
        if (camera.SuperResolutionMode() != ESuperResolutionMode::kFsr)
        {
            {
                std::lock_guard lock(s_fsr_failure_mutex);
                s_fsr_failed_cameras.erase(camera.HashCode());
            }
            StreamlineSR::ResolveCamera(camera);
            return;
        }

        const auto configured_size = camera.ConfiguredRenderSize();
        const auto output_size = camera.OutputSize();
        const bool supported_view = camera._is_scene_camera || &camera == Camera::sMain || &camera == Camera::sScene;
        auto* context = dynamic_cast<RHI::DX12::D3DContext*>(g_pGfxContext);
        const bool runtime_available = context != nullptr && context->GetFsrRuntime().IsSupported();
        bool failed = false;
        {
            std::lock_guard lock(s_fsr_failure_mutex);
            failed = s_fsr_failed_cameras.contains(camera.HashCode());
        }
        u32 render_width = 0u;
        u32 render_height = 0u;
        const bool can_use_fsr = !failed && runtime_available && supported_view && camera.TargetTexture() == nullptr &&
                                 camera._postprocess_enabled && !camera.IsCustomVP() && output_size.x > 0u &&
                                 output_size.y > 0u && context->GetFsrRuntime().GetRenderSize(
                                     output_size.x, output_size.y,
                                     static_cast<u32>(camera.SuperResolutionQuality()) + 1u,
                                     render_width, render_height);
        if (can_use_fsr)
        {
            camera.ResolveTemporalMode(EResolvedTemporalMode::kFsrSR,
                                       Vector2D<u32>{render_width, render_height}, false, runtime_available);
            return;
        }

        camera.ResolveTemporalMode(camera._anti_aliasing == EAntiAliasing::kTAA ? EResolvedTemporalMode::kTAA
                                                                               : EResolvedTemporalMode::kSpatial,
                                   configured_size, false, runtime_available);
#endif
    }

    SuperResolutionFrameData SuperResolution::Snapshot(const Camera& camera, std::uint64_t frame_index)
    {
        SuperResolutionFrameData data;
        const auto render_size = camera.RenderSize();
        const auto output_size = camera.OutputSize();
        const auto jitter = camera.GetJitter();
        data._camera_id = camera.HashCode();
        data._frame_index = frame_index;
        data._reset_revision = camera.GetTemporalHistoryResetRevision();
        data._render_width = render_size.x;
        data._render_height = render_size.y;
        data._output_width = output_size.x;
        data._output_height = output_size.y;
        data._jitter_x = jitter.z * static_cast<float>(render_size.x);
        data._jitter_y = jitter.w * static_cast<float>(render_size.y);
        data._frame_time_ms = std::max(1.0f, ScriptSystem::Get().GetDeltaTime() * 1000.0f);
        data._camera_near = camera.Near();
        data._camera_far = camera.Far();
        const float fov_horizontal = camera.FovH() * k2Radius;
        data._camera_fov_vertical = 2.0f * std::atan(std::tan(fov_horizontal * 0.5f) / std::max(camera.Aspect(), 0.001f));
        return data;
    }

    bool SuperResolution::Evaluate(const SuperResolutionFrameData& frame_data, Texture* color, Texture* depth,
                                  Texture* motion_vectors, Texture* output, RHICommandBuffer* command_buffer)
    {
#if !AILU_WITH_FSR
        (void) frame_data;
        (void) color;
        (void) depth;
        (void) motion_vectors;
        (void) output;
        (void) command_buffer;
        return false;
#else
        auto* context = dynamic_cast<RHI::DX12::D3DContext*>(g_pGfxContext);
        auto* dx_command_buffer = dynamic_cast<RHI::DX12::D3DCommandBuffer*>(command_buffer);
        if (context == nullptr || dx_command_buffer == nullptr || color == nullptr || depth == nullptr ||
            motion_vectors == nullptr || output == nullptr || frame_data._output_width == 0u ||
            frame_data._output_height == 0u || frame_data._render_width == 0u || frame_data._render_height == 0u)
            return false;

        auto get_native = [](Texture* texture) -> void*
        {
            const auto handle = texture->NativeResource();
            return handle && handle._type == RendererAPI::ERenderAPI::kDirectX12 ? handle.As<void>() : nullptr;
        };
        const bool result = context->GetFsrRuntime().Evaluate(
            dx_command_buffer->NativeCmdList(), get_native(color), get_native(depth), get_native(motion_vectors),
            get_native(output), frame_data._camera_id, frame_data._reset_revision, frame_data._render_width,
            frame_data._render_height, frame_data._output_width, frame_data._output_height, frame_data._jitter_x,
            frame_data._jitter_y, frame_data._frame_time_ms, frame_data._camera_near, frame_data._camera_far,
            frame_data._camera_fov_vertical);
        if (!result)
        {
            std::lock_guard lock(s_fsr_failure_mutex);
            if (s_fsr_failed_cameras.insert(frame_data._camera_id).second)
                LOG_WARNING("FSR: evaluation failed for camera {}; the next frame will use its configured AA fallback.",
                            frame_data._camera_id);
        }
        return result;
#endif
    }

    bool StreamlineSR::Evaluate(const Camera& camera, Texture* color, Texture* depth, Texture* motion_vectors,
                                Texture* output, RHICommandBuffer* command_buffer, std::uint64_t frame_index)
    {
#if !AILU_WITH_STREAMLINE
        (void) camera;
        (void) color;
        (void) depth;
        (void) motion_vectors;
        (void) output;
        (void) command_buffer;
        (void) frame_index;
        return false;
#else
        struct FailureMarker
        {
            u64 _camera_hash;
            ESuperResolutionQuality _quality;
            const char* _stage = "resource validation";
            i32 _result = -1;
            bool _success = false;
            ~FailureMarker()
            {
                if (_success)
                    return;
                std::lock_guard lock(s_dlss_state_mutex);
                s_dlss_failed_modes[_camera_hash] = _quality;
                LOG_WARNING("Streamline: DLSS failed for camera {} at {} (result={}); disabling SR on the next frame.",
                            _camera_hash, _stage, _result);
            }
        } failure_marker{camera.HashCode(), camera.SuperResolutionQuality()};

        auto* context = dynamic_cast<RHI::DX12::D3DContext*>(g_pGfxContext);
        auto* dx_command_buffer = dynamic_cast<RHI::DX12::D3DCommandBuffer*>(command_buffer);
        if (context == nullptr || dx_command_buffer == nullptr || color == nullptr || depth == nullptr ||
            motion_vectors == nullptr || output == nullptr)
            return false;

        auto& runtime = context->GetStreamlineRuntime();
        if (!runtime.IsDlssSupported())
            return false;

        ID3D12Resource* color_resource = nullptr;
        ID3D12Resource* depth_resource = nullptr;
        ID3D12Resource* motion_resource = nullptr;
        ID3D12Resource* output_resource = nullptr;
        if (!RHI::DX12::StreamlineDx12Bridge::GetNativeResource(color, &color_resource) ||
            !RHI::DX12::StreamlineDx12Bridge::GetNativeResource(depth, &depth_resource) ||
            !RHI::DX12::StreamlineDx12Bridge::GetNativeResource(motion_vectors, &motion_resource) ||
            !RHI::DX12::StreamlineDx12Bridge::GetNativeResource(output, &output_resource))
        {
            if (color_resource != nullptr) color_resource->Release();
            if (depth_resource != nullptr) depth_resource->Release();
            if (motion_resource != nullptr) motion_resource->Release();
            if (output_resource != nullptr) output_resource->Release();
            return false;
        }

        const auto make_resource = [](ID3D12Resource* native, D3D12_RESOURCE_STATES state)
        {
            const D3D12_RESOURCE_DESC desc = native->GetDesc();
            sl::Resource resource(sl::ResourceType::eTex2d, native, static_cast<std::uint32_t>(state));
            resource.width = static_cast<std::uint32_t>(desc.Width);
            resource.height = desc.Height;
            resource.nativeFormat = static_cast<std::uint32_t>(desc.Format);
            resource.mipLevels = desc.MipLevels;
            resource.arrayLayers = desc.DepthOrArraySize;
            return resource;
        };

        constexpr D3D12_RESOURCE_STATES kShaderResourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                                                               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        sl::Resource color_sl = make_resource(color_resource, kShaderResourceState);
        sl::Resource depth_sl = make_resource(depth_resource, kShaderResourceState);
        sl::Resource motion_sl = make_resource(motion_resource, kShaderResourceState);
        sl::Resource output_sl = make_resource(output_resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        color_resource->Release();
        depth_resource->Release();
        motion_resource->Release();
        output_resource->Release();

        const Vector2D<u32> output_size = camera.OutputSize();
        const u32 viewport_id = static_cast<u32>(camera.HashCode());
        const sl::ViewportHandle viewport(viewport_id);
        const ESuperResolutionQuality quality = camera.SuperResolutionQuality();
        const sl::DLSSMode dlss_mode = quality == ESuperResolutionQuality::kQuality ? sl::DLSSMode::eMaxQuality
            : quality == ESuperResolutionQuality::kBalanced ? sl::DLSSMode::eBalanced : sl::DLSSMode::eMaxPerformance;
        sl::DLSSOptions options{};
        options.mode = dlss_mode;
        options.outputWidth = output_size.x;
        options.outputHeight = output_size.y;
        options.colorBuffersHDR = sl::Boolean::eTrue;
        options.useAutoExposure = sl::Boolean::eTrue;

        using SetOptionsFunction = PFun_slDLSSSetOptions*;
        using GetTokenFunction = PFun_slGetNewFrameToken*;
        using SetConstantsFunction = PFun_slSetConstants*;
        using EvaluateFunction = PFun_slEvaluateFeature*;
        using SetTagForFrameFunction = PFun_slSetTagForFrame*;
        auto* set_options = reinterpret_cast<SetOptionsFunction>(runtime.GetFeatureFunction("slDLSSSetOptions"));
        auto* get_token = reinterpret_cast<GetTokenFunction>(runtime.GetCoreFunction("slGetNewFrameToken"));
        auto* set_constants = reinterpret_cast<SetConstantsFunction>(runtime.GetCoreFunction("slSetConstants"));
        auto* evaluate = reinterpret_cast<EvaluateFunction>(runtime.GetCoreFunction("slEvaluateFeature"));
        auto* set_tag_for_frame = reinterpret_cast<SetTagForFrameFunction>(runtime.GetCoreFunction("slSetTagForFrame"));
        if (set_options == nullptr || get_token == nullptr || set_constants == nullptr || evaluate == nullptr ||
            set_tag_for_frame == nullptr)
            return false;

        std::lock_guard lock(RHI::DX12::GetStreamlineApiMutex());
        const auto option_result = (*set_options)(viewport, options);
        failure_marker._stage = "slDLSSSetOptions";
        failure_marker._result = static_cast<i32>(option_result);
        if (option_result != sl::Result::eOk)
            return false;
        const u32 streamline_frame_index = static_cast<u32>(frame_index);
        sl::FrameToken* frame_token = nullptr;
        const auto token_result = (*get_token)(frame_token, &streamline_frame_index);
        failure_marker._stage = "slGetNewFrameToken";
        failure_marker._result = static_cast<i32>(token_result);
        if (token_result != sl::Result::eOk || frame_token == nullptr)
            return false;

        sl::Constants constants{};
        const Matrix4x4f projection = camera.GetProjNoJitter();
        const Matrix4x4f inverse_projection = MatrixInverse(projection);
        const Matrix4x4f clip_to_previous = MatrixInverse(camera.GetViewProjNoJitter()) * camera.GetPrevViewProj();
        const Matrix4x4f previous_to_clip = MatrixInverse(clip_to_previous);
        std::memcpy(&constants.cameraViewToClip, &projection, sizeof(projection));
        std::memcpy(&constants.clipToCameraView, &inverse_projection, sizeof(inverse_projection));
        std::memcpy(&constants.clipToPrevClip, &clip_to_previous, sizeof(clip_to_previous));
        std::memcpy(&constants.prevClipToClip, &previous_to_clip, sizeof(previous_to_clip));
        const Vector4f& jitter = camera.GetJitter();
        const Vector2D<u32> render_size = camera.RenderSize();
        constants.jitterOffset = {jitter.z * static_cast<f32>(render_size.x),
                                  jitter.w * static_cast<f32>(render_size.y)};
        constants.mvecScale = {1.0f, 1.0f};
        constants.cameraPinholeOffset = {0.0f, 0.0f};
        constants.cameraNear = camera.Near();
        constants.cameraFar = camera.Far();
        constants.cameraFOV = camera.FovH() * k2Radius;
        constants.cameraAspectRatio = camera.Aspect();
        constants.cameraPos = {camera.Position().x, camera.Position().y, camera.Position().z};
        constants.cameraUp = {camera.Up().x, camera.Up().y, camera.Up().z};
        constants.cameraRight = {camera.Right().x, camera.Right().y, camera.Right().z};
        constants.cameraFwd = {camera.Forward().x, camera.Forward().y, camera.Forward().z};
        constants.depthInverted = SystemInfo::kReverseZ ? sl::Boolean::eTrue : sl::Boolean::eFalse;
        constants.cameraMotionIncluded = sl::Boolean::eTrue;
        constants.motionVectors3D = sl::Boolean::eFalse;
        constants.motionVectorsJittered = sl::Boolean::eFalse;
        constants.motionVectorsDilated = sl::Boolean::eFalse;
        constants.orthographicProjection = camera.Type() == ECameraType::kOrthographic ? sl::Boolean::eTrue
                                                                                       : sl::Boolean::eFalse;
        static std::unordered_map<u32, u64> s_last_reset_revisions;
        const u64 reset_revision = camera.GetTemporalHistoryResetRevision();
        const auto last_revision = s_last_reset_revisions.find(viewport_id);
        const bool reset_history = last_revision == s_last_reset_revisions.end() ||
                                   last_revision->second != reset_revision;
        s_last_reset_revisions[viewport_id] = reset_revision;
        constants.reset = reset_history ? sl::Boolean::eTrue : sl::Boolean::eFalse;

        const sl::Extent color_extent{0u, 0u, color_sl.width, color_sl.height};
        const sl::Extent depth_extent{0u, 0u, depth_sl.width, depth_sl.height};
        const sl::Extent motion_extent{0u, 0u, motion_sl.width, motion_sl.height};
        const sl::Extent output_extent{0u, 0u, output_sl.width, output_sl.height};
        sl::ResourceTag tags[] = {
            sl::ResourceTag(&color_sl, sl::kBufferTypeScalingInputColor, sl::eValidUntilEvaluate, &color_extent),
            sl::ResourceTag(&depth_sl, sl::kBufferTypeDepth, sl::eValidUntilEvaluate, &depth_extent),
            sl::ResourceTag(&motion_sl, sl::kBufferTypeMotionVectors, sl::eValidUntilEvaluate, &motion_extent),
            sl::ResourceTag(&output_sl, sl::kBufferTypeScalingOutputColor, sl::eValidUntilEvaluate, &output_extent),
        };
        const auto constants_result = (*set_constants)(constants, *frame_token, viewport);
        failure_marker._stage = "slSetConstants";
        failure_marker._result = static_cast<i32>(constants_result);
        if (constants_result != sl::Result::eOk)
            return false;

        const auto tag_result = (*set_tag_for_frame)(*frame_token, viewport, tags, _countof(tags),
                                                      dx_command_buffer->NativeCmdList());
        failure_marker._stage = "slSetTagForFrame";
        failure_marker._result = static_cast<i32>(tag_result);
        if (tag_result != sl::Result::eOk)
            return false;

        // SDK 2.14.1 explicit allocation reads frame 0; evaluate initializes/resizes using this frame's tags.
        const sl::BaseStructure* inputs[] = {&viewport};
        const auto evaluate_result = (*evaluate)(sl::kFeatureDLSS, *frame_token, inputs, _countof(inputs),
                                                 dx_command_buffer->NativeCmdList());
        failure_marker._stage = "slEvaluateFeature";
        failure_marker._result = static_cast<i32>(evaluate_result);
        failure_marker._success = evaluate_result == sl::Result::eOk;
        return failure_marker._success;
#endif
    }
}
