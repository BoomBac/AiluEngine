"""Create an isolated GGX grazing-angle Editor fixture beside the selected executable."""
import importlib.util
import json
import sys
import uuid
from pathlib import Path

helper_path = Path(__file__).parents[1] / "Tools/generate_ddgi_test_scenes.py"
spec = importlib.util.spec_from_file_location("scene_helpers", helper_path)
helpers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helpers)
root = Path(sys.argv[1])
assets = root / "Assets/scene"
assets.mkdir(parents=True, exist_ok=True)
database = {"assets": []}
materials = [("Metal", (0.85, 0.75, 0.55), (0, 0, 0), 0.65, 1),
             ("Floor", (0.7, 0.7, 0.7), (0, 0, 0), 0.55, 1),
             ("Diffuse", (0.5, 0.5, 0.5), (0, 0, 0), 1, 0)]
for name, albedo, emission, roughness, metallic in materials:
    doc = helpers.material_document(name, albedo, emission, roughness, metallic)
    (assets / (name + ".alasset")).write_text(json.dumps(doc, indent=2), encoding="utf-8")
    helpers.register_asset(database, doc["_header"]["_guid"], "project://scene/" + name + ".alasset",
                           helpers.ASSET_TYPE_MATERIAL)
material = lambda name: helpers.guid_for("material/" + name)
entities = [helpers.box("Grazing metal floor", (0, -0.1, 2), (14, 0.2, 18), material("Floor")),
            helpers.ball("Rough gold", (-1.4, 1, 2), 2, material("Metal")),
            helpers.ball("Rough silver", (1.4, 1, 3), 2, material("Floor")),
            helpers.box("Diffuse control", (0, 0.75, 6), (1.5, 1.5, 1.5), material("Diffuse")),
            helpers.environment(intensity=1, render_sky=True, sky_color=(1, 1, 1, 1),
                                ground_color=(1, 1, 1, 1))]
entities[-1]["_environment_component"]["_source"] = "kTexture"
doc = helpers.scene_document("empty", "GGX sampling validation", entities)
(assets / "empty.almap").write_text(json.dumps(doc, indent=2), encoding="utf-8")
helpers.register_asset(database, doc["_header"]["_guid"], "project://scene/empty.almap", helpers.ASSET_TYPE_SCENE)
(root / "Library").mkdir(exist_ok=True)
(root / "Library/AssetDatabase.json").write_text(json.dumps(database, indent=2), encoding="utf-8")
(root / "Vndf.ailuproject").write_text(json.dumps({"_file_version": 1, "_project_name": "VNDF validation",
    "_project_guid": str(uuid.uuid4()), "_startup_scene": "project://scene/empty.almap"}, indent=2), encoding="utf-8")
print(root)
