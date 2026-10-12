"""Native Blender regression for nested collection instance identity, placement and incremental lifetime."""
import struct
import sys
from pathlib import Path

import bpy
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "Tools" / "Blender"))
from ailu_live_link import coordinate, identity, object_sync, protocol


class Recorder:
    def __init__(self):
        self.messages = []

    def send_json(self, kind, value):
        self.messages.append((kind, value))
        return True

    def send(self, kind, value):
        self.messages.append((kind, value))
        return True


bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
bpy.ops.mesh.primitive_cube_add()
source = bpy.context.object
collection = bpy.data.collections.new('InstanceSource')
for owner in list(source.users_collection):
    owner.objects.unlink(source)
collection.objects.link(source)
source.location = (1, 2, 3)
instancers = []
for index in range(2):
    instancer = bpy.data.objects.new('Instancer' + str(index), None)
    bpy.context.scene.collection.objects.link(instancer)
    instancer.instance_type = 'COLLECTION'
    instancer.instance_collection = collection
    instancer.location = (index * 5, 0, 0)
    instancers.append(instancer)
bpy.context.view_layer.update()
link = Recorder()
object_sync.sync_instances(bpy.context, link, full=True)
objects = {value['guid']: value for kind, value in link.messages if kind == protocol.OBJECT_UPSERT}
meshes = {value[:36].decode(): value for kind, value in link.messages if kind == protocol.MESH_UPSERT}
assert len(objects) == len(meshes) == 2
for value in objects.values():
    assert value['parent_guid'] == '' and value['visible']
expected = [coordinate.blender_to_ailu_position(instancer.matrix_world @ source.location) for instancer in instancers]
assert all(any((Vector(value['position']) - position).length < 1e-5 for value in objects.values()) for position in expected)
link.messages.clear()
object_sync.sync_instances(bpy.context, link)
assert not link.messages, 'Idle instance snapshots must not resend'
instancers[0].location.x += 2
bpy.context.view_layer.update()
object_sync.sync_instances(bpy.context, link, changed_objects=[instancers[0]])
assert sum(kind == protocol.OBJECT_UPSERT for kind, _ in link.messages) == 1
assert not any(kind == protocol.MESH_UPSERT for kind, _ in link.messages), 'Placement edits must reuse geometry'
assert set(object_sync._instance_states) == set(objects), 'Moving an instance must preserve identity'
link.messages.clear()
source.data.vertices[0].co.x += 0.5
source.data.update()
bpy.context.view_layer.update()
object_sync.sync_instances(bpy.context, link, changed_objects=[source])
updates = [value for kind, value in link.messages if kind == protocol.MESH_UPSERT]
assert len(updates) == 2 and all(struct.unpack_from('<Q', value, 36)[0] == 2 for value in updates)
link.messages.clear()
bpy.data.objects.remove(instancers[0], do_unlink=True)
bpy.context.view_layer.update()
object_sync.sync_instances(bpy.context, link)
assert sum(kind == protocol.OBJECT_DELETE for kind, _ in link.messages) == 1
assert len(object_sync._instance_states) == 1
link.messages.clear()
source.visible_camera = False
bpy.context.view_layer.update()
object_sync.sync_instances(bpy.context, link)
assert any(kind == protocol.OBJECT_UPSERT and not value['visible'] for kind, value in link.messages)
source.visible_camera = True
outer_collection = bpy.data.collections.new('NestedSource')
nested = bpy.data.objects.new('NestedInstancer', None)
nested.instance_type = 'COLLECTION'
nested.instance_collection = collection
outer_collection.objects.link(nested)
outer = bpy.data.objects.new('OuterInstancer', None)
outer.instance_type = 'COLLECTION'
outer.instance_collection = outer_collection
outer.location = (10, 0, 0)
bpy.context.scene.collection.objects.link(outer)
bpy.context.view_layer.update()
link.messages.clear()
object_sync.sync_instances(bpy.context, link)
assert len(object_sync._instance_states) == 2
assert any(kind == protocol.OBJECT_UPSERT and abs(value['position'][0] - 11) < 1e-5 for kind, value in link.messages)
print('INSTANCE_SYNC PASS: stable IDs, world placement, idle, transform reuse, revisions, deletion, camera visibility and nesting')
