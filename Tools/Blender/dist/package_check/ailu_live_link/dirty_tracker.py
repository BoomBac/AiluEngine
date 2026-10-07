import time

import bpy

from . import identity, protocol
from .object_sync import sync_object

_dirty = {}
_known_guids = set()
_last_full_sync = 0.0
_quiet_seconds = 0.12


@bpy.app.handlers.persistent
def _depsgraph_update(scene, depsgraph):
    for update in depsgraph.updates:
        value = update.id
        for obj in scene.objects:
            if value is obj or value is obj.data or any(slot.material is value for slot in obj.material_slots):
                _dirty[identity.get_guid(obj)] = (obj.name, time.monotonic())


def install_handler():
    if _depsgraph_update not in bpy.app.handlers.depsgraph_update_post:
        bpy.app.handlers.depsgraph_update_post.append(_depsgraph_update)


def remove_handler():
    if _depsgraph_update in bpy.app.handlers.depsgraph_update_post:
        bpy.app.handlers.depsgraph_update_post.remove(_depsgraph_update)
    _dirty.clear()
    _known_guids.clear()


def mark_synced(scene):
    _known_guids.clear()
    _known_guids.update(identity.get_guid(obj) for obj in scene.objects)
    _dirty.clear()


def flush(context, link):
    if not link.connected:
        return
    now = time.monotonic()
    current = {identity.get_guid(obj): obj for obj in context.scene.objects}
    removed = _known_guids - current.keys()
    for guid in removed:
        link.send_json(protocol.OBJECT_DELETE, {"guid": guid})
    _known_guids.intersection_update(current.keys())
    for guid, (name, changed_at) in list(_dirty.items()):
        if now - changed_at < _quiet_seconds:
            continue
        obj = current.get(guid)
        if obj is not None:
            sync_object(context, obj, link)
            _known_guids.add(guid)
        _dirty.pop(guid, None)
