#include "Automation/AutomationSceneService.h"

#include "Automation/AutomationAdapter.h"
#include "Automation/AutomationReadModel.h"
#include "Common/CameraControllers.h"
#include "Common/Selection.h"
#include "EditorApp.h"
#include "Framework/Common/Application.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/Utils.h"
#include "Project/ProjectManager.h"
#include "Render/Camera.h"
#include "Render/GraphicsContext.h"
#include "Render/Material.h"
#include "Render/Mesh.h"
#include "Scene/Component.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <tuple>

namespace Ailu
{
    namespace Editor
    {
        namespace
        {
            using namespace SceneManagement;

            Scene *ActiveScene()
            {
                return SceneMgr::Get().ActiveScene();
            }

            String BuildHierarchyPath(Scene *scene, ECS::Entity entity)
            {
                auto &reg = scene->GetRegister();
                Vector<String> parts;
                ECS::Entity current = entity;
                u32 guard = 0u;
                while (current != ECS::kInvalidEntity && !scene->IsSceneRoot(current) && guard++ < 1024u)
                {
                    const ECS::TagComponent *tag = reg.GetComponent<ECS::TagComponent>(current);
                    parts.emplace_back(tag ? tag->_name : String{});
                    const ECS::CHierarchy *hierarchy = reg.GetComponent<ECS::CHierarchy>(current);
                    current = hierarchy ? hierarchy->_parent : ECS::kInvalidEntity;
                }
                String path = "/";
                bool first = true;
                for (auto it = parts.rbegin(); it != parts.rend(); ++it)
                {
                    if (!first)
                        path += "/";
                    path += *it;
                    first = false;
                }
                return path;
            }

            String EntityName(Scene *scene, ECS::Entity entity)
            {
                const ECS::TagComponent *tag = scene->GetRegister().GetComponent<ECS::TagComponent>(entity);
                return tag ? tag->_name : String{};
            }

            String EntityGuidString(Scene *scene, ECS::Entity entity)
            {
                const Guid *guid = scene->FindEntityGuid(entity);
                return guid ? guid->ToString() : String{};
            }

            AutomationValue ToAutomationVector3(const Vector3f &value)
            {
                return AutomationValue(AutomationArray{
                    AutomationValue(value.x), AutomationValue(value.y), AutomationValue(value.z)});
            }

            bool TryReadVector3(const AutomationValue &value, Vector3f &result)
            {
                if (!value.IsArray() || value.AsArray().size() != 3u)
                    return false;

                const AutomationArray &array = value.AsArray();
                if (!array[0].IsNumber() || !array[1].IsNumber() || !array[2].IsNumber())
                    return false;

                result = Vector3f(static_cast<f32>(array[0].AsFloat()),
                                  static_cast<f32>(array[1].AsFloat()),
                                  static_cast<f32>(array[2].AsFloat()));
                return std::isfinite(result.x) && std::isfinite(result.y) && std::isfinite(result.z);
            }

            bool TryReadFloat(const AutomationObject &arguments, StringView name, f32 &result)
            {
                const auto it = arguments.find(String(name));
                if (it == arguments.end())
                    return false;
                if (!it->second.IsNumber())
                    return false;

                result = static_cast<f32>(it->second.AsFloat());
                return std::isfinite(result);
            }

            AutomationValue ToAutomationPhysicalSettings(const Render::PhysicalCameraSettings &settings)
            {
                return AutomationValue(AutomationObject{
                    {"sensor_width_mm", AutomationValue(settings._sensor_width_mm)},
                    {"sensor_height_mm", AutomationValue(settings._sensor_height_mm)},
                    {"focal_length_mm", AutomationValue(settings._focal_length_mm)},
                    {"aperture_f_number", AutomationValue(settings._aperture_f_number)},
                    {"shutter_seconds", AutomationValue(settings._shutter_seconds)},
                    {"iso", AutomationValue(settings._iso)},
                    {"focus_distance_m", AutomationValue(settings._focus_distance_m)},
                    {"exposure_compensation_ev", AutomationValue(settings._exposure_compensation_ev)},
                    {"lens_projection_mode", AutomationValue(static_cast<i32>(settings._lens_projection_mode))},
                    {"exposure_mode", AutomationValue(static_cast<i32>(settings._exposure_mode))}});
            }

            bool TryReadPhysicalSettings(const AutomationValue &value, Render::PhysicalCameraSettings &settings,
                                         String &error)
            {
                if (!value.IsObject())
                {
                    error = "physical_settings must be an object";
                    return false;
                }

                const AutomationObject &values = value.AsObject();
                const auto read_float = [&values, &error](StringView name, f32 &destination)
                {
                    const auto it = values.find(String(name));
                    if (it == values.end())
                        return true;
                    if (!it->second.IsNumber() || !std::isfinite(it->second.AsFloat()))
                    {
                        error = std::format("physical_settings.{} must be a finite number", name);
                        return false;
                    }
                    const f32 parsed_value = static_cast<f32>(it->second.AsFloat());
                    if (!std::isfinite(parsed_value))
                    {
                        error = std::format("physical_settings.{} is outside the supported range", name);
                        return false;
                    }
                    destination = parsed_value;
                    return true;
                };
                if (!read_float("sensor_width_mm", settings._sensor_width_mm) ||
                    !read_float("sensor_height_mm", settings._sensor_height_mm) ||
                    !read_float("focal_length_mm", settings._focal_length_mm) ||
                    !read_float("aperture_f_number", settings._aperture_f_number) ||
                    !read_float("shutter_seconds", settings._shutter_seconds) ||
                    !read_float("iso", settings._iso) ||
                    !read_float("focus_distance_m", settings._focus_distance_m) ||
                    !read_float("exposure_compensation_ev", settings._exposure_compensation_ev))
                    return false;

                const auto read_enum = [&values, &error](StringView name, i64 maximum, u8 &destination)
                {
                    const auto it = values.find(String(name));
                    if (it == values.end())
                        return true;
                    if (!it->second.IsInt() || it->second.AsInt() < 0 || it->second.AsInt() > maximum)
                    {
                        error = std::format("physical_settings.{} must be an integer from 0 to {}", name, maximum);
                        return false;
                    }
                    destination = static_cast<u8>(it->second.AsInt());
                    return true;
                };
                u8 lens_projection_mode = static_cast<u8>(settings._lens_projection_mode);
                u8 exposure_mode = static_cast<u8>(settings._exposure_mode);
                const i64 max_lens_projection_mode = static_cast<i64>(Render::ELensProjectionMode::kPhysicalFocalLength);
                if (!read_enum("lens_projection_mode", max_lens_projection_mode, lens_projection_mode) ||
                    !read_enum("exposure_mode", static_cast<i64>(Render::EExposureMode::kAutoISO), exposure_mode))
                    return false;
                settings._lens_projection_mode = static_cast<Render::ELensProjectionMode>(lens_projection_mode);
                settings._exposure_mode = static_cast<Render::EExposureMode>(exposure_mode);

                constexpr std::array<StringView, 10> kAllowedProperties = {
                    "sensor_width_mm", "sensor_height_mm", "focal_length_mm", "aperture_f_number",
                    "shutter_seconds", "iso", "focus_distance_m", "exposure_compensation_ev",
                    "lens_projection_mode", "exposure_mode"};
                for (const auto &[name, unused] : values)
                {
                    (void) unused;
                    if (std::find(kAllowedProperties.begin(), kAllowedProperties.end(), name) ==
                        kAllowedProperties.end())
                    {
                        error = std::format("unknown physical_settings property '{}'", name);
                        return false;
                    }
                }
                return true;
            }
        }// namespace

        void AutomationSceneService::Register(EditorAutomationRegistry &registry)
        {
            {
                AutomationMethodDesc desc;
                desc._name = "editor.get_state";
                desc._description = "Return editor state: mode, active scene, revisions and selection.";
                desc._permission = EAutomationPermission::kReadOnly;
                registry.Register("editor.get_state", std::move(desc), HandleGetState);
            }
            {
                AutomationMethodDesc desc;
                desc._name = "editor.get_scene_camera";
                desc._description = "Read the current SceneView camera pose and physical camera settings.";
                desc._permission = EAutomationPermission::kReadOnly;
                desc._input_schema.AddParam("test_boxes", "Native frustum checks: [{min:[x,y,z],max:[x,y,z]}].",
                                            EAutomationValueType::kObject, false, true);
                registry.Register("editor.get_scene_camera", std::move(desc), HandleGetSceneCamera);
            }
            {
                AutomationMethodDesc desc;
                desc._name = "editor.set_scene_camera";
                desc._description =
                    "Set SceneView pose, projection and partial physical camera settings. Set snap=false to move "
                    "toward the target using the SceneView camera controller interpolation.";
                desc._permission = EAutomationPermission::kSafeWrite;
                desc._input_schema.AddParam("position", "World-space target position [x,y,z].", EAutomationValueType::kVector);
                desc._input_schema.AddParam("pitch_degrees", "Target pitch in degrees.", EAutomationValueType::kFloat);
                desc._input_schema.AddParam("yaw_degrees", "Target yaw in degrees.", EAutomationValueType::kFloat);
                desc._input_schema.AddParam("fov_degrees", "Horizontal field of view in degrees (1 to 179).",
                                            EAutomationValueType::kFloat);
                desc._input_schema.AddParam("snap", "Apply position and orientation immediately; defaults to true.",
                                            EAutomationValueType::kBool);
                desc._input_schema.AddParam("aa_mode", "Runtime AA mode: 0 None, 1 FXAA, 2 TAA.",
                                            EAutomationValueType::kInteger);
                desc._input_schema.AddParam("fsr_enabled", "Request FSR Quality for this SceneView camera.", EAutomationValueType::kBool);
                desc._input_schema.AddParam("projection_type", "Runtime projection: 0 orthographic, 1 perspective.",
                                            EAutomationValueType::kInteger);
                desc._input_schema.AddParam("orthographic_size", "Positive orthographic width in world units.",
                                            EAutomationValueType::kFloat);
                desc._input_schema.AddParam("physical_settings",
                    "Partial settings object; mode ordinals: lens 0=LegacyFov/1=PhysicalFocalLength, exposure "
                    "0=LegacyManual/1=ManualPhysical/2=Auto/3=AperturePriority/4=ShutterPriority/5=AutoISO.",
                    EAutomationValueType::kObject);
                registry.Register("editor.set_scene_camera", std::move(desc), HandleSetSceneCamera);
            }
            {
                AutomationMethodDesc desc;
                desc._name = "scene.query_entities";
                desc._description = "Query entities in the active scene by name, required components and hierarchy path.";
                desc._permission = EAutomationPermission::kReadOnly;
                desc._input_schema.AddParam("name", "Entity name filter.", EAutomationValueType::kString);
                desc._input_schema.AddParam("name_match", "Name matching mode: exact, prefix or substring.", EAutomationValueType::kString);
                desc._input_schema.AddParam("required_components", "Stable component type names the entity must have.", EAutomationValueType::kString, false, true);
                desc._input_schema.AddParam("path_prefix", "Hierarchy path prefix filter.", EAutomationValueType::kString);
                desc._input_schema.AddParam("offset", "Pagination offset.", EAutomationValueType::kInteger);
                desc._input_schema.AddParam("limit", "Maximum number of items.", EAutomationValueType::kInteger);
                registry.Register("scene.query_entities", std::move(desc), HandleQueryEntities);
            }
            {
                AutomationMethodDesc desc;
                desc._name = "scene.inspect_entity";
                desc._description = "Inspect an entity by guid: structure, component types and optional property values.";
                desc._permission = EAutomationPermission::kReadOnly;
                desc._input_schema.AddParam("entity_guid", "Persistent guid of the entity.", EAutomationValueType::kGuid, true);
                desc._input_schema.AddParam("include_components", "Include the component list.", EAutomationValueType::kBool);
                desc._input_schema.AddParam("include_properties", "Read all readable component property values.", EAutomationValueType::kBool);
                desc._input_schema.AddParam("include_mesh_data", "Include bounded CPU mesh arrays and resolved material slots.", EAutomationValueType::kBool);
                desc._input_schema.AddParam("include_mesh_summary", "Include mesh counts and material bindings without vertex/index arrays.", EAutomationValueType::kBool);
                desc._input_schema.AddParam("components", "Per-type property requests: [{type, property_paths}].", EAutomationValueType::kObject, false, true);
                registry.Register("scene.inspect_entity", std::move(desc), HandleInspectEntity);
            {
                AutomationMethodDesc desc;
                desc._name = "scene.inspect_entities";
                desc._description = "Inspect a bounded list of entities in one read-only scene snapshot.";
                desc._permission = EAutomationPermission::kReadOnly;
                desc._input_schema.AddParam("entity_guids", "Persistent entity GUIDs; maximum 256.", EAutomationValueType::kGuid, true, true);
                desc._input_schema.AddParam("include_components", "Include the component list.", EAutomationValueType::kBool);
                desc._input_schema.AddParam("include_properties", "Read all readable component property values.", EAutomationValueType::kBool);
                desc._input_schema.AddParam("include_mesh_data", "Include bounded CPU mesh arrays and resolved material slots.", EAutomationValueType::kBool);
                desc._input_schema.AddParam("include_mesh_summary", "Include mesh counts and material bindings without vertex/index arrays.", EAutomationValueType::kBool);
                desc._input_schema.AddParam("include_mesh_data_guids", "Only include mesh arrays for these entity GUIDs.", EAutomationValueType::kGuid, false, true);
                registry.Register("scene.inspect_entities", std::move(desc), HandleInspectEntity);
            }
            }
        }

        AutomationResult AutomationSceneService::HandleGetState(EditorAutomationContext &, const AutomationObject &)
        {
            AutomationObject result;

            String mode = "edit";
            if (Application::Get()._is_playing_mode)
                mode = "play";
            else if (Application::Get()._is_simulate_mode)
                mode = "simulate";
            result.emplace("editor_mode", AutomationValue(std::move(mode)));

            result.emplace("project_path",
                           AutomationValue(ProjectManager::Get().HasOpenedProject()
                                               ? ToChar(ProjectManager::Get().CurrentProject().RootDirectory())
                                               : String{}));

            Scene *scene = ActiveScene();
            if (scene != nullptr)
            {
                result.emplace("scene_guid", AutomationValue(scene->AssetGuid().ToString()));
                result.emplace("scene_name", AutomationValue(scene->Name()));
                result.emplace("scene_structure_revision", AutomationValue(scene->StructureRevision()));
                result.emplace("scene_edit_revision", AutomationValue(scene->EditRevision()));
                result.emplace("entity_count", AutomationValue(scene->EntityNum()));
                result.emplace("tlas_rebuild_count", AutomationValue(static_cast<double>(scene->TlasRebuildCount())));
                result.emplace("tlas_skipped_rebuild_count",
                               AutomationValue(static_cast<double>(scene->TlasSkippedRebuildCount())));
                result.emplace("tlas_node_count", AutomationValue(scene->GetTLASNodeCount()));
            }
            else
            {
                result.emplace("scene_guid", AutomationValue(String{}));
                result.emplace("scene_name", AutomationValue(String{}));
            }

            AutomationArray selection;
            if (scene != nullptr)
            {
                for (ECS::Entity entity : Selection::SelectedEntities())
                {
                    const String guid = EntityGuidString(scene, entity);
                    if (!guid.empty())
                        selection.emplace_back(AutomationValue(guid));
                }
            }
            result.emplace("selection", AutomationValue(std::move(selection)));
            return AutomationResult::Ok(AutomationValue(std::move(result)));
        }

        AutomationResult AutomationSceneService::HandleGetSceneCamera(EditorAutomationContext &,
                                                                        const AutomationObject &arguments)
        {
            EditorApp *editor = EditorApp::GetEditor();
            if (editor == nullptr || editor->GetSceneCamera() == nullptr)
                return AutomationResult::Fail(AutomationErrors::kEditorNotReady, "SceneView camera is unavailable");

            const Render::Camera &camera = *editor->GetSceneCamera();
            const FirstPersonCameraController &controller = editor->GetSceneCameraController();
            const Quaternion &rotation = camera.Rotation();
            AutomationObject result;
            result.emplace("position", ToAutomationVector3(camera.Position()));
            result.emplace("target_position", ToAutomationVector3(controller._target_pos));
            result.emplace("forward", ToAutomationVector3(camera.Forward()));
            result.emplace("rotation", AutomationValue(AutomationArray{
                AutomationValue(rotation.x), AutomationValue(rotation.y),
                AutomationValue(rotation.z), AutomationValue(rotation.w)}));
            result.emplace("pitch_degrees", AutomationValue(controller._rotation.x));
            result.emplace("yaw_degrees", AutomationValue(controller._rotation.y));
            result.emplace("fov_degrees", AutomationValue(camera.FovH()));
            result.emplace("aa_mode", AutomationValue(static_cast<i32>(camera._anti_aliasing)));
            result.emplace("fsr_enabled", AutomationValue(
                    camera.SuperResolutionMode() == Render::ESuperResolutionMode::kFsr));
            result.emplace("fsr_active", AutomationValue(camera.IsFsrSRActive()));
            result.emplace("render_width", AutomationValue(camera.RenderSize().x));
            result.emplace("render_height", AutomationValue(camera.RenderSize().y));
            result.emplace("projection_type", AutomationValue(static_cast<i32>(camera.Type())));
            result.emplace("orthographic_size", AutomationValue(camera.Size()));
            result.emplace("physical_settings", ToAutomationPhysicalSettings(camera.PhysicalSettings()));
            result.emplace("camera_ev100_settings", AutomationValue(camera.ComputeEv100()));
            result.emplace("reference_exposure_multiplier", AutomationValue(camera.ComputeExposureMultiplier()));
            result.emplace("frame", AutomationValue(Render::g_pGfxContext->GetFrameCount()));
            result.emplace("width", AutomationValue(camera.OutputSize().x));
            result.emplace("height", AutomationValue(camera.OutputSize().y));
            const auto matrix_value = [](const Matrix4x4f &matrix)
            {
                AutomationArray values;
                for (u32 row = 0; row < 4u; ++row)
                    for (u32 column = 0; column < 4u; ++column)
                        values.emplace_back(AutomationValue(matrix[row][column]));
                return AutomationValue(std::move(values));
            };
            result.emplace("projection_stable", matrix_value(camera.GetProjNoJitter()));
            result.emplace("vp_stable", matrix_value(camera.GetViewProjNoJitter()));
            result.emplace("vp_render", matrix_value(camera.GetViewProjJittered()));
            result.emplace("ivp_render", matrix_value(MatrixInverse(camera.GetViewProjJittered())));
            result.emplace("vp_previous", matrix_value(camera.GetPrevViewProj()));
            const auto &jitter = camera.GetJitter();
            result.emplace("jitter", AutomationValue(AutomationArray{
                AutomationValue(jitter.x), AutomationValue(jitter.y),
                AutomationValue(jitter.z), AutomationValue(jitter.w)}));
            AutomationArray planes;
            for (const auto &plane : camera.GetViewFrustum()._planes)
                planes.emplace_back(AutomationValue(AutomationArray{
                    AutomationValue(plane._normal.x), AutomationValue(plane._normal.y),
                    AutomationValue(plane._normal.z), AutomationValue(plane._distance)}));
            result.emplace("frustum_planes", AutomationValue(std::move(planes)));
            Vector3f lt, lb, rt, rb;
            camera.GetCornerInWorld(lt, lb, rt, rb);
            result.emplace("render_corners", AutomationValue(AutomationArray{
                ToAutomationVector3(lt), ToAutomationVector3(lb), ToAutomationVector3(rt), ToAutomationVector3(rb)}));
            if (const auto it = arguments.find("test_boxes"); it != arguments.end())
            {
                if (!it->second.IsArray() || it->second.AsArray().size() > 16u)
                    return AutomationResult::Fail(AutomationErrors::kInvalidArgument, "at most 16 test boxes are supported");
                AutomationArray visibility;
                for (const auto &value : it->second.AsArray())
                {
                    Vector3f minimum, maximum;
                    if (!value.IsObject())
                        return AutomationResult::Fail(AutomationErrors::kInvalidArgument, "each test box must be an object");
                    const auto &box = value.AsObject();
                    if (!box.contains("min") || !box.contains("max") ||
                        !TryReadVector3(box.at("min"), minimum) || !TryReadVector3(box.at("max"), maximum) ||
                        minimum.x > maximum.x || minimum.y > maximum.y || minimum.z > maximum.z)
                        return AutomationResult::Fail(AutomationErrors::kInvalidArgument, "invalid test box bounds");
                    visibility.emplace_back(AutomationValue(camera.IsInFrustum(AABB(minimum, maximum))));
                }
                result.emplace("box_visibility", AutomationValue(std::move(visibility)));
            }


            return AutomationResult::Ok(AutomationValue(std::move(result)));
        }

        AutomationResult AutomationSceneService::HandleSetSceneCamera(EditorAutomationContext &context,
                                                                        const AutomationObject &arguments)
        {
            EditorApp *editor = EditorApp::GetEditor();
            if (editor == nullptr || editor->GetSceneCamera() == nullptr)
                return AutomationResult::Fail(AutomationErrors::kEditorNotReady, "SceneView camera is unavailable");

            Render::Camera *camera = editor->GetSceneCamera();
            FirstPersonCameraController &controller = editor->GetSceneCameraController();
            Vector3f position = controller._target_pos;
            f32 pitch = controller._rotation.x;
            f32 yaw = controller._rotation.y;
            f32 fov = camera->FovH();
            f32 orthographic_size = camera->Size();
            i64 aa_mode = static_cast<i32>(camera->_anti_aliasing);
            i64 projection_type = static_cast<i32>(camera->Type());
            Render::PhysicalCameraSettings physical_settings = camera->PhysicalSettings();
            String physical_settings_error;
            if (const auto it = arguments.find("physical_settings"); it != arguments.end() &&
                !TryReadPhysicalSettings(it->second, physical_settings, physical_settings_error))
                return AutomationResult::Fail(AutomationErrors::kInvalidArgument, physical_settings_error);
            for (const auto &[name, maximum, value] :
                 {std::tuple{"aa_mode", 2, &aa_mode}, std::tuple{"projection_type", 1, &projection_type}})
            {
                const auto it = arguments.find(name);
                if (it == arguments.end())
                    continue;
                if (!it->second.IsInt() || it->second.AsInt() < 0 || it->second.AsInt() > maximum)
                    return AutomationResult::Fail(AutomationErrors::kInvalidArgument,
                                                  std::format("{} must be an integer from 0 to {}", name, maximum));
                *value = it->second.AsInt();
            }
            if (const auto it = arguments.find("fsr_enabled"); it != arguments.end() && !it->second.IsBool())
                return AutomationResult::Fail(AutomationErrors::kInvalidArgument, "fsr_enabled must be a boolean");
            bool has_update = arguments.contains("aa_mode") || arguments.contains("projection_type") ||
                              arguments.contains("fsr_enabled") || arguments.contains("physical_settings");

            if (const auto it = arguments.find("position"); it != arguments.end())
            {
                if (!TryReadVector3(it->second, position))
                    return AutomationResult::Fail(AutomationErrors::kInvalidArgument,
                                                  "position must be a finite [x,y,z] vector");
                has_update = true;
            }
            if (arguments.contains("pitch_degrees"))
            {
                if (!TryReadFloat(arguments, "pitch_degrees", pitch))
                    return AutomationResult::Fail(AutomationErrors::kInvalidArgument,
                                                  "pitch_degrees must be finite");
                has_update = true;
            }
            if (arguments.contains("yaw_degrees"))
            {
                if (!TryReadFloat(arguments, "yaw_degrees", yaw))
                    return AutomationResult::Fail(AutomationErrors::kInvalidArgument,
                                                  "yaw_degrees must be finite");
                has_update = true;
            }
            if (arguments.contains("fov_degrees"))
            {
                if (!TryReadFloat(arguments, "fov_degrees", fov) || fov <= 1.0f || fov >= 179.0f)
                    return AutomationResult::Fail(AutomationErrors::kInvalidArgument,
                                                  "fov_degrees must be greater than 1 and less than 179");
                has_update = true;
            }
            if (arguments.contains("orthographic_size"))
            {
                if (!TryReadFloat(arguments, "orthographic_size", orthographic_size) || orthographic_size <= 0.0f)
                    return AutomationResult::Fail(AutomationErrors::kInvalidArgument,
                                                  "orthographic_size must be finite and positive");
                has_update = true;
            }
            if (!has_update)
                return AutomationResult::Fail(AutomationErrors::kInvalidArgument,
                                              "provide pose, fov_degrees, aa_mode or projection_type");

            bool snap = true;
            if (const auto it = arguments.find("snap"); it != arguments.end())
            {
                if (!it->second.IsBool())
                    return AutomationResult::Fail(AutomationErrors::kInvalidArgument, "snap must be a boolean");
                snap = it->second.AsBool();
            }

            if (snap)
                controller.SnapTo(position, pitch, yaw);
            else
            {
                controller.SetTargetPosition(position, true);
                controller.SetTargetRotation(pitch, yaw, true);
            }
            controller._camera_fov_h = fov;
            camera->FovH(fov);
            camera->_anti_aliasing = static_cast<Render::EAntiAliasing>(aa_mode);
            if (arguments.contains("physical_settings"))
            {
                camera->PhysicalSettings(physical_settings);
                if (!arguments.contains("fov_degrees"))
                    fov = camera->FovH();
            }
            if (arguments.contains("fsr_enabled"))
            {
                if (arguments.at("fsr_enabled").AsBool())
                    camera->SuperResolutionMode(Render::ESuperResolutionMode::kFsr);
                else if (camera->SuperResolutionMode() == Render::ESuperResolutionMode::kFsr)
                    camera->SuperResolutionMode(Render::ESuperResolutionMode::kOff);
            }
            camera->Type(static_cast<Render::ECameraType>(projection_type));
            if (arguments.contains("orthographic_size"))
                camera->Size(orthographic_size);
            camera->MarkDirty();
            camera->RecalculateMatrix();
            if (arguments.contains("aa_mode"))
                camera->EnableTemporalDebugFrames();

            return HandleGetSceneCamera(context, AutomationObject{});
        }

        AutomationResult AutomationSceneService::HandleQueryEntities(EditorAutomationContext &, const AutomationObject &arguments)
        {
            Scene *scene = ActiveScene();
            if (scene == nullptr)
                return AutomationResult::Fail(AutomationErrors::kSceneNotOpen, "no scene is open");

            const String name_filter = arguments.contains("name") ? arguments.at("name").AsString() : String{};
            const String name_match = arguments.contains("name_match") ? arguments.at("name_match").AsString() : String{"substring"};
            const String path_prefix = arguments.contains("path_prefix") ? arguments.at("path_prefix").AsString() : String{};
            const i64 offset = arguments.contains("offset") ? arguments.at("offset").AsInt() : 0;
            const i64 limit = arguments.contains("limit") ? arguments.at("limit").AsInt() : 20;

            Vector<String> required_components;
            if (const auto it = arguments.find("required_components"); it != arguments.end() && it->second.IsArray())
            {
                for (const AutomationValue &item : it->second.AsArray())
                    required_components.emplace_back(item.AsString());
            }

            auto &reg = scene->GetRegister();

            // Collect candidate entities through the TagComponent view (every scene entity has one).
            Vector<ECS::Entity> entities;
            u32 index = 0u;
            for (const auto &tag : reg.View<ECS::TagComponent>())
            {
                (void) tag;
                const ECS::Entity entity = reg.GetEntity<ECS::TagComponent>(index++);
                if (!scene->IsSceneRoot(entity))
                    entities.push_back(entity);
            }

            AutomationArray items;
            bool has_more = false;
            i64 to_skip = offset;
            for (ECS::Entity entity : entities)
            {
                const String entity_name = EntityName(scene, entity);

                if (!name_filter.empty())
                {
                    if (name_match == "exact")
                    {
                        if (entity_name != name_filter)
                            continue;
                    }
                    else if (name_match == "prefix")
                    {
                        if (entity_name.rfind(name_filter, 0) != 0)
                            continue;
                    }
                    else
                    {
                        if (entity_name.find(name_filter) == String::npos)
                            continue;
                    }
                }

                if (!required_components.empty())
                {
                    bool has_all = true;
                    for (const String &required : required_components)
                    {
                        bool found = false;
                        for (ECS::ComponentTypeId cid : reg.GetEntityComponentTypes(entity))
                        {
                            if (ComponentStableTypeName(cid) == required)
                            {
                                found = true;
                                break;
                            }
                        }
                        if (!found)
                        {
                            has_all = false;
                            break;
                        }
                    }
                    if (!has_all)
                        continue;
                }

                if (!path_prefix.empty() && BuildHierarchyPath(scene, entity).rfind(path_prefix, 0) != 0)
                    continue;

                if (to_skip > 0)
                {
                    --to_skip;
                    continue;
                }
                if (static_cast<i64>(items.size()) >= limit)
                {
                    has_more = true;
                    break;
                }

                AutomationObject item;
                item.emplace("entity_guid", AutomationValue(EntityGuidString(scene, entity)));
                item.emplace("name", AutomationValue(entity_name));
                item.emplace("hierarchy_path", AutomationValue(BuildHierarchyPath(scene, entity)));
                AutomationArray component_types;
                for (ECS::ComponentTypeId cid : reg.GetEntityComponentTypes(entity))
                    component_types.emplace_back(AutomationValue(ComponentStableTypeName(cid)));
                item.emplace("component_types", AutomationValue(std::move(component_types)));
                items.emplace_back(AutomationValue(std::move(item)));
            }

            AutomationObject result;
            result.emplace("scene_guid", AutomationValue(scene->AssetGuid().ToString()));
            result.emplace("scene_revision", AutomationValue(scene->EditRevision()));
            result.emplace("items", AutomationValue(std::move(items)));
            result.emplace("has_more", AutomationValue(has_more));
            return AutomationResult::Ok(AutomationValue(std::move(result)));
        }

        AutomationResult AutomationSceneService::HandleInspectEntity(EditorAutomationContext &context, const AutomationObject &arguments)
        {
            if (const auto bulk = arguments.find("entity_guids"); bulk != arguments.end())
            {
                if (!bulk->second.IsArray() || bulk->second.AsArray().empty() || bulk->second.AsArray().size() > 256u)
                    return AutomationResult::Fail(AutomationErrors::kInvalidArgument,
                                                  "entity_guids must contain between 1 and 256 GUIDs");
                const bool include_all_mesh_data = arguments.contains("include_mesh_data")
                    && arguments.at("include_mesh_data").AsBool();
                const auto mesh_guids = arguments.find("include_mesh_data_guids");
                if ((include_all_mesh_data && bulk->second.AsArray().size() > 4u)
                    || (mesh_guids != arguments.end() && (!mesh_guids->second.IsArray()
                        || mesh_guids->second.AsArray().size() > 4u)))
                    return AutomationResult::Fail(AutomationErrors::kInvalidArgument,
                                                  "a bulk snapshot may include mesh data for at most 4 entities");
                AutomationArray items;
                items.reserve(bulk->second.AsArray().size());
                for (const AutomationValue &guid : bulk->second.AsArray())
                {
                    if (!guid.IsString())
                        return AutomationResult::Fail(AutomationErrors::kInvalidArgument, "each entity GUID must be a string");
                    AutomationObject single_arguments = arguments;
                    single_arguments.erase("entity_guids");
                    const auto mesh_guids = arguments.find("include_mesh_data_guids");
                    single_arguments.erase("include_mesh_data_guids");
                    bool include_mesh = single_arguments.contains("include_mesh_data")
                        && single_arguments.at("include_mesh_data").AsBool();
                    if (!include_mesh && mesh_guids != arguments.end() && mesh_guids->second.IsArray())
                    {
                        for (const AutomationValue &mesh_guid : mesh_guids->second.AsArray())
                        {
                            if (mesh_guid.IsString() && mesh_guid.AsString() == guid.AsString())
                            {
                                include_mesh = true;
                                break;
                            }
                        }
                    }
                    single_arguments.insert_or_assign("include_mesh_data", AutomationValue(include_mesh));
                    single_arguments.insert_or_assign("entity_guid", guid);
                    AutomationResult inspected = HandleInspectEntity(context, single_arguments);
                    if (!inspected._success)
                        return inspected;
                    items.emplace_back(std::move(inspected._data));
                }
                AutomationObject result;
                result.emplace("items", AutomationValue(std::move(items)));
                return AutomationResult::Ok(AutomationValue(std::move(result)));
            }

            Scene *scene = ActiveScene();
            if (scene == nullptr)
                return AutomationResult::Fail(AutomationErrors::kSceneNotOpen, "no scene is open");

            const String guid_string = arguments.contains("entity_guid") ? arguments.at("entity_guid").AsString() : String{};
            const Guid guid(guid_string);
            if (!guid.IsValid() || guid.IsEmpty())
                return AutomationResult::Fail(AutomationErrors::kInvalidGuid, "entity_guid is missing or invalid");

            const ECS::Entity entity = scene->FindEntity(guid);
            if (entity == ECS::kInvalidEntity)
                return AutomationResult::Fail(AutomationErrors::kEntityNotFound, "entity not found in the active scene");

            const bool include_properties = arguments.contains("include_properties") && arguments.at("include_properties").AsBool();
            const bool include_mesh_data = arguments.contains("include_mesh_data") && arguments.at("include_mesh_data").AsBool();
            const bool include_mesh_summary = arguments.contains("include_mesh_summary") && arguments.at("include_mesh_summary").AsBool();

            // Optional per-type property path requests: components = [{type, property_paths}]
            HashMap<String, Vector<String>> requested_paths;
            if (const auto it = arguments.find("components"); it != arguments.end() && it->second.IsArray())
            {
                for (const AutomationValue &entry : it->second.AsArray())
                {
                    const AutomationObject &obj = entry.AsObject();
                    const String type = obj.contains("type") ? obj.at("type").AsString() : String{};
                    Vector<String> paths;
                    if (const auto pit = obj.find("property_paths"); pit != obj.end() && pit->second.IsArray())
                    {
                        for (const AutomationValue &path : pit->second.AsArray())
                            paths.emplace_back(path.AsString());
                    }
                    if (!type.empty())
                        requested_paths.emplace(type, std::move(paths));
                }
            }

            auto &reg = scene->GetRegister();
            AutomationObject result;
            result.emplace("entity_guid", AutomationValue(guid_string));
            result.emplace("scene_guid", AutomationValue(scene->AssetGuid().ToString()));
            result.emplace("name", AutomationValue(EntityName(scene, entity)));
            result.emplace("hierarchy_path", AutomationValue(BuildHierarchyPath(scene, entity)));
            result.emplace("visible", AutomationValue(scene->IsEntityEnabled(entity)));

            const ECS::TransformComponent *transform = reg.GetComponent<ECS::TransformComponent>(entity);
            if (transform != nullptr && !transform->_world_dirty)
            {
                const Vector3f position = transform->GetPosition();
                const Vector3f scale = transform->GetScale();
                const Quaternion rotation = transform->GetRotation();
                AutomationArray world_matrix;
                const Matrix4x4f &matrix = transform->GetWorldMatrix();
                for (u32 row = 0u; row < 4u; ++row)
                {
                    AutomationArray matrix_row;
                    for (u32 column = 0u; column < 4u; ++column)
                        matrix_row.emplace_back(AutomationValue(matrix[row][column]));
                    world_matrix.emplace_back(AutomationValue(std::move(matrix_row)));
                }
                result.emplace("world_matrix", AutomationValue(std::move(world_matrix)));
                AutomationObject world_transform;
                world_transform.emplace("position", AutomationValue(AutomationArray{
                    AutomationValue(position.x), AutomationValue(position.y), AutomationValue(position.z)}));
                world_transform.emplace("rotation", AutomationValue(AutomationArray{
                    AutomationValue(rotation.x), AutomationValue(rotation.y),
                    AutomationValue(rotation.z), AutomationValue(rotation.w)}));
                world_transform.emplace("scale", AutomationValue(AutomationArray{
                    AutomationValue(scale.x), AutomationValue(scale.y), AutomationValue(scale.z)}));
                result.emplace("world_transform", AutomationValue(std::move(world_transform)));
            }

            const ECS::CHierarchy *hierarchy = reg.GetComponent<ECS::CHierarchy>(entity);
            if (hierarchy != nullptr && hierarchy->_parent != ECS::kInvalidEntity && !scene->IsSceneRoot(hierarchy->_parent))
                result.emplace("parent_guid", AutomationValue(EntityGuidString(scene, hierarchy->_parent)));

            AutomationArray children;
            if (hierarchy != nullptr)
            {
                ECS::Entity child = hierarchy->_first_child;
                u32 guard = 0u;
                while (child != ECS::kInvalidEntity && guard++ < 4096u)
                {
                    children.emplace_back(AutomationValue(EntityGuidString(scene, child)));
                    const ECS::CHierarchy *child_hierarchy = reg.GetComponent<ECS::CHierarchy>(child);
                    child = child_hierarchy ? child_hierarchy->_next_sibling : ECS::kInvalidEntity;
                }
            }
            result.emplace("children_guids", AutomationValue(std::move(children)));

            AutomationArray components;
            for (ECS::ComponentTypeId cid : reg.GetEntityComponentTypes(entity))
            {
                const String stable_type = ComponentStableTypeName(cid);
                void *instance = reg.GetComponentInstance(entity, cid);
                if (instance == nullptr)
                    continue;

                AutomationObject component;
                component.emplace("type", AutomationValue(stable_type));

                const auto path_it = requested_paths.find(stable_type);
                const bool read_all = include_properties || (path_it != requested_paths.end() && !path_it->second.empty());
                const IAutomationTypeAdapter *adapter = AutomationAdapterRegistry::Get().Resolve(stable_type);
                if (read_all && adapter != nullptr)
                {
                    AutomationObject properties;
                    if (path_it != requested_paths.end() && !path_it->second.empty())
                    {
                        for (const String &path : path_it->second)
                        {
                            AutomationResult value = adapter->ReadProperty(stable_type, instance, path);
                            if (value._success)
                                properties.emplace(path, std::move(value._data));
                        }
                    }
                    else
                    {
                        for (const String &path : AutomationReadablePaths(adapter, stable_type))
                        {
                            AutomationResult value = adapter->ReadProperty(stable_type, instance, path);
                            if (value._success)
                                properties.emplace(path, std::move(value._data));
                        }
                    }
                    component.emplace("properties", AutomationValue(std::move(properties)));
                }
                components.emplace_back(AutomationValue(std::move(component)));
            }
            result.emplace("components", AutomationValue(std::move(components)));
            if (include_mesh_data || include_mesh_summary)
            {
                const ECS::StaticMeshComponent *static_mesh = reg.GetComponent<ECS::StaticMeshComponent>(entity);
                if (static_mesh == nullptr || !static_mesh->_p_mesh)
                    return AutomationResult::Fail(AutomationErrors::kInvalidArgument,
                                                  "entity has no readable static mesh");
                const Render::Mesh &mesh = *static_mesh->_p_mesh;
                constexpr size_t kMaxVertices = 4096u;
                constexpr size_t kMaxIndices = 65536u;
                if ((include_mesh_data && (mesh.GetVertices().size() > kMaxVertices || mesh.GetNormals().size() > kMaxVertices
                    || mesh.GetTangents().size() > kMaxVertices || mesh.GetUVs().size() > kMaxVertices))
                    || mesh.SubmeshCount() > 32u || static_mesh->_p_mats.size() > 32u)
                    return AutomationResult::Fail(AutomationErrors::kInvalidArgument,
                                                  "mesh exceeds the read-only inspection limits");
                AutomationObject mesh_data;
                AutomationArray vertices, normals, tangents, uvs, submeshes, material_slots;
                const auto vector3_array = [](std::span<const Vector3f> values)
                {
                    AutomationArray output;
                    output.reserve(values.size());
                    for (const Vector3f &value : values)
                        output.emplace_back(AutomationValue(AutomationArray{
                            AutomationValue(value.x), AutomationValue(value.y), AutomationValue(value.z)}));
                    return output;
                };
                if (include_mesh_data)
                {
                    vertices = vector3_array(mesh.GetVertices());
                    normals = vector3_array(mesh.GetNormals());
                }
                for (const Vector4f &value : include_mesh_data ? mesh.GetTangents() : std::span<const Vector4f>{})
                    tangents.emplace_back(AutomationValue(AutomationArray{
                        AutomationValue(value.x), AutomationValue(value.y), AutomationValue(value.z), AutomationValue(value.w)}));
                for (const Vector2f &value : include_mesh_data ? mesh.GetUVs() : std::span<const Vector2f>{})
                    uvs.emplace_back(AutomationValue(AutomationArray{AutomationValue(value.x), AutomationValue(value.y)}));
                size_t total_indices = 0u;
                for (u16 submesh_index = 0u; submesh_index < mesh.SubmeshCount(); ++submesh_index)
                {
                    const std::span<const u32> indices = mesh.GetIndices(submesh_index);
                    total_indices += indices.size();
                    if (include_mesh_data && total_indices > kMaxIndices)
                        return AutomationResult::Fail(AutomationErrors::kInvalidArgument,
                                                      "mesh indices exceed the read-only inspection limit");
                    AutomationArray index_values;
                    index_values.reserve(include_mesh_data ? indices.size() : 0u);
                    for (u32 index_value : include_mesh_data ? indices : std::span<const u32>{})
                        index_values.emplace_back(AutomationValue(static_cast<i64>(index_value)));
                    submeshes.emplace_back(AutomationValue(std::move(index_values)));
                }
                for (const Ref<Render::Material> &material : static_mesh->_p_mats)
                {
                    AutomationObject slot;
                    if (material)
                    {
                        const Vector4f base_color = material->GetVector("_AlbedoValue");
                        const String asset_path = ToChar(ResourceMgr::Get().GetAssetPath(material.get()));
                        const size_t asset_name_start = asset_path.find_last_of("/\\");
                        const size_t asset_extension = asset_path.rfind(".alasset");
                        String source_guid;
                        if (asset_extension != String::npos && asset_extension > asset_name_start)
                        {
                            const size_t source_start = asset_name_start == String::npos ? 0u : asset_name_start + 1u;
                            source_guid = asset_path.substr(source_start, asset_extension - source_start);
                        }
                        slot.emplace("name", AutomationValue(material->Name()));
                        slot.emplace("is_fallback", AutomationValue(material == Render::Material::s_checker.lock()));
                        slot.emplace("source_guid", AutomationValue(std::move(source_guid)));
                        slot.emplace("asset_guid", AutomationValue(ResourceMgr::Get().GetAssetGuid(material.get()).ToString()));
                        slot.emplace("base_color", AutomationValue(AutomationArray{
                            AutomationValue(base_color.x), AutomationValue(base_color.y),
                            AutomationValue(base_color.z), AutomationValue(base_color.w)}));
                        slot.emplace("roughness", AutomationValue(material->GetFloat("_RoughnessValue")));
                        slot.emplace("metallic", AutomationValue(material->GetFloat("_MetallicValue")));
                    }
                    material_slots.emplace_back(AutomationValue(std::move(slot)));
                }
                mesh_data.emplace("mesh_guid", AutomationValue(static_mesh->_mesh_guid.ToString()));
                mesh_data.emplace("vertex_count", AutomationValue(static_cast<i64>(mesh.GetVertexCount())));
                mesh_data.emplace("triangle_count", AutomationValue(static_cast<i64>(mesh.GetTriangleCount())));
                mesh_data.emplace("vertices", AutomationValue(std::move(vertices)));
                mesh_data.emplace("normals", AutomationValue(std::move(normals)));
                mesh_data.emplace("tangents", AutomationValue(std::move(tangents)));
                mesh_data.emplace("uv0", AutomationValue(std::move(uvs)));
                mesh_data.emplace("submeshes", AutomationValue(std::move(submeshes)));
                mesh_data.emplace("material_slots", AutomationValue(std::move(material_slots)));
                result.emplace("mesh_data", AutomationValue(std::move(mesh_data)));
            }
            return AutomationResult::Ok(AutomationValue(std::move(result)));
        }
    }// namespace Editor
}// namespace Ailu
