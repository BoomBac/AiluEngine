"""Create a disposable M1 fixture and independent bpy-derived expectations."""
import hashlib
import json
import math
import sys
import uuid
from pathlib import Path

import bpy
import numpy as np
from mathutils import Matrix, Vector


output = Path(sys.argv[sys.argv.index("--") + 1])
variant = sys.argv[sys.argv.index("--") + 2] if len(sys.argv) > sys.argv.index("--") + 2 else "full"
valid_variants = {"full", "hierarchy_mesh", "nested_instances", "directional_light", "point_light",
                  "spot_light", "area_light", "lights", "cameras", "perspective_camera", "orthographic_camera"}
if variant not in valid_variants:
    raise ValueError(f"Unsupported M1 fixture variant: {variant}")
blend_path = output / "m1_fixture.blend"
reference_path = output / "blender_reference.json"
session_guid = str(uuid.uuid5(uuid.NAMESPACE_URL, "ailu-blender-link-validation-m1-session"))
guid_namespace = uuid.UUID("d8653811-9ee3-4afc-8842-6ebf11ac62db")
guid_key = "ailu_live_link_guid"
session_key = "ailu_live_link_session_guid"
basis = Matrix(((1.0, 0.0, 0.0), (0.0, 0.0, 1.0), (0.0, -1.0, 0.0)))
basis4 = basis.to_4x4()


def stable_guid(label):
    return str(uuid.uuid5(guid_namespace, label))


def set_guid(datablock, label):
    value = stable_guid(label)
    datablock[guid_key] = value
    return value


def blender_position(value):
    return [float(value.x), float(value.z), float(-value.y)]


def converted_transform(matrix):
    position, rotation, scale = (basis4 @ matrix @ basis4.inverted()).decompose()
    return {"position": [float(position.x), float(position.y), float(position.z)],
            "rotation": [float(rotation.x), float(rotation.y), float(rotation.z), float(rotation.w)],
            "scale": [float(scale.x), float(scale.y), float(scale.z)]}


def converted_matrix(matrix):
    converted = basis4 @ matrix @ basis4.inverted()
    # Engine matrices use row-vector translation while Blender stores column-vector transforms.
    return [[float(converted[column][row]) for column in range(4)] for row in range(4)]


def make_material(name, base_color, roughness, metallic):
    material = bpy.data.materials.new(name)
    set_guid(material, "material:" + name)
    material.diffuse_color = base_color
    material.roughness = roughness
    material.metallic = metallic
    material.use_nodes = True
    principled = next(node for node in material.node_tree.nodes if node.type == "BSDF_PRINCIPLED")
    principled.inputs["Base Color"].default_value = base_color
    principled.inputs["Roughness"].default_value = roughness
    principled.inputs["Metallic"].default_value = metallic
    return material


def new_empty(name, collection, label):
    obj = bpy.data.objects.new(name, None)
    collection.objects.link(obj)
    set_guid(obj, label)
    return obj


def new_light(name, light_type, location, energy, color):
    data = bpy.data.lights.new(name + "Data", light_type)
    data.energy = energy
    data.color = color
    if light_type in {"POINT", "SPOT", "AREA"}:
        data.cutoff_distance = 27.0
        data.shadow_soft_size = 0.19
    if light_type == "SPOT":
        data.spot_size = math.radians(53.0)
        data.spot_blend = 0.28
    if light_type == "AREA":
        data.shape = "RECTANGLE"
        data.size = 1.7
        data.size_y = 0.8
    obj = bpy.data.objects.new(name, data)
    bpy.context.scene.collection.objects.link(obj)
    set_guid(obj, "object:" + name)
    obj.location = location
    obj.rotation_euler = (0.17, -0.31, 0.23)
    return obj


for obj in list(bpy.data.objects):
    bpy.data.objects.remove(obj, do_unlink=True)
scene = bpy.context.scene
scene[session_key] = session_guid
parent = new_empty("M1_TransformParent", scene.collection, "object:M1_TransformParent")
parent.location = (2.0, -1.0, 3.0)
parent.rotation_euler = (0.21, -0.37, 0.18)
parent.scale = (-1.25, 1.5, 0.75)

material_a = make_material("M1_MaterialA", (0.21, 0.46, 0.73, 1.0), 0.27, 0.63)
material_b = make_material("M1_MaterialB", (0.81, 0.32, 0.17, 1.0), 0.74, 0.08)
if variant in {"full", "hierarchy_mesh"}:
    bpy.ops.mesh.primitive_cube_add(size=1.8)
    mesh_object = bpy.context.object
    mesh_object.name = "M1_ParentedMesh"
    mesh_object.data.name = "M1_ParentedMeshData"
    set_guid(mesh_object, "object:M1_ParentedMesh")
    mesh_object.parent = parent
    mesh_object.matrix_parent_inverse = Matrix.Identity(4)
    mesh_object.location = (1.25, 0.6, -2.0)
    mesh_object.rotation_euler = (0.14, 0.42, -0.29)
    mesh_object.scale = (-0.65, 1.3, 0.9)
    mesh_object.hide_set(True)
    mesh_object.data.materials.clear()
    mesh_object.data.materials.append(material_a)
    mesh_object.data.materials.append(material_b)
    for polygon_index, polygon in enumerate(mesh_object.data.polygons):
        polygon.material_index = polygon_index % 2
    uv_layer = mesh_object.data.uv_layers.new(name="M1_UV0")
    for loop_index, loop_uv in enumerate(uv_layer.data):
        loop_uv.uv = ((loop_index % 7) / 6.0, ((loop_index * 3) % 11) / 10.0)

if variant in {"full", "nested_instances"}:
    inner = bpy.data.collections.new("M1_NestedSource")
    inner_mesh = bpy.data.meshes.new("M1_InstanceMeshData")
    inner_mesh.from_pydata([(-1, -1, -1), (1, -1, -1), (1, 1, -1), (-1, 1, -1),
                            (-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)], [],
                           [(0, 1, 2, 3), (4, 7, 6, 5), (0, 4, 5, 1), (1, 5, 6, 2),
                            (2, 6, 7, 3), (4, 0, 3, 7)])
    inner_mesh.materials.append(material_b)
    instance_source = bpy.data.objects.new("M1_NestedMeshSource", inner_mesh)
    inner.objects.link(instance_source)
    set_guid(instance_source, "object:M1_NestedMeshSource")
    instance_uv = inner_mesh.uv_layers.new(name="M1_InstanceUV")
    for loop_index, loop_uv in enumerate(instance_uv.data):
        loop_uv.uv = ((loop_index % 5) / 4.0, ((loop_index * 2) % 7) / 6.0)
    outer = bpy.data.collections.new("M1_OuterCollection")
    nested_instancer = new_empty("M1_NestedCollectionInstancer", outer, "object:M1_NestedCollectionInstancer")
    nested_instancer.instance_type = "COLLECTION"
    nested_instancer.instance_collection = inner
    nested_instancer.location = (2.5, -0.75, 1.25)
    nested_instancer.rotation_euler = (0.11, 0.24, -0.16)
    outer_instancer = new_empty("M1_OuterCollectionInstancer", scene.collection, "object:M1_OuterCollectionInstancer")
    outer_instancer.instance_type = "COLLECTION"
    outer_instancer.instance_collection = outer
    outer_instancer.location = (7.0, 2.0, -1.0)
    outer_instancer.rotation_euler = (-0.18, 0.36, 0.12)

for light_type, name, location, energy, color in (
    ("SUN", "M1_Sun", (1, 6, -3), 2.3, (0.78, 0.88, 1.0)),
    ("POINT", "M1_Point", (-3, 2, 5), 47.0, (1.0, 0.52, 0.24)),
    ("SPOT", "M1_Spot", (4, -2, 1), 63.0, (0.32, 0.86, 0.48)),
    ("AREA", "M1_Area", (-5, -1, 2), 29.0, (0.41, 0.56, 0.96)),
):
    variant_light = "directional_light" if light_type == "SUN" else light_type.lower() + "_light"
    if variant in {"full", "lights"} or variant == variant_light:
        new_light(name, light_type, location, energy, color)

for name, camera_type, location in (
    ("M1_PerspectiveCamera", "PERSP", (6, 4, 8)),
    ("M1_OrthographicCamera", "ORTHO", (-6, 5, 4)),
):
    if variant not in {"full", "cameras", camera_type.lower() + "_camera"}:
        continue
    camera_data = bpy.data.cameras.new(name + "Data")
    camera_data.type = camera_type
    camera_data.lens = 47.0
    camera_data.sensor_fit = "HORIZONTAL"
    camera_data.sensor_width = 36.0
    camera_data.clip_start = 0.23
    camera_data.clip_end = 321.0
    camera_data.shift_x = 0.07
    camera_data.shift_y = -0.04
    camera_data.ortho_scale = 8.5
    camera_obj = bpy.data.objects.new(name, camera_data)
    scene.collection.objects.link(camera_obj)
    set_guid(camera_obj, "object:" + name)
    camera_obj.location = location
    camera_obj.rotation_euler = (0.31, -0.22, 0.57)
scene.camera = bpy.data.objects.get("M1_PerspectiveCamera")
bpy.context.view_layer.update()


def mesh_reference(obj, depsgraph):
    evaluated = obj.evaluated_get(depsgraph)
    mesh = evaluated.to_mesh(preserve_all_data_layers=True, depsgraph=depsgraph)
    try:
        mesh.calc_loop_triangles()
        loops = mesh.loops
        loop_vertices = [loop.vertex_index for loop in loops]
        vertices = [blender_position(mesh.vertices[index].co) for index in loop_vertices]
        normals = [blender_position(loop.normal) for loop in loops]
        uv_layer = mesh.uv_layers.active
        uvs = [[float(value.uv.x), float(1.0 - value.uv.y)] for value in uv_layer.data] if uv_layer else []
        tangents = []
        if uv_layer:
            try:
                mesh.calc_tangents(uvmap=uv_layer.name)
                tangents = [[*blender_position(loop.tangent.normalized()),
                             float(1.0 if loop.bitangent_sign < 0.0 else -1.0)] for loop in loops]
            except RuntimeError:
                pass
        if not tangents:
            tangents = [[1.0, 0.0, 0.0, 1.0] for _ in loops]
        material_indices = [triangle.material_index for triangle in mesh.loop_triangles]
        slots = sorted(set(max(0, index) for index in material_indices))
        submeshes = []
        for slot in slots:
            indices = []
            for triangle in mesh.loop_triangles:
                if max(0, triangle.material_index) == slot:
                    indices.extend(int(loop_index) for loop_index in triangle.loops)
            submeshes.append(indices)
        materials = []
        for slot in slots:
            material = obj.material_slots[slot].material if slot < len(obj.material_slots) else None
            materials.append({"guid": material.get(guid_key, "") if material else "",
                              "name": material.name_full if material else "",
                              "base_color": list(material.node_tree.nodes.get("Principled BSDF").inputs["Base Color"].default_value)
                              if material and material.use_nodes else list(material.diffuse_color) if material else [],
                              "roughness": float(material.roughness) if material else 0.0,
                              "metallic": float(material.metallic) if material else 0.0})
        return {"vertices": vertices, "normals": normals, "tangents": tangents, "uv0": uvs,
                "submeshes": submeshes, "materials": materials,
                "vertex_count": len(vertices), "triangle_count": len(mesh.loop_triangles)}
    finally:
        evaluated.to_mesh_clear()


def object_reference(obj, depsgraph, matrix, parent_guid="", instance_key=None, visible=None):
    guid = obj.get(guid_key, "")
    ref = {"guid": instance_key or guid,
           "source_guid": guid,
           "name": obj.name_full + (" [Instance]" if instance_key else ""),
           "object_type": obj.type,
           "parent_guid": parent_guid,
           "visible": bool(visible if visible is not None else
                           obj.visible_get() and (obj.type != "MESH" or obj.visible_camera)),
           "transform": converted_transform(matrix),
           "world_transform": converted_transform(obj.matrix_world.copy() if instance_key is None else matrix),
           "world_matrix": converted_matrix(obj.matrix_world.copy() if instance_key is None else matrix)}
    if obj.type == "MESH":
        ref["mesh"] = mesh_reference(obj, depsgraph)
    elif obj.type == "LIGHT":
        data = obj.data
        ref["light"] = {"type": data.type, "color": list(data.color), "energy": float(data.energy),
                        "range": float(getattr(data, "cutoff_distance", 1000.0)),
                        "radius": float(getattr(data, "shadow_soft_size", 0.05)),
                        "inner_angle_degrees": float(getattr(data, "spot_size", 0.785398) * 57.2957795 *
                                                      (1.0 - getattr(data, "spot_blend", 0.0))),
                        "outer_angle_degrees": float(getattr(data, "spot_size", 0.785398) * 57.2957795),
                        "width": float(getattr(data, "size", 1.0)),
                        "height": float(getattr(data, "size_y", getattr(data, "size", 1.0)))}
    elif obj.type == "CAMERA":
        data = obj.data
        render = scene.render
        aspect = (render.resolution_x * render.pixel_aspect_x) / (render.resolution_y * render.pixel_aspect_y)
        frame = data.view_frame(scene=scene)
        xs = [corner.x / -corner.z for corner in frame] if data.type == "PERSP" else [corner.x for corner in frame]
        ys = [corner.y / -corner.z for corner in frame] if data.type == "PERSP" else [corner.y for corner in frame]
        height = max(ys) - min(ys)
        ref["camera"] = {"type": data.type, "fov_h_degrees": math.degrees(data.angle_x),
                          "fov_y_degrees": math.degrees(2.0 * math.atan(height * 0.5)) if data.type == "PERSP" else 60.0,
                          "aspect": aspect, "ortho_size": max(xs) - min(xs) if data.type == "ORTHO" else float(data.ortho_scale),
                          "near_clip": float(data.clip_start), "far_clip": float(data.clip_end)}
    return ref


depsgraph = bpy.context.evaluated_depsgraph_get()
scene_objects = set(scene.objects)
objects = []
for obj in scene.objects:
    parent_guid = obj.parent.get(guid_key, "") if obj.parent in scene_objects else ""
    objects.append(object_reference(obj, depsgraph, obj.matrix_local.copy(), parent_guid))

instances = []
for instance in depsgraph.object_instances:
    if not instance.is_instance or instance.object.type != "MESH":
        continue
    obj = instance.object.original
    source_guid = obj.get(guid_key, "")
    parent_guid = instance.parent.original.get(guid_key, "") if instance.parent else ""
    persistent_id = tuple(instance.persistent_id)
    instance_key = str(uuid.uuid5(uuid.NAMESPACE_URL,
                                  session_guid + ":" + parent_guid + ":" + source_guid + ":" + str(persistent_id)))
    instances.append(object_reference(obj, depsgraph, instance.matrix_world.copy(), "", instance_key,
                                      instance.show_self and obj.visible_camera))

if variant in {"full", "nested_instances"} and not instances:
    raise RuntimeError("M1 fixture failed to produce a nested mesh collection instance")
bpy.ops.wm.save_as_mainfile(filepath=str(blend_path))
reference = {"session_guid": session_guid, "source_blend_sha256": hashlib.sha256(blend_path.read_bytes()).hexdigest(),
            "objects": objects, "instances": instances}
reference_path.write_text(json.dumps(reference, indent=2), encoding="utf-8")
print("M1_FIXTURE_PASS", variant, len(objects), "direct objects", len(instances), "nested mesh instances", flush=True)
