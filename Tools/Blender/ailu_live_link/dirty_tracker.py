import time
from contextlib import contextmanager

import bpy

from . import identity, material_sync, protocol
from .object_sync import sync_object

_dirty = {}
_known_guids = set()
_last_full_sync = 0.0
_quiet_seconds = 0.12
_export_depth = 0


@contextmanager
def export_scope(context):
    global _export_depth
    # Process genuine edits before suppressing changes made by the exporter itself.
    if _export_depth == 0:
        context.view_layer.update()
    _export_depth += 1
    try:
        yield
    finally:
        try:
            if _export_depth == 1:
                # GUID writes, baking and scene restoration can leave deferred depsgraph notifications.
                # Drain them while suppression is still active, rather than dropping edits for a time window.
                context.view_layer.update()
        finally:
            _export_depth -= 1


def _uses_data(material, value, visited=None):
    if material is value:
        return True
    tree = material.node_tree if material and material.use_nodes else None
    visited = set() if visited is None else visited

    def uses_tree(tree):
        if not tree or tree.as_pointer() in visited:
            return False
        if tree is value:
            return True
        visited.add(tree.as_pointer())
        for node in tree.nodes:
            if getattr(node, "image", None) is value:
                return True
            if node.type == "GROUP" and uses_tree(node.node_tree):
                return True
        return False

    return uses_tree(tree)


@bpy.app.handlers.persistent
def _depsgraph_update(scene, depsgraph):
    from . import material_bake
    if _export_depth or material_bake._baking or scene.name.startswith("AiluLiveLinkBake"):
        return
    invalidated = False
    for update in depsgraph.updates:
        value = update.id.original
        if not invalidated:
            material_sync.invalidate_image_cache()
            invalidated = True
        for obj in scene.objects:
            if value is obj or value is obj.data or any(_uses_data(slot.material, value) for slot in obj.material_slots):
                _dirty[identity.get_guid(obj)] = (obj.name, time.monotonic())


def install_handler():
    if _depsgraph_update not in bpy.app.handlers.depsgraph_update_post:
        bpy.app.handlers.depsgraph_update_post.append(_depsgraph_update)


def remove_handler():
    if _depsgraph_update in bpy.app.handlers.depsgraph_update_post:
        bpy.app.handlers.depsgraph_update_post.remove(_depsgraph_update)
    _dirty.clear()
    _known_guids.clear()
    material_sync.invalidate_image_cache()


def mark_synced(scene):
    _known_guids.clear()
    _known_guids.update(identity.get_guid(obj) for obj in scene.objects)
    _dirty.clear()


def flush(context, link):
    if not link.connected or context.mode != "OBJECT":
        return
    now = time.monotonic()
    current = {identity.get_guid(obj): obj for obj in context.scene.objects}
    removed = _known_guids - current.keys()
    for guid in removed:
        link.send_json(protocol.OBJECT_DELETE, {"guid": guid})
    _known_guids.intersection_update(current.keys())
    sent_materials = set()
    for guid, (name, changed_at) in list(_dirty.items()):
        if now - changed_at < _quiet_seconds:
            continue
        obj = current.get(guid)
        if obj is not None:
            sync_object(context, obj, link, sent_materials)
            _known_guids.add(guid)
        _dirty.pop(guid, None)
