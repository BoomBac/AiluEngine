import struct
import tempfile
from array import array

from . import coordinate, identity, protocol

_mesh_revision = {}
_session_temp_dir = None


def _temp_dir():
    global _session_temp_dir
    if _session_temp_dir is None:
        _session_temp_dir = tempfile.mkdtemp(prefix="ailu_live_link_")
    return _session_temp_dir


def encode_mesh(obj, depsgraph):
    evaluated = obj.evaluated_get(depsgraph)
    mesh = evaluated.to_mesh(preserve_all_data_layers=True, depsgraph=depsgraph)
    try:
        mesh.calc_loop_triangles()
        uv_layer = mesh.uv_layers.active
        if uv_layer:
            try:
                mesh.calc_tangents(uvmap=uv_layer.name)
            except RuntimeError:
                pass
        positions, normals, tangents, uvs = [], [], [], []
        submeshes = {}
        for triangle in mesh.loop_triangles:
            slot = max(0, triangle.material_index)
            indices = submeshes.setdefault(slot, [])
            for loop_index in triangle.loops:
                loop = mesh.loops[loop_index]
                vertex = mesh.vertices[loop.vertex_index]
                position = coordinate.blender_to_ailu_position(vertex.co)
                normal = coordinate.blender_to_ailu_direction(loop.normal)
                positions.append(tuple(position))
                normals.append(tuple(normal))
                tangent = getattr(loop, "tangent", None)
                tangent = coordinate.blender_to_ailu_direction(tangent) if tangent else (1.0, 0.0, 0.0)
                handedness = -1.0 if getattr(loop, "bitangent_sign", 1.0) < 0.0 else 1.0
                tangents.append((*tangent, handedness))
                uv = uv_layer.data[loop_index].uv if uv_layer else (0.0, 0.0)
                uvs.append((float(uv[0]), float(uv[1])))
                indices.append(len(positions) - 1)
        if not positions:
            return None
        minima = tuple(min(position[axis] for position in positions) for axis in range(3))
        maxima = tuple(max(position[axis] for position in positions) for axis in range(3))
        guid = identity.get_guid(obj).encode("ascii")
        revision = _mesh_revision.get(guid, 0) + 1
        _mesh_revision[guid] = revision
        chunks = [struct.pack("<36sQII6f", guid, revision, len(positions), len(submeshes), *minima, *maxima)]
        chunks.append(array("f", (v for row in positions for v in row)).tobytes())
        chunks.append(array("f", (v for row in normals for v in row)).tobytes())
        chunks.append(array("f", (v for row in tangents for v in row)).tobytes())
        chunks.append(array("f", (v for row in uvs for v in row)).tobytes())
        for slot, indices in sorted(submeshes.items()):
            chunks.append(struct.pack("<II", slot, len(indices)))
            chunks.append(array("I", indices).tobytes())
        return b"".join(chunks)
    finally:
        evaluated.to_mesh_clear()


def send_mesh(obj, depsgraph, link):
    payload = encode_mesh(obj, depsgraph)
    return payload is not None and link.send(protocol.MESH_UPSERT, payload)
