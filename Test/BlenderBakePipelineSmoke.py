"""Native scene/worker lifetime, PNG semantics and bake-context restoration regression."""
import json
import os
import sys
from pathlib import Path

import bpy
import numpy as np

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / "Tools/Blender"))
from ailu_live_link import material_bake

output = Path(sys.argv[sys.argv.index("--") + 1])
output.mkdir(parents=True, exist_ok=True)
os.environ["AILU_BLENDER_BAKE_DEVICE"] = "CPU"
scene, layer = bpy.context.scene, bpy.context.view_layer
collections = (bpy.data.objects, bpy.data.meshes, bpy.data.materials, bpy.data.images, bpy.data.scenes)
counts = tuple(map(len, collections))
paths = {}
for process in (False, True):
    os.environ["AILU_BLENDER_BAKE_REUSE_SCENE"] = "1"
    os.environ["AILU_BLENDER_BAKE_PNG_PROCESS"] = str(int(process))
    with material_bake.ExportSession():
        assert material_bake._temporary_scene() == material_bake._temporary_scene()
        with material_bake.ExportSession():
            assert material_bake._session_active
        assert material_bake._session_active
        for color in (False, True):
            pixels = np.random.default_rng(41).uniform(-0.1, 2.0, (32, 32, 4)).astype(np.float32)
            pixels[:, :, 3] = np.linspace(0.0, 1.0, 32)[None, :]
            path = material_bake._save_pixels(f"pipeline_{process}_{color}", pixels, color)
            paths[f"{process}_{color}"] = path
    assert not material_bake._session_active and material_bake._session_scene is None
    assert material_bake._png_encoder is None
    assert bpy.context.scene == scene and bpy.context.view_layer == layer
    assert tuple(map(len, collections)) == counts

# Source context and all temporary state must recover even if an export aborts.
try:
    with material_bake.ExportSession():
        scratch = material_bake._temporary_scene()
        temporary = bpy.data.objects.new("AiluSessionProbe", bpy.context.object.data)
        scratch.collection.objects.link(temporary)
        try:
            with material_bake._override(scratch, temporary):
                raise RuntimeError("Injected session failure")
        finally:
            bpy.data.objects.remove(temporary, do_unlink=True)
except RuntimeError as error:
    assert str(error) == "Injected session failure"
assert bpy.context.scene == scene and bpy.context.view_layer == layer
assert tuple(map(len, collections)) == counts and not material_bake._session_active

# A dead encoder must fail export and release its process/scene owner without publishing a material.
try:
    with material_bake.ExportSession():
        material_bake._payloads["failed_worker_probe"] = {"incomplete": True}
        material_bake._png_encoder._process.terminate()
        material_bake._png_encoder._process.wait(timeout=10)
        material_bake._save_pixels("pipeline_failed", np.ones((16, 16, 4), dtype=np.float32))
        raise AssertionError("Dead PNG worker was accepted")
except RuntimeError as error:
    assert "PNG encoder exited" in str(error)
assert tuple(map(len, collections)) == counts and not material_bake._session_active
assert material_bake._png_encoder is None and material_bake._session_scene is None
assert not material_bake._payloads
(output / "pipeline-smoke.json").write_text(json.dumps({"paths": paths, "state_restored": True}, indent=2))
print("PIPELINE_SESSION_LIFETIME PASS")
