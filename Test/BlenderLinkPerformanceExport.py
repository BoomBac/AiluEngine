"""Read-only native Blender export, recording the exact packet stream for controlled Engine replays."""
import hashlib
import importlib.util
import json
import math
import os
import sys
import time
from collections import Counter
from pathlib import Path

import bpy
from mathutils import Vector

root = Path(__file__).resolve().parents[1]
addon_root = (Path(bpy.utils.user_resource("SCRIPTS", path="addons"))
              if os.environ.get("AILU_BLENDER_PERF_INSTALLED") == "1" else root / "Tools/Blender")
sys.path.insert(0, str(addon_root))
if installed_addon := sys.modules.get("ailu_live_link"):
    installed_addon.unregister()
for module_name in list(sys.modules):
    if module_name == "ailu_live_link" or module_name.startswith("ailu_live_link."):
        del sys.modules[module_name]
from ailu_live_link import connection, coordinate, material_bake, object_sync, protocol
if reference_path := os.environ.get("AILU_BLENDER_PERF_BAKE_REFERENCE"):
    import ailu_live_link
    spec = importlib.util.spec_from_file_location("ailu_live_link.material_bake", reference_path)
    material_bake = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = material_bake
    spec.loader.exec_module(material_bake)
    ailu_live_link.material_bake = material_bake
if reference_path := os.environ.get("AILU_BLENDER_PERF_MATERIAL_REFERENCE"):
    import ailu_live_link
    spec = importlib.util.spec_from_file_location("ailu_live_link.material_sync", reference_path)
    reference = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = reference
    spec.loader.exec_module(reference)
    ailu_live_link.material_sync = reference
    object_sync.material_sync = reference

output = Path(sys.argv[sys.argv.index("--") + 1])
output.mkdir(parents=True, exist_ok=True)
counts = Counter()
geometry = Counter()
source_collections = (bpy.data.objects, bpy.data.meshes, bpy.data.materials, bpy.data.images, bpy.data.scenes)
source_counts = tuple(map(len, source_collections))
scene = bpy.context.scene
source_layer = bpy.context.view_layer
camera = scene.camera
if camera:
    position = coordinate.blender_to_ailu_position(camera.matrix_world.translation)
    direction = coordinate.blender_to_ailu_direction(camera.matrix_world.to_3x3() @ Vector((0, 0, -1)))
    (output / "camera.json").write_text(json.dumps({"position": list(position),
        "pitch_degrees": math.degrees(math.asin(-direction.y)),
        "yaw_degrees": math.degrees(math.atan2(direction.x, direction.z)),
        "fov_degrees": math.degrees(camera.data.angle_x), "aa_mode": 0, "snap": True}), encoding="utf-8")

class RecordingLink(connection.LinkConnection):
    def send(self, kind, payload=b""):
        wire.write(protocol.packet(kind, self._sequence, payload))
        counts[kind] += 1
        if kind == protocol.MESH_UPSERT:
            geometry[hashlib.sha256(payload[44:]).hexdigest()] += 1
        return super().send(kind, payload)

with (output / "packets.bin").open("wb") as wire:
    link = RecordingLink()
    try:
        link.connect("127.0.0.1", int(os.environ.get("AILU_BLENDER_LIVE_LINK_PORT", "17779")), False)
        deadline = time.monotonic() + 30
        while not link.connected and time.monotonic() < deadline:
            link.poll()
            time.sleep(0.005)
        if not link.connected:
            raise RuntimeError(link.last_error)
        start = time.perf_counter()
        cpu_start = time.process_time()
        object_sync.full_sync(bpy.context, link)
        generation_ms = (time.perf_counter() - start) * 1000
        generation_cpu_ms = (time.process_time() - cpu_start) * 1000
        deadline = time.monotonic() + 180
        while (link.queued_bytes or link._wire_packet) and time.monotonic() < deadline:
            link.poll()
            time.sleep(0.001)
        if link.queued_bytes or link._wire_packet or link.last_error:
            raise RuntimeError("Transport incomplete: " + link.last_error)
        material_bake.invalidate()
        restored = (bpy.context.scene == scene and bpy.context.view_layer == source_layer
                    and source_counts == tuple(map(len, source_collections)))
        result = {"generation_ms": generation_ms, "export_and_drain_ms": (time.perf_counter() - start) * 1000,
            "generation_cpu_ms": generation_cpu_ms, "average_cpu_cores": generation_cpu_ms / generation_ms,
            "logical_cpu_count": os.cpu_count(),
            "source_datablock_counts": source_counts,
            "counts": dict(counts), "unique_geometry": len(geometry), "mesh_packets": sum(geometry.values()),
            "geometry_multiplicities": sorted(geometry.values(), reverse=True), "source_restored": restored,
            "addon_source": object_sync.__file__, "bake_source": material_bake.__file__,
            "bake_sha256": hashlib.sha256(Path(material_bake.__file__).read_bytes()).hexdigest(),
            "blender_version": bpy.app.version_string}
        if counts[protocol.FULL_SYNC_END] != 1 or not restored:
            raise RuntimeError("Missing End or source state mismatch")
        (output / "export.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
        print("PERFORMANCE_EXPORT_PASS", json.dumps(result), flush=True)
        # Keep the peer alive until Editor has applied End and the coordinator has captured the result.
        deadline = time.monotonic() + 600
        while not (output / "release").exists() and time.monotonic() < deadline:
            link.poll()
            time.sleep(0.05)
    finally:
        link.disconnect()
