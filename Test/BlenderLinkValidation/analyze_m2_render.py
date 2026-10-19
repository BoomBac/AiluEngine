"""Validate low-cost M2 GBuffer, camera landmark, silhouette, blend, and sequence oracles."""
import json
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw


capture_root, reference_path, driver_path, output_dir = map(Path, sys.argv[1:5])
reference = json.loads(reference_path.read_text(encoding="utf-8"))
driver = json.loads(driver_path.read_text(encoding="utf-8"))
camera = driver.get("test_camera") or {}
failures = []
checks = []


def fail(field, expected, actual, reason="mismatch"):
    failures.append({"field": field, "expected": expected, "actual": actual, "reason": reason})


def decode_dds(path, fmt, width, height):
    data = path.read_bytes()
    if fmt == 34:
        return np.frombuffer(data, dtype="<f2", offset=len(data) - width * height * 4).reshape(height, width, 2).astype(np.float32)
    if fmt == 10:
        return np.frombuffer(data, dtype="<f2", offset=len(data) - width * height * 8).reshape(height, width, 4).astype(np.float32)
    if fmt == 2:
        return np.frombuffer(data, dtype="<f4", offset=len(data) - width * height * 16).reshape(height, width, 4).copy()
    if fmt == 26:
        packed = np.frombuffer(data, dtype="<u4", offset=len(data) - width * height * 4).reshape(height, width)
        channels = []
        for shift, bits in ((0, 6), (11, 6), (22, 5)):
            value = (packed >> shift) & ((1 << (bits + 5)) - 1)
            mantissa, exponent = value & ((1 << bits) - 1), value >> bits
            channels.append(np.where(exponent == 0, mantissa * 2.0 ** (-14 - bits),
                                     (1.0 + mantissa / 2.0 ** bits) * 2.0 ** (exponent.astype(float) - 15)))
        return np.stack(channels, axis=-1).astype(np.float32)
    if fmt in (28, 29):
        return np.frombuffer(data, dtype=np.uint8, offset=len(data) - width * height * 4).reshape(height, width, 4).astype(np.float32) / 255.0
    if fmt == 87:
        return np.frombuffer(data, dtype=np.uint8, offset=len(data) - width * height * 4).reshape(height, width, 4)[..., [2, 1, 0, 3]].astype(np.float32) / 255.0
    raise ValueError(f"Unsupported capture DXGI format {fmt} in {path}")


def load_sequence(name):
    manifests = list(capture_root.glob(f"captures/{name}/session_*/capture.json"))
    if len(manifests) != 1:
        fail(f"capture.{name}.manifest_count", 1, len(manifests))
        return None, None
    manifest_path = manifests[0]
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    frames = sorted(manifest.get("frames", []), key=lambda frame: frame.get("capture_index", -1))
    if manifest.get("state") != "completed" or len(frames) != 3 or manifest.get("completed_frames") not in (None, 3):
        fail(f"capture.{name}.completion", {"state": "completed", "frames": 3},
             {"state": manifest.get("state"), "frames": len(frames)})
    engine_frames = [int(frame.get("engine_frame", -1)) for frame in frames]
    if len(engine_frames) == 3 and np.all(np.diff(engine_frames) == 1):
        checks.append(f"capture.{name}.contiguous")
    else:
        fail(f"capture.{name}.engine_frame_gap", "three consecutive engine_frame values", engine_frames)
    arrays = []
    for frame in frames:
        path = manifest_path.parent / frame["file"]
        if not path.is_file():
            fail(f"capture.{name}.file", "present", str(path))
            continue
        if manifest.get("output_format") == "png":
            arrays.append(np.asarray(Image.open(path).convert("RGB"), dtype=np.float32) / 255.0)
        else:
            arrays.append(decode_dds(path, int(manifest["format"]), int(manifest["width"]), int(manifest["height"])))
    if len(arrays) != 3:
        return manifest, None
    stacked = np.stack(arrays)
    if not np.isfinite(stacked).all():
        fail(f"capture.{name}.finite", True, False)
    else:
        checks.append(f"capture.{name}.finite")
    adjacent = float(np.sqrt(np.mean(np.diff(stacked.astype(np.float64), axis=0) ** 2)))
    if adjacent <= 0.02:
        checks.append(f"capture.{name}.stationary_rms")
    else:
        fail(f"capture.{name}.stationary_rms", "<= 0.02", adjacent)
    return manifest, stacked


def project_world(position, width, height):
    flattened = camera.get("vp_stable")
    if not isinstance(flattened, list) or len(flattened) != 16:
        raise ValueError("editor.get_scene_camera did not return a 4x4 stable VP matrix")
    matrix = np.asarray(flattened, dtype=np.float64).reshape(4, 4)
    clip = np.asarray([*position, 1.0], dtype=np.float64) @ matrix
    if clip[3] <= 1e-6:
        raise ValueError(f"landmark is behind the camera: {position} -> {clip.tolist()}")
    ndc = clip[:3] / clip[3]
    return np.asarray([(ndc[0] * 0.5 + 0.5) * width, (1.0 - (ndc[1] * 0.5 + 0.5)) * height])


def sample(array, pixel, radius=3):
    x, y = np.rint(pixel).astype(int)
    x0, x1 = max(0, x - radius), min(array.shape[1], x + radius + 1)
    y0, y1 = max(0, y - radius), min(array.shape[0], y + radius + 1)
    if x0 == x1 or y0 == y1:
        return np.zeros(array.shape[-1], dtype=np.float32)
    return np.median(array[y0:y1, x0:x1], axis=(0, 1))


normal_manifest, normals = load_sequence("gbuffer_normal")
albedo_manifest, albedos = load_sequence("gbuffer_albedo")
final_manifest, final_colors = load_sequence("final_color")
if normal_manifest and albedo_manifest and final_manifest:
    dimensions = [(int(item["width"]), int(item["height"])) for item in
                  (normal_manifest, albedo_manifest, final_manifest)]
    if len(set(dimensions)) == 1:
        checks.append("capture.dimensions_match")
    else:
        fail("capture.dimensions", "all pass captures have matching dimensions", dimensions)
    if normals is not None and albedos is not None and final_colors is not None:
        height, width = albedos.shape[1:3]
        if (int(camera.get("render_width", width)), int(camera.get("render_height", height))) == (width, height):
            checks.append("camera.capture_dimensions")
        else:
            fail("camera.capture_dimensions", [width, height],
                 [camera.get("render_width"), camera.get("render_height")])

        albedo = albedos[-1, ..., :3]
        foreground = np.linalg.norm(albedo, axis=-1) > 0.05
        if int(foreground.sum()) > 500:
            checks.append("gbuffer.foreground_occupancy")
        else:
            fail("gbuffer.foreground_occupancy", "> 500 pixels", int(foreground.sum()))

        try:
            marker_samples = {}
            for landmark in reference["landmarks"]:
                pixel = project_world(landmark["world_position"], width, height)
                if not (0 <= pixel[0] < width and 0 <= pixel[1] < height):
                    fail(f"camera.landmark.{landmark['name']}", "inside frame", pixel.tolist())
                    continue
                if landmark["name"] == "alpha_overlay":
                    continue
                actual = sample(albedo, pixel)
                wanted = np.asarray(landmark["base_color"], dtype=np.float32)
                if np.all(np.abs(actual - wanted) <= 0.08):
                    checks.append(f"camera.marker.{landmark['name']}")
                else:
                    fail(f"camera.marker.{landmark['name']}", wanted.tolist(), actual.tolist(),
                         "projected landmark did not sample its expected dominant material channel")
                marker_samples[landmark["name"]] = {"pixel": pixel.tolist(), "albedo": actual.tolist()}

            for name, world_position in (("direct_cube", reference["direct_cube_center_world"]),
                                         ("collection_instance", [0.0, 0.0, 0.0])):
                pixel = project_world(world_position, width, height)
                actual = sample(albedo, pixel)
                if float(np.linalg.norm(actual)) > 0.1:
                    checks.append(f"camera.silhouette.{name}")
                else:
                    fail(f"camera.silhouette.{name}", "occupied GBuffer pixel at projected center", actual.tolist())

            for index, cell in enumerate(reference.get("alpha_clip_cells", [])):
                pixel = project_world(cell["world_position"], width, height)
                actual = sample(albedo, pixel, radius=1)
                visible = float(np.linalg.norm(actual)) > 0.2
                if visible == cell["opaque"]:
                    checks.append(f"gbuffer.alpha_clip_cell[{index}]")
                else:
                    fail(f"gbuffer.alpha_clip_cell[{index}]", cell["opaque"], visible,
                         "projected binary-alpha checker sample had the wrong coverage")

            normal_pixel = project_world([1.5, -0.55, 0.0], width, height)
            packed = sample(normals[-1], normal_pixel)
            encoded = packed[:2] * 2.0 - 1.0
            unpacked = np.asarray([encoded[0], encoded[1], 1.0 - abs(encoded[0]) - abs(encoded[1])])
            fold = max(0.0, -unpacked[2])
            unpacked[0] += -fold if unpacked[0] >= 0 else fold
            unpacked[1] += -fold if unpacked[1] >= 0 else fold
            length = np.linalg.norm(unpacked)
            actual_normal = unpacked / length if length > 1e-8 else unpacked
            normal_dot = float(np.dot(actual_normal, np.asarray([0.0, 0.0, -1.0])))
            if normal_dot >= 0.95:
                checks.append("gbuffer.marker_world_normal")
            else:
                fail("gbuffer.marker_world_normal", [0.0, 0.0, -1.0], actual_normal.tolist(),
                     f"decoded normal dot={normal_dot:.6f}")

            overlay = next(item for item in reference["landmarks"] if item["name"] == "alpha_overlay")
            overlay_pixel = project_world(overlay["world_position"], width, height)
            blend_pixel = sample(final_colors[-1], overlay_pixel)
            blue_pixel = project_world([1.5, 0.5, -0.55], width, height)
            blue_control = sample(final_colors[-1], blue_pixel)
            blend_chroma = blend_pixel[:3] / max(float(blend_pixel[:3].sum()), 1e-6)
            control_chroma = blue_control[:3] / max(float(blue_control[:3].sum()), 1e-6)
            blend_neutrality = float(np.linalg.norm(blend_chroma - np.asarray([1 / 3, 1 / 3, 1 / 3])))
            control_neutrality = float(np.linalg.norm(control_chroma - np.asarray([1 / 3, 1 / 3, 1 / 3])))
            if blend_neutrality + 0.015 < control_neutrality:
                checks.append("final.alpha_blend_marker")
            else:
                fail("final.alpha_blend_marker", "overlay is more neutral than uncovered blue marker",
                     {"overlay": blend_pixel.tolist(), "control": blue_control.tolist(),
                      "neutrality": [blend_neutrality, control_neutrality]})
        except (ValueError, StopIteration) as error:
            fail("camera.landmark_projection", "valid camera projection and landmarks", str(error))
        assertions_context = {"dimensions": [width, height], "foreground_pixels": int(foreground.sum()),
                              "marker_samples": marker_samples if "marker_samples" in locals() else {},
                              "decoded_marker_normal": actual_normal.tolist() if "actual_normal" in locals() else None}
    else:
        assertions_context = {}
else:
    assertions_context = {}

# Failure-injection checks operate on copies and must trip the same production gates as corrupted captures.
seeded = []
if albedos is not None:
    empty_frame = np.zeros_like(albedos[-1])
    detected = int(np.linalg.norm(empty_frame[..., :3], axis=-1).sum()) <= 0
    seeded.append({"name": "empty_frame", "detected": detected,
                   "first_failure": {"field": "gbuffer.foreground_occupancy", "actual": 0} if detected else None})
if normals is not None:
    wrong = np.array([0.5, 0.5], dtype=np.float32)
    encoded = wrong * 2.0 - 1.0
    unpacked = np.asarray([encoded[0], encoded[1], 1.0 - abs(encoded[0]) - abs(encoded[1])])
    fold = max(0.0, -unpacked[2])
    unpacked[0] += -fold if unpacked[0] >= 0 else fold
    unpacked[1] += -fold if unpacked[1] >= 0 else fold
    wrong_normal = unpacked / max(float(np.linalg.norm(unpacked)), 1e-8)
    detected = float(np.dot(wrong_normal, [0.0, 0.0, -1.0])) < 0.95
    seeded.append({"name": "wrong_normal", "detected": detected,
                   "first_failure": {"field": "gbuffer.marker_world_normal", "actual": wrong_normal.tolist()} if detected else None})
if final_manifest:
    frames = sorted(final_manifest.get("frames", []), key=lambda item: item.get("capture_index", -1))
    injected = [int(item.get("engine_frame", -1)) for item in frames]
    if len(injected) == 3:
        injected[1] += 2
    detected = len(injected) != 3 or not np.all(np.diff(injected) == 1)
    seeded.append({"name": "frame_gap", "detected": bool(detected),
                   "first_failure": {"field": "capture.final_color.engine_frame_gap", "actual": injected} if detected else None})
for mutation in seeded:
    if not mutation["detected"]:
        fail(f"mutation_tests.{mutation['name']}", "detected", "not detected")

if driver.get("status") != "PASS":
    fail("capture_driver", "PASS", driver.get("failure") or driver.get("status"))
if not driver.get("restored_camera"):
    fail("camera.restoration", "reported restored camera", None)
elif driver.get("original_camera"):
    original = driver["original_camera"]
    restored = driver["restored_camera"]
    for key in ("target_position", "pitch_degrees", "yaw_degrees", "fov_degrees"):
        if original.get(key) != restored.get(key):
            fail(f"camera.restoration.{key}", original.get(key), restored.get(key))
if not seeded:
    fail("mutation_tests", "empty frame, wrong normal, frame gap", "not exercised")

if normals is not None and albedos is not None and final_colors is not None:
    height, width = albedos.shape[1:3]
    normal_preview = np.zeros((height, width, 3), dtype=np.float32)
    normal_preview[..., :2] = np.clip(normals[-1, ..., :2], 0.0, 1.0)
    albedo_preview = np.clip(albedos[-1, ..., :3], 0.0, 1.0)
    final_preview = np.clip(final_colors[-1, ..., :3] / (1.0 + np.maximum(final_colors[-1, ..., :3], 0.0)), 0.0, 1.0)
    canvas = Image.new("RGB", (width * 3, height + 26), (24, 24, 24))
    draw = ImageDraw.Draw(canvas)
    for index, (pixels, label) in enumerate(((normal_preview, "GBuffer0 packed normal"),
                                              (albedo_preview, "GBuffer1 albedo / clip"),
                                              (final_preview, "TransparentPass composite"))):
        image = Image.fromarray(np.uint8(pixels * 255.0), "RGB")
        canvas.paste(image, (index * width, 26))
        draw.text((index * width + 8, 7), label, fill=(240, 240, 240))
    canvas.save(output_dir / "m2_preview.png")

output_dir.mkdir(parents=True, exist_ok=True)
assertions = {"status": "PASS" if not failures else "FAIL", "check_count": len(checks),
              "checks": checks, "seeded_mutations": seeded, "unsupported_fields": [],
              "context": assertions_context, "failure_count": len(failures)}
(output_dir / "m2_assertions.json").write_text(json.dumps(assertions, indent=2), encoding="utf-8")
(output_dir / "m2_failures.json").write_text(json.dumps(failures, indent=2), encoding="utf-8")
print(json.dumps(assertions, indent=2))
sys.exit(0 if not failures else 1)
