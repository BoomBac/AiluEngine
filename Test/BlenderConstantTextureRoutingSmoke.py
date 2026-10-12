"""Verify constant baked channels are factors and mixed objects route per material."""
import json
import sys
from pathlib import Path

import bpy
import numpy as np

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / "Tools/Blender"))
from ailu_live_link import identity, material_bake, material_sync, protocol

output = Path(sys.argv[sys.argv.index("--") + 1])
output.mkdir(parents=True, exist_ok=True)


class Recorder:
    def __init__(self):
        self.values = []

    def send_json(self, kind, value):
        self.values.append((kind, value))


def ConstantChannels():
    size = 8
    pixels = lambda value: np.broadcast_to(np.asarray(value, dtype=np.float32), (size, size, 4)).copy()
    coverage = pixels((1, 1, 1, 1))
    scalars = coverage.copy()
    scalars[:, :, 0] = 1.0
    scalars[:, :, 1] = 0.4
    scalars[:, :, 2] = 0.7
    return {
        "color": pixels((0.25, 0.5, 0.75, 1)),
        "normal": pixels((128 / 255, 128 / 255, 1, 1)),
        "emission": pixels((0.2, 0.1, 0, 1)),
        "coverage": coverage,
        "alpha": np.ones((size, size), dtype=np.float32),
        "scalars": scalars,
    }


def TestConstantChannels():
    material = bpy.data.materials.new("ConstantChannelFixture")
    material.use_nodes = True
    invalid = ConstantChannels()["color"]
    invalid[0, 0, 0] = np.nan
    assert material_bake._uniform_channel(invalid, 4, color=True) is None
    output_node = next(node for node in material.node_tree.nodes if node.type == "OUTPUT_MATERIAL")
    saved = []
    original_save = material_bake._save_pixels
    material_bake._save_pixels = lambda name, pixels, color=False: saved.append(name) or name
    try:
        link = Recorder()
        material_bake._publish_material(material, "constant-guid", type("Obj", (), {"name_full": "Fixture"})(),
                                        output_node.inputs["Surface"], False, ConstantChannels(), link, 8)
        payload = link.values[-1][1]
        assert saved == ["constant-guid_normal"], saved
        assert all(not payload[key] for key in ("base_color_path", "emission_path",
                                                 "roughness_path", "metallic_path")), payload
        assert payload["normal_path"]
        assert np.allclose(payload["base_color"][:3], (0.25, 0.5, 0.75), atol=2 / 255)
        assert payload["base_color"][3] == 1.0
        assert np.allclose(payload["emission"], (0.2, 0.1, 0.0), atol=2 / 255)
        assert abs(payload["roughness"] - 0.4) <= 2 / 255
        assert abs(payload["metallic"] - 0.7) <= 2 / 255

        saved.clear()
        link = Recorder()
        material_bake._publish_material(material, "no-normal-guid", type("Obj", (), {"name_full": "Fixture"})(),
                                        output_node.inputs["Surface"], False, ConstantChannels(), link, 8,
                                        normal_required=False)
        payload = link.values[-1][1]
        assert "no-normal-guid_normal" not in saved, saved
        assert payload["normal_path"] == "", payload

        varied = ConstantChannels()
        varied["color"][0, 0, 0] += 0.2
        varied["normal"][0, 0, 0] = 0.75
        varied["emission"][0, 0, 1] += 0.2
        varied["scalars"][0, 0, 1] = 0.8
        saved.clear()
        link = Recorder()
        material_bake._publish_material(material, "varied-guid", type("Obj", (), {"name_full": "Fixture"})(),
                                        output_node.inputs["Surface"], False, varied, link, 8)
        payload = link.values[-1][1]
        assert set(saved) == {"varied-guid_base", "varied-guid_normal", "varied-guid_emission",
                              "varied-guid_roughness"}, saved
        assert not payload["base_color_path"] == ""
        assert not payload["normal_path"] == ""
        assert not payload["emission_path"] == ""
        assert not payload["roughness_path"] == ""
        assert payload["metallic_path"] == ""
    finally:
        material_bake._save_pixels = original_save
        material_bake.invalidate()
        bpy.data.materials.remove(material)


def TestMaterialRouting():
    bpy.ops.mesh.primitive_cube_add()
    obj = bpy.context.object
    direct = bpy.data.materials.new("DirectFixture")
    direct.use_nodes = True
    complex_material = bpy.data.materials.new("ComplexFixture")
    complex_material.use_nodes = True
    principled = next(node for node in complex_material.node_tree.nodes if node.type == "BSDF_PRINCIPLED")
    noise = complex_material.node_tree.nodes.new("ShaderNodeTexNoise")
    complex_material.node_tree.links.new(noise.outputs["Fac"], principled.inputs["Roughness"])
    obj.data.materials.clear()
    obj.data.materials.append(direct)
    obj.data.materials.append(complex_material)
    obj.data.polygons[0].material_index = 1
    assert not material_bake.requires_bake(direct)
    assert material_bake.requires_bake(complex_material)
    direct_principled = next(node for node in direct.node_tree.nodes if node.type == "BSDF_PRINCIPLED")
    assert not material_bake._surface_requires_normal_bake(
        next(node for node in direct.node_tree.nodes if node.type == "OUTPUT_MATERIAL").inputs["Surface"])
    bump = direct.node_tree.nodes.new("ShaderNodeBump")
    direct.node_tree.links.new(bump.outputs["Normal"], direct_principled.inputs["Normal"])
    assert material_bake._surface_requires_normal_bake(
        next(node for node in direct.node_tree.nodes if node.type == "OUTPUT_MATERIAL").inputs["Surface"])
    direct.node_tree.links.remove(direct_principled.inputs["Normal"].links[0])
    direct.node_tree.nodes.remove(bump)
    assert not material_bake.requires_bake_for_object(direct, obj)

    baked = []
    original_export = material_bake._export_materials

    def RecordBake(materials, target, link, resolution):
        baked.extend(material.name for material in materials)
        for material in materials:
            guid = material_bake.material_guid(material, target)
            material_bake._payloads[guid] = {"material_guid": guid, "name": material.name}

    material_bake._export_materials = RecordBake
    link = Recorder()
    try:
        material_sync.send_object_materials(obj, link)
        assert baked == [complex_material.name], baked
        direct_values = [value for kind, value in link.values if kind == protocol.MATERIAL_UPSERT
                         and value.get("name") == direct.name_full]
        assert len(direct_values) == 1, link.values
        assert direct_values[0]["material_guid"] == identity.get_guid(direct)
        complex_material["ailu_export_mode"] = "BAKE"
        assert not material_bake.requires_bake_for_object(direct, obj)
        image = bpy.data.images.new("UVDependentFixture", width=2, height=2)
        image_node = direct.node_tree.nodes.new("ShaderNodeTexImage")
        image_node.image = image
        direct.node_tree.links.new(image_node.outputs["Color"], direct_principled.inputs["Base Color"])
        assert material_bake.requires_bake_for_object(direct, obj)
        direct["ailu_export_mode"] = "DIRECT"
        assert not material_bake.requires_bake_for_object(direct, obj)
        del direct["ailu_export_mode"]
        direct.node_tree.links.remove(direct_principled.inputs["Base Color"].links[0])
        direct.node_tree.nodes.remove(image_node)
        bpy.data.images.remove(image)
        assert not material_bake.requires_bake_for_object(direct, obj)
    finally:
        material_bake._export_materials = original_export
        material_bake.invalidate()
        bpy.data.objects.remove(obj, do_unlink=True)
        bpy.data.materials.remove(direct)
        bpy.data.materials.remove(complex_material)


TestConstantChannels()
TestMaterialRouting()
(output / "constant_texture_routing.json").write_text(json.dumps({"status": "pass"}, indent=2), encoding="utf-8")
print("CONSTANT_TEXTURE_ROUTING PASS")
