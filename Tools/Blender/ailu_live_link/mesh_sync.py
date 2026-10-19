import struct
import time
import tempfile
from array import array
import numpy as np

from . import coordinate, identity, protocol

_mesh_revision = {}
_mesh_geometry_state = {}
_session_temp_dir = None
kStaticMergeMaxVertices = 200000
kStaticMergeMaxTriangles = 400000
kStaticMergeMaxInstances = 1024


def IsMeshSource(obj):
    return obj.type in {"MESH", "CURVE", "SURFACE", "FONT", "META"}


def DirectMeshObjects(context, depsgraph):
    # Blender exposes some converted Curve/Geometry Nodes mesh components as self instances. Do not draw them twice.
    self_instances = set()
    for instance in depsgraph.object_instances:
        if instance.is_instance and instance.object.type == "MESH" and instance.parent is not None:
            source = instance.object.original
            if source.type != "MESH" and instance.parent.original == source:
                self_instances.add(source)
    return {obj for obj in context.scene.objects if IsMeshSource(obj) and obj not in self_instances}


def _temp_dir():
    global _session_temp_dir
    if _session_temp_dir is None:
        _session_temp_dir = tempfile.mkdtemp(prefix="ailu_live_link_")
    return _session_temp_dir


def encode_mesh(obj, depsgraph, mesh_override=None, guid_override=None):
    import bpy
    from . import material_bake
    depsgraph = bpy.context.evaluated_depsgraph_get()
    evaluated = obj.evaluated_get(depsgraph) if mesh_override is None else None
    baked = mesh_override is None and material_bake.object_needs_bake(obj)
    mesh = (mesh_override if mesh_override is not None else material_bake.prepared_mesh(obj, depsgraph).copy()
            if baked else evaluated.to_mesh(preserve_all_data_layers=True, depsgraph=depsgraph))
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
            tangents[:, 3] = np.where(loop_signs < 0, 1.0, -1.0)
        else:
            tangents[:] = (1.0, 0.0, 0.0, 1.0)
        uvs = np.asarray(uv_values, dtype=np.float32).reshape(-1, 2)
        if uv_layer:
            # Blender UVs start at the bottom; Ailu samples imported images from the top.
            uvs[:, 1] = 1.0 - uvs[:, 1]
        material_indices = np.empty(len(mesh.loop_triangles), dtype=np.int32)
        mesh.loop_triangles.foreach_get("material_index", material_indices)
        np.maximum(material_indices, 0, out=material_indices)
        slots = np.unique(material_indices)
        minima, maxima = positions.min(axis=0), positions.max(axis=0)
        guid = (guid_override or identity.get_guid(obj)).encode("ascii")
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
        if mesh_override is None:
            if baked:
                import bpy
                bpy.data.meshes.remove(mesh)
            else:
                evaluated.to_mesh_clear()


def EncodedMeshCounts(payload):
    if payload is None or len(payload) < 76:
        return 0, 0
    _, _, vertex_count, submesh_count = struct.unpack_from("<36sQII", payload, 0)
    offset = 76 + vertex_count * 48
    triangle_count = 0
    for _ in range(submesh_count):
        _, index_count = struct.unpack_from("<II", payload, offset)
        triangle_count += index_count // 3
        offset += 8 + index_count * 4
    if offset != len(payload):
        raise ValueError("Invalid encoded mesh payload")
    return vertex_count, triangle_count


def MergeEncodedMeshes(entries, guid_override):
    """Bake instance transforms into one mesh while preserving slots, UVs, normals and tangents."""
    if not entries:
        return None
    position_blocks = []
    normal_blocks = []
    tangent_blocks = []
    uv_blocks = []
    indices_by_slot = {}
    vertex_offset = 0
    basis = np.array(((1.0, 0.0, 0.0, 0.0),
                      (0.0, 0.0, 1.0, 0.0),
                      (0.0, -1.0, 0.0, 0.0),
                      (0.0, 0.0, 0.0, 1.0)), dtype=np.float64)
    basis_inverse = basis.T

    for payload, matrix_world in entries:
        if payload is None or len(payload) < 76:
            continue
        _, _, vertex_count, submesh_count = struct.unpack_from("<36sQII", payload, 0)
        if vertex_count == 0:
            continue
        array_offset = 76
        positions = np.frombuffer(payload, dtype=np.float32, count=vertex_count * 3,
                                  offset=array_offset).reshape(vertex_count, 3).copy()
        array_offset += vertex_count * 12
        normals = np.frombuffer(payload, dtype=np.float32, count=vertex_count * 3,
                                 offset=array_offset).reshape(vertex_count, 3).copy()
        array_offset += vertex_count * 12
        tangents = np.frombuffer(payload, dtype=np.float32, count=vertex_count * 4,
                                 offset=array_offset).reshape(vertex_count, 4).copy()
        array_offset += vertex_count * 16
        uvs = np.frombuffer(payload, dtype=np.float32, count=vertex_count * 2,
                             offset=array_offset).reshape(vertex_count, 2).copy()
        array_offset += vertex_count * 8

        blender_matrix = np.asarray(matrix_world, dtype=np.float64)
        engine_matrix = basis @ blender_matrix @ basis_inverse
        linear = engine_matrix[:3, :3]
        determinant = float(np.linalg.det(linear))
        if abs(determinant) < 1e-12:
            raise ValueError("Cannot merge a singular instance transform")
        positions = positions @ linear.T + engine_matrix[:3, 3]
        normal_matrix = np.linalg.inv(linear).T
        normals = normals @ normal_matrix.T
        normal_lengths = np.linalg.norm(normals, axis=1, keepdims=True)
        np.divide(normals, normal_lengths, out=normals, where=normal_lengths > 1e-12)
        tangent_vectors = tangents[:, :3] @ linear.T
        tangent_lengths = np.linalg.norm(tangent_vectors, axis=1, keepdims=True)
        np.divide(tangent_vectors, tangent_lengths, out=tangent_vectors, where=tangent_lengths > 1e-12)
        tangents[:, :3] = tangent_vectors
        if determinant < 0.0:
            tangents[:, 3] *= -1.0

        position_blocks.append(positions)
        normal_blocks.append(normals)
        tangent_blocks.append(tangents)
        uv_blocks.append(uvs)
        for _ in range(submesh_count):
            slot, index_count = struct.unpack_from("<II", payload, array_offset)
            array_offset += 8
            indices = np.frombuffer(payload, dtype=np.uint32, count=index_count,
                                    offset=array_offset).reshape(-1, 3).copy()
            array_offset += index_count * 4
            if determinant < 0.0:
                indices[:, [1, 2]] = indices[:, [2, 1]]
            indices += vertex_offset
            if index_count:
                indices_by_slot.setdefault(slot, []).append(indices.reshape(-1))
        if array_offset != len(payload):
            raise ValueError("Invalid encoded mesh payload")
        vertex_offset += vertex_count

    if not position_blocks:
        return None
    positions = np.concatenate(position_blocks)
    normals = np.concatenate(normal_blocks)
    tangents = np.concatenate(tangent_blocks)
    uvs = np.concatenate(uv_blocks)
    minima = positions.min(axis=0)
    maxima = positions.max(axis=0)
    guid = guid_override.encode("ascii")
    revision = _mesh_revision.get(guid, 0) + 1
    _mesh_revision[guid] = revision
    chunks = [struct.pack("<36sQII6f", guid, revision, len(positions), len(indices_by_slot),
                          *minima, *maxima)]
    chunks.extend(values.astype(np.float32, copy=False).tobytes()
                  for values in (positions, normals, tangents, uvs))
    for slot in sorted(indices_by_slot):
        indices = np.concatenate(indices_by_slot[slot]).astype(np.uint32, copy=False)
        chunks.append(struct.pack("<II", int(slot), len(indices)))
        chunks.append(indices.tobytes())
    return b"".join(chunks)


def send_mesh(obj, depsgraph, link, prototype=False):
    encode_start = time.perf_counter()
    guid = identity.get_guid(obj)
    payload = encode_mesh(obj, depsgraph)
    _mesh_geometry_state[guid] = payload is not None
    encode_ms = (time.perf_counter() - encode_start) * 1000.0
    if payload is None:
        return False
    enqueue_start = time.perf_counter()
    message_type = protocol.MESH_PROTOTYPE_UPSERT if prototype else protocol.MESH_UPSERT
    queued = link.send(message_type, payload)
    enqueue_ms = (time.perf_counter() - enqueue_start) * 1000.0
    if encode_ms + enqueue_ms >= 50.0:
        print(
            f"[BlenderLiveLink] Slow mesh export: object={obj.name_full!r} payload_bytes={len(payload)} "
            f"encode_ms={encode_ms:.2f} enqueue_ms={enqueue_ms:.2f} queued={queued}"
        )
    return queued


def has_geometry(guid):
    return _mesh_geometry_state.get(guid, False)
