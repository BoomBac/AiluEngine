"""Compare independent equal-spp blocks from raw Editor HDR captures (no denoising)."""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from analyze_path_tracer_stage1 import ReadDds, DisplayPixels


def FrameDirectory(root, name):
    status = json.loads((root / (name + "_raw_status.json")).read_text())
    directory = Path(status["output_directory"])
    manifest = json.loads((directory / "capture.json").read_text())
    return status, directory, manifest


def ReadBlocks(root, name, matched_frames, block_size=16):
    status, directory, manifest = FrameDirectory(root, name)
    frames = sorted(manifest["frames"], key=lambda frame: frame["capture_index"])
    assert status["state"] == "completed" and len(frames) == 256
    indices = [frame["engine_frame"] for frame in frames]
    assert all(b == a + 1 for a, b in zip(indices, indices[1:])), "Capture frames are not contiguous"
    captured_count = len(frames)
    frames = [frame for frame in frames if frame["engine_frame"] in matched_frames]
    assert len(frames) % block_size == 0
    blocks, total = [], None
    for index, frame in enumerate(frames):
        rgb = ReadDds(directory / frame["file"])[..., :3].astype(np.float64)
        assert np.isfinite(rgb).all() and (rgb >= 0).all()
        total = rgb if total is None else total + rgb
        if (index + 1) % block_size == 0:
            blocks.append(total / block_size)
            total = None
    return np.array(blocks), {"captured_count": captured_count, "paired_count": len(frames),
                              "engine_frames": [frames[0]["engine_frame"], frames[-1]["engine_frame"]],
                              "dimensions": [manifest["width"], manifest["height"]]}


def Main():
    root = Path(sys.argv[1])
    ndf_name = sys.argv[2] if len(sys.argv) > 2 else "ndf_hdr"
    ndf_frames = {frame["engine_frame"] for frame in FrameDirectory(root, ndf_name)[2]["frames"]}
    vndf_frames = {frame["engine_frame"] for frame in FrameDirectory(root, "vndf")[2]["frames"]}
    matched_frames = sorted(ndf_frames & vndf_frames)
    matched_frames = set(matched_frames[:len(matched_frames) // 16 * 16])
    assert len(matched_frames) >= 128, "Insufficient matching DXR initial seeds"
    ndf, ndf_integrity = ReadBlocks(root, ndf_name, matched_frames)
    vndf, vndf_integrity = ReadBlocks(root, "vndf", matched_frames)
    # Reference uses disjoint seeds from the first four low-spp trials; the arms use common random numbers.
    reference = np.concatenate([ndf[4:], vndf[4:]]).mean(axis=0)
    rois = {"floor": (50, 225, 590, 340), "gold": (230, 145, 290, 185),
            "silver": (350, 148, 390, 184), "diffuse": (304, 151, 333, 175)}
    results = {"ndf_integrity": ndf_integrity, "vndf_integrity": vndf_integrity,
               "reference_spp": (len(ndf) - 4) * 32, "paired_spp": len(matched_frames),
               "rng": "Matched engine_frame = DXR _FrameIndex initial seeds; four disjoint 16-spp trials per arm",
               "rois": {}}
    for name, (x0, y0, x1, y1) in rois.items():
        selection = np.s_[:, y0:y1, x0:x1, :]
        a, b = ndf[selection], vndf[selection]
        ref = reference[y0:y1, x0:x1]
        mse_a = float(np.mean((a[:4] - ref) ** 2))
        mse_b = float(np.mean((b[:4] - ref) ** 2))
        var_a, var_b = float(a.var(axis=0, ddof=1).mean()), float(b.var(axis=0, ddof=1).mean())
        results["rois"][name] = {"rect": [x0, y0, x1, y1], "ndf_mse_16spp": mse_a,
            "vndf_mse_16spp": mse_b, "mse_reduction_percent": 100 * (1 - mse_b / mse_a),
            "ndf_variance_16spp": var_a, "vndf_variance_16spp": var_b,
            "variance_reduction_percent": 100 * (1 - var_b / var_a),
            "ndf_mean_paired": float(a.mean()), "vndf_mean_paired": float(b.mean()),
            "mean_difference_percent": 100 * float(b.mean() / a.mean() - 1)}
    width, height = reference.shape[1], reference.shape[0]
    canvas = Image.new("RGB", (width * 3, (height + 32) * 2), (24, 24, 24))
    draw = ImageDraw.Draw(canvas)
    for row, spp in enumerate([16, len(matched_frames)]):
        for col, (label, blocks) in enumerate([("NDF", ndf), ("Heitz 2018 VNDF", vndf), ("Pooled reference", None)]):
            rgb = reference if blocks is None else blocks[:spp // 16].mean(axis=0)
            image = Image.fromarray(DisplayPixels(rgb))
            canvas.paste(image, (col * width, row * (height + 32) + 32))
            draw.text((col * width + 12, row * (height + 32) + 10),
                      label + (f" / {results['reference_spp']} estimates" if blocks is None
                               else f" / {spp} spp, paired seeds"), fill="white")
    canvas.save(root / "comparison.png")
    floor = rois["floor"]
    crop_width = (floor[2] - floor[0]) * 2
    crop = Image.new("RGB", (crop_width * 2, (floor[3] - floor[1]) * 2 + 32), (24, 24, 24))
    crop_draw = ImageDraw.Draw(crop)
    for column, (label, blocks) in enumerate([("NDF / 16 spp", ndf), ("VNDF / 16 spp", vndf)]):
        image = Image.fromarray(DisplayPixels(blocks[0])).crop(floor).resize(
            (crop_width, (floor[3] - floor[1]) * 2), Image.Resampling.NEAREST)
        crop.paste(image, (column * crop_width, 32))
        crop_draw.text((column * crop_width + 10, 10), label, fill="white")
    crop.save(root / "floor_comparison.png")
    (root / "analysis.json").write_text(json.dumps(results, indent=2))
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    Main()
