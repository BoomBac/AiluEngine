"""Host-independent regression checks for reconnect and dirty-set membership."""
import ast
import importlib.util
import pathlib
import sys
import types
import uuid

root = pathlib.Path(__file__).resolve().parents[1]
addon_dir = root / "Tools/Blender/ailu_live_link"
for file in addon_dir.glob("*.py"):
    ast.parse(file.read_text(encoding="utf-8-sig"), filename=str(file))

package = types.ModuleType("ailu_live_link")
package.__path__ = [str(addon_dir)]
sys.modules[package.__name__] = package
bpy = types.ModuleType("bpy")
bpy.app = types.SimpleNamespace(handlers=types.SimpleNamespace(persistent=lambda function: function))
bpy.types = types.SimpleNamespace(Operator=object, Panel=object)
sys.modules["bpy"] = bpy

def module(name):
    value = types.ModuleType("ailu_live_link." + name)
    sys.modules[value.__name__] = value
    return value

sync = module("object_sync")
sent = []
sync._instance_states = {}
sync._object_affine_helpers = set()
sync.sync_object = lambda context, obj, link, materials, *flags: sent.append(obj)
sync.sync_instances = lambda *args, **kwargs: None
sync.full_sync = lambda context, link: sent.append("full")
material = module("material_sync")
material.invalidate_image_cache = lambda: None
bake = module("material_bake")
bake._baking = False
protocol = module("protocol")
protocol.OBJECT_DELETE = 4
connection = module("connection")

from ailu_live_link import identity, dirty_tracker

class Obj(dict):
    def __init__(self, name, guid):
        super().__init__(ailu_live_link_guid=guid)
        self.name = name
        self.name_full = name
        self.type = "MESH"
        self.material_slots = []
        self.matrix_local = ((1.0, 0.0, 0.0, 0.0), (0.0, 1.0, 0.0, 0.0),
                             (0.0, 0.0, 1.0, 0.0), (0.0, 0.0, 0.0, 1.0))

original = Obj("Original", "11111111-1111-4111-8111-111111111111")
duplicate = Obj("Duplicate", identity.get_guid(original))
scene = types.SimpleNamespace(objects=[original, duplicate], render=types.SimpleNamespace(
    resolution_x=1920, resolution_y=1080, pixel_aspect_x=1.0, pixel_aspect_y=1.0))
context = types.SimpleNamespace(scene=scene, mode="OBJECT",
                                window_manager=types.SimpleNamespace(windows=[]))
link = types.SimpleNamespace(connected=True, send_json=lambda *args: sent.append(args), poll=lambda: None)
dirty_tracker._known_guids.add(identity.get_guid(original))
dirty_tracker.flush(context, link)
assert sent == [duplicate], "A repaired duplicate must be added even without a depsgraph event"
assert identity.get_guid(original) != identity.get_guid(duplicate)
sent.clear()
scene.objects.remove(original)
dirty_tracker.flush(context, link)
assert sent == [(protocol.OBJECT_DELETE, {"guid": identity.get_guid(original)})], "Offline membership must detect deletes"

sent.clear()
deleted_guid = "44444444-4444-4444-8444-444444444444"
dirty_tracker._known_guids.add(deleted_guid)
sync._object_affine_helpers.add(deleted_guid)
dirty_tracker.flush(context, link)
assert sent == [(protocol.OBJECT_DELETE, {"guid": deleted_guid}),
                (protocol.OBJECT_DELETE, {"guid": str(uuid.uuid5(uuid.NAMESPACE_URL, deleted_guid + ":affine-parent"))})]
assert deleted_guid not in sync._object_affine_helpers, "Deleting a source must release its affine helper bookkeeping"

sent.clear()
duplicate.type = "CAMERA"
scene.render.resolution_x = 1080
dirty_tracker.flush(context, link)
assert sent == [duplicate], "Render dimension changes must refresh camera projection without a depsgraph event"

from ailu_live_link import addon
connection.connection = link
scene = {identity.SESSION_GUID_KEY: "22222222-2222-4222-8222-222222222222"}
bpy.context = types.SimpleNamespace(scene=scene, window_manager=types.SimpleNamespace(windows=[]))
dirty_tracker.mark_synced = lambda scene: None
dirty_tracker.flush = lambda *args: sent.append("delta")
sent.clear()
addon._timer()
assert sent == ["full", "delta"], "First connection must establish a snapshot"
sent.clear()
addon._timer()
assert sent == ["delta"], "An established baseline must use deltas"
scene[identity.SESSION_GUID_KEY] = "33333333-3333-4333-8333-333333333333"
sent.clear()
addon._timer()
assert sent == ["full", "delta"], "Source scene changes must establish a new snapshot"
addon._on_load(None)
sent.clear()
addon._timer()
assert sent == ["full", "delta"], "Reloading the same source file must reset the baseline"
addon._on_load(None)
sent.clear()
assert addon.AILU_OT_full_sync().execute(bpy.context) == {"FINISHED"}
addon._timer()
assert sent == ["full", "delta"], "Manual Full Sync must not trigger a redundant automatic snapshot"
print("BlenderPersistenceSmoke PASS: Python syntax, duplicates, deletes, reconnect, scene switch, file reload")
