"""Generate numerical camera references using Blender's own projection helper; no scene files are saved."""
import importlib.util
import json
import math
import sys
from pathlib import Path
import types

import bpy
from bpy_extras.object_utils import world_to_camera_view
from mathutils import Euler, Matrix, Vector

root = Path(__file__).resolve().parents[1]
package = types.ModuleType("camera_reference_addon")
package.__path__ = [str(root / "Tools/Blender/ailu_live_link")]
sys.modules[package.__name__] = package
camera_sync = importlib.import_module(package.__name__ + ".camera_sync")
coordinate = importlib.import_module(package.__name__ + ".coordinate")
scene = bpy.context.scene
camera = bpy.data.objects.new("CameraReference", bpy.data.cameras.new("CameraReferenceLens"))
scene.collection.objects.link(camera)
parent_object = bpy.data.objects.new("CameraReferenceParent", None)
scene.collection.objects.link(parent_object)
camera.data.lens = 50.0
camera.data.clip_start = 0.1
camera.data.clip_end = 100.0
cases = []
for camera_type in ("PERSP", "ORTHO"):
    for sensor_fit in ("AUTO", "HORIZONTAL", "VERTICAL"):
        for setup, dimensions, pixel_aspect, shift in (
            ("landscape", (1920, 1080), (1.0, 1.0), (0.0, 0.0)),
            ("portrait_shift", (1080, 1920), (1.0, 1.0), (0.17, -0.11)),
            ("non_square_pixels", (1440, 1080), (1.5, 1.0), (-0.12, 0.08)),
        ):
            scene.render.resolution_x, scene.render.resolution_y = dimensions
            scene.render.pixel_aspect_x, scene.render.pixel_aspect_y = pixel_aspect
            camera.data.type = camera_type
            camera.data.sensor_fit = sensor_fit
            camera.data.shift_x, camera.data.shift_y = shift
            camera.data.ortho_scale = 7.0
            for pose in ("identity", "rotated_parent"):
                parent = Matrix.Translation((1.0, 2.0, -3.0)) @ Euler((0.3, -0.4, 0.2)).to_matrix().to_4x4()
                local = Matrix.Translation((-2.0, 1.0, 4.0)) @ Euler((1.1, 0.2, -0.5)).to_matrix().to_4x4()
                if pose == "identity":
                    parent = Matrix.Identity(4)
                    local = Matrix.Identity(4)
                parent_object.matrix_world = parent
                camera.parent = parent_object
                camera.matrix_parent_inverse = Matrix.Identity(4)
                camera.matrix_basis = local
                bpy.context.view_layer.update()
                packets = []
                link = types.SimpleNamespace(send_json=lambda message_type, value: packets.append(value))
                camera_sync.export_camera(camera, scene, link)
                payload = packets[-1]
                position, rotation, scale = coordinate.blender_to_ailu_transform(camera.matrix_world)
                parent_position, parent_rotation, _ = coordinate.blender_to_ailu_transform(parent_object.matrix_world)
                local_position, local_rotation, _ = coordinate.blender_to_ailu_transform(camera.matrix_local)
                frame = camera.data.view_frame(scene=scene)
                xs = [v.x / -v.z for v in frame] if camera_type == "PERSP" else [v.x for v in frame]
                ys = [v.y / -v.z for v in frame] if camera_type == "PERSP" else [v.y for v in frame]
                points = []
                for u, v in ((0.1, 0.2), (0.5, 0.5), (0.8, 0.7)):
                    x = min(xs) + u * (max(xs) - min(xs))
                    y = min(ys) + v * (max(ys) - min(ys))
                    if camera_type == "PERSP":
                        x *= 5.0
                        y *= 5.0
                    world = camera.matrix_world @ Vector((x, y, -5.0))
                    uv = world_to_camera_view(scene, camera, world)
                    assert abs(uv.x - u) < 1e-5 and abs(uv.y - v) < 1e-5
                    points.append({"world": list(coordinate.blender_to_ailu_position(world)),
                                   "screen": [uv.x * dimensions[0], (1.0 - uv.y) * dimensions[1]]})
                forward = coordinate.blender_to_ailu_direction(camera.matrix_world.to_3x3() @ Vector((0, 0, -1)))
                up = coordinate.blender_to_ailu_direction(camera.matrix_world.to_3x3() @ Vector((0, 1, 0)))
                cases.append({"name": camera_type + "_" + sensor_fit + "_" + setup + "_" + pose,
                              "payload": payload, "position": list(position),
                              "rotation": [rotation.x, rotation.y, rotation.z, rotation.w],
                              "parent_position": list(parent_position),
                              "parent_rotation": [parent_rotation.x, parent_rotation.y, parent_rotation.z, parent_rotation.w],
                              "local_position": list(local_position),
                              "local_rotation": [local_rotation.x, local_rotation.y, local_rotation.z, local_rotation.w],
                              "forward": list(forward), "up": list(up), "viewport": dimensions, "points": points})
output = Path(sys.argv[sys.argv.index("--") + 1])
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps({"blender_version": bpy.app.version_string, "cases": cases}, indent=2), encoding="utf-8")
print("BLENDER_CAMERA_REFERENCE_PASS", len(cases), "cases", output)
