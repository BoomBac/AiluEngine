"""Compare native bake channels with a saved reference exporter and verify exception restoration."""
import importlib.util
import json
import sys
from pathlib import Path

import bpy
import numpy as np

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / "Tools/Blender"))
from ailu_live_link import material_bake

output = Path(sys.argv[sys.argv.index("--") + 1])
spec = importlib.util.spec_from_file_location("ailu_live_link.bake_reference",
                                            output / "source_before/material_bake.py")
reference = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reference)
bpy.ops.mesh.primitive_cube_add()
obj = bpy.context.object
material = bpy.data.materials.new("PackedScalarFixture")
material.use_nodes = True
obj.data.materials.clear()
obj.data.materials.append(material)
other = bpy.data.materials.new("OtherSlot")
obj.data.materials.append(other)
obj.data.polygons[0].material_index = 1
tree = material.node_tree
principled = next(node for node in tree.nodes if node.type == "BSDF_PRINCIPLED")
surface = next(node for node in tree.nodes if node.type == "OUTPUT_MATERIAL")
image = bpy.data.images.new("SpatialChannels", width=2, height=2, float_buffer=True)
image.colorspace_settings.name = "Non-Color"
image.pixels.foreach_set(np.array([[0.2, 0.7, 0.4, 1], [0.8, 0.3, 0.6, 1],
                                  [0.4, 0.1, 0.9, 1], [0.6, 0.9, 0.2, 1]], dtype=np.float32).ravel())
texture = tree.nodes.new("ShaderNodeTexImage")
texture.image = image
texture.interpolation = "Closest"
separate = tree.nodes.new("ShaderNodeSeparateColor")
tree.links.new(texture.outputs["Color"], separate.inputs["Color"])
for channel, name in (("Red", "Alpha"), ("Green", "Roughness"), ("Blue", "Metallic")):
    tree.links.new(separate.outputs[channel], principled.inputs[name])


class Recorder:
    def send_json(self, kind, value):
        self.value = value


def Bake(module):
    module.invalidate()
    arrays = {}
    original_save = module._save_pixels
    def Save(name, pixels, color=False):
        arrays[name.rsplit("_", 1)[-1]] = pixels.copy()
        return name.rsplit("_", 1)[-1]
    module._save_pixels = Save
    link = Recorder()
    try:
        module.export(material, obj, link, resolution=32)
    finally:
        module._save_pixels = original_save
        module.invalidate()
    return link.value, arrays


results = []
for fixture in ("spatial_scalars_multi_slot", "transparent_mix", "diffuse_only"):
    if fixture == "transparent_mix":
        transparent = tree.nodes.new("ShaderNodeBsdfTransparent")
        mix = tree.nodes.new("ShaderNodeMixShader")
        tree.links.new(separate.outputs["Red"], mix.inputs[0])
        tree.links.new(transparent.outputs[0], mix.inputs[1])
        tree.links.new(principled.outputs[0], mix.inputs[2])
        tree.links.new(mix.outputs[0], surface.inputs["Surface"])
    elif fixture == "diffuse_only":
        diffuse = tree.nodes.new("ShaderNodeBsdfDiffuse")
        tree.links.new(texture.outputs["Color"], diffuse.inputs["Color"])
        tree.links.new(diffuse.outputs[0], surface.inputs["Surface"])
    before, old_arrays = Bake(reference)
    after, new_arrays = Bake(material_bake)
    assert before == after, (fixture, before, after)
    maximum = {}
    for name, pixels in new_arrays.items():
        # Scalar textures are consumed through red; the old exporter may carry arbitrary unused G/B.
        selected = (slice(None), slice(None), 0) if name in {"roughness", "metallic"} else (...,)
        delta = np.abs(pixels[selected] - old_arrays[name][selected])
        maximum[name] = float(delta.max())
        assert np.isfinite(pixels).all() and maximum[name] <= 2 / 255, (fixture, name, maximum[name])
    results.append({"fixture": fixture, "max_difference": maximum})

scene = bpy.context.scene
layer = bpy.context.view_layer
counts = tuple(len(collection) for collection in (bpy.data.objects, bpy.data.meshes, bpy.data.materials,
                                                  bpy.data.images, bpy.data.scenes))
original_channel = material_bake.ChannelGraph.shader_channel
def Fail(*args, **kwargs):
    raise RuntimeError("Injected graph failure")
material_bake.ChannelGraph.shader_channel = Fail
try:
    try:
        material_bake.export(material, obj, Recorder(), resolution=16)
        raise AssertionError("Failure was not propagated")
    except RuntimeError as error:
        assert str(error) == "Injected graph failure"
finally:
    material_bake.ChannelGraph.shader_channel = original_channel
    material_bake.invalidate()
assert bpy.context.scene == scene and bpy.context.view_layer == layer and not material_bake._baking
assert counts == tuple(len(collection) for collection in (bpy.data.objects, bpy.data.meshes, bpy.data.materials,
                                                         bpy.data.images, bpy.data.scenes))
(output / "scalar-comparison.json").write_text(json.dumps(results, indent=2))
print("BAKE_OPTIMIZATION PASS: native channels, multi-slot isolation, transparency and exception restoration")
