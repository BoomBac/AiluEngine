"""Native Blender regression: channel values, nested masks, atlas transport and dependency invalidation."""
import sys
from pathlib import Path
from types import SimpleNamespace

import bpy
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ailu_live_link import dirty_tracker, identity, material_bake, mesh_sync


class Recorder:
    def send_json(self, kind, value):
        self.value = value
        if globals().get("transport_link") is not None:
            assert transport_link.send_json(kind, value), "Connected bake packet was not queued"


def pixels(path):
    image = bpy.data.images.load(path, check_existing=False)
    try:
        image.colorspace_settings.name = "Non-Color"
        data = np.empty(image.size[0] * image.size[1] * 4, dtype=np.float32)
        image.pixels.foreach_get(data)
        return data.reshape(-1, 4)
    finally:
        bpy.data.images.remove(image)


def BaseColorPixels(value):
    factor = np.asarray(value["base_color"], dtype=np.float32)
    assert factor.shape == (4,) and np.isfinite(factor).all(), factor
    if not value["base_color_path"]:
        return factor.reshape(1, 4)
    data = pixels(value["base_color_path"])
    rgb = data[:, :3]
    data[:, :3] = np.where(rgb <= 0.04045, rgb / 12.92, ((rgb + 0.055) / 1.055) ** 2.4)
    return data * factor


bpy.ops.mesh.primitive_plane_add()
obj = bpy.context.object
material = bpy.data.materials.new("GraphRegression")
material.use_nodes = True
obj.data.materials.append(material)
tree = material.node_tree
first = next(node for node in tree.nodes if node.type == "BSDF_PRINCIPLED")
first.inputs["Base Color"].default_value = (1, 0, 0, 1)
first.inputs["Roughness"].default_value = 0.2
second = tree.nodes.new("ShaderNodeBsdfPrincipled")
second.inputs["Base Color"].default_value = (0, 0, 1, 1)
second.inputs["Roughness"].default_value = 0.6
mix = tree.nodes.new("ShaderNodeMixShader")
tree.links.new(first.outputs[0], mix.inputs[1])
tree.links.new(second.outputs[0], mix.inputs[2])
map_range = tree.nodes.new("ShaderNodeMapRange")
map_range.inputs[0].default_value = 0.75
tree.links.new(map_range.outputs[0], mix.inputs[0])
mask = tree.nodes.new("ShaderNodeMath")
mask.operation = "MULTIPLY"
mask.inputs[0].default_value = mask.inputs[1].default_value = 0.5
clamp = tree.nodes.new("ShaderNodeClamp")
tree.links.new(mask.outputs[0], clamp.inputs[0])
for node in (first, second):
    tree.links.new(clamp.outputs[0], node.inputs["Alpha"])
output = next(node for node in tree.nodes if node.type == "OUTPUT_MATERIAL")
tree.links.new(mix.outputs[0], output.inputs["Surface"])
material.use_backface_culling = False
before = (len(bpy.data.scenes), len(bpy.data.objects), len(bpy.data.images), len(bpy.data.materials))
original_uv = obj.data.uv_layers.active.name
source_scene, source_layer = bpy.context.window.scene, bpy.context.window.view_layer
recorder = Recorder()
material_bake.export(material, obj, recorder, resolution=32)
value = recorder.value
base = BaseColorPixels(value)
covered = base[:, 3] > 0.2
assert covered.any(), "Bake contains no surface"
if value["base_color_path"]:
    assert np.count_nonzero(covered) > 100, "Bake contains no surface"
linear = base[covered, :3].mean(axis=0)
assert np.allclose(linear, [0.25, 0, 0.75], atol=0.02), linear
assert np.allclose(base[covered, 3], 0.25, atol=2 / 255), base[covered, 3]
roughness = float(value["roughness"])
if value["roughness_path"]:
    roughness = pixels(value["roughness_path"])[:, 0] * roughness
assert np.allclose(roughness, 0.5, atol=2 / 255), roughness
assert value["alpha_blend"] and not value["alpha_test"] and value["double_sided"]
assert (len(bpy.data.scenes), len(bpy.data.objects), len(bpy.data.images), len(bpy.data.materials)) == before
assert obj.data.uv_layers.active.name == original_uv and len(obj.data.uv_layers) == 1
assert bpy.context.window.scene == source_scene and bpy.context.window.view_layer == source_layer
atlas = material_bake.prepared_mesh(obj, bpy.context.evaluated_depsgraph_get())
assert atlas.uv_layers.active.name == "AiluLiveLinkAtlas"
assert mesh_sync.encode_mesh(obj, bpy.context.evaluated_depsgraph_get())
assert value["material_guid"] == material_bake.material_guid(material, obj)
other = obj.copy()
bpy.context.scene.collection.objects.link(other)
other[identity.OBJECT_GUID_KEY] = "other-object"
assert material_bake.material_guid(material, other) == value["material_guid"], "UV-only instances should share bakes"
other.matrix_world = other.matrix_world @ __import__("mathutils").Matrix.Scale(2.0, 4)
assert material_bake.material_guid(material, other) != value["material_guid"], "Scale-dependent bakes must differ"

# Native texture-coordinate transforms and bump height must affect exported pixels, not merely retain an image.
material_bake.invalidate()
coordinates = tree.nodes.new("ShaderNodeTexCoord")
mapping = tree.nodes.new("ShaderNodeMapping")
mapping.inputs["Scale"].default_value = (3, 3, 3)
checker = tree.nodes.new("ShaderNodeTexChecker")
checker.inputs["Color1"].default_value = (0, 1, 0, 1)
checker.inputs["Color2"].default_value = (1, 0, 0, 1)
checker.inputs["Scale"].default_value = 2
tree.links.new(coordinates.outputs["UV"], mapping.inputs["Vector"])
tree.links.new(mapping.outputs[0], checker.inputs["Vector"])
tree.links.new(checker.outputs["Color"], first.inputs["Base Color"])
tree.links.new(first.outputs[0], output.inputs["Surface"])
noise = tree.nodes.new("ShaderNodeTexNoise")
tree.links.new(mapping.outputs[0], noise.inputs["Vector"])
bump = tree.nodes.new("ShaderNodeBump")
bump.inputs["Distance"].default_value = 0.2
tree.links.new(noise.outputs["Fac"], bump.inputs["Height"])
tree.links.new(bump.outputs["Normal"], first.inputs["Normal"])
material_bake.export(material, obj, recorder, resolution=32)
assert recorder.value["base_color_path"] and recorder.value["normal_path"], "Spatial channels must retain textures"
mapped = BaseColorPixels(recorder.value)
normal = pixels(recorder.value["normal_path"])
mask_pixels = mapped[:, 3] > 0.2
assert mapped[mask_pixels, 0].std() > 0.1 and mapped[mask_pixels, 1].std() > 0.1, "Mapping/checker lost"
assert normal[mask_pixels, :2].std(axis=0).max() > 0.03, "Procedural bump height lost"
assert np.isfinite(normal).all()

# Nested Transparent mixes must preserve pigment, and PNG emission storage must preserve HDR strength.
material_bake.invalidate()
transparent = tree.nodes.new("ShaderNodeBsdfTransparent")
transparent_mix = tree.nodes.new("ShaderNodeMixShader")
transparent_mix.inputs[0].default_value = 0.5
tree.links.new(transparent.outputs[0], transparent_mix.inputs[1])
tree.links.new(first.outputs[0], transparent_mix.inputs[2])
tree.links.new(transparent_mix.outputs[0], output.inputs["Surface"])
first.inputs["Emission Color"].default_value = (0.1, 0.2, 0.4, 1)
first.inputs["Emission Strength"].default_value = 10
material_bake.export(material, obj, recorder, resolution=32)
edge = BaseColorPixels(recorder.value)
edge_coverage = edge[:, 3] > 0.1
assert edge_coverage.any(), "Transparent mix lost all coverage"
assert np.allclose(edge[edge_coverage, 3], 0.125, atol=2 / 255)
assert edge[edge_coverage, 2].max() < 0.02, "Transparent shader introduced white pigment"
assert abs(recorder.value["emission_strength"] - 4.0) < 0.02, recorder.value["emission_strength"]

# A Principled inside a group must use the instance's unlinked scalar/color defaults.
material_bake.invalidate()
shader_tree = bpy.data.node_groups.new("BakeDefaults", "ShaderNodeTree")
shader_tree.interface.new_socket(name="Tint", in_out="INPUT", socket_type="NodeSocketColor")
shader_tree.interface.new_socket(name="Opacity", in_out="INPUT", socket_type="NodeSocketFloat")
shader_tree.interface.new_socket(name="Shader", in_out="OUTPUT", socket_type="NodeSocketShader")
group_input = shader_tree.nodes.new("NodeGroupInput")
group_output = shader_tree.nodes.new("NodeGroupOutput")
inner = shader_tree.nodes.new("ShaderNodeBsdfPrincipled")
shader_tree.links.new(group_input.outputs["Tint"], inner.inputs["Base Color"])
shader_tree.links.new(group_input.outputs["Opacity"], inner.inputs["Alpha"])
shader_tree.links.new(inner.outputs[0], group_output.inputs[0])
shader_group = tree.nodes.new("ShaderNodeGroup")
shader_group.node_tree = shader_tree
shader_group.inputs["Tint"].default_value = (0, 1, 0, 1)
shader_group.inputs["Opacity"].default_value = 0.4
tree.links.new(shader_group.outputs[0], output.inputs["Surface"])
material_bake.export(material, obj, recorder, resolution=32)
group_pixels = BaseColorPixels(recorder.value)
group_coverage = group_pixels[:, 3] > 0.3
assert group_coverage.any(), "Group opacity lost all coverage"
assert np.allclose(group_pixels[group_coverage, :3], [0, 1, 0], atol=2 / 255)
assert np.allclose(group_pixels[group_coverage, 3], 0.4, atol=2 / 255)

# Nested node-tree and image-only changes must reach all dependent object slots.
group_tree = bpy.data.node_groups.new("DirtyNested", "ShaderNodeTree")
image = bpy.data.images.new("DirtyImage", width=2, height=2)
texture = group_tree.nodes.new("ShaderNodeTexImage")
texture.image = image
group = tree.nodes.new("ShaderNodeGroup")
group.node_tree = group_tree
material["ailu_export_mode"] = "BAKE"
material_bake.invalidate()
for changed in (image, group_tree):
    material_bake.export(material, obj, recorder, resolution=32)
    baked_guid = material_bake.material_guid(material, obj)
    assert baked_guid in material_bake._payloads, "Dependency case must seed a baked payload"
    cached_meshes = dict(material_bake._meshes)
    dirty_tracker._dirty.clear()
    dirty_tracker._depsgraph_update(bpy.context.scene,
                                   SimpleNamespace(updates=[SimpleNamespace(id=changed, is_updated_geometry=False,
                                                                            is_updated_transform=False, is_updated_shading=True)]))
    assert identity.get_guid(obj) in dirty_tracker._dirty
    assert identity.get_guid(other) in dirty_tracker._dirty
    assert baked_guid not in material_bake._payloads, "Dependent baked output was not invalidated"
    assert dict(material_bake._meshes) == cached_meshes, "Material changes must preserve reusable geometry/UVs"
material_bake.invalidate()
assert not material_bake._meshes and not material_bake._payloads
print("MATERIAL_BAKE_SMOKE PASS: weighted shader colors, scalar mask, continuous alpha, culling, "
      "atlas, instance sharing, mapping, procedural bump, transparent pigment, HDR emission, group defaults, "
      "cleanup, image/group dependencies")
