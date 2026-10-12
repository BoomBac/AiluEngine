import time
import uuid
from contextlib import nullcontext

from . import camera_sync, identity, light_sync, material_sync, mesh_sync, protocol
from .coordinate import blender_to_ailu_transform

_instance_states = {}


def _object_value(obj, objects):
    from . import material_bake
    position, rotation, scale = blender_to_ailu_transform(obj.matrix_local)
    parent_guid = identity.get_guid(obj.parent) if obj.parent in objects else ""
    materials = [material_bake.material_guid(slot.material, obj) if slot.material else "" for slot in obj.material_slots]
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
        phase_start = time.perf_counter()
        for obj in objects:
            link.send_json(protocol.OBJECT_UPSERT, _object_value(obj, objects))
        phases["objects"] = (time.perf_counter() - phase_start) * 1000.0

        phase_start = time.perf_counter()
        depsgraph = context.evaluated_depsgraph_get()
        mesh_count = 0
        sent_materials = set()
        material_ms = 0.0
        for obj in objects:
            if obj.type == "MESH":
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


def sync_object(context, obj, link, sent_materials=None):
    from . import dirty_tracker
    with dirty_tracker.export_scope(context):
        scene_objects = set(context.scene.objects)
        link.send_json(protocol.OBJECT_UPSERT, _object_value(obj, scene_objects))
        if obj.type == "MESH":
            material_sync.send_object_materials(obj, link, sent_materials)
            mesh_sync.send_mesh(obj, context.evaluated_depsgraph_get(), link)
        elif obj.type == "LIGHT":
            light_sync.export_light(obj, link)
        elif obj.type == "CAMERA":
            camera_sync.export_camera(obj, context.scene, link)


def sync_instances(context, link, sent_materials=None, changed_objects=(), full=False, phases=None):
    """Flatten evaluated mesh instances using stable identities and their final world transforms."""
    from . import dirty_tracker
    with dirty_tracker.export_scope(context):
        depsgraph = context.evaluated_depsgraph_get()
        session_guid = identity.get_session_guid(context.scene)
        changed_guids = {identity.get_guid(obj) for obj in changed_objects}
        groups = {}
        current = {}
        # DepsgraphObjectInstance RNA wrappers expire when the iterator advances; snapshot values now.
        for instance in depsgraph.object_instances:
            if not instance.is_instance or instance.object.type != "MESH":
                continue
            obj = instance.object.original
            source_guid = identity.get_guid(obj)
            parent_guid = identity.get_guid(instance.parent.original) if instance.parent else ""
            instance_key = session_guid + ":" + parent_guid + ":" + source_guid + ":" + str(tuple(instance.persistent_id))
            guid = str(uuid.uuid5(uuid.NAMESPACE_URL, instance_key))
            position, rotation, scale = blender_to_ailu_transform(instance.matrix_world.copy())
            value = _object_value(obj, set())
            value.update(guid=guid, name=obj.name_full + " [Instance]", parent_guid="", visible=instance.show_self and obj.visible_camera,
                         position=list(position), rotation=[rotation.x, rotation.y, rotation.z, rotation.w], scale=list(scale))
            state = (tuple(value["position"]), tuple(value["rotation"]), tuple(value["scale"]), tuple(value["material_guids"]), value["visible"])
            current[guid] = state
            previous = _instance_states.get(guid)
            mesh_changed = full or previous is None or source_guid in changed_guids
            if mesh_changed or state != previous:
                link.send_json(protocol.OBJECT_UPSERT, value)
            if mesh_changed:
                groups.setdefault(obj, []).append(guid)
        sent_materials = set() if sent_materials is None else sent_materials
        material_ms = 0.0
        for obj, guids in groups.items():
            payload = mesh_sync.encode_mesh(obj, context.evaluated_depsgraph_get())
            if payload is not None:
                material_start = time.perf_counter()
                material_sync.send_object_materials(obj, link, sent_materials)
                material_ms += (time.perf_counter() - material_start) * 1000.0
                for guid in guids:
                    mesh_sync.send_instance_mesh(payload, guid, link)
        if not full:
            for guid in _instance_states.keys() - current.keys():
                link.send_json(protocol.OBJECT_DELETE, {"guid": guid})
        _instance_states.clear()
        _instance_states.update(current)
        if phases is not None:
            phases["instance_materials"] = material_ms
