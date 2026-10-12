"""Native Blender scalar-map export regression and Editor Live Link fixture generation."""
import base64
import json
import sys
from pathlib import Path

import bpy
import numpy as np

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / "Tools/Blender"))
from ailu_live_link import identity, material_bake, material_sync, mesh_sync

output = Path(sys.argv[sys.argv.index("--") + 1])
output.mkdir(parents=True, exist_ok=True)
# Keep all generated scalar maps with this unique fixture.
mesh_sync._session_temp_dir = str(output)


class RecordingLink:
    def send_json(self, kind, value):
        self.value = value


link = RecordingLink()
bpy.ops.mesh.primitive_cube_add()
obj = bpy.context.object
material = bpy.data.materials.new("IndependentScalarFixture")
material.use_nodes = True
obj.data.materials.clear()
obj.data.materials.append(material)
tree = material.node_tree
principled = next(node for node in tree.nodes if node.type == "BSDF_PRINCIPLED")
principled.inputs["Roughness"].default_value = 0.6
principled.inputs["Metallic"].default_value = 0.3
image = bpy.data.images.new("ScalarChannels", width=2, height=2, float_buffer=True)
image.colorspace_settings.name = "Non-Color"
image.pixels.foreach_set(np.tile([0.25, 0.75, 0.1, 1.0], 4).astype(np.float32))
texture = tree.nodes.new("ShaderNodeTexImage")
texture.image = image
separate = tree.nodes.new("ShaderNodeSeparateColor")
tree.links.new(texture.outputs["Color"], separate.inputs["Color"])
rough_link = tree.links.new(separate.outputs["Red"], principled.inputs["Roughness"])
metal_link = tree.links.new(separate.outputs["Green"], principled.inputs["Metallic"])
material_sync.export_material(material, link)
both = link.value.copy()
assert both["roughness"] == both["metallic"] == 1.0
assert both["roughness_path"] != both["metallic_path"]
for channel, expected in (("roughness", 0.25), ("metallic", 0.75)):
    loaded = bpy.data.images.load(both[channel + "_path"], check_existing=False)
    loaded.colorspace_settings.name = "Non-Color"
    assert np.allclose(list(loaded.pixels)[::4], expected, atol=1.0 / 255), channel
    bpy.data.images.remove(loaded)
tree.links.remove(metal_link)
material_sync.export_material(material, link)
assert link.value["metallic_path"] == "" and abs(link.value["metallic"] - 0.3) < 1e-6
tree.links.remove(rough_link)
material_sync.export_material(material, link)
assert link.value["roughness_path"] == "" and abs(link.value["roughness"] - 0.6) < 1e-6
assert "roughness_metallic_path" not in both
fixture = {"object": {"guid": identity.get_guid(obj), "name": obj.name,
                      "material_guids": [both["material_guid"]]},
           "material": both, "mesh": base64.b64encode(mesh_sync.encode_mesh(
               obj, bpy.context.evaluated_depsgraph_get())).decode()}
(output / "fixture.json").write_text(json.dumps(fixture, indent=2))
material_bake.export(material, obj, link, resolution=16)
baked = link.value
assert baked["roughness"] == baked["metallic"] == 1.0
assert baked["roughness_path"] != baked["metallic_path"]
assert "roughness_metallic_path" not in baked
for channel, expected in (("roughness", 0.6), ("metallic", 0.3)):
    loaded = bpy.data.images.load(baked[channel + "_path"], check_existing=False)
    loaded.colorspace_settings.name = "Non-Color"
    pixels = np.asarray(loaded.pixels[:]).reshape(-1, 4)
    # This constant cube's UV padding is black; verify every non-padding sample and sufficient coverage.
    covered = pixels[:, 0] > 0.01
    assert np.count_nonzero(covered) > len(pixels) / 2, channel
    assert np.allclose(pixels[covered, 0], expected, atol=2.0 / 255), channel
    bpy.data.images.remove(loaded)
(output / "baked.json").write_text(json.dumps(baked, indent=2))
for channel, expected in (("roughness", 0.25), ("metallic", 0.75)):
    assert both[channel + "_path"] != baked[channel + "_path"], "Direct/baked cache files must be distinct"
    loaded = bpy.data.images.load(both[channel + "_path"], check_existing=False)
    loaded.colorspace_settings.name = "Non-Color"
    assert np.allclose(list(loaded.pixels)[::4], expected, atol=1.0 / 255), channel
    bpy.data.images.remove(loaded)
print("SCALAR_MATERIAL PASS: separate direct/baked maps, R/G extraction, linked factors and unlinked defaults")

