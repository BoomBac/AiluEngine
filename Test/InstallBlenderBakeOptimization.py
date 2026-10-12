"""Back up and install the bake, material and timing modules without changing Blender preferences."""
import hashlib
import json
import shutil
import sys
from pathlib import Path

import bpy

root = Path(__file__).resolve().parents[1]
output = Path(sys.argv[sys.argv.index("--") + 1])
output.mkdir(parents=True, exist_ok=False)
destination = Path(bpy.utils.user_resource("SCRIPTS", path="addons")) / "ailu_live_link"
if not (destination / "__init__.py").is_file():
    raise RuntimeError("Existing Blender Live Link installation not found")
result = {"destination": str(destination), "preferences_changed": False, "files": []}
for name in ("material_bake.py", "material_sync.py", "object_sync.py", "png_encoder.py", "png_worker.py"):
    source = root / "Tools/Blender/ailu_live_link" / name
    target = destination / name
    existed = target.is_file()
    if existed:
        shutil.copy2(target, output / name)
    shutil.copy2(source, target)
    assert source.read_bytes() == target.read_bytes()
    result["files"].append({"name": name, "existed": existed,
                            "sha256": hashlib.sha256(target.read_bytes()).hexdigest()})
(output / "installation.json").write_text(json.dumps(result, indent=2))
print("BAKE_OPTIMIZATION_INSTALLED", json.dumps(result))
