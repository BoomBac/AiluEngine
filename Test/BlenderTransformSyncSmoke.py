"""Compare source world matrices to independently reconstructed wire TRS hierarchies in a fresh Blender process."""
import json
import sys
import uuid
from pathlib import Path

import bpy
from mathutils import Matrix, Quaternion, Vector

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / "Tools/Blender"))
from ailu_live_link import coordinate, identity, material_sync, mesh_sync, object_sync, protocol

output = Path(sys.argv[sys.argv.index("--") + 1])
objects = set(bpy.context.scene.objects)
bpy.context.view_layer.update()
depsgraph = bpy.context.evaluated_depsgraph_get()
identity.repair_duplicate_guids(objects)
session = identity.get_session_guid(bpy.context.scene)
basis = Matrix(((1,0,0,0),(0,0,1,0),(0,-1,0,0),(0,0,0,1)))
failures = []
checks = 0

class RecordingLink:
    def __init__(self):
        self.messages = []

    def send_json(self, kind, value):
        self.messages.append((kind, value))

def HasGeometry(obj):
    evaluated = obj.evaluated_get(depsgraph)
    mesh = evaluated.to_mesh()
    try:
        return mesh is not None and len(mesh.polygons) > 0
    finally:
        evaluated.to_mesh_clear()

def RecordPrototype(obj, graph, link, prototype=False):
    mesh_sync._mesh_geometry_state[identity.get_guid(obj)] = HasGeometry(obj)
    return mesh_sync.has_geometry(identity.get_guid(obj))

def MatrixFromValue(value):
    x, y, z, w = value["rotation"]
    return Matrix.LocRotScale(Vector(value["position"]), Quaternion((w,x,y,z)), Vector(value["scale"]))

def Verify(values, expected):
    global checks
    cache = {}
    def World(guid):
        if not guid:
            return Matrix.Identity(4)
        if guid not in cache:
            value = values[guid]
            cache[guid] = World(value["parent_guid"]) @ MatrixFromValue(value)
        return cache[guid]
    for guid, (name, matrix) in expected.items():
        wanted = basis @ matrix @ basis.inverted()
        actual = World(guid)
        error = max(abs(wanted[r][c]-actual[r][c]) for r in range(4) for c in range(4))
        if error > 1e-4:
            failures.append({"name":name,"guid":guid,"max_error":error})
        checks += 1

direct_link = RecordingLink()
for obj in objects:
    object_sync.SendObject(obj,objects,direct_link)
values = {value["guid"]:value for kind,value in direct_link.messages if kind == protocol.OBJECT_UPSERT}
expected = {identity.get_guid(obj):(obj.name_full,obj.matrix_world.copy()) for obj in objects}
Verify(values, expected)
direct_failures = len(failures)
mesh_sync.send_mesh = RecordPrototype
material_sync.send_object_materials = lambda *args, **kwargs: None
for obj in objects:
    if obj.type == "MESH":
        mesh_sync._mesh_geometry_state[identity.get_guid(obj)] = HasGeometry(obj)
link = RecordingLink()
object_sync.sync_instances(bpy.context,link,full=True)
instance_expected = {}
for instance in depsgraph.object_instances:
    if not instance.is_instance or instance.object.type != "MESH" or not HasGeometry(instance.object.original):
        continue
    source_guid = identity.get_guid(instance.object.original)
    parent_guid = identity.get_guid(instance.parent.original) if instance.parent else ""
    guid = str(uuid.uuid5(uuid.NAMESPACE_URL,session+":"+parent_guid+":"+source_guid+":"+str(tuple(instance.persistent_id))))
    instance_expected[guid] = (instance.object.original.name_full,instance.matrix_world.copy())
for kind,value in link.messages:
    if kind == protocol.OBJECT_UPSERT:
        values[value["guid"]] = value
    elif kind == protocol.INSTANCE_BATCH_UPSERT:
        values.update((item["guid"],item) for item in value["instances"])
Verify(values,instance_expected)
helper_count = sum(kind == protocol.OBJECT_UPSERT for kind,_ in link.messages)
link.messages.clear()
object_sync.sync_instances(bpy.context,link)
assert not link.messages,"Idle affine snapshots must not resend helpers or instances"

# Exercise the affine split independently, including reflections and near-singular scales.
for scale in ((2,0.7,1.3),(-2,0.7,1.3),(2,-0.7,-1.3),(0.00001,2,3)):
    matrix = Matrix.LocRotScale(Vector((2,-3,4)),Quaternion((0.8,0.2,-0.3,0.4)).normalized(),Vector(scale))
    matrix @= Matrix(((1,0.3,0,0),(0,1,0.1,0),(0,0,1,0),(0,0,0,1)))
    child,parent = coordinate.SplitAffineTransform(matrix)
    assert parent is not None
    actual = parent @ child
    assert max(abs(matrix[r][c]-actual[r][c]) for r in range(4) for c in range(4)) < 1e-4
    checks += 1

# Placement-only changes must update the affine factors without re-exporting shared geometry.
mesh = bpy.data.meshes.new("AffineLifecycleMesh")
mesh.from_pydata([(0,0,0),(1,0,0),(0,1,0)],[],[(0,1,2)])
source = bpy.data.objects.new("AffineLifecycleSource",mesh)
collection = bpy.data.collections.new("AffineLifecycleCollection")
collection.objects.link(source)
source.rotation_euler = (0.2,0.4,0.1)
instancer = bpy.data.objects.new("AffineLifecycleInstancer",None)
bpy.context.scene.collection.objects.link(instancer)
instancer.instance_type = "COLLECTION"
instancer.instance_collection = collection
instancer.scale = (2,1,0.5)
bpy.context.view_layer.update()
link.messages.clear()
object_sync.sync_instances(bpy.context,link,full=True)
instance = next(item for kind,value in link.messages if kind == protocol.INSTANCE_BATCH_UPSERT
                for item in value["instances"] if item["name"] == source.name + " [Instance]")
helper_guid = instance["parent_guid"]
assert helper_guid
instance_guid = instance["guid"]
link.messages.clear()
instancer.location.x += 3
bpy.context.view_layer.update()
object_sync.sync_instances(bpy.context,link,changed_objects=[instancer])
assert any(kind == protocol.OBJECT_UPSERT and value["guid"] == helper_guid for kind,value in link.messages)
assert instance_guid in object_sync._instance_states
link.messages.clear()
instancer.scale = (1,1,1)
bpy.context.view_layer.update()
object_sync.sync_instances(bpy.context,link,changed_objects=[instancer])
assert (protocol.OBJECT_DELETE,{"guid":helper_guid}) in link.messages
assert any(kind == protocol.INSTANCE_BATCH_UPSERT and any(item["guid"] == instance_guid and not item["parent_guid"]
           for item in value["instances"]) for kind,value in link.messages)
instancer.scale = (2,1,0.5)
bpy.context.view_layer.update()
object_sync.sync_instances(bpy.context,link,changed_objects=[instancer])
link.messages.clear()
bpy.data.objects.remove(instancer,do_unlink=True)
bpy.context.view_layer.update()
object_sync.sync_instances(bpy.context,link)
assert (protocol.OBJECT_DELETE,{"guid":instance_guid}) in link.messages
assert (protocol.OBJECT_DELETE,{"guid":helper_guid}) in link.messages
checks += 4
result = {"status":"FAIL" if failures else "PASS","scope":"source-to-wire transforms; geometry publishing stubbed",
          "direct_objects":len(expected),"instances":len(instance_expected),"affine_helpers":helper_count,
          "direct_affine_helpers":len(values)-len(expected)-len(instance_expected)-helper_count,
          "check_count":checks,"direct_failure_count":direct_failures,"failures":failures,"idle_no_resend":True,"affine_lifecycle":True}
output.parent.mkdir(parents=True,exist_ok=True)
output.write_text(json.dumps(result,indent=2),encoding="utf-8")
print("TRANSFORM_SMOKE",json.dumps(result),flush=True)
if failures:
    raise RuntimeError("Source-to-wire world transform regression")
