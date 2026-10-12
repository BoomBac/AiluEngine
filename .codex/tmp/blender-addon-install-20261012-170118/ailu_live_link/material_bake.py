"""Object-specific native Cycles conversion for graphs outside the direct PBR transport contract."""
import os
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


def invalidate():
    _payloads.clear()
    for mesh in _meshes.values():
        bpy.data.meshes.remove(mesh)
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
    if obj is not None and object_needs_bake(obj):
        return str(uuid.uuid5(uuid.NAMESPACE_URL, _bake_identity(obj) + ":" + identity.get_guid(material)))
    return identity.get_guid(material)


def object_needs_bake(obj):
    return obj.type == "MESH" and any(requires_bake(slot.material) for slot in obj.material_slots)


def _bake_identity(obj):
    # Unmodified UV-only mesh instances share both their atlas and channel textures. Coordinate-sensitive or
    # evaluated geometry keeps an object-specific identity; never reuse a world-space bake across instances.
    visited = set()

    def spatial(tree):
        if not tree or tree.as_pointer() in visited:
            return False
        visited.add(tree.as_pointer())
        for node in tree.nodes:
            if node.type == "NEW_GEOMETRY":
                return True
            if node.type == "TEX_COORD" and any(socket.is_linked and socket.name != "UV" for socket in node.outputs):
                return True
            if node.type == "GROUP" and spatial(node.node_tree):
                return True
        return False

    if obj.modifiers or any(spatial(slot.material.node_tree) for slot in obj.material_slots
                            if slot.material and slot.material.use_nodes):
        return identity.get_guid(obj)
    slots = ":".join(identity.get_guid(slot.material) if slot.material else "" for slot in obj.material_slots)
    return identity.get_guid(obj.data) + ":" + obj.data.name_full + ":" + slots + ":" + str(tuple(obj.matrix_world.to_scale()))


def _temporary_scene():
    scene = bpy.data.scenes.new("AiluLiveLinkBake")
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 1
    scene.render.bake.margin = 4
    scene.render.bake.use_clear = True
    return scene


@contextmanager
def _override(scene, obj):
    layer = scene.view_layers[0]
    layer.objects.active = obj
    obj.select_set(True, view_layer=layer)
    window = bpy.context.window
    previous_scene = window.scene if window else None
    previous_layer = window.view_layer if window else None
    try:
        if window:
            window.scene = scene
        with bpy.context.temp_override(scene=scene, view_layer=layer, object=obj, active_object=obj,
                                       selected_objects=[obj], selected_editable_objects=[obj]):
            yield
    finally:
        if window:
            window.scene = previous_scene
            window.view_layer = previous_layer


def prepared_mesh(obj, depsgraph):
    """Cache the exact evaluated mesh/UV atlas used both by the bake and by mesh transport."""
    global _baking
    key = _bake_identity(obj)
    if key in _meshes:
        _meshes.move_to_end(key)
        return _meshes[key]
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
        else:
            with _override(scene, temporary):
                bpy.ops.object.mode_set(mode="EDIT")
                try:
                    bpy.ops.mesh.select_all(action="SELECT")
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
        bpy.data.scenes.remove(scene)
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

    def shader_channel(self, socket, channel, groups=(), depth=0):
        from .material_sync import _shader_source
        if depth > 64:
            raise ValueError("Shader graph exceeds traversal limit")
        source = _shader_source(socket, groups)
        if source is None:
            return self.constant(0.0 if channel in {"alpha", "emission", "metallic"} else 0.5)
        node, groups = source
        if node.type in {"MIX_SHADER", "ADD_SHADER"}:
            inputs = list(node.inputs)[1:] if node.type == "MIX_SHADER" else list(node.inputs)
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
    if not np.isfinite(pixels).all():
        raise ValueError(f"Non-finite baked pixels: {name}")
    if color:
        rgb = np.maximum(pixels[:, :, :3], 0.0)
        pixels[:, :, :3] = np.where(rgb <= 0.0031308, rgb * 12.92, 1.055 * rgb ** (1.0 / 2.4) - 0.055)
    image = bpy.data.images.new(name, width=pixels.shape[1], height=pixels.shape[0], alpha=True)
    try:
        image.colorspace_settings.name = "Non-Color"
        image.alpha_mode = "STRAIGHT"
        image.pixels.foreach_set(pixels.astype(np.float32).ravel())
        image.filepath_raw = os.path.join(_temp_dir(), name + ".png")
        image.file_format = "PNG"
        image.save()
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


def export(material, obj, link, resolution=None):
    global _baking
    if bpy.context.mode != "OBJECT":
        raise RuntimeError("Live Link graph baking requires Object Mode; source edit mode was preserved")
    guid = material_guid(material, obj)
    default_resolution = 1024 if str(material.get("ailu_export_mode", "AUTO")).upper() == "BAKE" else 256
    resolution = int(material.get("ailu_bake_resolution", default_resolution)) if resolution is None else resolution
    resolution = max(16, min(resolution, 4096))
    if guid in _payloads:
        link.send_json(protocol.MATERIAL_UPSERT, _payloads[guid])
        return
    _baking = True
    scene = None
    temporary = None
    mesh = None
    generated_materials = []
    images = []
    try:
        if not material.use_nodes:
            material = material.copy()
            generated_materials.append(material)
            material.use_nodes = True
            principled = next(node for node in material.node_tree.nodes if node.type == "BSDF_PRINCIPLED")
            principled.inputs["Base Color"].default_value = material.diffuse_color
            principled.inputs["Roughness"].default_value = material.roughness
            principled.inputs["Metallic"].default_value = material.metallic
            principled.inputs["Alpha"].default_value = material.diffuse_color[3]
        atlas_mesh = prepared_mesh(obj, bpy.context.evaluated_depsgraph_get())
        source_uv = atlas_mesh.get("ailu_source_uv", "")
        mesh = atlas_mesh.copy()
        scene = _temporary_scene()
        temporary = bpy.data.objects.new("AiluBakeObject", mesh)
        temporary.matrix_world = obj.matrix_world
        scene.collection.objects.link(temporary)
        surface_output = next(node for node in material.node_tree.nodes
                              if node.type == "OUTPUT_MATERIAL" and node.is_active_output)
        surface = surface_output.inputs["Surface"]
        slot_indices = {index for index, slot in enumerate(obj.material_slots)
                        if slot.material and material_guid(slot.material, obj) == guid}
        material_indices = np.empty(len(mesh.polygons), dtype=np.int32)
        mesh.polygons.foreach_get("material_index", material_indices)
        # Other material slots receive separate targets so their UV pixels cannot overwrite this material's atlas.
        channels = {}
        for channel in ("color", "alpha", "roughness", "metallic", "emission", "normal", "coverage"):
            channel_material_start = len(generated_materials)
            channel_image_start = len(images)
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
                source = graph.constant(1.0) if channel == "coverage" else graph.shader_channel(surface, channel)
                tree.links.new(source, emission.inputs["Color"])
                tree.links.new(emission.outputs[0], output.inputs["Surface"])
            target = bpy.data.images.new("AiluBakeTarget", width=resolution, height=resolution,
                                         alpha=True, float_buffer=True)
            images.append(target)
            target.colorspace_settings.name = "Non-Color"
            image_node = tree.nodes.new("ShaderNodeTexImage")
            image_node.image = target
            tree.nodes.active = image_node
            mesh.materials.clear()
            slot_count = max(1, len(obj.material_slots))
            for index in range(slot_count):
                if index in slot_indices:
                    mesh.materials.append(converted)
                else:
                    other = converted.copy()
                    generated_materials.append(other)
                    other_target = bpy.data.images.new("AiluOtherTarget", width=resolution, height=resolution,
                                                       alpha=True, float_buffer=True)
                    images.append(other_target)
                    other.node_tree.nodes.active.image = other_target
                    mesh.materials.append(other)
            mesh.polygons.foreach_set("material_index", material_indices)
            with _override(scene, temporary):
                bpy.ops.object.bake(type="NORMAL" if channel == "normal" else "EMIT",
                                    uv_layer=atlas_mesh["ailu_atlas_uv"], normal_space="TANGENT")
            pixels = np.empty(resolution * resolution * 4, dtype=np.float32)
            target.pixels.foreach_get(pixels)
            channels[channel] = pixels.reshape(resolution, resolution, 4)
            # Release Cycles targets and copied node graphs before allocating the next channel/slot set.
            mesh.materials.clear()
            for generated in generated_materials[channel_material_start:]:
                bpy.data.materials.remove(generated)
            del generated_materials[channel_material_start:]
            for image in images[channel_image_start:]:
                bpy.data.images.remove(image)
            del images[channel_image_start:]
        alpha = np.clip(channels["alpha"][:, :, 0], 0.0, 1.0)
        covered = channels["coverage"][:, :, 0] > 0.5
        continuous, alpha_test = opacity_mode(material, alpha, covered)
        channels["color"][:, :, 3] = alpha
        base_scale = max(1.0, float(np.max(channels["color"][:, :, :3])))
        emission_scale = max(1.0, float(np.max(channels["emission"][:, :, :3])))
        channels["color"][:, :, :3] /= base_scale
        channels["emission"][:, :, :3] /= emission_scale
        rm = channels["roughness"]
        rm[:, :, 1] = channels["metallic"][:, :, 0]
        rm[:, :, 3] = 1.0
        value = {
            "material_guid": guid, "name": material.name_full + "@" + obj.name_full,
            "base_color": [base_scale] * 3 + [1.0], "roughness": 1.0, "metallic": 1.0,
            "emission": [1.0] * 3, "emission_strength": emission_scale,
            "emission_texture_factor": True,
            "base_color_path": _save_pixels(guid + "_base", channels["color"], color=True),
            "normal_path": _save_pixels(guid + "_normal", channels["normal"]),
            "emission_path": _save_pixels(guid + "_emission", channels["emission"], color=True),
            "roughness_metallic_path": _save_pixels(guid + "_rm", rm),
            "alpha_test": alpha_test, "alpha_blend": continuous,
            "alpha_cutoff": float(np.clip(material.get("ailu_alpha_cutoff", 0.5), 0.0, 1.0)),
            "double_sided": not material.use_backface_culling,
        }
        _payloads[guid] = value
        link.send_json(protocol.MATERIAL_UPSERT, value)
        print(f"[BlenderLiveLink] Baked graph: material={material.name_full!r}, object={obj.name_full!r}, "
              f"resolution={resolution}, camera-ray approximation, alpha_blend={continuous}")
    finally:
        if temporary:
            bpy.data.objects.remove(temporary, do_unlink=True)
        if mesh:
            bpy.data.meshes.remove(mesh)
        for generated in generated_materials:
            bpy.data.materials.remove(generated)
        for image in images:
            bpy.data.images.remove(image)
        if scene:
            bpy.data.scenes.remove(scene)
        _baking = False
