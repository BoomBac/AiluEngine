"""Compare binary mesh export against independent per-corner Blender coordinate conversion."""
import struct
import sys
from pathlib import Path

import bpy
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "Tools" / "Blender"))
from ailu_live_link import coordinate, mesh_sync

bpy.ops.mesh.primitive_cube_add()
obj = bpy.context.object
for index in range(2):
    obj.data.materials.append(bpy.data.materials.new("BulkSlot" + str(index)))
for index, polygon in enumerate(obj.data.polygons):
    polygon.material_index = index % 2

for with_uv in (True, False):
    if not with_uv:
        for layer in list(obj.data.uv_layers):
            obj.data.uv_layers.remove(layer)
        obj.data.update()
        bpy.context.view_layer.update()
    depsgraph = bpy.context.evaluated_depsgraph_get()
    payload = mesh_sync.encode_mesh(obj, depsgraph)
    header = struct.Struct("<36sQII6f")
    _, _, count, slots, *bounds = header.unpack_from(payload)
    decoded = np.frombuffer(payload, dtype="<f4", count=count * 12, offset=header.size)
    position = decoded[:count * 3].reshape(-1, 3)
    normal = decoded[count * 3:count * 6].reshape(-1, 3)
    tangent = decoded[count * 6:count * 10].reshape(-1, 4)
    uv = decoded[count * 10:].reshape(-1, 2)
    evaluated = obj.evaluated_get(depsgraph)
    mesh = evaluated.to_mesh(preserve_all_data_layers=True, depsgraph=depsgraph)
    try:
        mesh.calc_loop_triangles()
        if with_uv:
            mesh.calc_tangents(uvmap=mesh.uv_layers.active.name)
        expected = {"position": [], "normal": [], "tangent": [], "uv": []}
        indices = {}
        corner_indices = []
        for triangle in mesh.loop_triangles:
            for loop_index in triangle.loops:
                loop = mesh.loops[loop_index]
                indices.setdefault(triangle.material_index, []).append(loop_index)
                corner_indices.append(loop_index)
                expected["position"].append(tuple(coordinate.blender_to_ailu_position(mesh.vertices[loop.vertex_index].co)))
                expected["normal"].append(tuple(coordinate.blender_to_ailu_direction(loop.normal)))
                expected["tangent"].append((*coordinate.blender_to_ailu_direction(loop.tangent),
                                            1.0 if loop.bitangent_sign < 0 else -1.0) if with_uv else (1, 0, 0, 1))
                source_uv = mesh.uv_layers.active.data[loop_index].uv if with_uv else None
                expected["uv"].append((source_uv.x, 1.0 - source_uv.y) if with_uv else (0, 0))
        for name, values in (("position", position), ("normal", normal), ("tangent", tangent), ("uv", uv)):
            assert np.allclose(values[corner_indices], expected[name], atol=1e-6), (with_uv, name)
        assert np.allclose(bounds[:3], position.min(axis=0)) and np.allclose(bounds[3:], position.max(axis=0))
        offset = header.size + decoded.nbytes
        assert slots == len(indices)
        for slot in sorted(indices):
            actual_slot, index_count = struct.unpack_from("<II", payload, offset)
            offset += 8
            assert actual_slot == slot
            actual = np.frombuffer(payload, dtype="<u4", count=index_count, offset=offset)
            assert actual.tolist() == indices[slot]
            offset += actual.nbytes
        assert offset == len(payload)
    finally:
        evaluated.to_mesh_clear()
print("MESH_BULK PASS: UV/no-UV positions, normals, tangent sign, UVs, bounds, corner order and two submesh slots")
