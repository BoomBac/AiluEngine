"""Bake one real material on a selected Cycles device and verify preferences/context cleanup."""
import json
import os
import sys
import time
from pathlib import Path

import bpy

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / "Tools/Blender"))
from ailu_live_link import material_bake

output, backend = sys.argv[sys.argv.index("--") + 1:]
output = Path(output)
output.mkdir(parents=True, exist_ok=True)
preferences = bpy.context.preferences.addons["cycles"].preferences
preferences.refresh_devices()
previous_backend = preferences.compute_device_type
previous_devices = [(device.name, device.type, device.use) for device in preferences.devices]
scene, layer = bpy.context.scene, bpy.context.view_layer
obj = bpy.context.object
material = obj.data.materials[0]


class Recorder:
    def __init__(self):
        self.values = []

    def send_json(self, kind, value):
        self.values.append(value.copy())


os.environ["AILU_BLENDER_BAKE_DEVICE"] = backend
os.environ["AILU_BLENDER_BAKE_PNG_PROCESS"] = "0"
os.environ["AILU_BLENDER_BAKE_REUSE_SCENE"] = "0"
started = time.perf_counter()
link = Recorder()
with material_bake.ExportSession():
    material_bake.export(material, obj, link, resolution=32)
elapsed = (time.perf_counter() - started) * 1000
material_bake.invalidate()
assert bpy.context.scene == scene and bpy.context.view_layer == layer
assert preferences.compute_device_type == previous_backend
assert [(device.name, device.type, device.use) for device in preferences.devices] == previous_devices
assert not material_bake._session_active and material_bake._session_scene is None
assert len(link.values) == 1
(output / f"device-{backend}.json").write_text(json.dumps({"device": backend, "elapsed_ms": elapsed,
                                                          "preferences_restored": True,
                                                          "material": link.values[0]}, indent=2))
print("GPU_DEVICE_SCOPE PASS", backend, elapsed)
