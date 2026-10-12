import bpy
import json
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / "Tools/Blender"))
from ailu_live_link import material_sync

def describe(tree, depth=0):
    if depth > 5:
        return []
    return [{"name": node.name, "type": node.type,
             "inputs": [{"name": value.name,
                         "value": (list(value.default_value) if hasattr(value.default_value, "__len__")
                                   else value.default_value) if hasattr(value, "default_value") else None,
                         "links": [link.from_node.name + ":" + link.from_socket.name for link in value.links]}
                        for value in node.inputs],
             "group": describe(node.node_tree, depth + 1) if node.type == "GROUP" else []}
            for node in tree.nodes]

report = [{"material": mat.name, "nodes": describe(mat.node_tree)} for mat in bpy.data.materials
          if mat.use_nodes and any(value in mat.name.lower() for value in ("glass", "window", "headlight"))]
output = Path(sys.argv[sys.argv.index("--") + 1])
output.write_text(json.dumps(report, indent=2), encoding="utf-8")
print("BMW_GLASS_INSPECT", len(report), str(output))
