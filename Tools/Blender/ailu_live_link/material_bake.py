"""Object-specific native Cycles conversion for graphs outside the direct PBR transport contract."""
import os
import time
import uuid
from collections import OrderedDict
from contextlib import contextmanager

import bpy
import numpy as np

from . import identity, protocol
from .mesh_sync import _temp_dir

_meshes = OrderedDict()
_payloads = {}
_baking = False
_session_scene = None
_session_active = False
_session_device = "CPU"
_session_timings = None
_png_encoder = None
_session_depsgraph = None


@contextmanager
def _Measure(name):
    started = time.perf_counter()
    try:
        yield
    finally:
        if _session_timings is not None:
            _session_timings[name] = _session_timings.get(name, 0.0) + time.perf_counter() - started


@contextmanager
def ExportSession():
    """Own optional FullSync scratch scene and diagnostics; nested exports retain the outer owner."""
    global _session_active, _session_scene, _session_device, _session_timings, _png_encoder
    global _session_depsgraph
    if _session_active:
        yield
        return
    _session_active = True
    _session_timings = {}
    started = time.perf_counter()
    preferences = None
    previous_devices = []
    previous_backend = None
    try:
        if os.environ.get("AILU_BLENDER_BAKE_REUSE_DEPSGRAPH", "1") == "1":
            with _Measure("source_depsgraph_initial"):
                _session_depsgraph = bpy.context.evaluated_depsgraph_get()
        backend = os.environ.get("AILU_BLENDER_BAKE_DEVICE", "CPU").upper()
        if backend != "CPU":
            if backend not in {"CUDA", "OPTIX"}:
                raise ValueError(f"Unsupported diagnostic bake device: {backend}")
            preferences = bpy.context.preferences.addons["cycles"].preferences
            previous_backend = preferences.compute_device_type
            preferences.refresh_devices()
            previous_devices = [(device, device.use) for device in preferences.devices]
            preferences.compute_device_type = backend
            preferences.refresh_devices()
            found = False
            for device in preferences.devices:
                device.use = device.type == backend and "NVIDIA" in device.name.upper()
                found |= device.use
            if not found:
                raise RuntimeError(f"No NVIDIA {backend} device available for bake experiment")
            _session_device = "GPU"
        if os.environ.get("AILU_BLENDER_BAKE_REUSE_SCENE", "0") == "1":
            _session_scene = _temporary_scene()
        png_mode = os.environ.get("AILU_BLENDER_BAKE_PNG_PROCESS", "AUTO").upper()
        if png_mode != "0":
            from .png_encoder import PngEncoder
            if png_mode == "1" or (png_mode == "AUTO" and PngEncoder.IsAvailable()):
                try:
                    _png_encoder = PngEncoder(_temp_dir())
                except OSError:
                    if png_mode != "AUTO":
                        raise
                    print("[BlenderLiveLink] PNG worker unavailable; using native image saving")
        yield
    except Exception:
        # Async image failure can leave cached paths unpublished or incomplete; never reuse that snapshot.
        invalidate()
        raise
    finally:
        try:
            if _png_encoder:
                _png_encoder.Close()
        finally:
            _png_encoder = None
            try:
                if _session_scene:
                    bpy.data.scenes.remove(_session_scene)
            finally:
                _session_scene = None
                try:
                    if preferences:
                        preferences.compute_device_type = previous_backend
                        for device, enabled in previous_devices:
                            device.use = enabled
                finally:
                    phases = " ".join(f"{name}={value * 1000:.2f}ms" for name, value in _session_timings.items())
                    total_ms = (time.perf_counter() - started) * 1000
                    print(f"[BlenderLiveLink] Bake session: total={total_ms:.2f}ms {phases}")
                    _session_timings = None
                    _session_depsgraph = None
                    _session_device = "CPU"
                    _session_active = False


def _ReleaseScene(scene):
    if scene and scene != _session_scene:
        bpy.data.scenes.remove(scene)


def FlushMaterials():
    if _png_encoder:
        _png_encoder.Drain(wait=True)


def _SendMaterial(value, link):
    if _png_encoder:
        _png_encoder.QueueMaterial(value, link)
    else:
        link.send_json(protocol.MATERIAL_UPSERT, value)


def invalidate():
    _payloads.clear()
    for mesh in _meshes.values():
        try:
            if mesh.as_pointer():
                bpy.data.meshes.remove(mesh)
        except ReferenceError:
            pass
    _meshes.clear()


def requires_bake(material):
    if not material or not material.use_nodes:
        return False
    mode = str(material.get("ailu_export_mode", "AUTO")).upper()
    if mode == "BAKE":
        return True
    from .material_sync import _can_export_direct
    if mode == "DIRECT" or _can_export_direct(material):
        return False
    output = next((node for node in material.node_tree.nodes
                   if node.type == "OUTPUT_MATERIAL" and node.is_active_output), None)
    if not output or not output.inputs["Surface"].is_linked:
        return False
    visited = set()

    def complex_branch(socket):
        if not socket.is_linked:
            return False
        node = socket.links[0].from_node
        if node.as_pointer() in visited:
            return False
        visited.add(node.as_pointer())
        if node.type not in {"BSDF_PRINCIPLED", "BSDF_TRANSPARENT", "TEX_IMAGE", "NORMAL_MAP", "REROUTE"}:
            return True
        if node.type == "TEX_IMAGE" and node.inputs["Vector"].is_linked:
            return True
        if node.type == "NORMAL_MAP" and (node.space != "TANGENT" or node.uv_map
                                         or node.inputs["Strength"].default_value != 1.0):
            return True
        return any(complex_branch(value) for value in node.inputs)

    return complex_branch(output.inputs["Surface"])


def material_guid(material, obj):
    if obj is not None and requires_bake_for_object(material, obj):
        return str(uuid.uuid5(uuid.NAMESPACE_URL, _bake_identity(obj) + ":" + identity.get_guid(material)))
    return identity.get_guid(material)


def object_needs_bake(obj):
    return obj.type == "MESH" and any(requires_bake(slot.material) for slot in obj.material_slots)


def requires_bake_for_object(material, obj):
    if requires_bake(material):
        return True
    if str(material.get("ailu_export_mode", "AUTO")).upper() == "DIRECT":
        return False
    if obj is None or not object_needs_bake(obj):
        return False
    has_explicit_atlas = any(slot.material and str(slot.material.get("ailu_export_mode", "AUTO")).upper() == "BAKE"
                             for slot in obj.material_slots)
    has_source_uv = any(layer.active_render for layer in obj.data.uv_layers)
    if not has_explicit_atlas and has_source_uv:
        return False
    visited = set()

    def has_image(tree):
        if not tree or tree.as_pointer() in visited:
            return False
        visited.add(tree.as_pointer())
        return any(node.type == "TEX_IMAGE" or (node.type == "GROUP" and has_image(node.node_tree))
                   for node in tree.nodes)

    return material.use_nodes and has_image(material.node_tree)


def _tree_uses_spatial_coordinates(tree, visited=None):
    visited = set() if visited is None else visited
    if not tree or tree.as_pointer() in visited:
        return False
    visited.add(tree.as_pointer())
    for node in tree.nodes:
        if node.type == "NEW_GEOMETRY":
            return True
        if node.type == "TEX_COORD" and any(
                socket.is_linked and socket.name != "UV" for socket in node.outputs):
            return True
        if node.type == "GROUP" and _tree_uses_spatial_coordinates(node.node_tree, visited):
            return True
    return False


def _uses_spatial_coordinates(material):
    return material and material.use_nodes and _tree_uses_spatial_coordinates(material.node_tree)


def invalidate_object(obj):
    """Invalidate only object-specific baked payloads affected by a transform."""
    spatial_materials = [slot.material for slot in obj.material_slots
                         if slot.material and _uses_spatial_coordinates(slot.material)]
    should_resync = False
    for slot in obj.material_slots:
        material = slot.material
        if material and requires_bake_for_object(material, obj):
            guid = material_guid(material, obj)
            if material in spatial_materials:
                _payloads.pop(guid, None)
                should_resync = True
            elif guid not in _payloads:
                should_resync = True
    return should_resync


def invalidate_materials(obj):
    """Drop cached baked outputs when a material input changes without changing its object atlas."""
    for slot in obj.material_slots:
        material = slot.material
        if material and requires_bake_for_object(material, obj):
            _payloads.pop(material_guid(material, obj), None)


def _bake_identity(obj):
    # Unmodified UV-only mesh instances share both their atlas and channel textures. Coordinate-sensitive or
    # evaluated geometry keeps an object-specific identity; never reuse a world-space bake across instances.
    if obj.modifiers or any(_uses_spatial_coordinates(slot.material) for slot in obj.material_slots):
        return identity.get_guid(obj)
    slots = ":".join(identity.get_guid(slot.material) if slot.material else "" for slot in obj.material_slots)
    return identity.get_guid(obj.data) + ":" + obj.data.name_full + ":" + slots + ":" + str(tuple(obj.matrix_world.to_scale()))


def _temporary_scene():
    if _session_scene is not None:
        return _session_scene
    scene = bpy.data.scenes.new("AiluLiveLinkBake")
    scene.render.engine = "CYCLES"
    scene.cycles.device = _session_device
    scene.cycles.samples = 1
    scene.render.bake.margin = 4
    scene.render.bake.use_clear = True
    return scene


@contextmanager
def _override(scene, obj, window_required=False):
    layer = scene.view_layers[0]
    layer.objects.active = obj
    obj.select_set(True, view_layer=layer)
    window = bpy.context.window
    switch_window = window and (window_required
                               or os.environ.get("AILU_BLENDER_BAKE_CONTEXT_OVERRIDE_ONLY", "1") != "1")
    previous_scene = window.scene if window else None
    previous_layer = window.view_layer if window else None
    try:
        with _Measure("context_enter"):
            if switch_window:
                window.scene = scene
        with bpy.context.temp_override(scene=scene, view_layer=layer, object=obj, active_object=obj,
                                       selected_objects=[obj], selected_editable_objects=[obj]):
            yield
    finally:
        with _Measure("context_restore"):
            if switch_window:
                window.scene = previous_scene
                window.view_layer = previous_layer


def prepared_mesh(obj, depsgraph):
    """Cache the exact evaluated mesh/UV atlas used both by the bake and by mesh transport."""
    global _baking
    key = _bake_identity(obj)
    if key in _meshes:
        mesh = _meshes[key]
        try:
            is_alive = mesh.as_pointer() != 0
        except ReferenceError:
            is_alive = False
        if is_alive:
            _meshes.move_to_end(key)
            return mesh
        del _meshes[key]
    with _Measure("atlas_evaluation"):
        mesh = bpy.data.meshes.new_from_object(obj.evaluated_get(depsgraph), preserve_all_data_layers=True,
                                              depsgraph=depsgraph)
    scene = _temporary_scene()
    temporary = bpy.data.objects.new("AiluBakeAtlas", mesh)
    scene.collection.objects.link(temporary)
    previous_baking = _baking
    _baking = True
    try:
        source_uv = next((layer.name for layer in mesh.uv_layers if layer.active_render), "")
        atlas = mesh.uv_layers.new(name="AiluLiveLinkAtlas")
        mesh.uv_layers.active = atlas
        mesh["ailu_source_uv"] = source_uv
        mesh["ailu_atlas_uv"] = atlas.name
        if source_uv:
            mesh.uv_layers[source_uv].active_render = True
        # Keep the source render UV available to texture nodes with an unconnected Vector input.
        exact_atlas = any(str(slot.material.get("ailu_export_mode", "AUTO")).upper() == "BAKE"
                          for slot in obj.material_slots if slot.material)
        if source_uv and not exact_atlas:
            # AUTO retains existing UVs, avoiding million-face Edit Mode/BMesh conversion and island packing.
            uv_values = np.empty(len(mesh.loops) * 2, dtype=np.float32)
            mesh.uv_layers[source_uv].data.foreach_get("uv", uv_values)
            atlas.data.foreach_set("uv", uv_values)
        elif mesh.polygons:
            with _override(scene, temporary, window_required=True):
                bpy.ops.object.mode_set(mode="EDIT")
                try:
                    bpy.ops.mesh.select_all(action="SELECT")
                    with _Measure("atlas_projection"):
                        bpy.ops.uv.smart_project(island_margin=0.01)
                finally:
                    bpy.ops.object.mode_set(mode="OBJECT")
        _meshes[key] = mesh
        while len(_meshes) > 4:
            _, retired = _meshes.popitem(last=False)
            bpy.data.meshes.remove(retired)
        return mesh
    except Exception:
        bpy.data.objects.remove(temporary, do_unlink=True)
        temporary = None
        bpy.data.meshes.remove(mesh)
        raise
    finally:
        if temporary:
            bpy.data.objects.remove(temporary, do_unlink=True)
        _ReleaseScene(scene)
        _baking = previous_baking


class ChannelGraph:
    """Flatten actual group-instance connections into a native Blender channel graph."""

    def __init__(self, material, source_uv):
        self._tree = material.node_tree
        self._source_uv = source_uv
        self._nodes = {}
        self._resolving = set()

    def constant(self, value):
        if isinstance(value, (int, float)):
            node = self._tree.nodes.new("ShaderNodeValue")
            node.outputs[0].default_value = value
        else:
            node = self._tree.nodes.new("ShaderNodeRGB")
            node.outputs[0].default_value = tuple(value[:3]) + (1.0,)
        return node.outputs[0]

    def input(self, socket, groups=(), default=0.0):
        if socket is None:
            return self.constant(default)
        if not socket.is_linked:
            return self.constant(socket.default_value)
        return self.output(socket.links[0].from_socket, groups)

    def output(self, socket, groups):
        node = socket.node
        if node.type == "REROUTE":
            return self.input(node.inputs[0], groups)
        if node.type == "GROUP":
            output = next(value for value in node.node_tree.nodes
                          if value.type == "GROUP_OUTPUT" and value.is_active_output)
            target = next(value for value in output.inputs if value.identifier == socket.identifier)
            return self.input(target, groups + (node,))
        if node.type == "GROUP_INPUT":
            target = next(value for value in groups[-1].inputs if value.identifier == socket.identifier)
            return self.input(target, groups[:-1])
        if node.type == "LIGHT_PATH":
            # A static texture cannot reproduce ray- or view-dependent effects. Use the visible camera branch.
            return self.constant(1.0 if socket.name == "Is Camera Ray" else 0.0)
        key = (node.as_pointer(), tuple(group.as_pointer() for group in groups))
        if key in self._resolving:
            raise ValueError("Cyclic material graph")
        if key not in self._nodes:
            self._resolving.add(key)
            copied = self._tree.nodes.new(node.bl_idname)
            self._nodes[key] = copied
            for prop in node.bl_rna.properties:
                # bl_* / registration setters mutate the shared node type, not this node instance.
                if (prop.is_readonly or prop.is_registered or prop.is_registered_optional
                        or prop.identifier.startswith("bl_")
                        or prop.identifier in {"name", "location", "parent", "select"}):
                    continue
                try:
                    setattr(copied, prop.identifier, getattr(node, prop.identifier))
                except (AttributeError, TypeError, ValueError):
                    pass
            if node.type == "VALTORGB":
                source = node.color_ramp
                target = copied.color_ramp
                target.interpolation = source.interpolation
                target.color_mode = source.color_mode
                target.hue_interpolation = source.hue_interpolation
                while len(target.elements) > 1:
                    target.elements.remove(target.elements[-1])
                target.elements[0].position = source.elements[0].position
                target.elements[0].color = source.elements[0].color
                for element in list(source.elements)[1:]:
                    target.elements.new(element.position).color = element.color
            if node.type in {"CURVE_RGB", "CURVE_VEC", "CURVE_FLOAT"}:
                copied.mapping.initialize()
                for source_curve, target_curve in zip(node.mapping.curves, copied.mapping.curves):
                    target_curve.points[0].location = source_curve.points[0].location
                    target_curve.points[-1].location = source_curve.points[-1].location
                    for point in list(source_curve.points)[1:-1]:
                        target_curve.points.new(*point.location).handle_type = point.handle_type
                copied.mapping.update()
            for index, source_input in enumerate(list(node.inputs)):
                if index >= len(copied.inputs):
                    break
                destination = copied.inputs[index]
                if source_input.is_linked:
                    self._tree.links.new(self.input(source_input, groups), destination)
                elif hasattr(source_input, "default_value"):
                    destination.default_value = source_input.default_value
            if node.type == "TEX_IMAGE" and not node.inputs["Vector"].is_linked and self._source_uv:
                uv = self._tree.nodes.new("ShaderNodeUVMap")
                uv.uv_map = self._source_uv
                self._tree.links.new(uv.outputs[0], copied.inputs["Vector"])
            if node.type == "TEX_COORD" and self._source_uv:
                # Unnamed UV coordinates refer to the original render UV, not the new bake atlas.
                uv = self._tree.nodes.new("ShaderNodeUVMap")
                uv.uv_map = self._source_uv
                copied_outputs = {value.name: value for value in copied.outputs}
                self._nodes[(key, "UV")] = uv.outputs[0]
            self._resolving.remove(key)
        if node.type == "TEX_COORD" and socket.name == "UV" and self._source_uv:
            return self._nodes[(key, "UV")]
        return self._nodes[key].outputs[list(node.outputs).index(socket)]

    def mix(self, first, second, factor, add=False):
        node = self._tree.nodes.new("ShaderNodeMixRGB")
        node.blend_type = "ADD" if add else "MIX"
        node.inputs[0].default_value = 1.0
        if not add:
            self._tree.links.new(factor, node.inputs[0])
        self._tree.links.new(first, node.inputs[1])
        self._tree.links.new(second, node.inputs[2])
        return node.outputs[0]

    def math(self, operation, first, second):
        node = self._tree.nodes.new("ShaderNodeMath")
        node.operation = operation
        self._tree.links.new(first, node.inputs[0])
        self._tree.links.new(second, node.inputs[1])
        return node.outputs[0]

    def _weighted_lobe_channel(self, socket, channel, lobe, groups=(), depth=0):
        from .material_sync import _shader_source
        if depth > 64:
            raise ValueError("Shader graph exceeds traversal limit")
        source = _shader_source(socket, groups)
        if source is None:
            return self.constant(0.0), self.constant(0.0)
        node, groups = source
        if node.type in {"MIX_SHADER", "ADD_SHADER"}:
            added = node.type == "ADD_SHADER"
            inputs = list(node.inputs) if added else list(node.inputs)[1:]
            first, first_weight = self._weighted_lobe_channel(inputs[0], channel, lobe, groups, depth + 1)
            second, second_weight = self._weighted_lobe_channel(inputs[1], channel, lobe, groups, depth + 1)
            factor = self.constant(0.5) if added else self.input(node.inputs[0], groups)
            return self.mix(first, second, factor, add=added), self.mix(first_weight, second_weight, factor, add=added)
        if node.type != lobe:
            return self.constant(0.0), self.constant(0.0)
        name = "Color" if channel == "color" else "Roughness"
        return self.input(node.inputs.get(name), groups), self.constant(1.0)

    def shader_channel(self, socket, channel, groups=(), depth=0):
        from .material_sync import _shader_source, _principled_source, is_diffuse_glossy
        if depth > 64:
            raise ValueError("Shader graph exceeds traversal limit")
        source = _shader_source(socket, groups)
        if source is None:
            return self.constant(0.0 if channel in {"alpha", "emission", "metallic"} else 0.5)
        source_groups = groups
        node, groups = source
        if node.type in {"MIX_SHADER", "ADD_SHADER"}:
            if channel in {"color", "roughness", "metallic"} and is_diffuse_glossy(socket, source_groups):
                if channel == "metallic":
                    return self.constant(0.0)
                # Diffuse roughness is not GGX width, and white Glossy pigment is not diffuse albedo.
                # Accumulate only the relevant lobes before normalizing, including nested/texture weights.
                lobe = "BSDF_DIFFUSE" if channel == "color" else "BSDF_GLOSSY"
                total, weight = self._weighted_lobe_channel(socket, channel, lobe, source_groups, depth)
                divide = self._tree.nodes.new("ShaderNodeMixRGB")
                divide.blend_type = "DIVIDE"
                divide.inputs[0].default_value = 1.0
                self._tree.links.new(total, divide.inputs[1])
                self._tree.links.new(weight, divide.inputs[2])
                present = self.math("GREATER_THAN", weight, self.constant(0.0))
                fallback = self.constant(1.0 if channel == "roughness" else (0.0, 0.0, 0.0))
                return self.mix(fallback, divide.outputs[0], present)
            inputs = list(node.inputs)[1:] if node.type == "MIX_SHADER" else list(node.inputs)
            # Transport is exported from the primary dielectric closure. An added Glossy lobe must not
            # turn its glass into a half-metal or tint the transmitted background with reflection pigment.
            if node.type == "ADD_SHADER" and channel in {"color", "roughness", "metallic"}:
                primary = _principled_source(inputs[0], groups) or _principled_source(inputs[1], groups)
                if primary and primary[0].type in {"BSDF_GLASS", "BSDF_REFRACTION"}:
                    dielectric, dielectric_groups = primary
                    if channel == "metallic":
                        return self.constant(0.0)
                    socket_name = "Color" if channel == "color" else "Roughness"
                    return self.input(dielectric.inputs.get(socket_name), dielectric_groups)
            first = self.shader_channel(inputs[0], channel, groups, depth + 1)
            second = self.shader_channel(inputs[1], channel, groups, depth + 1)
            added = node.type == "ADD_SHADER"
            factor = self.constant(0.5) if added else self.input(node.inputs[0], groups)
            if channel == "alpha" or (added and channel == "emission"):
                return self.mix(first, second, factor, add=added)
            # Transparent lobes contribute coverage, not white pigment. Normalize the remaining surface weights
            # so foliage edges do not mix the Transparent BSDF's white into their albedo.
            first_alpha = self.shader_channel(inputs[0], "alpha", groups, depth + 1)
            second_alpha = self.shader_channel(inputs[1], "alpha", groups, depth + 1)
            first_weight = self.math("MULTIPLY", first_alpha, self.math("SUBTRACT", self.constant(1.0), factor))
            second_weight = self.math("MULTIPLY", second_alpha, factor)
            normalized = self.math("DIVIDE", second_weight, self.math("ADD", first_weight, second_weight))
            return self.mix(first, second, normalized)
        if channel == "alpha":
            return (self.constant(0.0) if node.type == "BSDF_TRANSPARENT"
                    else self.input(node.inputs.get("Alpha"), groups, 1.0))
        if channel == "roughness":
            return self.input(node.inputs.get("Roughness"), groups, 0.5)
        if channel == "metallic":
            return self.input(node.inputs.get("Metallic"), groups, 1.0 if node.type == "BSDF_GLOSSY" else 0.0)
        if channel == "color":
            if node.type == "EMISSION":
                return self.constant((0.0, 0.0, 0.0))
            return self.input(node.inputs.get("Base Color", node.inputs.get("Color")), groups, (0.8, 0.8, 0.8))
        if channel == "emission":
            color = node.inputs.get("Emission Color", node.inputs.get("Emission"))
            strength = node.inputs.get("Emission Strength")
            if node.type == "EMISSION":
                color, strength = node.inputs.get("Color"), node.inputs.get("Strength")
            multiply = self._tree.nodes.new("ShaderNodeMixRGB")
            multiply.blend_type = "MULTIPLY"
            multiply.inputs[0].default_value = 1.0
            self._tree.links.new(self.input(color, groups, (0.0, 0.0, 0.0)), multiply.inputs[1])
            self._tree.links.new(self.input(strength, groups, 0.0), multiply.inputs[2])
            return multiply.outputs[0]
        raise ValueError(channel)


def _save_pixels(name, pixels, color=False):
    started = time.perf_counter()
    if not np.isfinite(pixels).all():
        raise ValueError(f"Non-finite baked pixels: {name}")
    if color:
        rgb = np.maximum(pixels[:, :, :3], 0.0)
        pixels[:, :, :3] = np.where(rgb <= 0.0031308, rgb * 12.92, 1.055 * rgb ** (1.0 / 2.4) - 0.055)
    if _png_encoder:
        return _png_encoder.SendPixels(os.path.join(_temp_dir(), name + ".png"), pixels)
    image = bpy.data.images.new(name, width=pixels.shape[1], height=pixels.shape[0], alpha=True)
    try:
        image.colorspace_settings.name = "Non-Color"
        image.alpha_mode = "STRAIGHT"
        image.pixels.foreach_set(pixels.astype(np.float32).ravel())
        image.filepath_raw = os.path.join(_temp_dir(), name + ".png")
        image.file_format = "PNG"
        prepared = time.perf_counter()
        image.save()
        print(f"[BlenderLiveLink] PNG save: prepare={(prepared - started) * 1000:.2f}ms "
              f"encode_write={(time.perf_counter() - prepared) * 1000:.2f}ms")
        return image.filepath_raw
    finally:
        bpy.data.images.remove(image)


def opacity_mode(material, alpha, covered=None):
    covered = np.ones(alpha.shape, dtype=bool) if covered is None else covered
    visible = covered & (alpha > 0.02)
    fractional = visible & (alpha < 0.98)
    mode = material.get("ailu_alpha_mode", "AUTO")
    blend = mode == "BLEND" or (mode == "AUTO" and np.count_nonzero(fractional)
                                > 0.1 * max(1, np.count_nonzero(visible)))
    clip = bool(np.any(alpha[covered] < 0.98)) and not blend
    if mode == "CLIP":
        blend, clip = False, True
    if mode == "OPAQUE":
        blend, clip = False, False
    return blend, clip


def _resolution(material, resolution=None):
    default = 1024 if str(material.get("ailu_export_mode", "AUTO")).upper() == "BAKE" else 256
    value = int(material.get("ailu_bake_resolution", default)) if resolution is None else resolution
    return max(16, min(value, 4096))


def export(material, obj, link, resolution=None):
    if bpy.context.mode != "OBJECT":
        raise RuntimeError("Live Link graph baking requires Object Mode; source edit mode was preserved")
    guid = material_guid(material, obj)
    if guid in _payloads:
        _SendMaterial(_payloads[guid], link)
        return
    _export_materials([material], obj, link, _resolution(material, resolution))


def prepare_object_materials(obj, sent_materials):
    """Populate payloads in bounded batches; callers retain their existing material packet order."""
    if not object_needs_bake(obj):
        return
    groups = {}
    seen = set(sent_materials) | _payloads.keys()
    for slot in obj.material_slots:
        material = slot.material
        if not material or not requires_bake_for_object(material, obj):
            continue
        guid = material_guid(material, obj)
        if guid in seen:
            continue
        if material.use_nodes:
            output = next((node for node in material.node_tree.nodes
                           if node.type == "OUTPUT_MATERIAL" and node.is_active_output), None)
            if not output or not output.inputs["Surface"].is_linked:
                continue
        seen.add(guid)
        groups.setdefault(_resolution(material), []).append(material)
    for resolution, materials in groups.items():
        # Five pass arrays plus unpacked scalars: retain at most four materials / 128 MiB of pixel arrays.
        limit = max(1, min(4, (128 * 1024 * 1024) // (resolution * resolution * 128)))
        for start in range(0, len(materials), limit):
            _export_materials(materials[start:start + limit], obj, None, resolution)


def _has_zero_emission(surface, depth=0):
    """Prove zero emission only; unknown graphs retain their native bake and padding behavior."""
    from .material_sync import _shader_source
    if depth >= 32:
        return False
    source = _shader_source(surface)
    if source is None:
        return False
    node, groups = source
    if node.type in {"MIX_SHADER", "ADD_SHADER"}:
        # Zero remains zero for both existing emission mixing rules, even with a spatial factor.
        if groups:
            return False
        inputs = list(node.inputs)[1:] if node.type == "MIX_SHADER" else list(node.inputs)
        return all(_has_zero_emission(value, depth + 1) for value in inputs)
    if node.type not in {"BSDF_PRINCIPLED", "BSDF_DIFFUSE", "BSDF_GLOSSY", "BSDF_GLASS",
                         "BSDF_REFRACTION", "BSDF_TRANSPARENT", "EMISSION"}:
        return False

    def default(name, fallback):
        socket = node.inputs.get(name)
        return None if socket and socket.is_linked else (socket.default_value if socket else fallback)

    color_name = "Color" if node.type == "EMISSION" else "Emission Color"
    if node.type != "EMISSION" and node.inputs.get(color_name) is None:
        color_name = "Emission"
    strength_name = "Strength" if node.type == "EMISSION" else "Emission Strength"
    color = default(color_name, (0.0, 0.0, 0.0))
    strength = default(strength_name, 0.0)
    return (color is not None and strength is not None and np.isfinite(strength)
            and all(np.isfinite(value) and float(value) * float(strength) == 0.0 for value in color[:3]))


def _constant_scalars(surface, diffuse_only):
    """Match ChannelGraph's primitive closure defaults; layered/linked channels keep native baking."""
    from .material_sync import _shader_source
    source = _shader_source(surface)
    if source is None:
        return None
    node, _ = source
    if node.type not in {"BSDF_PRINCIPLED", "BSDF_DIFFUSE", "BSDF_GLOSSY", "BSDF_GLASS",
                         "BSDF_REFRACTION", "BSDF_TRANSPARENT", "EMISSION"}:
        return None
    values = []
    for name, fallback in (("Alpha", 1.0), ("Roughness", 0.5),
                           ("Metallic", 1.0 if node.type == "BSDF_GLOSSY" else 0.0)):
        if name == "Alpha" and node.type == "BSDF_TRANSPARENT":
            values.append(0.0)
        elif diffuse_only and name != "Alpha":
            values.append(1.0 if name == "Roughness" else 0.0)
        else:
            socket = node.inputs.get(name)
            if socket and socket.is_linked:
                return None
            values.append(float(socket.default_value) if socket else fallback)
    # Native shader rounding around opacity thresholds can change classification even with tiny pixel deltas.
    if any(abs(values[0] - threshold) < 1e-6 for threshold in (0.02, 0.98)):
        return None
    return tuple(values) if all(np.isfinite(value) and 0.0 <= value <= 1.0 for value in values) else None


def _channel_material(surface, source_uv, channel, diffuse_only, generated_materials):
    converted = bpy.data.materials.new("AiluBakeChannel")
    generated_materials.append(converted)
    converted.use_nodes = True
    tree = converted.node_tree
    tree.nodes.clear()
    graph = ChannelGraph(converted, source_uv)
    output = tree.nodes.new("ShaderNodeOutputMaterial")
    if channel == "normal":
        tree.links.new(graph.output(surface.links[0].from_socket, ()), output.inputs["Surface"])
    else:
        emission = tree.nodes.new("ShaderNodeEmission")
        if channel == "scalars":
            combine = tree.nodes.new("ShaderNodeCombineColor")
            combine.mode = "RGB"
            for index, scalar in enumerate(("alpha", "roughness", "metallic")):
                if diffuse_only and scalar != "alpha":
                    combine.inputs[index].default_value = 1.0 if scalar == "roughness" else 0.0
                    continue
                # The previous emission bake consumed red, including graphs returning RGB sockets.
                separate = tree.nodes.new("ShaderNodeSeparateColor")
                separate.mode = "RGB"
                tree.links.new(graph.shader_channel(surface, scalar), separate.inputs[0])
                tree.links.new(separate.outputs[0], combine.inputs[index])
            source = combine.outputs[0]
        else:
            source = (graph.constant(1.0) if channel == "coverage"
                      else graph.shader_channel(surface, channel))
        tree.links.new(source, emission.inputs["Color"])
        tree.links.new(emission.outputs[0], output.inputs["Surface"])
    return converted


def _surface_requires_normal_bake(surface):
    from .material_sync import _shader_source
    visited = set()

    def has_normal_input(socket, groups=()):
        source = _shader_source(socket, groups)
        if source is None:
            return bool(socket and socket.is_linked)
        node, groups = source
        key = (node.as_pointer(), tuple(group.as_pointer() for group in groups))
        if key in visited or len(visited) >= 64:
            return True
        visited.add(key)
        try:
            if node.type in {"MIX_SHADER", "ADD_SHADER"}:
                shader_inputs = list(node.inputs)[1:] if node.type == "MIX_SHADER" else list(node.inputs)
                return any(has_normal_input(value, groups) for value in shader_inputs[:2])
            normal = node.inputs.get("Normal")
            if normal is not None:
                return normal.is_linked
            return node.type not in {"BSDF_DIFFUSE", "BSDF_GLOSSY", "BSDF_GLASS", "BSDF_REFRACTION",
                                     "BSDF_TRANSPARENT", "BSDF_PRINCIPLED", "EMISSION"}
        finally:
            visited.remove(key)

    return has_normal_input(surface)


def _uniform_channel(pixels, component_count, color=False):
    if pixels is None or pixels.size == 0:
        return None
    values = pixels[:, :, :component_count].copy()
    if not np.isfinite(values).all():
        return None
    if color:
        rgb = np.maximum(values[:, :, :3], 0.0)
        values[:, :, :3] = np.where(rgb <= 0.0031308, rgb * 12.92,
                                    1.055 * np.power(rgb, 1.0 / 2.4) - 0.055)
    quantized = np.floor(np.clip(values, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
    sample = quantized[0, 0]
    if not np.all(quantized == sample):
        return None
    result = sample.astype(np.float32) / 255.0
    if color:
        result[:3] = np.where(result[:3] <= 0.04045, result[:3] / 12.92,
                              np.power((result[:3] + 0.055) / 1.055, 2.4))
    return result


def _publish_material(material, guid, obj, surface, diffuse_only, channels, link, resolution,
                      normal_required=True, export_id=""):
    from .material_sync import transmission_parameters
    packed = channels.pop("scalars")
    for index, scalar in enumerate(("alpha", "roughness", "metallic")):
        if diffuse_only and scalar != "alpha":
            continue
        pixels = np.ones_like(packed)
        pixels[:, :, :3] = packed[:, :, index:index + 1]
        channels[scalar] = pixels
    alpha = np.clip(channels["alpha"][:, :, 0], 0.0, 1.0)
    covered = channels["coverage"][:, :, 0] > 0.5
    continuous, alpha_test = opacity_mode(material, alpha, covered)
    channels["color"][:, :, 3] = alpha
    base_scale = max(1.0, float(np.max(channels["color"][:, :, :3])))
    emission_scale = max(1.0, float(np.max(channels["emission"][:, :, :3])))
    channels["color"][:, :, :3] /= base_scale
    channels["emission"][:, :, :3] /= emission_scale
    base_color_constant = _uniform_channel(channels["color"], 4, color=True)
    emission_constant = _uniform_channel(channels["emission"], 3, color=True)
    roughness_constant = _uniform_channel(channels.get("roughness"), 1)
    metallic_constant = _uniform_channel(channels.get("metallic"), 1)
    image_prefix = f"{guid}_{export_id}" if export_id else guid
    value = {
        "material_guid": guid, "name": material.name_full + "@" + obj.name_full,
        "base_color": [base_scale] * 3 + [1.0], "roughness": 1.0, "metallic": 1.0,
        "emission": (emission_constant.tolist() if emission_constant is not None else [1.0] * 3),
        "emission_strength": emission_scale,
        "emission_texture_factor": True,
        "base_color_path": ("" if base_color_constant is not None else
                            _save_pixels(image_prefix + "_base", channels["color"], color=True)),
        # PNG quantization cannot encode 0.5 exactly: byte 128 decodes to a tiny positive tangent X/Y offset.
        "normal_path": (_save_pixels(image_prefix + "_normal", channels["normal"]) if normal_required else ""),
        "emission_path": ("" if emission_constant is not None else
                          _save_pixels(image_prefix + "_emission", channels["emission"], color=True)),
        "roughness_path": ("" if diffuse_only or roughness_constant is not None else
                           _save_pixels(image_prefix + "_roughness", channels["roughness"])),
        "metallic_path": ("" if diffuse_only or metallic_constant is not None else
                          _save_pixels(image_prefix + "_metallic", channels["metallic"])),
        "alpha_test": alpha_test, "alpha_blend": continuous,
        "alpha_cutoff": float(np.clip(material.get("ailu_alpha_cutoff", 0.5), 0.0, 1.0)),
        "double_sided": not material.use_backface_culling,
    }
    if base_color_constant is not None:
        value["base_color"] = [float(component) * base_scale for component in base_color_constant[:3]] + [
            float(base_color_constant[3])]
    if roughness_constant is not None:
        value["roughness"] = float(roughness_constant[0])
    if metallic_constant is not None:
        value["metallic"] = float(metallic_constant[0])
    if diffuse_only:
        value.update(roughness=1.0, metallic=0.0, roughness_path="", metallic_path="")
    value.update(transmission_parameters(surface))
    if value["transmission"] > 0.0:
        print(f"[BlenderLiveLink] Dielectric closure preserved: material={material.name_full!r}; "
              "layered reflection is approximated by the primary dielectric")
    _payloads[guid] = value
    if link is not None:
        _SendMaterial(value, link)
    print(f"[BlenderLiveLink] Baked graph: material={material.name_full!r}, object={obj.name_full!r}, "
          f"resolution={resolution}, camera-ray approximation, alpha_blend={continuous}")


def _export_materials(materials, obj, link, resolution):
    global _baking
    from .material_sync import is_diffuse_only
    if bpy.context.mode != "OBJECT":
        raise RuntimeError("Live Link graph baking requires Object Mode; source edit mode was preserved")
    previous_baking = _baking
    _baking = True
    scene = temporary = mesh = None
    generated_materials = []
    images = []
    entries = {}
    constant_scalars = {}
    bake_passes = 0
    constant_channels = 0
    export_id = uuid.uuid4().hex
    timings = {"prepare": 0.0, "graphs": 0.0, "bake": 0.0, "pixels_cleanup": 0.0, "save_publish": 0.0}
    started = time.perf_counter()
    try:
        with _Measure("depsgraph_acquire"):
            depsgraph = _session_depsgraph or bpy.context.evaluated_depsgraph_get()
        atlas_mesh = prepared_mesh(obj, depsgraph)
        if not atlas_mesh.polygons:
            print(f"[BlenderLiveLink] Skipping surface bake: object={obj.name_full!r} has no evaluated faces")
            return
        source_uv = atlas_mesh.get("ailu_source_uv", "")
        mesh = atlas_mesh.copy()
        scene = _temporary_scene()
        temporary = bpy.data.objects.new("AiluBakeObject", mesh)
        temporary.matrix_world = obj.matrix_world
        scene.collection.objects.link(temporary)
        for material in materials:
            guid = material_guid(material, obj)
            if not material.use_nodes:
                material = material.copy()
                generated_materials.append(material)
                material.use_nodes = True
                principled = next(node for node in material.node_tree.nodes if node.type == "BSDF_PRINCIPLED")
                principled.inputs["Base Color"].default_value = material.diffuse_color
                principled.inputs["Roughness"].default_value = material.roughness
                principled.inputs["Metallic"].default_value = material.metallic
                principled.inputs["Alpha"].default_value = material.diffuse_color[3]
            output = next(node for node in material.node_tree.nodes
                          if node.type == "OUTPUT_MATERIAL" and node.is_active_output)
            surface = output.inputs["Surface"]
            entries[guid] = (material, surface, is_diffuse_only(surface), {})
        normal_required = {guid: _surface_requires_normal_bake(surface)
                           for guid, (_, surface, _, _) in entries.items()}
        slot_guids = [material_guid(slot.material, obj) if slot.material else "" for slot in obj.material_slots]
        material_indices = np.empty(len(mesh.polygons), dtype=np.int32)
        mesh.polygons.foreach_get("material_index", material_indices)
        timings["prepare"] = time.perf_counter() - started
        with _override(scene, temporary):
            for channel in ("color", "scalars", "emission", "normal", "coverage"):
                if channel == "normal" and not any(normal_required.values()):
                    constant_channels += len(entries)
                    continue
                if channel == "scalars":
                    values = {guid: _constant_scalars(surface, diffuse_only)
                              for guid, (_, surface, diffuse_only, _) in entries.items()}
                    if all(value is not None for value in values.values()):
                        # Defer reconstruction until the existing final coverage pass; preserve pass order.
                        constant_scalars = values
                        constant_channels += len(values)
                        continue
                if channel == "emission" and all(_has_zero_emission(entry[1]) for entry in entries.values()):
                    for _, _, _, channels in entries.values():
                        # Both EMIT targets carry the same raster coverage in alpha; retain original pass order.
                        pixels = np.zeros_like(channels["color"])
                        pixels[:, :, 3] = channels["color"][:, :, 3]
                        channels[channel] = pixels
                    constant_channels += len(entries)
                    continue
                phase_start = time.perf_counter()
                material_start = len(generated_materials)
                image_start = len(images)
                targets = {}
                converted_by_guid = {}
                for guid, (_, surface, diffuse_only, _) in entries.items():
                    converted = _channel_material(surface, source_uv, channel, diffuse_only, generated_materials)
                    target = bpy.data.images.new("AiluBakeTarget", width=resolution, height=resolution,
                                                 alpha=True, float_buffer=True)
                    images.append(target)
                    target.colorspace_settings.name = "Non-Color"
                    node = converted.node_tree.nodes.new("ShaderNodeTexImage")
                    node.image = target
                    converted.node_tree.nodes.active = node
                    targets[guid] = target
                    converted_by_guid[guid] = converted
                mesh.materials.clear()
                for index in range(max(1, len(obj.material_slots))):
                    guid = slot_guids[index] if index < len(slot_guids) else ""
                    if guid in converted_by_guid:
                        mesh.materials.append(converted_by_guid[guid])
                    else:
                        # Non-batched slots use isolated targets and cannot overwrite any material atlas.
                        other = next(iter(converted_by_guid.values())).copy()
                        generated_materials.append(other)
                        target = bpy.data.images.new("AiluOtherTarget", width=resolution, height=resolution,
                                                     alpha=True, float_buffer=True)
                        images.append(target)
                        other.node_tree.nodes.active.image = target
                        mesh.materials.append(other)
                mesh.polygons.foreach_set("material_index", material_indices)
                timings["graphs"] += time.perf_counter() - phase_start
                phase_start = time.perf_counter()
                bpy.ops.object.bake(type="NORMAL" if channel == "normal" else "EMIT",
                                    uv_layer=atlas_mesh["ailu_atlas_uv"], normal_space="TANGENT")
                bake_passes += 1
                timings["bake"] += time.perf_counter() - phase_start
                phase_start = time.perf_counter()
                for guid, target in targets.items():
                    if channel == "normal" and not normal_required[guid]:
                        continue
                    pixels = np.empty(resolution * resolution * 4, dtype=np.float32)
                    target.pixels.foreach_get(pixels)
                    entries[guid][3][channel] = pixels.reshape(resolution, resolution, 4)
                mesh.materials.clear()
                for generated in generated_materials[material_start:]:
                    bpy.data.materials.remove(generated)
                del generated_materials[material_start:]
                for image in images[image_start:]:
                    bpy.data.images.remove(image)
                del images[image_start:]
                timings["pixels_cleanup"] += time.perf_counter() - phase_start
        phase_start = time.perf_counter()
        for guid, (material, surface, diffuse_only, channels) in entries.items():
            if guid in constant_scalars:
                packed = channels["coverage"].copy()
                packed[:, :, :3] *= np.asarray(constant_scalars[guid], dtype=np.float32)
                channels["scalars"] = packed
            _publish_material(material, guid, obj, surface, diffuse_only, channels, link, resolution,
                              normal_required[guid], export_id)
            channels.clear()
        timings["save_publish"] = time.perf_counter() - phase_start
        phases = " ".join(f"{name}={value * 1000:.2f}ms" for name, value in timings.items())
        print(f"[BlenderLiveLink] Bake batch: object={obj.name_full!r} materials={len(entries)} "
              f"resolution={resolution} passes={bake_passes} constant_channels={constant_channels} {phases}")
    finally:
        cleanup_start = time.perf_counter()
        if temporary:
            bpy.data.objects.remove(temporary, do_unlink=True)
        if mesh:
            bpy.data.meshes.remove(mesh)
        for generated in generated_materials:
            bpy.data.materials.remove(generated)
        for image in images:
            bpy.data.images.remove(image)
        _ReleaseScene(scene)
        if _session_timings is not None:
            _session_timings["batch_cleanup"] = (_session_timings.get("batch_cleanup", 0.0)
                                                   + time.perf_counter() - cleanup_start)
        _baking = previous_baking
