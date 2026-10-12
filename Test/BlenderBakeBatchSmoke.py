"""Compare multi-material native bake batches with the previous single-material exporter."""
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
obj.data.materials.clear()
materials = []
for index in range(3):
    material = bpy.data.materials.new(f"BatchSlot{index}")
    material.use_nodes = True
    material["ailu_export_mode"] = "BAKE"
    material["ailu_bake_resolution"] = 32
    tree = material.node_tree
    principled = next(node for node in tree.nodes if node.type == "BSDF_PRINCIPLED")
    principled.inputs["Base Color"].default_value = (0.1 + index * 0.2, 0.7 - index * 0.2, 0.3, 1)
    principled.inputs["Roughness"].default_value = 0.2 + index * 0.3
    principled.inputs["Metallic"].default_value = index * 0.4
    principled.inputs["Alpha"].default_value = 0.4 + index * 0.2
    if index == 1:
        texture = tree.nodes.new("ShaderNodeTexNoise")
        uv = tree.nodes.new("ShaderNodeUVMap")
        bump = tree.nodes.new("ShaderNodeBump")
        bump.inputs["Strength"].default_value = 0.25
        tree.links.new(uv.outputs[0], texture.inputs["Vector"])
        tree.links.new(texture.outputs["Fac"], bump.inputs["Height"])
        tree.links.new(bump.outputs[0], principled.inputs["Normal"])
    obj.data.materials.append(material)
    materials.append(material)
obj.data.materials.append(materials[0])
for index, polygon in enumerate(obj.data.polygons):
    polygon.material_index = index % 4


class Recorder:
    def __init__(self):
        self.values = {}

    def send_json(self, kind, value):
        self.values[value["material_guid"]] = value.copy()


def Bake(module, batch):
    module.invalidate()
    arrays = {}
    original_save = module._save_pixels
    operator_class = type(bpy.ops.object.bake)
    original_call = operator_class.__call__
    bake_name = bpy.ops.object.bake.idname()
    calls = []
    def Save(name, pixels, color=False):
        arrays[name] = pixels.copy()
        return name
    def CountBake(operator, *args, **kwargs):
        if operator.idname() == bake_name:
            calls.append(kwargs["type"])
        return original_call(operator, *args, **kwargs)
    module._save_pixels = Save
    operator_class.__call__ = CountBake
    link = Recorder()
    try:
        if batch:
            module.prepare_object_materials(obj, set())
        for material in materials:
            module.export(material, obj, link, resolution=32)
    finally:
        module._save_pixels = original_save
        operator_class.__call__ = original_call
        module.invalidate()
    return link.values, arrays, len(calls)


before, old_arrays, old_calls = Bake(reference, True)
after, new_arrays, new_calls = Bake(material_bake, True)
assert before == after and old_arrays.keys() == new_arrays.keys()
assert (old_calls, new_calls) == (5, 3), (old_calls, new_calls)
maximum = {}
for name, pixels in new_arrays.items():
    delta = np.abs(pixels - old_arrays[name])
    maximum[name] = float(delta.max())
    assert np.isfinite(pixels).all() and maximum[name] <= 2 / 255, (name, maximum[name])

# Spatial scalar and emission inputs must keep their native passes, including nonzero HDR emission.
tree = materials[1].node_tree
principled = next(node for node in tree.nodes if node.type == "BSDF_PRINCIPLED")
noise = next(node for node in tree.nodes if node.type == "TEX_NOISE")
tree.links.new(noise.outputs["Fac"], principled.inputs["Roughness"])
tree.links.new(noise.outputs["Color"], principled.inputs["Emission Color"])
principled.inputs["Emission Strength"].default_value = 2.5
other = next(node for node in materials[0].node_tree.nodes if node.type == "BSDF_PRINCIPLED")
other.inputs["Emission Color"].default_value = (0.2, 0.6, 0.1, 1.0)
other.inputs["Emission Strength"].default_value = 3.0
spatial_before, spatial_old, spatial_old_calls = Bake(reference, True)
spatial_after, spatial_new, spatial_new_calls = Bake(material_bake, True)
assert spatial_before == spatial_after and (spatial_old_calls, spatial_new_calls) == (5, 5)
for name, pixels in spatial_new.items():
    assert np.isfinite(pixels).all() and np.max(np.abs(pixels - spatial_old[name])) <= 2 / 255, name
# Nonzero constant emission also retains its native bake and exact normalization factors.
saved_link = next(iter(principled.inputs["Emission Color"].links))
tree.links.remove(saved_link)
principled.inputs["Emission Color"].default_value = (0.3, 0.1, 0.8, 1.0)
constant_before, constant_old, _ = Bake(reference, True)
constant_after, constant_new, constant_calls = Bake(material_bake, True)
assert constant_before == constant_after and constant_calls == 5
for name, pixels in constant_new.items():
    assert np.max(np.abs(pixels - constant_old[name])) <= 2 / 255, name

surface = next(node for node in materials[0].node_tree.nodes if node.type == "OUTPUT_MATERIAL").inputs["Surface"]
for alpha in (0.02, 0.98):
    other.inputs["Alpha"].default_value = alpha
    assert material_bake._constant_scalars(surface, False) is None
other.inputs["Alpha"].default_value = 0.4

# Different resolutions must form separate batches and keep both payloads/targets intact.
materials[1]["ailu_bake_resolution"] = 16
material_bake.prepare_object_materials(obj, set())
assert len(material_bake._payloads) == 3
bpy.ops.object.mode_set(mode="EDIT")
try:
    try:
        material_bake.export(materials[0], obj, Recorder())
        raise AssertionError("Cached bake must still reject Edit Mode")
    except RuntimeError as error:
        assert "Object Mode" in str(error) and bpy.context.mode == "EDIT_MESH"
finally:
    bpy.ops.object.mode_set(mode="OBJECT")
material_bake.invalidate()
scene = bpy.context.scene
layer = bpy.context.view_layer
collections = (bpy.data.objects, bpy.data.meshes, bpy.data.materials, bpy.data.images, bpy.data.scenes)
counts = tuple(map(len, collections))
original = material_bake.ChannelGraph.shader_channel
def Fail(*args, **kwargs):
    raise RuntimeError("Injected batch failure")
material_bake.ChannelGraph.shader_channel = Fail
try:
    try:
        material_bake.prepare_object_materials(obj, set())
        raise AssertionError("Batch failure was not propagated")
    except RuntimeError as error:
        assert str(error) == "Injected batch failure"
finally:
    material_bake.ChannelGraph.shader_channel = original
    material_bake.invalidate()
assert bpy.context.scene == scene and bpy.context.view_layer == layer and not material_bake._baking
assert tuple(map(len, collections)) == counts
(output / "batch-smoke.json").write_text(json.dumps({"old_calls": old_calls, "new_calls": new_calls,
                                                     "maximum_pixel_difference": maximum}, indent=2))
print(f"BAKE_BATCH PASS: {old_calls} -> {new_calls} calls, independent slots, bump, alpha and cleanup")
