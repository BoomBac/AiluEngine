"""Verify native RGBA32F raster captures produced by SamplerSmoke."""
import json
import struct
import sys
from pathlib import Path


def read_dds(path):
    data = path.read_bytes()
    assert data[:4] == b"DDS ", path
    height, width = struct.unpack_from("<II", data, 12)
    if data[84:88] == b"DX10":
        assert struct.unpack_from("<I", data, 128)[0] == 2, "expected R32G32B32A32_FLOAT"
        offset = 148
    else:
        assert struct.unpack_from("<I", data, 84)[0] == 116, "expected legacy A32B32G32R32F"
        offset = 128
    return width, height, list(struct.iter_unpack("<4f", data[offset:offset + width * height * 16]))


def main(directory):
    directory = Path(directory)
    expected = {"raster_base": None, "raster_auto": (0, 0, 1, 1), "raster_opt_out": None,
                "raster_combined": (1, 1, 1, 1), "raster_negative": None}
    summary = {}
    for phase, color in expected.items():
        paths = list(directory.rglob(phase + "*.dds"))
        assert len(paths) == 1, (phase, paths)
        width, height, pixels = read_dds(paths[0])
        assert (width, height) == (4, 4), (phase, width, height)
        if color is not None:
            error = max(abs(a - b) for pixel in pixels for a, b in zip(pixel, color))
            assert error < 0.02, (phase, error, pixels)
        else:
            red = sum(max(abs(a - b) for a, b in zip(pixel, (1, 0, 0, 1))) < 0.02 for pixel in pixels)
            green = sum(max(abs(a - b) for a, b in zip(pixel, (0, 1, 0, 1))) < 0.02 for pixel in pixels)
            assert red == green == 8, (phase, pixels)
            error = 0.0
        summary[phase] = {"file": str(paths[0]), "size": [width, height], "max_error": error}
    assert (directory / "checks.txt").read_text().strip().endswith("PASS")
    assert (directory / "shutdown.txt").is_file(), "shutdown did not complete"
    assert not (directory / "gpu_errors.txt").exists(), "D3D12 validation raised an exception"
    (directory / "analysis.json").write_text(json.dumps(summary, indent=2))
    print("PASS: five raster captures, GPU samples, persistence, camera policy and clean shutdown")


if __name__ == "__main__":
    main(sys.argv[1])
