"""Native bake regression for lobe-specific Diffuse/Glossy conversion, group inputs and texture weights."""
import sys
from pathlib import Path

import bpy
import numpy as np

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / "Tools/Blender"))
from ailu_live_link import material_bake, material_sync, mesh_sync

output = Path(sys.argv[sys.argv.index("--") + 1])
output.mkdir(parents=True, exist_ok=True)
mesh_sync._session_temp_dir = str(output)
assert Path(material_bake.__file__).resolve().parent == root / "Tools/Blender/ailu_live_link"
bpy.ops.mesh.primitive_cube_add()
obj = bpy.context.object
material = bpy.data.materials.new("MixedLobeFixture")
material.use_nodes = True
obj.data.materials.clear()
obj.data.materials.append(material)
tree = material.node_tree
tree.nodes.clear()
surface = tree.nodes.new("ShaderNodeOutputMaterial")
diffuse = tree.nodes.new("ShaderNodeBsdfDiffuse")
diffuse.inputs["Color"].default_value = (0.2, 0.3, 0.4, 1)
diffuse.inputs["Roughness"].default_value = 0
glossy = tree.nodes.new("ShaderNodeBsdfAnisotropic")
glossy.inputs["Color"].default_value = (1, 1, 1, 1)
glossy.inputs["Roughness"].default_value = 0.6
mix = tree.nodes.new("ShaderNodeMixShader")
tree.links.new(diffuse.outputs[0], mix.inputs[1])
tree.links.new(glossy.outputs[0], mix.inputs[2])
tree.links.new(mix.outputs[0], surface.inputs["Surface"])


class Recorder:
    def send_json(self, kind, value):
        self.value = value
        return True


def ReadPixels(file, color=False):
    image = bpy.data.images.load(file, check_existing=False)
    image.colorspace_settings.name = "Non-Color"
    pixels = np.asarray(image.pixels[:]).reshape(-1, 4)[:, :3]
    bpy.data.images.remove(image)
    if color:
        pixels = np.where(pixels <= 0.04045, pixels / 12.92, ((pixels + 0.055) / 1.055) ** 2.4)
    return pixels


def Check(name, roughness, color=(0.2, 0.3, 0.4)):
    material_bake.invalidate()
    link = Recorder()
    material_bake.export(material, obj, link, resolution=32)
    value = link.value
    rough = ReadPixels(value["roughness_path"])[:, 0]
    covered = rough > 0.001
    assert covered.sum() > len(rough) / 2
    expected = np.atleast_1d(roughness)
    assert np.all(np.min(abs(rough[covered, None] - expected), axis=1) <= 2 / 255), (name, np.unique(rough[covered]))
    assert all(np.any(abs(rough[covered] - value) <= 2 / 255) for value in expected), name
    assert np.allclose(ReadPixels(value["metallic_path"]), 0), name
    assert np.allclose(ReadPixels(value["base_color_path"], True)[covered], color, atol=0.01), name
    print("PASS", name, float(np.median(rough[covered])))


for weight in (0.02, 0.5, 0.0, 1.0):
    mix.inputs[0].default_value = weight
    Check("weight_" + str(weight), 1 if weight == 0 else 0.6, (0, 0, 0) if weight == 1 else (0.2, 0.3, 0.4))

group = bpy.data.node_groups.new("MixedLobeGroup", "ShaderNodeTree")
for name, direction, socket_type in (("Diffuse", "INPUT", "NodeSocketShader"),
                                     ("Glossy", "INPUT", "NodeSocketShader"),
                                     ("Factor", "INPUT", "NodeSocketFloat"),
                                     ("Shader", "OUTPUT", "NodeSocketShader")):
    group.interface.new_socket(name=name, in_out=direction, socket_type=socket_type)
group_input = group.nodes.new("NodeGroupInput")
group_output = group.nodes.new("NodeGroupOutput")
group_mix = group.nodes.new("ShaderNodeMixShader")
for name, index in (("Factor", 0), ("Diffuse", 1), ("Glossy", 2)):
    group.links.new(group_input.outputs[name], group_mix.inputs[index])
group.links.new(group_mix.outputs[0], group_output.inputs[0])
instance = tree.nodes.new("ShaderNodeGroup")
instance.node_tree = group
instance.inputs["Factor"].default_value = 0.02
tree.links.new(diffuse.outputs[0], instance.inputs["Diffuse"])
tree.links.new(glossy.outputs[0], instance.inputs["Glossy"])
tree.links.new(instance.outputs[0], surface.inputs["Surface"])
Check("group_shader_inputs", 0.6)

second_glossy = tree.nodes.new("ShaderNodeBsdfAnisotropic")
second_glossy.inputs["Roughness"].default_value = 0.2
reflection_mix = tree.nodes.new("ShaderNodeMixShader")
reflection_mix.inputs[0].default_value = 0.25
tree.links.new(glossy.outputs[0], reflection_mix.inputs[1])
tree.links.new(second_glossy.outputs[0], reflection_mix.inputs[2])
tree.links.new(reflection_mix.outputs[0], instance.inputs["Glossy"])
Check("nested_reflection_normalization", 0.5)

image = bpy.data.images.new("SpatialFactor", width=2, height=2, float_buffer=True)
image.colorspace_settings.name = "Non-Color"
image.pixels.foreach_set(np.tile([0.25, 0.25, 0.25, 1], 4).astype(np.float32))
texture = tree.nodes.new("ShaderNodeTexImage")
texture.image = image
tree.links.new(texture.outputs["Color"], reflection_mix.inputs[0])
tree.links.new(texture.outputs["Color"], instance.inputs["Factor"])
Check("texture_driven_nested_group", 0.5)
texture.interpolation = "Closest"
spatial_pixels = np.array([[value, value, value, 1] for value in (0.25, 0.75, 0.75, 0.25)], dtype=np.float32)
image.pixels.foreach_set(spatial_pixels.ravel())
Check("spatial_texture_weights", (0.3, 0.5))

add = tree.nodes.new("ShaderNodeAddShader")
tree.links.new(diffuse.outputs[0], add.inputs[0])
tree.links.new(reflection_mix.outputs[0], add.inputs[1])
tree.links.new(add.outputs[0], surface.inputs["Surface"])
Check("add_shader", (0.3, 0.5))


def CheckCompatibility(node, roughness, metallic, transmission=0):
    tree.links.new(node.outputs[0], surface.inputs["Surface"])
    material_bake.invalidate()
    link = Recorder()
    material_bake.export(material, obj, link, resolution=32)
    value = link.value
    for channel, expected in (("roughness", roughness), ("metallic", metallic)):
        if not value[channel + "_path"]:
            assert value[channel] == expected
        else:
            pixels = ReadPixels(value[channel + "_path"])[:, 0]
            if expected:
                pixels = pixels[pixels > 0.001]
                assert len(pixels) > 0
            assert np.allclose(pixels, expected, atol=2 / 255), (node.type, channel)
    assert value["transmission"] == transmission
    print("COMPATIBILITY PASS", node.type)


CheckCompatibility(diffuse, 1, 0)
CheckCompatibility(glossy, 0.6, 1)
principled = tree.nodes.new("ShaderNodeBsdfPrincipled")
principled.inputs["Roughness"].default_value = 0.4
principled.inputs["Metallic"].default_value = 0.3
CheckCompatibility(principled, 0.4, 0.3)
glass = tree.nodes.new("ShaderNodeBsdfGlass")
glass.inputs["Roughness"].default_value = 0.2
tree.links.new(glass.outputs[0], add.inputs[0])
tree.links.new(glossy.outputs[0], add.inputs[1])
CheckCompatibility(add, 0.2, 0, 1)
print("DIFFUSE_GLOSSY PASS: weight endpoints, pigment, groups, nested reflection, texture inputs and Add Shader")
