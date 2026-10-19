import time
import uuid
from contextlib import nullcontext

from . import camera_sync, identity, light_sync, material_sync, mesh_sync, protocol
from .coordinate import ObjectLocalMatrix, SplitAffineTransform, blender_to_ailu_transform

_instance_states = {}
_merged_source_states = {}
_merged_batch_states = {}
_object_affine_helpers = set()
_INSTANCE_BATCH_SIZE = 128
kStaticMergeInstanceThreshold = 4096
_last_static_merge_summary = {}


def _object_value(obj, objects, affine_helpers=None):
    from . import material_bake
    parent = obj.parent if obj.parent in objects else None
    matrix = ObjectLocalMatrix(obj, parent)
    parent_guid = identity.get_guid(parent) if parent is not None else ""
    if affine_helpers is not None:
        matrix, helper_matrix = SplitAffineTransform(matrix)
        if helper_matrix is not None:
            helper = AffineParentValue(identity.get_guid(obj), helper_matrix, parent_guid)
            affine_helpers.append(helper)
            parent_guid = helper["guid"]
    position, rotation, scale = blender_to_ailu_transform(matrix)
    materials = [material_bake.material_guid(material, obj) if material else "" for material in material_bake.EvaluatedMaterials(obj)]
    return {
        "guid": identity.get_guid(obj),
        "name": obj.name_full,
        "parent_guid": parent_guid,
        "position": list(position),
        "rotation": [rotation.x, rotation.y, rotation.z, rotation.w],
        "scale": list(scale),
        "visible": obj.visible_get() and (obj.type != "MESH" or obj.visible_camera),
        "object_type": obj.type,
        "material_guids": materials,
    }


def AffineParentValue(guid, matrix, parent_guid=""):
    position, rotation, scale = blender_to_ailu_transform(matrix)
    return {"guid": str(uuid.uuid5(uuid.NAMESPACE_URL, guid + ":affine-parent")), "name": "__AiluAffine_" + guid,
            "parent_guid": parent_guid, "object_type": "EMPTY", "visible": True, "material_guids": [],
            "position": list(position), "rotation": [rotation.x, rotation.y, rotation.z, rotation.w], "scale": list(scale)}


def _emit_static_merge_batches(records, prototype_materials, prototype_objects, generated_prototypes,
                               session_guid, link, sent_materials, update_materials):
    start_time = time.perf_counter()
    grouped_records = {}
    prototype_guids = set()
    for record in records:
        if record["payload"] is None:
            continue
        value = record["value"]
        key = (tuple(value["material_guids"]), bool(value["visible"]))
        grouped_records.setdefault(key, []).append(record)
        prototype_guids.add(record["prototype_guid"])

    if update_materials:
        for prototype_guid in prototype_guids:
            if prototype_guid not in generated_prototypes:
                source_obj = prototype_objects.get(prototype_guid)
                if source_obj is not None:
                    material_sync.send_object_materials(source_obj, link, sent_materials)
                continue
            for material in prototype_materials.get(prototype_guid, ()):
                if material:
                    material_guid = identity.get_guid(material)
                    if material_guid not in sent_materials:
                        material_sync.export_material(material, link)
                        sent_materials.add(material_guid)

    batch_states = {}
    source_count = 0
    batch_count = 0
    merged_vertices = 0
    merged_triangles = 0
    for (material_guids, visible), group_records in grouped_records.items():
        group_records.sort(key=lambda record: record["value"]["guid"])
        group_guid = str(uuid.uuid5(uuid.NAMESPACE_URL, session_guid + ":static-merge:" +
                                    ",".join(material_guids) + ":" + str(int(visible))))
        chunks = []
        chunk = []
        chunk_vertices = 0
        chunk_triangles = 0
        for record in group_records:
            vertex_count, triangle_count = mesh_sync.EncodedMeshCounts(record["payload"])
            exceeds_limits = (chunk and
                              (chunk_vertices + vertex_count > mesh_sync.kStaticMergeMaxVertices or
                               chunk_triangles + triangle_count > mesh_sync.kStaticMergeMaxTriangles or
                               len(chunk) >= mesh_sync.kStaticMergeMaxInstances))
            if exceeds_limits:
                chunks.append((chunk, chunk_vertices, chunk_triangles))
                chunk = []
                chunk_vertices = 0
                chunk_triangles = 0
            if (vertex_count > mesh_sync.kStaticMergeMaxVertices or
                    triangle_count > mesh_sync.kStaticMergeMaxTriangles):
                raise ValueError("A generated mesh exceeds the static-merge vertex or triangle budget")
            chunk.append(record)
            chunk_vertices += vertex_count
            chunk_triangles += triangle_count
        if chunk:
            chunks.append((chunk, chunk_vertices, chunk_triangles))

        for chunk_index, (chunk_records, vertex_count, triangle_count) in enumerate(chunks):
            mesh_guid = str(uuid.uuid5(uuid.NAMESPACE_URL, group_guid + ":mesh:" + str(chunk_index)))
            object_guid = str(uuid.uuid5(uuid.NAMESPACE_URL, group_guid + ":object:" + str(chunk_index)))
            payload = mesh_sync.MergeEncodedMeshes(
                [(record["payload"], record["matrix_world"]) for record in chunk_records], mesh_guid)
            if payload is None:
                continue
            link.send(protocol.MESH_STATIC_BATCH_UPSERT, payload)
            batch_value = dict(chunk_records[0]["value"])
            batch_value.update(
                guid=object_guid,
                name=chunk_records[0]["source_name"] + " [Merged Batch " + str(chunk_index) + "]",
                parent_guid="",
                visible=visible,
                position=[0.0, 0.0, 0.0],
                rotation=[0.0, 0.0, 0.0, 1.0],
                scale=[1.0, 1.0, 1.0],
                material_guids=list(material_guids),
            )
            state = (tuple(batch_value["position"]), tuple(batch_value["rotation"]),
                     tuple(batch_value["scale"]), tuple(material_guids), visible, None)
            batch_states[object_guid] = state
            link.send_json(protocol.INSTANCE_BATCH_UPSERT, {
                "prototype_guid": mesh_guid,
                "instances": [batch_value],
            })
            source_count += len(chunk_records)
            batch_count += 1
            merged_vertices += vertex_count
            merged_triangles += triangle_count

    elapsed_ms = (time.perf_counter() - start_time) * 1000.0
    print("[BlenderLiveLink] Static merge: source_instances={} batches={} vertices={} triangles={} elapsed_ms={:.2f}"
          .format(source_count, batch_count, merged_vertices, merged_triangles, elapsed_ms))
    _last_static_merge_summary.update(source_instances=source_count, batches=batch_count,
                                      vertices=merged_vertices, triangles=merged_triangles,
                                      elapsed_ms=elapsed_ms)
    return batch_states, elapsed_ms


def SendObject(obj, objects, link):
    helpers = []
    value = _object_value(obj, objects, helpers)
    guid = value["guid"]
    for helper in helpers:
        link.send_json(protocol.OBJECT_UPSERT, helper)
    link.send_json(protocol.OBJECT_UPSERT, value)
    if helpers:
        _object_affine_helpers.add(guid)
    elif guid in _object_affine_helpers:
        link.send_json(protocol.OBJECT_DELETE, {"guid": str(uuid.uuid5(uuid.NAMESPACE_URL, guid + ":affine-parent"))})
        _object_affine_helpers.discard(guid)


def full_sync(context, link):
    from . import dirty_tracker, material_bake
    session = getattr(material_bake, "ExportSession", nullcontext)
    with dirty_tracker.export_scope(context), session():
        _full_sync(context, link)


def _full_sync(context, link):
    full_start = time.perf_counter()
    diagnostic_batch = link.begin_diagnostic_batch("full_sync")
    phases = {}
    counts = {}
    scene = context.scene
    try:
        phase_start = time.perf_counter()
        objects = set(scene.objects)
        identity.repair_duplicate_guids(objects)
        phases["scene_scan"] = (time.perf_counter() - phase_start) * 1000.0

        link.send_json(protocol.FULL_SYNC_BEGIN, {"session_guid": identity.get_session_guid(scene)})
        from . import scene_sync
        scene_sync.export_scene_settings(scene, link)
        from . import world_sync
        world_sync.export_world(context, link)
        _object_affine_helpers.clear()
        phase_start = time.perf_counter()
        for obj in objects:
            SendObject(obj, objects, link)
        phases["objects"] = (time.perf_counter() - phase_start) * 1000.0

        phase_start = time.perf_counter()
        depsgraph = context.evaluated_depsgraph_get()
        direct_mesh_objects = mesh_sync.DirectMeshObjects(context, depsgraph)
        mesh_count = 0
        sent_materials = set()
        material_ms = 0.0
        for obj in objects:
            if obj in direct_mesh_objects:
                material_start = time.perf_counter()
                material_sync.send_object_materials(obj, link, sent_materials)
                material_ms += (time.perf_counter() - material_start) * 1000.0
                if mesh_sync.send_mesh(obj, depsgraph, link):
                    mesh_count += 1
            elif obj.type == "LIGHT":
                light_sync.export_light(obj, link)
            elif obj.type == "CAMERA":
                camera_sync.export_camera(obj, scene, link)
        phases["materials"] = material_ms
        phases["geometry_and_other"] = (time.perf_counter() - phase_start) * 1000.0 - material_ms
        counts = {"objects": len(objects), "meshes": mesh_count, "materials": len(sent_materials)}
        phase_start = time.perf_counter()
        sync_instances(context, link, sent_materials, full=True, phases=phases)
        phases["instances"] = (time.perf_counter() - phase_start) * 1000.0
        counts["materials_total"] = len(sent_materials)
        from . import material_bake
        flush = getattr(material_bake, "FlushMaterials", None)
        if flush:
            flush()
        link.send_json(protocol.FULL_SYNC_END, {})
    except Exception as error:
        # An incomplete snapshot must not leave the host waiting for End or prune unseen entities.
        link.disconnect()
        link.last_error = f"Full Sync failed: {error}"
        raise
    finally:
        link.end_diagnostic_batch(diagnostic_batch, (time.perf_counter() - full_start) * 1000.0, phases, counts)


def sync_object(context, obj, link, sent_materials=None, update_geometry=True, update_materials=True,
                update_data=True):
    from . import dirty_tracker
    with dirty_tracker.export_scope(context):
        scene_objects = set(context.scene.objects)
        SendObject(obj, scene_objects, link)
        if mesh_sync.IsMeshSource(obj):
            if update_materials:
                material_sync.send_object_materials(obj, link, sent_materials)
            if update_geometry and (obj.type == "MESH" or obj in mesh_sync.DirectMeshObjects(context, context.evaluated_depsgraph_get())):
                mesh_sync.send_mesh(obj, context.evaluated_depsgraph_get(), link)
        elif obj.type == "LIGHT":
            if update_data:
                light_sync.export_light(obj, link)
        elif obj.type == "CAMERA":
            if update_data:
                camera_sync.export_camera(obj, context.scene, link)


def sync_instances(context, link, sent_materials=None, changed_objects=(), full=False, phases=None,
                   update_materials=True):
    """Flatten evaluated mesh instances using stable identities and their final world transforms."""
    global _last_static_merge_summary
    _last_static_merge_summary = {}
    from . import dirty_tracker
    with dirty_tracker.export_scope(context):
        depsgraph = context.evaluated_depsgraph_get()
        session_guid = identity.get_session_guid(context.scene)
        changed_guids = {identity.get_guid(obj) for obj in changed_objects}
        groups = {}
        affine_helpers = {}
        prototype_objects = {}
        prototype_source_guids = {}
        instance_prototype_payloads = {}
        instance_prototype_materials = {}
        generated_prototypes = set()
        mesh_changed_guids = set()
        current = {}
        instance_records = []
        merged_source_states = {}
        instanced_source_guids = set()
        # DepsgraphObjectInstance RNA wrappers expire when the iterator advances; snapshot values now.
        for instance in depsgraph.object_instances:
            if not instance.is_instance or instance.object.type != "MESH":
                continue
            obj = instance.object.original
            source_guid = identity.get_guid(obj)
            parent = instance.parent.original if instance.parent else None
            # A Geometry Nodes self-instance exposes its component mesh, not the owner's full generated mesh.
            is_generated_component = parent is obj
            prototype_guid = (str(uuid.uuid5(uuid.NAMESPACE_URL, source_guid + ":instance-geometry"))
                              if is_generated_component else source_guid)
            prototype_objects[prototype_guid] = obj
            prototype_source_guids[prototype_guid] = source_guid
            if is_generated_component and prototype_guid not in instance_prototype_payloads:
                generated_prototypes.add(prototype_guid)
                instance_prototype_payloads[prototype_guid] = mesh_sync.encode_mesh(
                    obj, depsgraph, mesh_override=instance.object.data, guid_override=prototype_guid)
                instance_prototype_materials[prototype_guid] = [
                    (instance.object.material_slots[index].material.original
                     if index < len(instance.object.material_slots)
                     and instance.object.material_slots[index].link == "OBJECT"
                     and instance.object.material_slots[index].material else
                     material.original if material else None)
                    for index, material in enumerate(instance.object.data.materials)
                ]
            parent_guid = identity.get_guid(parent) if parent else ""
            instance_key = session_guid + ":" + parent_guid + ":" + source_guid + ":" + str(tuple(instance.persistent_id))
            guid = str(uuid.uuid5(uuid.NAMESPACE_URL, instance_key))
            child_matrix, helper_matrix = SplitAffineTransform(instance.matrix_world.copy())
            position, rotation, scale = blender_to_ailu_transform(child_matrix)
            helper_guid = str(uuid.uuid5(uuid.NAMESPACE_URL, guid + ":affine-parent")) if helper_matrix is not None else ""
            helper_state = None
            if helper_matrix is not None:
                helper_position, helper_rotation, helper_scale = blender_to_ailu_transform(helper_matrix)
                helper_state = (tuple(helper_position), tuple(helper_rotation), tuple(helper_scale))
                affine_helpers[guid] = AffineParentValue(guid, helper_matrix)
            value = _object_value(obj, set())
            value.update(
                guid=guid,
                name=obj.name_full + " [Instance]",
                parent_guid=helper_guid,
                visible=instance.show_self and obj.visible_camera,
                position=list(position),
                rotation=[rotation.x, rotation.y, rotation.z, rotation.w],
                scale=list(scale),
            )
            if is_generated_component:
                value["material_guids"] = [identity.get_guid(material) if material else ""
                                           for material in instance_prototype_materials[prototype_guid]]
            state = (
                tuple(value["position"]),
                tuple(value["rotation"]),
                tuple(value["scale"]),
                tuple(value["material_guids"]),
                value["visible"],
                helper_state,
            )
            merged_source_states[guid] = state
            instanced_source_guids.add(source_guid)
            previous_source = _merged_source_states.get(guid)
            instance_changed = full or previous_source is None or state != previous_source or source_guid in changed_guids
            instance_records.append({
                "guid": guid,
                "state": state,
                "value": value,
                "prototype_guid": prototype_guid,
                "matrix_world": instance.matrix_world.copy(),
                "source_name": obj.name_full,
                "changed": instance_changed,
                "payload": None,
            })
            if not is_generated_component:
                current[guid] = state
            if instance_changed:
                mesh_changed_guids.add(source_guid)
        sent_materials = set() if sent_materials is None else sent_materials
        material_ms = 0.0
        source_objects = {identity.get_guid(obj): obj for obj in mesh_sync.DirectMeshObjects(context, depsgraph)}
        use_static_merge = len(instance_records) >= kStaticMergeInstanceThreshold
        if use_static_merge:
            batch_changed = (full or bool(changed_guids & instanced_source_guids) or
                             merged_source_states != _merged_source_states or
                             any(record["changed"] for record in instance_records))
            if batch_changed:
                static_payloads = {}
                for record in instance_records:
                    prototype_guid = record["prototype_guid"]
                    if prototype_guid not in static_payloads:
                        payload = instance_prototype_payloads.get(prototype_guid)
                        if payload is None and prototype_guid not in generated_prototypes:
                            payload = mesh_sync.encode_mesh(
                                prototype_objects[prototype_guid], depsgraph, guid_override=prototype_guid)
                        static_payloads[prototype_guid] = payload
                    record["payload"] = static_payloads[prototype_guid]
                batch_states, merge_ms = _emit_static_merge_batches(
                    instance_records, instance_prototype_materials, prototype_objects, generated_prototypes,
                    session_guid, link, sent_materials, update_materials)
                _merged_batch_states.clear()
                _merged_batch_states.update(batch_states)
                material_ms += merge_ms
            for record in instance_records:
                current.pop(record["guid"], None)
            current.update(_merged_batch_states)
            for prototype_guid in {record["prototype_guid"] for record in instance_records}:
                groups.pop(prototype_guid, None)
        else:
            _merged_batch_states.clear()
            for record in instance_records:
                current[record["guid"]] = record["state"]
                if record["changed"]:
                    groups.setdefault(record["prototype_guid"], []).append(record["value"])
        _merged_source_states.clear()
        _merged_source_states.update(merged_source_states)
        for prototype_guid, instances in groups.items():
            source_guid = prototype_source_guids[prototype_guid]
            source_obj = prototype_objects.get(prototype_guid)
            generated_component = prototype_guid in generated_prototypes
            is_external_prototype = source_guid not in source_objects
            if generated_component and source_guid in mesh_changed_guids:
                payload = instance_prototype_payloads[prototype_guid]
                if payload is not None:
                    link.send(protocol.MESH_PROTOTYPE_UPSERT, payload)
                if update_materials:
                    for material in instance_prototype_materials[prototype_guid]:
                        if not material:
                            continue
                        material_guid = identity.get_guid(material)
                        if material_guid not in sent_materials:
                            material_sync.export_material(material, link)
                            sent_materials.add(material_guid)
            elif is_external_prototype and source_guid in mesh_changed_guids:
                mesh_sync.send_mesh(source_obj, depsgraph, link, prototype=True)
            has_geometry = (instance_prototype_payloads[prototype_guid] is not None if generated_component
                            else mesh_sync.has_geometry(source_guid))
            if source_obj is None or not has_geometry:
                for instance in instances:
                    current.pop(instance["guid"], None)
                continue
            if update_materials and not generated_component:
                material_start = time.perf_counter()
                material_sync.send_object_materials(source_obj, link, sent_materials)
                material_ms += (time.perf_counter() - material_start) * 1000.0
            for instance in instances:
                if instance["guid"] in affine_helpers:
                    link.send_json(protocol.OBJECT_UPSERT, affine_helpers[instance["guid"]])
            for start in range(0, len(instances), _INSTANCE_BATCH_SIZE):
                link.send_json(protocol.INSTANCE_BATCH_UPSERT, {
                    "prototype_guid": prototype_guid,
                    "instances": instances[start:start + _INSTANCE_BATCH_SIZE],
                })
        if not full:
            for guid in _instance_states.keys() - current.keys():
                link.send_json(protocol.OBJECT_DELETE, {"guid": guid})
            for guid, previous in _instance_states.items():
                if previous[-1] is not None and (guid not in current or current[guid][-1] is None):
                    link.send_json(protocol.OBJECT_DELETE, {"guid": str(uuid.uuid5(uuid.NAMESPACE_URL, guid + ":affine-parent"))})
        _instance_states.clear()
        _instance_states.update(current)
        if phases is not None:
            phases["instance_materials"] = material_ms
