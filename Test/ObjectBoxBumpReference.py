"""Independent native Blender OBJECT-space normal bake for the GPU BOX bump smoke."""
import json
import itertools
import sys
from pathlib import Path
import bpy
import numpy as np
from mathutils import Vector

directory = Path(sys.argv[sys.argv.index("--") + 1])
directory.mkdir(parents=True, exist_ok=True)
size = 1024
axis = (np.arange(size, dtype=np.float32) + 0.5) / size
xx, yy = np.meshgrid(axis, axis)
height = 0.5 + 0.05 * np.sin(xx * (2 * np.pi * 32)) + 0.04 * np.cos(yy * (2 * np.pi * 23))
pixels = np.ones((size, size, 4), dtype=np.float32)
pixels[:, :, :3] = height[:, :, None]
if "--color" in sys.argv:
    pixels[:, :, 1] = 0.4 + 0.08 * np.cos(xx * (2 * np.pi * 19))
    pixels[:, :, 2] = 0.3 + 0.06 * np.sin(yy * (2 * np.pi * 13))
image = bpy.data.images.new("ObjectBoxHeight", width=size, height=size)
image.colorspace_settings.name = "Non-Color"
image.pixels.foreach_set(pixels.ravel())
image.filepath_raw = str(directory / "height.png")
image.file_format = "PNG"
image.save()
bpy.data.images.remove(image)
image = bpy.data.images.load(str(directory / "height.png"), check_existing=False)
image.colorspace_settings.name = "Non-Color"
scene = bpy.context.scene
scene.render.engine = "CYCLES"
scene.cycles.device = "CPU"
scene.cycles.samples = 1
scene.render.bake.margin = 0
scene.render.bake.use_clear = True
cases = []
directions = [(0, 0, 1), (0, 1, 0), (1, 0, 0), (1, 1, 1), (-1, 1, -1),
              (1, 2, 0), (1, 2, 3), (0.9, 1, 1.1), (-2, 1, -1)]
for index, (srgb, distance, inverted, normal) in enumerate(
        itertools.product((False, True), (0.01, 0.1), (False, True), directions)):
    image.colorspace_settings.name = "sRGB" if srgb else "Non-Color"
    normal = Vector(normal).normalized()
    tangent = normal.cross(Vector((0, 1, 0)) if abs(normal.y) < 0.9 else Vector((1, 0, 0))).normalized()
    bitangent = normal.cross(tangent).normalized()
    origin = Vector((0.131, 0.217, 0.319))
    u, v = tangent * 0.08, bitangent * 0.08
    mesh = bpy.data.meshes.new("BoxBumpReference")
    mesh.from_pydata([origin, origin + u, origin + u + v, origin + v], [], [(0, 1, 2, 3)])
    uv = mesh.uv_layers.new(name="UVMap")
    for item, value in zip(uv.data, [(0, 0), (1, 0), (1, 1), (0, 1)]):
        item.uv = value
    obj = bpy.data.objects.new("BoxBumpReference", mesh)
    scene.collection.objects.link(obj)
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    material = bpy.data.materials.new("BoxBumpReference")
    material.use_nodes = True
    obj.data.materials.append(material)
    tree = material.node_tree
    shader = next(node for node in tree.nodes if node.type == "BSDF_PRINCIPLED")
    coordinates = tree.nodes.new("ShaderNodeTexCoord")
    texture = tree.nodes.new("ShaderNodeTexImage")
    texture.image = image
    texture.projection = "BOX"
    blend = 0.4545454680919647 if inverted else 0.5
    texture.projection_blend = blend
    bump = tree.nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.25
    bump.inputs["Distance"].default_value = distance
    bump.invert = inverted
    tree.links.new(coordinates.outputs["Object"], texture.inputs["Vector"])
    tree.links.new(texture.outputs["Color"], bump.inputs["Height"])
    tree.links.new(bump.outputs["Normal"], shader.inputs["Normal"])
    target = bpy.data.images.new("BoxBumpTarget", width=256, height=256, float_buffer=True)
    target.colorspace_settings.name = "Non-Color"
    node = tree.nodes.new("ShaderNodeTexImage")
    node.image = target
    tree.nodes.active = node
    bpy.ops.object.bake(type="NORMAL", normal_space="OBJECT")
    normals = np.empty(256 * 256 * 4, dtype=np.float32)
    target.pixels.foreach_get(normals)
    np.save(directory / f"normal_{index}.npy", normals.reshape(256, 256, 4))
    if "--color" in sys.argv:
        emission = tree.nodes.new("ShaderNodeEmission")
        output = next(node for node in tree.nodes if node.type == "OUTPUT_MATERIAL")
        tree.links.new(texture.outputs["Color"], emission.inputs["Color"])
        tree.links.new(emission.outputs[0], output.inputs["Surface"])
        bpy.ops.object.bake(type="EMIT")
        target.pixels.foreach_get(normals)
        np.save(directory / f"color_{index}.npy", normals.reshape(256, 256, 4))
    cases.append({"name": f"normal_{index}", "origin": list(origin), "u": list(u), "v": list(v),
                  "normal": list(normal), "parameters": [-0.25 if inverted else 0.25, distance, blend, 1.0],
                  "srgb": srgb, "invert": inverted})
    bpy.data.objects.remove(obj, do_unlink=True)
    bpy.data.meshes.remove(mesh)
    bpy.data.materials.remove(material)
    bpy.data.images.remove(target)
(directory / "cases.json").write_text(json.dumps(cases, indent=2), encoding="utf-8")
print("OBJECT_BOX_BUMP_REFERENCE_READY", len(cases), flush=True)
