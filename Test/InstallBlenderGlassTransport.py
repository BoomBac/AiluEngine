"""Update only the two glass transport modules, backing up the installed copies; do not change preferences."""
import bpy
import hashlib
import json
import shutil
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
evidence = Path(sys.argv[sys.argv.index("--") + 1])
evidence.mkdir(parents=True, exist_ok=False)
destination = Path(bpy.utils.user_resource("SCRIPTS", path="addons")) / "ailu_live_link"
if not (destination / "__init__.py").is_file():
    raise RuntimeError("Existing Blender Live Link installation not found")
report = {"destination": str(destination), "preferences_changed": False, "files": []}
for name in ("material_sync.py", "material_bake.py"):
    source = root / "Tools/Blender/ailu_live_link" / name
    target = destination / name
    shutil.copy2(target, evidence / name)
    shutil.copy2(source, target)
    assert source.read_bytes() == target.read_bytes()
    report["files"].append({"name": name, "sha256": hashlib.sha256(target.read_bytes()).hexdigest()})
(evidence / "installation.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
print("GLASS_MODULES_INSTALLED", json.dumps(report))
