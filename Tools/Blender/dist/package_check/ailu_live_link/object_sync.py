from . import camera_sync, identity, light_sync, material_sync, mesh_sync, protocol
from .coordinate import blender_to_ailu_transform


def _object_value(obj, objects):
    position, rotation, scale = blender_to_ailu_transform(obj.matrix_local)
    parent_guid = identity.get_guid(obj.parent) if obj.parent in objects else ""
    materials = [identity.get_guid(slot.material) if slot.material else "" for slot in obj.material_slots]
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
    scene = context.scene
    objects = set(scene.objects)
    identity.repair_duplicate_guids(objects)
    link.status = "Syncing"
    link.send_json(protocol.FULL_SYNC_BEGIN, {"session_guid": identity.get_session_guid(scene)})
    for obj in objects:
        if obj.type == "MESH":
            material_sync.send_object_materials(obj, link)
    for obj in objects:
        link.send_json(protocol.OBJECT_UPSERT, _object_value(obj, objects))
    depsgraph = context.evaluated_depsgraph_get()
    for obj in objects:
        if obj.type == "MESH":
            mesh_sync.send_mesh(obj, depsgraph, link)
        elif obj.type == "LIGHT":
            light_sync.export_light(obj, link)
        elif obj.type == "CAMERA":
            camera_sync.export_camera(obj, scene, link)
    link.send_json(protocol.FULL_SYNC_END, {})


def sync_object(context, obj, link):
    scene_objects = set(context.scene.objects)
    link.send_json(protocol.OBJECT_UPSERT, _object_value(obj, scene_objects))
    if obj.type == "MESH":
        material_sync.send_object_materials(obj, link)
        mesh_sync.send_mesh(obj, context.evaluated_depsgraph_get(), link)
    elif obj.type == "LIGHT":
        light_sync.export_light(obj, link)
    elif obj.type == "CAMERA":
        camera_sync.export_camera(obj, context.scene, link)
