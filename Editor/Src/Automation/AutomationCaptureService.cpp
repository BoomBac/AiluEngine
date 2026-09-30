#include "Automation/AutomationCaptureService.h"
#include "EditorApp.h"

#include "Framework/Common/Log.h"
#include "Framework/Common/Profiler.h"
#include "Project/ProjectManager.h"
#include "Render/GraphicsContext.h"
#include "Render/GpuResource.h"
#include "Render/ImageCapture/Adapters/RenderGraphImageCaptureBridge.h"
#include "Render/ImageCapture/ImageCaptureService.h"
#include "Render/RenderingStates.h"
#include "Render/RenderPipeline.h"
#include "Render/Renderer.h"
#include "Render/Features/RayTraceGI.h"
#include "Scene/Component.h"
#include "Scene/Scene.h"

#include <filesystem>
#include <format>

namespace Ailu
{
    namespace Editor
    {
        using namespace Render;

        namespace
        {
            // 会话信息由 EditorLayer 在开始抓帧时登记，自动化侧只读。
            struct ActiveCaptureSession
            {
                ImageCaptureSessionId _session_id = 0u;
                String _pass_name;
                String _resource_name;
                WString _output_directory;
                u32 _frame_count = 0u;
            };

            ActiveCaptureSession &SessionSlot()
            {
                static ActiveCaptureSession s_session;
                return s_session;
            }

            String StateToString(EImageCaptureState state)
            {
                switch (state)
                {
                    case EImageCaptureState::kPending: return "pending";
                    case EImageCaptureState::kCapturing: return "capturing";
                    case EImageCaptureState::kFinalizing: return "finalizing";
                    case EImageCaptureState::kCompleted: return "completed";
                    case EImageCaptureState::kCancelled: return "cancelled";
                    case EImageCaptureState::kFailed: return "failed";
                    default: return "unknown";
                }
            }

            String ResolveProjectRoot()
            {
                return ProjectManager::Get().HasOpenedProject()
                           ? ToChar(ProjectManager::Get().CurrentProject().RootDirectory())
                           : String{};
            }

            AutomationObject BuildStatusObject()
            {
                ActiveCaptureSession &session = SessionSlot();
                AutomationObject result;
                result.emplace("session_id", AutomationValue(static_cast<u64>(session._session_id)));
                result.emplace("pass_name", AutomationValue(session._pass_name));
                result.emplace("resource_name", AutomationValue(session._resource_name));
                result.emplace("requested_frames", AutomationValue(session._frame_count));
                if (session._session_id == 0u)
                {
                    result.emplace("state", AutomationValue(String("idle")));
                    return result;
                }

                const ImageCaptureStatus status = ImageCaptureService::GetStatus(session._session_id);
                result.emplace("state", AutomationValue(StateToString(status._state)));
                result.emplace("completed_frames", AutomationValue(status._completed_frame_count));
                result.emplace("in_flight_frames", AutomationValue(status._in_flight_count));
                result.emplace("error", AutomationValue(status._error));

                const String project_root = ResolveProjectRoot();
                const String relative = ToChar(session._output_directory);
                const String absolute = project_root.empty()
                                            ? relative
                                            : (std::filesystem::path(project_root) / relative).string();
                result.emplace("output_directory", AutomationValue(absolute));

                // 列出已落盘的图片，便于调用方直接读文件。
                AutomationArray files;
                std::error_code ec;
                if (std::filesystem::is_directory(absolute, ec))
                {
                    for (const auto &entry : std::filesystem::directory_iterator(absolute, ec))
                    {
                        if (!entry.is_regular_file())
                            continue;
                        const String extension = entry.path().extension().string();
                        if (extension != ".png" && extension != ".dds")
                            continue;
                        files.emplace_back(AutomationValue(entry.path().string()));
                    }
                }
                result.emplace("files", AutomationValue(std::move(files)));
                return result;
            }
        }// namespace

        void AutomationCaptureService::Register(EditorAutomationRegistry &registry)
        {
            {
                AutomationMethodDesc desc;
                desc._name = "render.gpu_timings";
                desc._description = "Read GPU panel rolling-average pass times in milliseconds; values may lag rendering.";
                desc._permission = EAutomationPermission::kReadOnly;
                registry.Register("render.gpu_timings", std::move(desc),
                    [](EditorAutomationContext &, const AutomationObject &) -> AutomationResult
                    {
                        AutomationObject timings;
                        for (const auto &[name, time] : Profiler::Get().GetPassGPUTime())
                            timings.emplace(name, AutomationValue(static_cast<double>(time)));
                        return AutomationResult::Ok(AutomationValue(std::move(timings)));
                    });
            }
            {
                AutomationMethodDesc desc;
                desc._name = "render.surfel_spawn_once";
                desc._description = "Request one manual placement batch on the active Surfel GI component.";
                desc._permission = EAutomationPermission::kExternalEffect;
                desc._input_schema.AddParam("capture_pix", "Request a silent PIX capture with this placement batch.",
                                            EAutomationValueType::kBool);
                registry.Register("render.surfel_spawn_once", std::move(desc),
                                  [](EditorAutomationContext &, const AutomationObject &arguments) -> AutomationResult
                {
                    auto *scene = SceneManagement::SceneMgr::Get().ActiveScene();
                    if (!scene)
                        return AutomationResult::Fail(AutomationError::Make(
                            AutomationErrors::kSceneNotOpen, "no scene is open"));
                    auto &reg = scene->GetRegister();
                    u32 component_index = 0u;
                    for (auto &component : reg.View<ECS::SurfelGIComponent>())
                    {
                        const auto entity = reg.GetEntity<ECS::SurfelGIComponent>(component_index++);
                        if (!scene->IsEntityEnabled(entity) ||
                            !reg.IsComponentEnabled<ECS::SurfelGIComponent>(entity) || !component._enabled)
                            continue;
                        component._manual_spawn = true;
                        component._spawn_once_requested = true;
                        const auto capture_it = arguments.find("capture_pix");
                        if (capture_it != arguments.end() && capture_it->second.AsBool())
                            GraphicsContext::Get().TakePixCapture(false);
                        return AutomationResult::Ok(AutomationValue(AutomationObject{
                            {"spawn_requested", AutomationValue(true)}}));
                    }
                    return AutomationResult::Fail(AutomationError::Make(
                        AutomationErrors::kTargetNotFound, "no enabled Surfel GI component"));
                });
            }
            {
                AutomationMethodDesc desc;
                desc._name = "render.capture_pass";
                desc._description =
                    "Capture the output (or input) of a render graph pass for N frames and write it to disk "
                    "as PNG. Returns a session id plus the output directory; poll render.capture_status "
                    "until state is completed.";
                desc._permission = EAutomationPermission::kExternalEffect;
                desc._input_schema.AddParam("pass_name",
                                            "Render graph pass name, e.g. 'RayTrace GI DXR' or 'RayTrace GI'.",
                                            EAutomationValueType::kString, true);
                desc._input_schema.AddParam("resource_name",
                                            "Expected resource name; empty accepts any resource of that pass.",
                                            EAutomationValueType::kString);
                desc._input_schema.AddParam("pass_ordinal",
                                            "Ordinal among passes sharing the same name (0 based).",
                                            EAutomationValueType::kInteger);
                desc._input_schema.AddParam("access_index",
                                            "Output (or input) access index of the pass.",
                                            EAutomationValueType::kInteger);
                desc._input_schema.AddParam("capture_output",
                                            "True captures the pass output, false captures an input.",
                                            EAutomationValueType::kBool);
                desc._input_schema.AddParam("camera_ordinal",
                                            "Camera ordinal when the pipeline runs once per camera.",
                                            EAutomationValueType::kInteger);
                desc._input_schema.AddParam("frame_count", "Number of frames to capture.",
                                            EAutomationValueType::kInteger);
                desc._input_schema.AddParam("file_prefix", "Output file prefix.",
                                            EAutomationValueType::kString);
                desc._input_schema.AddParam("output_directory",
                                            "Output directory relative to the project root.",
                                            EAutomationValueType::kString);
                desc._input_schema.AddParam("output_format", "png (default) or dds_native for linear floating-point validation.", EAutomationValueType::kString);
                registry.Register("render.capture_pass", std::move(desc), HandleCapturePass);
            }
            {
                AutomationMethodDesc desc;
                desc._name = "render.capture_status";
                desc._description = "Query the active capture session state and the files it has written.";
                desc._permission = EAutomationPermission::kReadOnly;
                registry.Register("render.capture_status", std::move(desc), HandleCaptureStatus);
            }
            {
                AutomationMethodDesc desc;
                desc._name = "render.submission_stats";
                desc._description =
                    "Read the last completed frame's CPU render submission counters (material binding cache, "
                    "material cbuffer frame cache, GPU resource resolve). Used to validate render hot-path work.";
                desc._permission = EAutomationPermission::kReadOnly;
                registry.Register("render.submission_stats", std::move(desc),
                    [](EditorAutomationContext &, const AutomationObject &) -> AutomationResult
                    {
                        const auto &ds = RenderingStates::DisplayData();
                        const auto gpu_resolve = GpuResourceRegistry::GetResolveStats();
                        const auto registry_sync = GpuResourceRegistry::Get().GetSyncStats();
                        auto counter = [](u64 value) { return AutomationValue(static_cast<double>(value)); };
                        AutomationObject stats{
                            {"material_capture_count", counter(ds.MaterialCaptureCount)},
                            {"material_binding_full_rebuild_count", counter(ds.MaterialBindingResolveCount)},
                            {"material_binding_global_update_count", counter(ds.MaterialBindingGlobalUpdateCount)},
                            {"material_binding_cache_hit_count", counter(ds.MaterialBindingCacheHitCount)},
                            {"material_cbuffer_upload_count", counter(ds.MaterialCBufferUploadCount)},
                            {"material_cbuffer_upload_bytes", counter(ds.MaterialCBufferUploadBytes)},
                            {"material_cbuffer_cache_hit_count", counter(ds.MaterialCBufferCacheHitCount)},
                            {"draw_command_count", counter(ds.DrawCommandCount)},
                            {"frame_rate_fps", AutomationValue(static_cast<double>(ds.FrameRate))},
                            {"draw_sort_item_count", counter(ds.DrawSortItemCount)},
                            {"draw_sort_compare_count", counter(ds.DrawSortCompareCount)},
                            {"draw_batch_compare_count", counter(ds.DrawBatchCompareCount)},
                            {"draw_state_bucket_count", counter(ds.DrawStateBucketCount)},
                            {"draw_batch_count", counter(ds.DrawBatchCount)},
                            {"draw_instanced_batch_count", counter(ds.DrawInstancedBatchCount)},
                            {"draw_instanced_primitive_count", counter(ds.DrawInstancedPrimitiveCount)},
                            {"draw_non_contiguous_batch_count", counter(ds.DrawNonContiguousBatchCount)},
                            {"pipeline_resource_override_count", counter(ds.PipelineResourceOverrideCount)},
                            {"gpu_resource_resolve_count", counter(gpu_resolve._total_resolve_count)},
                            {"gpu_resource_resolve_invalid_count", counter(gpu_resolve._invalid_handle_resolve_count)},
                            {"gpu_resource_resolve_locked_count", counter(gpu_resolve._locked_resolve_count)},
                            {"gpu_registry_sync_count", counter(registry_sync._sync_count)},
                            {"gpu_registry_applied_add_count", counter(registry_sync._applied_add_count)},
                            {"gpu_registry_applied_retire_count", counter(registry_sync._applied_retire_count)},
                            {"gpu_registry_collected_resource_count", counter(registry_sync._collected_resource_count)},
                            {"gpu_registry_max_pending_add_count", counter(registry_sync._max_pending_add_count)},
                            {"gpu_registry_max_pending_retire_count", counter(registry_sync._max_pending_retire_count)},
                            {"cpu_render_recording_ms", counter(static_cast<u64>(ds.CpuRenderRecordingTimeMs * 1000.0f))},
                            {"cpu_render_recording_graph_count", counter(ds.CpuRenderRecordingFrameCount)},
                        };
                        return AutomationResult::Ok(AutomationValue(std::move(stats)));
                    });
            }
            {
                AutomationMethodDesc desc;
                desc._name = "render.set_feature_active";
                desc._description =
                    "Enable or disable a render feature by name (for example RayTraceGI). Some features, "
                    "such as RayTraceGI, only register their render graph passes while active, so they must "
                    "be turned on before render.capture_pass can target them.";
                desc._permission = EAutomationPermission::kExternalEffect;
                desc._input_schema.AddParam("name", "Render feature name, e.g. 'RayTraceGI'.",
                                            EAutomationValueType::kString, true);
                desc._input_schema.AddParam("active", "True to activate, false to deactivate.",
                                            EAutomationValueType::kBool, true);
                registry.Register("render.set_feature_active", std::move(desc), HandleSetFeatureActive);
            }
            {
                AutomationMethodDesc desc;
                desc._name = "render.list_features";
                desc._description = "List render features with their active state.";
                desc._permission = EAutomationPermission::kReadOnly;
                registry.Register("render.list_features", std::move(desc), HandleListFeatures);
            }
            {
                AutomationMethodDesc desc;
                desc._name = "render.path_tracer_settings";
                desc._description = "Read or change runtime PathTracer settings and SceneView output size for render validation; does not save the scene.";
                desc._permission = EAutomationPermission::kExternalEffect;
                desc._input_schema.AddParam("enabled", "Enable PathTracer rendering.", EAutomationValueType::kBool);
                desc._input_schema.AddParam("hardware_ray_tracing", "Use DXR instead of Compute.", EAutomationValueType::kBool);
                desc._input_schema.AddParam("temporal", "Enable temporal accumulation.", EAutomationValueType::kBool);
                desc._input_schema.AddParam("accumulation_mode", "disabled, reference or realtime accumulation.",
                                            EAutomationValueType::kString);
                desc._input_schema.AddParam("use_power_weighted_light_sampling", "Use power-weighted Alias light selection; false selects Uniform.", EAutomationValueType::kBool);
                desc._input_schema.AddParam("enable_mnee", "Enable PathTracer MNEE.", EAutomationValueType::kBool);
                desc._input_schema.AddParam("enable_ris", "Enable PathTracer light RIS.", EAutomationValueType::kBool);
                desc._input_schema.AddParam("enable_resampling", "Enable PathTracer reservoir resampling.", EAutomationValueType::kBool);
                desc._input_schema.AddParam("enable_environment_nee", "Use Environment NEE/MIS; false selects the BSDF-only baseline.", EAutomationValueType::kBool);
                desc._input_schema.AddParam("debug_target", "0 final, 1 raw, 2 prev, 3 curr, 4 validity, 5 length.",
                                            EAutomationValueType::kInteger);
                desc._input_schema.AddParam("output_size", "SceneView render dimensions [width,height], each 16 to 4095.", EAutomationValueType::kVector);
                registry.Register("render.path_tracer_settings", std::move(desc), [](EditorAutomationContext &, const AutomationObject &arguments)
                {
                    auto *pipeline = RenderPipeline::Instance();
                    auto *renderer = pipeline != nullptr ? pipeline->GetRenderer() : nullptr;
                    auto *editor = EditorApp::GetEditor();
                    auto *camera = editor != nullptr ? editor->GetSceneCamera() : nullptr;
                    RayTraceGI *path_tracer = nullptr;
                    if (renderer != nullptr)
                        for (auto *feature : renderer->GetFeatures())
                            if (feature->Name() == "RayTraceGI")
                                path_tracer = dynamic_cast<RayTraceGI *>(feature);
                    if (path_tracer == nullptr || camera == nullptr)
                        return AutomationResult::Fail(AutomationErrors::kEditorNotReady, "PathTracer or SceneView camera is unavailable");
                    for (const char *key : {"enabled", "hardware_ray_tracing", "temporal", "enable_mnee",
                                             "enable_ris", "enable_resampling", "enable_environment_nee"})
                        if (const auto it = arguments.find(key); it != arguments.end() && !it->second.IsBool())
                            return AutomationResult::Fail(AutomationErrors::kInvalidArgument, std::format("{} must be a boolean", key));
                    if (const auto it = arguments.find("debug_target"); it != arguments.end() &&
                        (!it->second.IsInt() || it->second.AsInt() < 0 || it->second.AsInt() > 5))
                        return AutomationResult::Fail(AutomationErrors::kInvalidArgument, "debug_target must be 0 to 5");
                    if (const auto it = arguments.find("accumulation_mode"); it != arguments.end() &&
                        (!it->second.IsString() || (it->second.AsString() != "disabled" &&
                         it->second.AsString() != "reference" && it->second.AsString() != "realtime")))
                        return AutomationResult::Fail(AutomationErrors::kInvalidArgument,
                                                      "accumulation_mode must be disabled, reference or realtime");
                    auto output_size = camera->OutputSize();
                    if (const auto it = arguments.find("output_size"); it != arguments.end())
                    {
                        if (!it->second.IsArray() || it->second.AsArray().size() != 2u)
                            return AutomationResult::Fail(AutomationErrors::kInvalidArgument, "output_size must be [width,height]");
                        const auto &values = it->second.AsArray();
                        for (const auto &value : values)
                            if (!value.IsInt() || value.AsInt() < 16 || value.AsInt() > 4095)
                                return AutomationResult::Fail(AutomationErrors::kInvalidArgument, "output dimensions must be integers from 16 to 4095");
                        output_size = {static_cast<u32>(values[0].AsInt()), static_cast<u32>(values[1].AsInt())};
                    }
                    const bool hardware_supported = GraphicsContext::Get().IsHardwareRayTracingSupported();
                    if (arguments.contains("hardware_ray_tracing") && arguments.at("hardware_ray_tracing").AsBool() && !hardware_supported)
                        return AutomationResult::Fail(AutomationErrors::kInvalidArgument, "hardware ray tracing is unsupported");
                    if (arguments.contains("enabled")) renderer->_is_use_raytracing = arguments.at("enabled").AsBool();
                    if (arguments.contains("hardware_ray_tracing")) path_tracer->_use_hardware_ray_tracing = arguments.at("hardware_ray_tracing").AsBool();
                    if (arguments.contains("temporal"))
                        path_tracer->_accumulation_mode = arguments.at("temporal").AsBool()
                            ? Render::EPathTracerAccumulationMode::kRealtime
                            : Render::EPathTracerAccumulationMode::kDisabled;
                    if (arguments.contains("accumulation_mode"))
                    {
                        const auto &mode = arguments.at("accumulation_mode").AsString();
                        path_tracer->_accumulation_mode = mode == "reference"
                            ? Render::EPathTracerAccumulationMode::kReference
                            : mode == "realtime" ? Render::EPathTracerAccumulationMode::kRealtime
                                                  : Render::EPathTracerAccumulationMode::kDisabled;
                    }
                    if (arguments.contains("use_power_weighted_light_sampling"))
                        path_tracer->_use_power_weighted_light_sampling = arguments.at("use_power_weighted_light_sampling").AsBool();
                    if (arguments.contains("enable_mnee"))
                        path_tracer->_enable_mnee = arguments.at("enable_mnee").AsBool();
                    if (arguments.contains("enable_ris")) path_tracer->_enable_ris = arguments.at("enable_ris").AsBool();
                    if (arguments.contains("enable_resampling"))
                        path_tracer->_enable_resampling = arguments.at("enable_resampling").AsBool();
                    if (arguments.contains("enable_environment_nee"))
                        path_tracer->_enable_environment_nee = arguments.at("enable_environment_nee").AsBool();
                    if (arguments.contains("debug_target")) path_tracer->_debug_temporal_target = static_cast<u32>(arguments.at("debug_target").AsInt());
                    if (arguments.contains("output_size")) camera->OutputSize(static_cast<u16>(output_size.x), static_cast<u16>(output_size.y));
                    AutomationObject result;
                    result.emplace("enabled", AutomationValue(renderer->_is_use_raytracing));
                    result.emplace("hardware_ray_tracing", AutomationValue(path_tracer->_use_hardware_ray_tracing));
                    result.emplace("hardware_supported", AutomationValue(hardware_supported));
                    const char *mode_name = "disabled";
                    if (path_tracer->_accumulation_mode == Render::EPathTracerAccumulationMode::kReference)
                        mode_name = "reference";
                    else if (path_tracer->_accumulation_mode == Render::EPathTracerAccumulationMode::kRealtime)
                        mode_name = "realtime";
                    result.emplace("temporal", AutomationValue(
                        path_tracer->_accumulation_mode == Render::EPathTracerAccumulationMode::kRealtime));
                    result.emplace("accumulation_mode", AutomationValue(mode_name));
                    result.emplace("use_power_weighted_light_sampling", AutomationValue(path_tracer->_use_power_weighted_light_sampling));
                    result.emplace("enable_mnee", AutomationValue(path_tracer->_enable_mnee));
                    result.emplace("enable_ris", AutomationValue(path_tracer->_enable_ris));
                    result.emplace("enable_resampling", AutomationValue(path_tracer->_enable_resampling));
                    result.emplace("enable_environment_nee", AutomationValue(path_tracer->_enable_environment_nee));
                    result.emplace("debug_target", AutomationValue(path_tracer->_debug_temporal_target));
                    result.emplace("output_size", AutomationValue(AutomationArray{AutomationValue(output_size.x), AutomationValue(output_size.y)}));
                    return AutomationResult::Ok(AutomationValue(std::move(result)));
                });
            }
        }

        AutomationResult AutomationCaptureService::HandleSetFeatureActive(EditorAutomationContext &,
                                                                          const AutomationObject &arguments)
        {
            const auto name_it = arguments.find("name");
            if (name_it == arguments.end() || !name_it->second.IsString())
            {
                return AutomationResult::Fail(AutomationError::Make(
                    AutomationErrors::kInvalidArgument, "name is required"));
            }
            const auto active_it = arguments.find("active");
            const bool active = active_it == arguments.end() || active_it->second.AsBool(true);
            const String target = name_it->second.AsString();

            RenderPipeline *pipeline = RenderPipeline::Instance();
            Renderer *renderer = pipeline != nullptr ? pipeline->GetRenderer() : nullptr;
            if (renderer == nullptr)
            {
                return AutomationResult::Fail(AutomationError::Make(
                    AutomationErrors::kEditorNotReady, "render pipeline or renderer is not available"));
            }

            for (RenderFeature *feature: renderer->GetFeatures())
            {
                if (feature == nullptr || feature->Name() != target)
                    continue;
                feature->SetActive(active);
                LOG_INFO("[Automation] render feature '{}' active={}", target, active);
                AutomationObject result;
                result.emplace("name", AutomationValue(target));
                result.emplace("active", AutomationValue(feature->IsActive()));
                return AutomationResult::Ok(AutomationValue(std::move(result)));
            }

            AutomationError error = AutomationError::Make(
                AutomationErrors::kTargetNotFound, std::format("render feature '{}' was not found", target));
            error._details.emplace("name", AutomationValue(target));
            return AutomationResult::Fail(std::move(error));
        }

        AutomationResult AutomationCaptureService::HandleListFeatures(EditorAutomationContext &,
                                                                      const AutomationObject &)
        {
            AutomationArray features;
            RenderPipeline *pipeline = RenderPipeline::Instance();
            Renderer *renderer = pipeline != nullptr ? pipeline->GetRenderer() : nullptr;
            if (renderer != nullptr)
            {
                for (RenderFeature *feature: renderer->GetFeatures())
                {
                    if (feature == nullptr)
                        continue;
                    AutomationObject entry;
                    entry.emplace("name", AutomationValue(feature->Name()));
                    entry.emplace("active", AutomationValue(feature->IsActive()));
                    features.emplace_back(AutomationValue(std::move(entry)));
                }
            }
            AutomationObject result;
            result.emplace("features", AutomationValue(std::move(features)));
            return AutomationResult::Ok(AutomationValue(std::move(result)));
        }

        AutomationResult AutomationCaptureService::HandleCapturePass(EditorAutomationContext &,
                                                                     const AutomationObject &arguments)
        {
            const auto arg_string = [&arguments](const char *key, const String &fallback = String{}) -> String
            {
                const auto it = arguments.find(key);
                return it != arguments.end() && it->second.IsString() ? it->second.AsString() : fallback;
            };
            const auto arg_int = [&arguments](const char *key, i64 fallback) -> i64
            {
                const auto it = arguments.find(key);
                return it != arguments.end() && it->second.IsInt() ? it->second.AsInt() : fallback;
            };
            const auto arg_bool = [&arguments](const char *key, bool fallback) -> bool
            {
                const auto it = arguments.find(key);
                return it != arguments.end() && it->second.IsBool() ? it->second.AsBool() : fallback;
            };

            const String pass_name = arg_string("pass_name");
            if (pass_name.empty())
            {
                return AutomationResult::Fail(AutomationError::Make(
                    AutomationErrors::kInvalidArgument, "pass_name is required"));
            }

            const String output_format = arg_string("output_format", "png");
            if (output_format != "png" && output_format != "dds_native")
                return AutomationResult::Fail(AutomationErrors::kInvalidArgument, "output_format must be png or dds_native");
            ActiveCaptureSession &session = SessionSlot();
            // 旧会话先收尾，避免选择器残留。
            if (session._session_id != 0u)
            {
                ImageCaptureService::Cancel(session._session_id);
                RenderGraphImageCaptureBridge::RemoveSelector(session._session_id);
                session = {};
            }

            const i64 frame_count = std::max<i64>(1, arg_int("frame_count", 8));
            const String file_prefix = arg_string("file_prefix", "capture");
            const String output_directory = arg_string("output_directory", "Saved/RenderCapture/Automation");

            ImageCaptureSequenceDesc desc;
            desc._frame_count = static_cast<u32>(frame_count);
            desc._frame_interval = 1u;
            desc._output_format = output_format == "dds_native" ? EImageCaptureOutputFormat::kDdsNative : EImageCaptureOutputFormat::kPng;
            desc._file_prefix = file_prefix;
            desc._output_directory = ToWChar(output_directory);
            const ImageCaptureSessionId session_id = ImageCaptureService::BeginSequence(desc);
            if (session_id == 0u)
            {
                return AutomationResult::Fail(AutomationError::Make(
                    AutomationErrors::kCommandFailed, "ImageCaptureService::BeginSequence failed"));
            }

            RenderGraphCaptureSelector selector;
            selector._pass_name = pass_name;
            selector._pass_name_ordinal = static_cast<u32>(std::max<i64>(0, arg_int("pass_ordinal", 0)));
            selector._resource_name = arg_string("resource_name");
            selector._input_access_index = static_cast<u32>(std::max<i64>(0, arg_int("access_index", 0)));
            selector._capture_output = arg_bool("capture_output", true);
            selector._camera_ordinal = static_cast<u32>(std::max<i64>(0, arg_int("camera_ordinal", 0)));
            RenderGraphImageCaptureBridge::SetSelector(session_id, std::move(selector));

            session._session_id = session_id;
            session._pass_name = pass_name;
            session._resource_name = arg_string("resource_name");
            session._frame_count = static_cast<u32>(frame_count);
            {
                const WString root = desc._output_directory.empty() ? L"Saved/RenderCapture" : desc._output_directory;
                session._output_directory = std::filesystem::path(root) / std::format(L"session_{}", session_id);
            }

            LOG_INFO("[Automation] render.capture_pass armed session {} for pass '{}'", session_id, pass_name);
            return AutomationResult::Ok(AutomationValue(BuildStatusObject()));
        }

        AutomationResult AutomationCaptureService::HandleCaptureStatus(EditorAutomationContext &,
                                                                      const AutomationObject &)
        {
            return AutomationResult::Ok(AutomationValue(BuildStatusObject()));
        }
    }// namespace Editor
}// namespace Ailu
