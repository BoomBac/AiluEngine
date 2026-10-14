import math
import time
from contextlib import contextmanager

import bpy

from . import identity, material_sync, protocol
from .object_sync import sync_instances, sync_object, _instance_states

_dirty = {}
_known_guids = set()
_observed_transforms = {}
_observed_materials = {}
_material_change_logged = set()
_last_render_settings = ()
_last_full_sync = 0.0
_quiet_seconds = 0.12
_material_quiet_seconds = 0.45
_export_depth = 0


def _same_id(left, right):
    if left is right:
        return True
    if left is None or right is None:
        return False
    try:
        left_pointer = left.as_pointer()
        return left_pointer != 0 and left_pointer == right.as_pointer()
    except (AttributeError, ReferenceError, RuntimeError):
        return False


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
    if _same_id(material, value):
        return True
    tree = material.node_tree if material and material.use_nodes else None
    visited = set() if visited is None else visited

    def uses_tree(tree):
        if not tree or tree.as_pointer() in visited:
            return False
        if _same_id(tree, value):
            return True
        visited.add(tree.as_pointer())
        for node in tree.nodes:
            if _same_id(node, value):
                return True
            if _same_id(getattr(node, "image", None), value):
                return True
            if node.type == "GROUP" and uses_tree(node.node_tree):
                return True
        return False

    return uses_tree(tree)


def _value_signature(value):
    def signature_float(component):
        number = float(component)
        return round(number, 7) if math.isfinite(number) else str(number)

    try:
        return tuple(signature_float(component) for component in value)
    except (TypeError, ValueError):
        try:
            return signature_float(value)
        except (TypeError, ValueError):
            return value


def _tree_signature(tree, visited):
    if not tree:
        return ()
    pointer = tree.as_pointer()
    if pointer in visited:
        return (pointer,)
    visited.add(pointer)
    nodes = []
    for node in tree.nodes:
        inputs = tuple((socket.identifier, _value_signature(socket.default_value), socket.is_linked)
                       for socket in node.inputs if hasattr(socket, "default_value"))
        image = getattr(node, "image", None)
        group = getattr(node, "node_tree", None) if node.type == "GROUP" else None
        nodes.append((node.bl_idname, node.name, inputs, image.as_pointer() if image else 0,
                      _tree_signature(group, visited)))
    links = tuple((link.from_node.name, link.from_socket.identifier,
                   link.to_node.name, link.to_socket.identifier) for link in tree.links)
    return tuple(nodes), links


def _object_material_signature(obj, cache):
    signatures = []
    for slot in obj.material_slots:
        material = slot.material
        if material is None:
            signatures.append(None)
            continue
        pointer = material.as_pointer()
        if pointer not in cache:
            cache[pointer] = (pointer, tuple(_value_signature(value) for value in material.diffuse_color),
                              round(float(material.roughness), 7), round(float(material.metallic), 7),
                              material.use_nodes,
                              _tree_signature(material.node_tree, set()) if material.use_nodes else ())
        signatures.append(cache[pointer])
    return tuple(signatures)


@bpy.app.handlers.persistent
def _depsgraph_update(scene, depsgraph):
    from . import material_bake
    if _export_depth or material_bake._baking or scene.name.startswith("AiluLiveLinkBake"):
        return
    cache_invalidated = False
    bake_invalidated = False
    for update in depsgraph.updates:
        try:
            value = update.id.original
            value_type = value.bl_rna.identifier
        except (AttributeError, ReferenceError, RuntimeError):
            continue
        geometry_changed = update.is_updated_geometry or value_type == "Mesh"
        transform_only = value_type == "Object" and update.is_updated_transform and not geometry_changed
        if value_type == "Image" and not cache_invalidated:
            material_sync.invalidate_image_cache()
            cache_invalidated = True
        elif geometry_changed and not bake_invalidated:
            material_bake.invalidate()
            bake_invalidated = True
        for obj in scene.objects:
            object_changed = _same_id(value, obj)
            data_changed = _same_id(value, obj.data)
            update_geometry = object_changed and geometry_changed
            update_materials = object_changed and update.is_updated_shading and not transform_only
            update_data = False
            if data_changed:
                update_geometry |= obj.type == "MESH"
                update_data |= obj.type != "MESH"
            for slot in obj.material_slots:
                if slot.material and _uses_data(slot.material, value):
                    update_materials = True
                    material_bake.invalidate_material(slot.material, obj)
            if object_changed and update.is_updated_transform:
                update_materials |= material_bake.invalidate_object(obj)
            if update_geometry and obj.type == "MESH" and material_bake.object_needs_bake(obj):
                update_materials = True
            if object_changed or data_changed or update_materials:
                _mark_dirty(obj, time.monotonic(), update_geometry, update_materials, update_data)


def install_handler():
    if _depsgraph_update not in bpy.app.handlers.depsgraph_update_post:
        bpy.app.handlers.depsgraph_update_post.append(_depsgraph_update)


def is_transforming(context):
    window_manager = context.window_manager
    for window in window_manager.windows:
        if any(operator.bl_idname.startswith("TRANSFORM_OT_") for operator in window.modal_operators):
            return True
    return False


def remove_handler():
    from . import material_bake
    if _depsgraph_update in bpy.app.handlers.depsgraph_update_post:
        bpy.app.handlers.depsgraph_update_post.remove(_depsgraph_update)
    _dirty.clear()
    _known_guids.clear()
    _observed_transforms.clear()
    _observed_materials.clear()
    _material_change_logged.clear()
    _instance_states.clear()
    material_sync.invalidate_image_cache()
    material_bake.invalidate()


def _mark_dirty(obj, changed_at, update_geometry=False, update_materials=False, update_data=False):
    guid = identity.get_guid(obj)
    previous = _dirty.get(guid)
    if previous:
        update_geometry |= previous[2]
        update_materials |= previous[3]
        update_data |= previous[4]
    _dirty[guid] = (obj.name, changed_at, update_geometry, update_materials, update_data)


def mark_synced(scene):
    global _last_render_settings
    render = scene.render
    _last_render_settings = (render.resolution_x, render.resolution_y, render.pixel_aspect_x, render.pixel_aspect_y)
    _known_guids.clear()
    _known_guids.update(identity.get_guid(obj) for obj in scene.objects)
    _observed_transforms.clear()
    _observed_transforms.update((identity.get_guid(obj), _transform_signature(obj)) for obj in scene.objects)
    material_cache = {}
    _observed_materials.clear()
    _observed_materials.update((identity.get_guid(obj), _object_material_signature(obj, material_cache))
                               for obj in scene.objects)
    _material_change_logged.clear()
    _dirty.clear()


def _transform_signature(obj):
    return tuple(round(value, 7) for row in obj.matrix_local for value in row)


def flush(context, link):
    global _last_render_settings
    if not link.connected or context.mode != "OBJECT" or is_transforming(context):
        return
    now = time.monotonic()
    render = context.scene.render
    render_settings = (render.resolution_x, render.resolution_y, render.pixel_aspect_x, render.pixel_aspect_y)
    if render_settings != _last_render_settings:
        for obj in context.scene.objects:
            if obj.type == "CAMERA":
                _mark_dirty(obj, now - _quiet_seconds, update_data=True)
        _last_render_settings = render_settings
    identity.repair_duplicate_guids(context.scene.objects)
    current = {identity.get_guid(obj): obj for obj in context.scene.objects}
    material_cache = {}
    from . import material_bake
    for guid, obj in current.items():
        signature = _transform_signature(obj)
        previous_transform = _observed_transforms.get(guid)
        if previous_transform != signature:
            _observed_transforms[guid] = signature
        if previous_transform is not None and previous_transform != signature:
            update_materials = material_bake.invalidate_object(obj)
            _mark_dirty(obj, now, update_materials=update_materials)
        material_signature = _object_material_signature(obj, material_cache)
        previous_material = _observed_materials.get(guid)
        if previous_material != material_signature:
            _observed_materials[guid] = material_signature
        if previous_material is not None and previous_material != material_signature:
            material_bake.invalidate_materials(obj, previous_material, material_signature)
            if guid not in _material_change_logged:
                material_names = ", ".join(slot.material.name_full for slot in obj.material_slots if slot.material)
                print(f"[BlenderLiveLink] Material change detected: object={obj.name_full!r}, "
                      f"materials={material_names!r}")
                _material_change_logged.add(guid)
            _mark_dirty(obj, now, update_materials=True)
    # New objects (including duplicates with repaired identities) must not depend on a depsgraph notification.
    for guid in current.keys() - _known_guids:
        _mark_dirty(current[guid], now - _quiet_seconds, update_geometry=True, update_materials=True,
                    update_data=True)
    removed = _known_guids - current.keys()
    for guid in removed:
        link.send_json(protocol.OBJECT_DELETE, {"guid": guid})
    _known_guids.intersection_update(current.keys())
    sent_materials = set()
    changed_objects = []
    changed_mesh_objects = []
    for guid, (name, changed_at, update_geometry, update_materials, update_data) in list(_dirty.items()):
        quiet_seconds = (_material_quiet_seconds
                         if update_materials and not (update_geometry or update_data) else _quiet_seconds)
        if now - changed_at < quiet_seconds:
            continue
        obj = current.get(guid)
        if obj is not None:
            sync_object(context, obj, link, sent_materials, update_geometry, update_materials, update_data)
            if update_materials:
                print(f"[BlenderLiveLink] Material delta queued: object={obj.name_full!r}")
                _material_change_logged.discard(guid)
            changed_objects.append(obj)
            _observed_transforms[guid] = _transform_signature(obj)
            _observed_materials[guid] = _object_material_signature(obj, material_cache)
            if update_geometry:
                changed_mesh_objects.append(obj)
            _known_guids.add(guid)
        _dirty.pop(guid, None)
    if changed_objects or removed:
        sync_instances(context, link, sent_materials, changed_mesh_objects, update_materials=False)
