"""Create a disposable render-validation scene and independent camera/material landmarks."""
import hashlib
import json
import sys
import uuid
from pathlib import Path

import bpy
import numpy as np
from mathutils import Matrix


output = Path(sys.argv[sys.argv.index("--") + 1])
blend_path = output / "m2_fixture.blend"
reference_path = output / "blender_render_reference.json"
guid_key = "ailu_live_link_guid"
session_guid = str(uuid.uuid5(uuid.NAMESPACE_URL, "ailu-blender-link-validation-m2-session"))
namespace = uuid.UUID("9fa6e9eb-44cd-45a3-8892-8d7f42b51398")
basis = Matrix(((1.0, 0.0, 0.0), (0.0, 0.0, 1.0), (0.0, -1.0, 0.0)))
basis4 = basis.to_4x4()


def guid(label):
    return str(uuid.uuid5(namespace, label))


def set_guid(datablock, label):
    value = guid(label)
    datablock[guid_key] = value
    return value


def make_material(name, color, alpha=1.0):
    material = bpy.data.materials.new(name)
    set_guid(material, "material:" + name)
    material.diffuse_color = (*color, alpha)
    material.use_nodes = True
    principled = next(node for node in material.node_tree.nodes if node.type == "BSDF_PRINCIPLED")
    principled.inputs["Base Color"].default_value = (*color, 1.0)
    principled.inputs["Roughness"].default_value = 0.8
    principled.inputs["Metallic"].default_value = 0.0
    principled.inputs["Alpha"].default_value = alpha
    emission = principled.inputs.get("Emission Color", principled.inputs.get("Emission"))
    if emission is not None:
        emission.default_value = (*color, 1.0)
    strength = principled.inputs.get("Emission Strength")
    if strength is not None:
        strength.default_value = 1.0
    return material


def add_cube(name, label, location, material, collection=None):
    bpy.ops.mesh.primitive_cube_add(size=1.8, location=location)
    obj = bpy.context.object
    obj.name = name
    obj.data.name = name + "Data"
    if collection is not None:
        for owner in list(obj.users_collection):
            owner.objects.unlink(obj)
        collection.objects.link(obj)
    obj.data.materials.clear()
    obj.data.materials.append(material)
    set_guid(obj, label)
    return obj


for obj in list(bpy.data.objects):
    bpy.data.objects.remove(obj, do_unlink=True)
scene = bpy.context.scene
scene["ailu_live_link_session_guid"] = session_guid
scene.render.resolution_x = 640
scene.render.resolution_y = 360
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = "PNG"

cube_material = make_material("M2_CubeNeutral", (0.28, 0.32, 0.38))
marker_colors = {
    "red": (0.85, 0.06, 0.04),
    "green": (0.04, 0.78, 0.08),
    "blue": (0.05, 0.1, 0.88),
    "yellow": (0.82, 0.72, 0.04),
}
marker_materials = [make_material("M2_Marker_" + name, color) for name, color in marker_colors.items()]
overlay_material = make_material("M2_AlphaOverlay", (0.92, 0.92, 0.92), 0.45)

# One direct cube, one collection instance, and a four-colour plane exercise silhouettes and material landmarks.
direct_cube = add_cube("M2_DirectCube", "object:M2_DirectCube", (-2.0, 0.0, 0.0), cube_material)
instance_source_collection = bpy.data.collections.new("M2_InstanceSource")
instance_source = add_cube("M2_InstanceCubeSource", "object:M2_InstanceCubeSource", (0.0, 0.0, 0.0),
                           cube_material, instance_source_collection)
instancer = bpy.data.objects.new("M2_CubeInstance", None)
scene.collection.objects.link(instancer)
set_guid(instancer, "object:M2_CubeInstance")
instancer.instance_type = "COLLECTION"
instancer.instance_collection = instance_source_collection
instancer.location = (0.0, 0.0, 0.0)

vertices = []
for row in range(3):
    z = -1.0 + row
    for column in range(3):
        x = 1.0 + column
        vertices.append((x, 0.55, z))
faces = []
face_colors = []
for row in range(2):
    for column in range(2):
        bottom_left = row * 3 + column
        top_left = (row + 1) * 3 + column
        top_right = top_left + 1
        bottom_right = bottom_left + 1
        # +Z cross +X in Blender coordinates points toward +Y, which maps to Engine -Z.
        faces.append((bottom_left, top_left, top_right, bottom_right))
        face_colors.append(row * 2 + column)
marker_mesh = bpy.data.meshes.new("M2_MarkerPlaneData")
marker_mesh.from_pydata(vertices, [], faces)
marker_mesh.update()
marker_plane = bpy.data.objects.new("M2_MarkerPlane", marker_mesh)
scene.collection.objects.link(marker_plane)
set_guid(marker_plane, "object:M2_MarkerPlane")
for material in marker_materials:
    marker_mesh.materials.append(material)
for polygon, material_index in zip(marker_mesh.polygons, face_colors):
    polygon.material_index = material_index
uv_layer = marker_mesh.uv_layers.new(name="M2_UV0")
for loop in marker_mesh.loops:
    x, _, z = marker_mesh.vertices[loop.vertex_index].co
    uv_layer.data[loop.index].uv = ((x - 1.0) * 0.5, (z + 1.0) * 0.5)

# A white alpha-blended patch covers the red marker's upper-right region in the final colour pass only.
overlay_mesh = bpy.data.meshes.new("M2_AlphaOverlayData")
overlay_mesh.from_pydata([(1.5, 0.62, 0.0), (1.5, 0.62, 0.5), (2.0, 0.62, 0.5), (2.0, 0.62, 0.0)],
                         [], [(0, 1, 2, 3)])
overlay_mesh.update()
overlay = bpy.data.objects.new("M2_AlphaOverlay", overlay_mesh)
scene.collection.objects.link(overlay)
set_guid(overlay, "object:M2_AlphaOverlay")
overlay_mesh.materials.append(overlay_material)
overlay_uv = overlay_mesh.uv_layers.new(name="M2_UV0")
for loop in overlay_mesh.loops:
    co = overlay_mesh.vertices[loop.vertex_index].co
    overlay_uv.data[loop.index].uv = ((co.x - 1.5) * 2.0, co.z * 2.0)

# A packed binary-alpha image creates a small checker whose holes must stay clipped in the GBuffer.
clip_image = bpy.data.images.new("M2_AlphaClipImage", width=16, height=16, alpha=True)
clip_image.colorspace_settings.name = "Non-Color"
clip_pixels = np.ones((16, 16, 4), dtype=np.float32)
for image_y in range(16):
    for image_x in range(16):
        opaque = ((image_x // 4) + (image_y // 4)) % 2 == 0
        clip_pixels[image_y, image_x] = (1.0, 1.0, 1.0, 1.0 if opaque else 0.0)
clip_image.pixels.foreach_set(clip_pixels.ravel())
clip_image.pack()
clip_material = bpy.data.materials.new("M2_AlphaClip")
set_guid(clip_material, "material:M2_AlphaClip")
clip_material.diffuse_color = (0.88, 0.04, 0.82, 1.0)
clip_material.use_nodes = True
clip_nodes = clip_material.node_tree.nodes
clip_principled = next(node for node in clip_nodes if node.type == "BSDF_PRINCIPLED")
clip_principled.inputs["Base Color"].default_value = (0.88, 0.04, 0.82, 1.0)
clip_principled.inputs["Roughness"].default_value = 0.8
clip_emission = clip_principled.inputs.get("Emission Color", clip_principled.inputs.get("Emission"))
if clip_emission is not None:
    clip_emission.default_value = (0.88, 0.04, 0.82, 1.0)
clip_strength = clip_principled.inputs.get("Emission Strength")
if clip_strength is not None:
    clip_strength.default_value = 1.0
clip_texture = clip_nodes.new("ShaderNodeTexImage")
clip_texture.image = clip_image
clip_material.node_tree.links.new(clip_texture.outputs["Alpha"], clip_principled.inputs["Alpha"])
clip_mesh = bpy.data.meshes.new("M2_AlphaClipPlaneData")
clip_mesh.from_pydata([(-4.1, 0.55, -0.5), (-4.1, 0.55, 0.5), (-3.1, 0.55, 0.5), (-3.1, 0.55, -0.5)],
                      [], [(0, 1, 2, 3)])
clip_mesh.update()
clip_plane = bpy.data.objects.new("M2_AlphaClipPlane", clip_mesh)
scene.collection.objects.link(clip_plane)
set_guid(clip_plane, "object:M2_AlphaClipPlane")
clip_mesh.materials.append(clip_material)
clip_uv = clip_mesh.uv_layers.new(name="M2_UV0")
for loop in clip_mesh.loops:
    x, _, z = clip_mesh.vertices[loop.vertex_index].co
    clip_uv.data[loop.index].uv = ((x + 4.1), (z + 0.5))

landmarks = []
for row in range(2):
    for column in range(2):
        color_name = list(marker_colors)[row * 2 + column]
        landmarks.append({"name": color_name, "world_position": [1.5 + column * 1.0,
                          -0.5 + row * 1.0, -0.55], "base_color": list(marker_colors[color_name])})
landmarks.append({"name": "alpha_overlay", "world_position": [1.75, 0.25, -0.62],
                  "base_color": [0.92, 0.92, 0.92], "alpha": 0.45})
clip_cells = [{"world_position": [-4.1 + (column + 0.5) * 0.25, -0.5 + (row + 0.5) * 0.25, -0.55],
               "opaque": (row + column) % 2 == 0}
              for row in range(4) for column in range(4)]

# Ensure the evaluated scene is stable before serializing; the original source file is generated only in the run stage.
bpy.context.view_layer.update()
bpy.ops.wm.save_as_mainfile(filepath=str(blend_path))
reference = {
    "session_guid": session_guid,
    "source_blend_sha256": hashlib.sha256(blend_path.read_bytes()).hexdigest(),
    "expected_objects": [direct_cube.name, instancer.name, marker_plane.name, overlay.name, clip_plane.name],
    "landmarks": landmarks,
    "marker_materials": [{"name": material.name, "base_color": list(material.diffuse_color)}
                         for material in marker_materials],
    "marker_bounds_world": [[1.0, -0.55, -1.0], [3.0, -0.55, 1.0]],
    "direct_cube_center_world": [-2.0, 0.0, 0.0],
    "direct_cube_half_extent": 0.9,
    "instance_source_guid": instance_source.get(guid_key, ""),
    "alpha_clip_cells": clip_cells,
}
reference_path.write_text(json.dumps(reference, indent=2), encoding="utf-8")
print("M2_FIXTURE_PASS", len(scene.objects), "visible source objects", len(landmarks), "image landmarks", flush=True)
