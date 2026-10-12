"""Native regression: empty collection meshes must not interrupt baking or Full Sync lifecycle."""
import sys
from pathlib import Path

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "Tools" / "Blender"))
from ailu_live_link import addon, dirty_tracker, material_sync, object_sync, protocol

assert Path(object_sync.__file__).resolve().parent == Path(__file__).resolve().parents[1] / "Tools/Blender/ailu_live_link"


class Recorder:
    def __init__(self):
        self.messages = []
        self.connected = True
        self.last_error = ""
        self.disconnected = False

    def begin_diagnostic_batch(self, name):
        return 1

    def end_diagnostic_batch(self, *args):
        pass

    def send_json(self, kind, value):
        self.messages.append((kind, value))
        return True

    def send(self, kind, value):
        self.messages.append((kind, value))
        return True

    def disconnect(self):
        self.connected = False
        self.disconnected = True

    def poll(self):
        pass


bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
collection = bpy.data.collections.new("EmptyMeshSource")
empty = bpy.data.objects.new("NoFaces", bpy.data.meshes.new("NoFaces"))
collection.objects.link(empty)
material = bpy.data.materials.new("NeedsBake")
material.use_nodes = True
tree = material.node_tree
noise = tree.nodes.new("ShaderNodeTexNoise")
principled = next(node for node in tree.nodes if node.type == "BSDF_PRINCIPLED")
tree.links.new(noise.outputs["Color"], principled.inputs["Base Color"])
empty.data.materials.append(material)
bpy.ops.mesh.primitive_cube_add()
cube = bpy.context.object
for owner in list(cube.users_collection):
    owner.objects.unlink(cube)
collection.objects.link(cube)
cube.data.materials.append(material)
instancer = bpy.data.objects.new("CollectionInstance", None)
instancer.instance_type = "COLLECTION"
instancer.instance_collection = collection
bpy.context.scene.collection.objects.link(instancer)
bpy.context.view_layer.update()
link = Recorder()
object_sync.full_sync(bpy.context, link)
assert sum(kind == protocol.OBJECT_UPSERT for kind, _ in link.messages) == 3
assert sum(kind == protocol.MESH_UPSERT for kind, _ in link.messages) == 1
assert sum(kind == protocol.MATERIAL_UPSERT for kind, _ in link.messages) == 1
assert link.messages[-1][0] == protocol.FULL_SYNC_END and not link.disconnected
material_sync.send_object_materials(empty, link)
assert sum(kind == protocol.MATERIAL_UPSERT for kind, _ in link.messages) == 1

# The same empty geometry can also be a direct scene object, before the instance phase.
bpy.context.scene.collection.objects.link(empty)
link = Recorder()
object_sync.full_sync(bpy.context, link)
assert link.messages[-1][0] == protocol.FULL_SYNC_END and not link.disconnected
assert sum(kind == protocol.MESH_UPSERT for kind, _ in link.messages) == 1

original_export = material_sync.send_object_materials


def fail_export(*args):
    raise RuntimeError("Injected material failure")


material_sync.send_object_materials = fail_export
link = Recorder()
try:
    object_sync.full_sync(bpy.context, link)
    raise AssertionError("Export failure must propagate")
except RuntimeError as error:
    assert str(error) == "Injected material failure"
assert link.disconnected and "Injected material failure" in link.last_error
assert not any(kind == protocol.FULL_SYNC_END for kind, _ in link.messages)
assert dirty_tracker._export_depth == 0

addon.connection.connection = Recorder()
addon._was_connected = False
assert addon._timer() == 0.05
assert addon.connection.connection.disconnected and not addon._was_connected
material_sync.send_object_materials = original_export
addon.connection.connection = Recorder()
assert addon._timer() == 0.05 and addon._was_connected
assert addon.connection.connection.messages[-1][0] == protocol.FULL_SYNC_END
print("EMPTY_INSTANCE PASS: real bake, complete snapshot, failure disconnect, timer survives and reconnect succeeds")
