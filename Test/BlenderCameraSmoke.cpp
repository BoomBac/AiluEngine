#include "Assets/AssetDocument.h"
#include "Common/CameraControllers.h"
#include "Framework/Common/Log.h"
#include "Framework/Math/Transform.h"
#include "Objects/JsonArchive.h"
#include "Render/Camera.h"
#include <Ext/rapidjson/inc/document.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace Ailu;
using namespace Ailu::Render;

static void Require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

static Vector3f ReadVector(const rapidjson::Value& value)
{
    return Vector3f(value[0].GetFloat(), value[1].GetFloat(), value[2].GetFloat());
}

int main(int argc, char** argv)
{
    LogMgr::Init();
    try
    {
        Require(argc == 2, "Supply BlenderCameraReference.json");
        std::ifstream file(argv[1]);
        String text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        rapidjson::Document fixtures;
        fixtures.Parse(text.data(), text.size());
        Require(!fixtures.HasParseError() && fixtures.HasMember("cases"), "Invalid Blender reference fixtures");
        f32 max_pixel_error = 0.f;
        u32 count = 0u;
        for (const auto& fixture : fixtures["cases"].GetArray())
        {
            const auto& payload = fixture["payload"];
            Camera camera;
            camera.Type(String(payload["type"].GetString()) == "ORTHO" ? ECameraType::kOrthographic : ECameraType::kPerspective);
            camera.SetLocalViewAxes(Vector3f(0.f, -1.f, 0.f), Vector3f(0.f, 0.f, -1.f));
            camera.ReverseProjectionX(true);
            camera.LockAspect(true);
            camera.Aspect(payload["aspect"].GetFloat());
            camera.FovH(payload["fov_y_degrees"].GetFloat());
            camera.Size(payload["ortho_size"].GetFloat());
            camera.ProjectionOffset(Vector2f(payload["projection_offset"][0].GetFloat(), payload["projection_offset"][1].GetFloat()));
            camera.Near(payload["near_clip"].GetFloat());
            camera.Far(payload["far_clip"].GetFloat());
            const auto read_rotation = [](const rapidjson::Value& q)
            {
                return Quaternion(q[0].GetFloat(), q[1].GetFloat(), q[2].GetFloat(), q[3].GetFloat());
            };
            const Transform parent(ReadVector(fixture["parent_position"]), read_rotation(fixture["parent_rotation"]), Vector3f::kOne);
            const Transform local(ReadVector(fixture["local_position"]), read_rotation(fixture["local_rotation"]), Vector3f::kOne);
            const Transform world = Transform::Combine(parent, local);
            Require(Magnitude(world._position - ReadVector(fixture["position"])) < 1e-5f, "Hierarchy camera position differs from Blender");
            camera.Position(world._position);
            camera.Rotation(world._rotation);
            const auto& viewport = fixture["viewport"];
            const u16 width = static_cast<u16>(viewport[0].GetUint()), height = static_cast<u16>(viewport[1].GetUint());
            camera.OutputSize(width, height);
            camera.SetPixelSize(width, height);
            Require(std::abs(camera.Aspect() - payload["aspect"].GetFloat()) < 1e-6f, "Locked aspect changed on output resize");
            camera.RecalculateMatrix(true);
            Require(Magnitude(camera.Forward() - ReadVector(fixture["forward"])) < 1e-5f, "Camera forward differs from Blender");
            Require(Magnitude(camera.Up() - ReadVector(fixture["up"])) < 1e-5f, "Camera up differs from Blender");
            Camera scene_camera;
            const auto original_type = camera.Type() == ECameraType::kPerspective
                                           ? ECameraType::kOrthographic : ECameraType::kPerspective;
            const bool original_lock_aspect = count % 2u == 0u;
            const f32 original_aspect = original_lock_aspect ? 1.25f : static_cast<f32>(width) / height;
            scene_camera.Type(original_type);
            scene_camera.LockAspect(original_lock_aspect);
            scene_camera.Aspect(original_aspect);
            scene_camera.Size(23.0f);
            scene_camera.ProjectionOffset(Vector2f(0.07f, -0.11f));
            scene_camera.SetPixelSize(width, height);
            scene_camera.OutputSize(width, height);
            Editor::FirstPersonCameraController controller(&scene_camera);
            controller._camera_fov_h = 70.0f;
            controller._camera_near = 0.03f;
            controller._camera_far = 500.0f;
            Require(!controller.RestoreProjection(), "Projection restored without Align");
            controller.SnapTo(camera);
            controller.Interpolate(0.5f);
            Require(Magnitude(scene_camera.Forward() - camera.Forward()) < 1e-5f, "Aligned view forward mismatch");
            Require(Magnitude(scene_camera.Up() - camera.Up()) < 1e-5f, "Aligned view up mismatch");
            Require(controller._camera_fov_h == camera.FovH(), "Aligned controller FOV mismatch");
            Require(controller._camera_near == camera.Near() && controller._camera_far == camera.Far(),
                    "Aligned controller clipping mismatch");
            scene_camera.SetPixelSize(width + 64u, height + 64u);
            Require(scene_camera.Aspect() == camera.Aspect(), "Aligned aspect changed on viewport resize");
            scene_camera.SetPixelSize(width, height);
            scene_camera.OutputSize(width, height);
            scene_camera.RecalculateMatrix(true);
            for (const auto& point : fixture["points"].GetArray())
            {
                const Vector3f world = ReadVector(point["world"]);
                const Vector2f screen = camera.WorldToScreen(world);
                const Vector2f aligned_screen = scene_camera.WorldToScreen(world);
                Require(std::abs(aligned_screen.x - screen.x) < 0.02f && std::abs(aligned_screen.y - screen.y) < 0.02f,
                        "Aligned view projection differs from selected camera");
                const f32 error = std::max(std::abs(screen.x - point["screen"][0].GetFloat()), std::abs(screen.y - point["screen"][1].GetFloat()));
                max_pixel_error = std::max(max_pixel_error, error);
                if (error >= 0.02f) throw std::runtime_error(String(fixture["name"].GetString()) + ": projection pixel mismatch");
                const auto& frustum = camera.GetViewFrustum();
                for (const auto& plane : frustum._planes)
                    Require(DotProduct(plane._normal, world) + plane._distance <= 0.002f, "Inside point rejected by mirrored frustum");
                if (camera.Type() == ECameraType::kPerspective)
                {
                    const auto ray = camera.ScreenToWorld(screen);
                    Require(DotProduct(ray, Normalize(world - camera.Position())) > 0.99999f, "Perspective unprojection differs from Blender");
                }
                else
                {
                    Require(Magnitude(camera.ScreenToWorld(screen, 5.f) - world) < 0.002f, "Orthographic unprojection differs from Blender");
                    Require(DotProduct(camera.ScreenToWorld(screen), camera.Forward()) > 0.99999f, "Orthographic ray direction is reversed");
                }
            }
            // A second Align must retain the original viewport settings, rather than the first camera's lens.
            controller.SnapTo(camera);
            const Vector3f aligned_position = scene_camera.Position();
            const Vector3f aligned_forward = scene_camera.Forward();
            const Vector3f level_right = Quaternion::AngleAxis(controller._rotation.y, Vector3f::kUp) * Vector3f::kRight;
            const Vector3f level_up = Normalize(CrossProduct(aligned_forward, level_right));
            Require(controller.RestoreProjection(), "Aligned projection was not restored");
            Require(scene_camera.Position() == aligned_position && Magnitude(scene_camera.Forward() - aligned_forward) < 1e-5f &&
                    Magnitude(scene_camera.Up() - level_up) < 1e-5f, "Restore delayed roll reset or changed position/direction");
            controller.Interpolate(0.5f);
            Require(scene_camera.Position() == aligned_position && Magnitude(scene_camera.Forward() - aligned_forward) < 1e-5f &&
                    Magnitude(scene_camera.Up() - level_up) < 1e-5f, "Interpolation reintroduced roll after restore");
            Require(scene_camera.Type() == original_type && scene_camera.FovH() == 70.0f &&
                    scene_camera.Near() == 0.03f && scene_camera.Far() == 500.0f && scene_camera.Size() == 23.0f,
                    "Original viewport lens was not restored");
            Require(scene_camera.LockAspect() == original_lock_aspect && scene_camera.Aspect() == original_aspect &&
                    scene_camera.ProjectionOffset() == Vector2f(0.07f, -0.11f) && !scene_camera.ReverseProjectionX(),
                    "Original viewport projection settings were not restored");
            Require(!controller.RestoreProjection(), "Projection restoration was not consumed");
            ++count;
        }
        SceneCameraComponentDocument document;
        document._local_forward = Vector3f(0.f, -1.f, 0.f);
        document._local_up = Vector3f(0.f, 0.f, -1.f);
        document._projection_offset = Vector2f(0.2f, -0.3f);
        document._reverse_projection_x = true;
        document._lock_aspect = true;
        JsonArchive output;
        SerializerWrapper<SceneCameraComponentDocument>::Serialize(&document, output);
        JsonArchive input;
        Require(input.LoadFromString(output.SaveToString()), "Camera document JSON failed");
        SceneCameraComponentDocument restored;
        SerializerWrapper<SceneCameraComponentDocument>::Deserialize(&restored, input);
        Require(restored._local_forward == document._local_forward && restored._local_up == document._local_up
                && restored._projection_offset == document._projection_offset && restored._reverse_projection_x && restored._lock_aspect,
                "Imported camera settings did not roundtrip");
        JsonArchive legacy;
        Require(legacy.LoadFromString(R"({"_type":"Perspective","_aspect":1.7777778,"_fov_h":60})"), "Legacy camera JSON failed");
        SceneCameraComponentDocument old_camera;
        SerializerWrapper<SceneCameraComponentDocument>::Deserialize(&old_camera, legacy);
        Require(old_camera._local_forward == Vector3f::kForward && old_camera._local_up == Vector3f::kUp
                && !old_camera._reverse_projection_x && !old_camera._lock_aspect, "Legacy camera defaults changed");
        std::cout << "BlenderCameraSmoke PASS: " << count << " Blender reference cases, max pixel error=" << max_pixel_error
                  << ", view axes, screen handedness, unprojection, culling, serialization, legacy defaults\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "BlenderCameraSmoke FAIL: " << error.what() << '\n';
        return 1;
    }
}
