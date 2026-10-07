import struct
import time
import tempfile
from array import array
import numpy as np

from . import coordinate, identity, protocol

_mesh_revision = {}
_session_temp_dir = None


def _temp_dir():
    global _session_temp_dir
    if _session_temp_dir is None:
        _session_temp_dir = tempfile.mkdtemp(prefix="ailu_live_link_")
    return _session_temp_dir


def encode_mesh(obj, depsgraph):
    from . import material_bake
    evaluated = obj.evaluated_get(depsgraph)
    baked = material_bake.object_needs_bake(obj)
    mesh = (material_bake.prepared_mesh(obj, depsgraph).copy() if baked
            else evaluated.to_mesh(preserve_all_data_layers=True, depsgraph=depsgraph))
    try:
        mesh.calc_loop_triangles()
        uv_layer = mesh.uv_layers.active
        uv_values = array("f", [0.0]) * (len(mesh.loops) * 2)
        has_tangents = False
        if uv_layer:
            uv_map_name = uv_layer.name
            uv_layer.data.foreach_get("uv", uv_values)
            try:
                mesh.calc_tangents(uvmap=uv_map_name)
                has_tangents = True
            except RuntimeError:
                pass
        triangle_vertex_count = len(mesh.loop_triangles) * 3
        if not triangle_vertex_count:
            return None
        vertex_count = len(mesh.loops)
        triangle_loops = np.empty(triangle_vertex_count, dtype=np.int32)
        mesh.loop_triangles.foreach_get("loops", triangle_loops)
        loop_vertices = np.empty(len(mesh.loops), dtype=np.int32)
        mesh.loops.foreach_get("vertex_index", loop_vertices)
        coordinates = np.empty(len(mesh.vertices) * 3, dtype=np.float32)
        mesh.vertices.foreach_get("co", coordinates)

        def convert_basis(values, normalize=False):
            converted = values[:, [0, 2, 1]].copy()
            converted[:, 2] *= -1
            if normalize:
                lengths = np.linalg.norm(converted, axis=1, keepdims=True)
                np.divide(converted, lengths, out=converted, where=lengths > 0)
            return converted

        positions = convert_basis(coordinates.reshape(-1, 3)[loop_vertices])
        loop_normals = np.empty(len(mesh.loops) * 3, dtype=np.float32)
        mesh.loops.foreach_get("normal", loop_normals)
        normals = convert_basis(loop_normals.reshape(-1, 3), normalize=True)
        tangents = np.empty((vertex_count, 4), dtype=np.float32)
        if has_tangents:
            loop_tangents = np.empty(len(mesh.loops) * 3, dtype=np.float32)
            mesh.loops.foreach_get("tangent", loop_tangents)
            loop_signs = np.empty(len(mesh.loops), dtype=np.float32)
            mesh.loops.foreach_get("bitangent_sign", loop_signs)
            tangents[:, :3] = convert_basis(loop_tangents.reshape(-1, 3), normalize=True)
            tangents[:, 3] = np.where(loop_signs < 0, -1.0, 1.0)
        else:
            tangents[:] = (1.0, 0.0, 0.0, 1.0)
        uvs = np.asarray(uv_values, dtype=np.float32).reshape(-1, 2)
        material_indices = np.empty(len(mesh.loop_triangles), dtype=np.int32)
        mesh.loop_triangles.foreach_get("material_index", material_indices)
        np.maximum(material_indices, 0, out=material_indices)
        slots = np.unique(material_indices)
        minima, maxima = positions.min(axis=0), positions.max(axis=0)
        guid = identity.get_guid(obj).encode("ascii")
        revision = _mesh_revision.get(guid, 0) + 1
        _mesh_revision[guid] = revision
        chunks = [struct.pack("<36sQII6f", guid, revision, vertex_count, len(slots), *minima, *maxima)]
        chunks.extend(values.tobytes() for values in (positions, normals, tangents, uvs))
        triangle_indices = triangle_loops.astype(np.uint32).reshape(-1, 3)
        for slot in slots:
            indices = triangle_indices[material_indices == slot].ravel()
            chunks.append(struct.pack("<II", int(slot), len(indices)))
            chunks.append(indices.tobytes())
        return b"".join(chunks)
    finally:
        if baked:
            import bpy
            bpy.data.meshes.remove(mesh)
        else:
            evaluated.to_mesh_clear()


def send_mesh(obj, depsgraph, link):
    encode_start = time.perf_counter()
    payload = encode_mesh(obj, depsgraph)
    encode_ms = (time.perf_counter() - encode_start) * 1000.0
    if payload is None:
        return False
    enqueue_start = time.perf_counter()
    queued = link.send(protocol.MESH_UPSERT, payload)
    enqueue_ms = (time.perf_counter() - enqueue_start) * 1000.0
    if encode_ms + enqueue_ms >= 50.0:
        print(
            f"[BlenderLiveLink] Slow mesh export: object={obj.name_full!r} payload_bytes={len(payload)} "
            f"encode_ms={encode_ms:.2f} enqueue_ms={enqueue_ms:.2f} queued={queued}"
        )
    return queued
