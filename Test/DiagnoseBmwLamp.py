"""Read-only lamp mesh/material inspection from the original BMW blend."""
import bpy
import json
import sys
from pathlib import Path

output = Path(sys.argv[sys.argv.index("--") + 1])
output.mkdir(parents=True, exist_ok=True)
keywords = sys.argv[sys.argv.index("--") + 2:] or ["light"]
objects = []
materials = {}
for obj in bpy.context.scene.objects:
    if obj.type != "MESH" or not any(word.lower() in name.lower() for word in keywords
        for name in [obj.name] + [slot.name for slot in obj.material_slots]):
        continue
    mesh = obj.data
    uv = mesh.uv_layers.active
    objects.append({"name": obj.name, "mesh": mesh.name, "vertices": len(mesh.vertices),
                    "faces": len(mesh.polygons), "materials": [slot.name for slot in obj.material_slots],
                    "transform": [list(row) for row in obj.matrix_world],
                    "uv_bounds": [[min(item.uv[axis] for item in uv.data), max(item.uv[axis] for item in uv.data)]
                                  for axis in range(2)] if uv else None})
    for slot in obj.material_slots:
        material = slot.material
        if not material or material.name in materials or not material.use_nodes:
            continue
        materials[material.name] = [{"name": node.name, "type": node.type,
            "inputs": [{"name": item.name,
                        "value": (list(item.default_value) if hasattr(item.default_value, "__len__")
                                  else item.default_value) if hasattr(item, "default_value") else None,
                        "links": [link.from_node.name + "." + link.from_socket.name for link in item.links]}
                       for item in node.inputs]}
            for node in material.node_tree.nodes]
(output / "lamp_geometry.json").write_text(json.dumps({"objects": objects, "materials": materials}, indent=2))
print("BMW_LAMP_INSPECTION_COMPLETE", len(objects))
