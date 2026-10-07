import time

from . import camera_sync, identity, light_sync, material_sync, mesh_sync, protocol
from .coordinate import blender_to_ailu_transform


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
        "visible": not obj.hide_viewport,
        "object_type": obj.type,
        "material_guids": materials,
    }


def full_sync(context, link):
    from . import dirty_tracker
    with dirty_tracker.export_scope(context):
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
        link.send_json(protocol.FULL_SYNC_END, {})
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
