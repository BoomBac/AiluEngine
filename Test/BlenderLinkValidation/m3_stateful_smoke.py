"""Exercise Blender Live Link dirty tracking against a real Blender scene.

Run with: blender --background --factory-startup --python m3_stateful_smoke.py -- --output <dir>
The fake link records tracker decisions; it does not claim Engine receiver acceptance.
"""
import json
import sys
import uuid
from pathlib import Path

import bpy


root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / "Tools/Blender"))
from ailu_live_link import dirty_tracker, identity, protocol


arguments = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
if len(arguments) != 2 or arguments[0] != "--output":
    raise SystemExit("Usage: blender --background --factory-startup --python m3_stateful_smoke.py -- --output DIR")
output_dir = Path(arguments[1]).resolve()
output_dir.mkdir(parents=True, exist_ok=True)


class RecordingLink:
    connected = True

    def __init__(self):
        self.messages = []

    def send_json(self, kind, value):
        self.messages.append((kind, value))


scene = bpy.context.scene
for obj in list(scene.objects):
    bpy.data.objects.remove(obj, do_unlink=True)
scene[identity.SESSION_GUID_KEY] = str(uuid.uuid4())
bpy.ops.mesh.primitive_cube_add()
cube = bpy.context.object
cube.name = "M3_StatefulCube"
cube[identity.OBJECT_GUID_KEY] = str(uuid.uuid4())
material = bpy.data.materials.new("M3_StatefulMaterial")
material.diffuse_color = (0.2, 0.3, 0.4, 1.0)
cube.data.materials.append(material)

captured = []
instance_updates = []
original_sync_object = dirty_tracker.sync_object
original_sync_instances = dirty_tracker.sync_instances
original_quiet_seconds = dirty_tracker._quiet_seconds
original_material_quiet_seconds = dirty_tracker._material_quiet_seconds
dirty_tracker.sync_object = lambda context, obj, link, materials, geometry=False, materials_changed=False, data=False: \
    captured.append({"guid": identity.get_guid(obj), "geometry": geometry,
                     "materials": materials_changed, "data": data})
dirty_tracker.sync_instances = lambda *args, **kwargs: instance_updates.append(True)
dirty_tracker._quiet_seconds = 0.0
dirty_tracker._material_quiet_seconds = 0.0
link = RecordingLink()
report = {"status": "FAIL", "checks": [], "source_blend": None}


def check(name, condition, details=None):
    snapshot = json.loads(json.dumps(details)) if details is not None else None
    report["checks"].append({"name": name, "status": "PASS" if condition else "FAIL", "details": snapshot})
    if not condition:
        raise AssertionError(f"{name} failed: {details}")


try:
    dirty_tracker.mark_synced(scene)
    dirty_tracker.flush(bpy.context, link)
    check("idle_no_resend", not captured and not link.messages, {"syncs": len(captured), "messages": len(link.messages)})

    cube.location.x += 1.0
    bpy.context.view_layer.update()
    dirty_tracker.flush(bpy.context, link)
    check("transform_only", len(captured) == 1 and not captured[-1]["geometry"]
          and not captured[-1]["materials"] and not captured[-1]["data"], captured[-1:] or None)

    captured.clear()
    material.diffuse_color = (0.8, 0.1, 0.2, 1.0)
    bpy.context.view_layer.update()
    dirty_tracker.flush(bpy.context, link)
    check("material_edit", len(captured) == 1 and captured[-1]["materials"], captured[-1:] or None)

    captured.clear()
    mesh_update = type("MeshUpdate", (), {"id": type("Id", (), {"original": cube.data,
                                          "bl_rna": type("RNA", (), {"identifier": "Mesh"})()})(),
                                          "is_updated_geometry": True, "is_updated_transform": False,
                                          "is_updated_shading": False})()
    dirty_tracker._depsgraph_update(scene, type("Depsgraph", (), {"updates": [mesh_update]})())
    dirty_tracker.flush(bpy.context, link)
    check("mesh_edit", len(captured) == 1 and captured[-1]["geometry"], captured[-1:] or None)

    captured.clear()
    bpy.ops.mesh.primitive_cube_add(location=(3.0, 0.0, 0.0))
    added = bpy.context.object
    added.name = "M3_AddedCube"
    cube_guid = identity.get_guid(cube)
    bpy.context.view_layer.update()
    dirty_tracker.flush(bpy.context, link)
    added_guid = identity.get_guid(added)
    check("add_object", added_guid != cube_guid and any(item["guid"] == added_guid for item in captured),
          {"added_guid": added_guid, "cube_guid": cube_guid, "syncs": captured})

    captured.clear()
    deleted_guid = identity.get_guid(added)
    bpy.data.objects.remove(added, do_unlink=True)
    bpy.context.view_layer.update()
    dirty_tracker.flush(bpy.context, link)
    deletes = [value for kind, value in link.messages if kind == protocol.OBJECT_DELETE]
    check("delete_object", any(value.get("guid") == deleted_guid for value in deletes), deletes)

    captured.clear()
    dirty_tracker.mark_synced(scene)
    cube.location.y += 2.0
    bpy.context.view_layer.update()
    dirty_tracker.flush(bpy.context, link)
    check("reconnect_requires_delta_from_baseline", len(captured) == 1
          and captured[-1]["guid"] == identity.get_guid(cube), captured)

    source_blend = output_dir / "m3_persistence_copy.blend"
    guids_before_save = {obj.name: identity.get_guid(obj) for obj in scene.objects}
    bpy.ops.wm.save_as_mainfile(filepath=str(source_blend), copy=True)
    bpy.ops.wm.open_mainfile(filepath=str(source_blend))
    guids_after_reload = {obj.name: identity.get_guid(obj) for obj in bpy.context.scene.objects}
    check("save_reopen_guid_stability", guids_after_reload == guids_before_save,
          {"before": guids_before_save, "after": guids_after_reload})
    report["source_blend"] = str(source_blend)
    report["status"] = "PASS"
finally:
    dirty_tracker.sync_object = original_sync_object
    dirty_tracker.sync_instances = original_sync_instances
    dirty_tracker._quiet_seconds = original_quiet_seconds
    dirty_tracker._material_quiet_seconds = original_material_quiet_seconds
    (output_dir / "m3_stateful_smoke.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print("M3_STATEFUL_SMOKE", json.dumps(report), flush=True)
