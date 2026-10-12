"""Verify raw GBuffer scalar multiplication and independently cleared Live Link texture slots."""
import json
import struct
import sys
import zlib
from pathlib import Path

import numpy as np


def Decode(path, fmt, width, height):
    data = path.read_bytes()
    if fmt == 28:
        return np.frombuffer(data, dtype="u1", offset=len(data) - width * height * 4).reshape(height, width, 4) / 255.
    if fmt == 10:
        return np.frombuffer(data, dtype="<f2", offset=len(data) - width * height * 8).reshape(height, width, 4)
    raise ValueError(f"Unsupported raw GBuffer format {fmt}")


def SavePng(path, pixels):
    pixels = np.ascontiguousarray(np.clip(pixels * 255, 0, 255).astype(np.uint8))
    height, width, _ = pixels.shape
    def Chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    rows = b"".join(b"\0" + row.tobytes() for row in pixels)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + Chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                     + Chunk(b"IDAT", zlib.compress(rows)) + Chunk(b"IEND", b""))


root = Path(sys.argv[1])
report_path = Path(sys.argv[2])
report_path.parent.mkdir(parents=True, exist_ok=True)
results = []
for expected in json.loads((root / "expected.json").read_text()):
    for channel, resource in (("roughness", "_GBuffer1"), ("metallic", "_GBuffer2")):
        name = expected["name"] + "_" + channel
        manifests = list((root / name).glob("session_*/capture.json"))
        assert len(manifests) == 1, name
        manifest_path = manifests[0]
        manifest = json.loads(manifest_path.read_text())
        frames = sorted(manifest["frames"], key=lambda frame: frame["capture_index"])
        assert manifest["state"] == "completed" and len(frames) == manifest["frame_count"] == 3, name
        assert [frame["capture_index"] for frame in frames] == list(range(3)), name
        assert all(b["engine_frame"] == a["engine_frame"] + 1 for a, b in zip(frames, frames[1:])), name
        status = json.loads((root / (name + "_status.json")).read_text())
        assert status["pass_name"] == "DeferedGeometryPass" and status["resource_name"] == resource, name
        images = np.stack([Decode(manifest_path.parent / frame["file"], manifest["format"],
                                  manifest["width"], manifest["height"]) for frame in frames])
        assert np.isfinite(images).all(), name
        height, width = images.shape[1:3]
        roi = images[:, height // 2 - 8:height // 2 + 8, width // 2 - 8:width // 2 + 8]
        if channel == "roughness":
            assert np.min(roi[..., :3]) > .9, "Center ROI must lie on the white fixture cube"
        error = float(np.max(abs(roi[..., 3] - expected[channel])))
        assert error <= 2 / 255, (name, error, expected[channel], float(np.mean(roi[..., 3])))
        results.append({"case": name, "expected": expected[channel], "measured": float(np.mean(roi[..., 3])),
                        "max_error": error, "frames": [frame["engine_frame"] for frame in frames]})
        if expected["name"] == "both":
            preview = np.repeat(images[-1, ..., 3:4], 3, axis=-1)
            SavePng(report_path.parent / (channel + ".png"), preview)
restoration = json.loads((root / "restoration.json").read_text())
assert all(entry["restored"] for entry in restoration), "Editor state restoration"
report_path.write_text(json.dumps({"passed": True, "cases": results,
                                  "restored_files": len(restoration)}, indent=2))
print(f"SCALAR_GPU PASS: {len(results)} scalar cases, 24 finite contiguous frames, restored {len(restoration)} files")
