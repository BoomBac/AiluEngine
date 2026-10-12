"""Independent read-only BMW scene export using the checkout addon, without changing the installed addon."""
import bpy
import json
import sys
import time
import os
import importlib.util
import shutil
from pathlib import Path
root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / "Tools/Blender"))
# Blender may preload its installed addon before --python. The diagnostic must exercise this checkout.
for module_name in list(sys.modules):
    if module_name == "ailu_live_link" or module_name.startswith("ailu_live_link."):
        del sys.modules[module_name]
from ailu_live_link import connection, object_sync, protocol
if bake_module := os.environ.get("AILU_BMW_BAKE_MODULE"):
    import ailu_live_link
    spec = importlib.util.spec_from_file_location("ailu_live_link.material_bake", bake_module)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    ailu_live_link.material_bake = module
output = Path(sys.argv[sys.argv.index("--") + 1])
output.mkdir(parents=True, exist_ok=True)
(output / "addon_source.json").write_text(json.dumps({"connection": connection.__file__,
                                                      "object_sync": object_sync.__file__}), encoding="utf-8")

class RecordingLink(connection.LinkConnection):
    def __init__(self):
        super().__init__()
        self.materials = []
    def send_json(self, kind, value):
        if kind == protocol.MATERIAL_UPSERT:
            self.materials.append(value)
            if os.environ.get("AILU_BMW_DIFFUSE_CONTROL") == "old" and value["name"] == "TireRubber":
                value = dict(value, roughness=0.0)
            if os.environ.get("AILU_RASTER_GLASS_CONTROL") == "opaque":
                value = dict(value, transmission=0.0, alpha_blend=False)
        return super().send_json(kind, value)

link = RecordingLink()
try:
    link.connect("127.0.0.1", int(os.environ.get("AILU_BLENDER_LIVE_LINK_PORT", "17777")), auto_reconnect=False)
    deadline = time.monotonic() + 30
    while not link.connected and time.monotonic() < deadline:
        link.poll()
        time.sleep(0.05)
    if not link.connected:
        raise RuntimeError("Live Link connection failed: " + link.last_error)
    object_sync.full_sync(bpy.context, link)
    (output / "materials.json").write_text(json.dumps(link.materials, indent=2), encoding="utf-8")
    if os.environ.get("AILU_RASTER_LAMP_DIAG") == "1":
        for material in link.materials:
            if material["name"].startswith("HeadlightGlass@"):
                for channel in ("base_color", "normal", "roughness", "metallic"):
                    if texture := material.get(channel + "_path"):
                        shutil.copy2(texture, output / ("lamp_" + channel + ".png"))
    deadline = time.monotonic() + 60
    while link.queued_bytes and time.monotonic() < deadline:
        link.poll()
        time.sleep(0.01)
    if link.queued_bytes:
        raise RuntimeError("Live Link transfer did not drain")
    (output / "export_completed.json").write_text(json.dumps({"materials": len(link.materials)}), encoding="utf-8")
    deadline = time.monotonic() + 900
    baseline_sent = False
    glass_restored = False
    wheel_control_sent = False
    while not (output / "stop_blender").exists() and time.monotonic() < deadline:
        if (output / "wheel_control_request").exists() and not wheel_control_sent:
            material = next(value for value in link.materials if value["name"] == "TireRubber")
            connection.LinkConnection.send_json(link, protocol.MATERIAL_UPSERT, dict(material, roughness=0.0))
            while link.queued_bytes:
                link.poll()
                time.sleep(0.01)
            wheel_control_sent = True
            (output / "wheel_control_ready").write_text("ready", encoding="utf-8")
        if (output / "opaque_baseline_request").exists() and not baseline_sent:
            for material in list(link.materials):
                value = dict(material)
                value.update(transmission=0.0, alpha_blend=False)
                connection.LinkConnection.send_json(link, protocol.MATERIAL_UPSERT, value)
            while link.queued_bytes:
                link.poll()
                time.sleep(0.01)
            baseline_sent = True
            (output / "opaque_baseline_ready").write_text("ready", encoding="utf-8")
        if (output / "glass_restore_request").exists() and baseline_sent and not glass_restored:
            for material in link.materials:
                connection.LinkConnection.send_json(link, protocol.MATERIAL_UPSERT, material)
            while link.queued_bytes:
                link.poll()
                time.sleep(0.01)
            glass_restored = True
            (output / "glass_restore_ready").write_text("ready", encoding="utf-8")
        link.poll()
        time.sleep(0.1)
finally:
    link.disconnect()
