"""Compare exported material semantics and consumed texture channels from two native packet recordings."""
import json
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

output = Path(sys.argv[1])
stage = sys.argv[2] if len(sys.argv) > 2 else "optimized"


def Materials(path):
    values = {}
    owners = {}
    materials = []
    with path.open("rb") as stream:
        while header := stream.read(20):
            _, _, kind, size, _ = struct.unpack("<IHHIQ", header)
            if kind not in {12, 21}:
                stream.seek(size, 1)
                continue
            value = json.loads(stream.read(size))
            if kind == 12:
                for guid in value["material_guids"]:
                    owners.setdefault(guid, []).append(value["name"])
            else:
                materials.append(value)
    for value in materials:
        # Match the actual binding owners, independent of randomized GUIDs and the first bake owner suffix.
        key = (value["name"].split("@", 1)[0], tuple(sorted(owners[value["material_guid"]])))
        assert key not in values, "Ambiguous material binding identity"
        values[key] = value
    return values


result = {}
for scene in ("bmw", "classroom"):
    before = Materials(output / "baseline" / f"{scene}_0/packets.bin")
    after = Materials(output / stage / f"{scene}_0/packets.bin")
    assert before.keys() == after.keys(), "Material binding identities changed"
    differences = []
    compared = 0
    for identity, old in before.items():
        name = old["name"]
        new = after[identity]
        paths = {key for key in old if key.endswith("_path")}
        ignored = paths | {"material_guid", "name"}
        assert {key: value for key, value in old.items() if key not in ignored} == {
            key: value for key, value in new.items() if key not in ignored}, name
        for key in sorted(paths):
            assert bool(old[key]) == bool(new[key]), (name, key)
            if not old[key]:
                continue
            first = np.asarray(Image.open(old[key]), dtype=np.float32)
            second = np.asarray(Image.open(new[key]), dtype=np.float32)
            assert first.shape == second.shape, (name, key)
            if key in {"roughness_path", "metallic_path"} and first.ndim == 3:
                first, second = first[:, :, 0], second[:, :, 0]
            delta = np.abs(first - second)
            maximum = float(delta.max())
            compared += 1
            if maximum:
                differences.append({"material": name, "channel": key, "max": maximum,
                                    "rms": float(np.sqrt(np.mean(delta ** 2)))})
            assert maximum <= 2, (name, key, maximum)
    result[scene] = {"materials": len(before), "textures_compared": compared, "differences": differences}
(output / f"{stage}-texture-comparison.json").write_text(json.dumps(result, indent=2))
print(json.dumps(result, indent=2))
