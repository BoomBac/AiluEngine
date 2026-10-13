import os
import time
import bpy
import numpy as np
from contextlib import contextmanager

from . import identity, protocol
from .mesh_sync import _temp_dir

_packed_image_cache = {}
_scalar_image_cache = {}


def _shader_source(socket, groups=(), visited=None):
    if socket is None or not socket.is_linked:
        return None
    if visited is None:
        visited = set()
    key = (socket.as_pointer(), tuple(group.as_pointer() for group in groups))
    if key in visited or len(visited) >= 64:
        return None
    visited.add(key)
    output = socket.links[0].from_socket
    node = output.node
    if node.type == "REROUTE":
        return _shader_source(node.inputs[0], groups, visited)
    if node.type == "GROUP" and node.node_tree:
        group_output = next((value for value in node.node_tree.nodes
                             if value.type == "GROUP_OUTPUT" and value.is_active_output), None)
        if group_output:
            target = next((value for value in group_output.inputs if value.identifier == output.identifier), None)
            return _shader_source(target, groups + (node,), visited)
    if node.type == "GROUP_INPUT" and groups:
        target = next((value for value in groups[-1].inputs if value.identifier == output.identifier), None)
        return _shader_source(target, groups[:-1], visited)
    return node, groups


def _surface_lobes(socket, groups=()):
    visited = set()

    def lobes(value, groups=()):
        source = _shader_source(value, groups)
        if source is None:
            return {"other"}
        node, groups = source
        key = (node.as_pointer(), tuple(group.as_pointer() for group in groups))
        if key in visited or len(visited) >= 64:
            return {"other"}
        visited.add(key)
        try:
            if node.type in {"BSDF_DIFFUSE", "BSDF_GLOSSY"}:
                return {node.type}
            if node.type in {"BSDF_TRANSPARENT", "EMISSION"}:
                return {"neutral"}
            if node.type in {"MIX_SHADER", "ADD_SHADER"}:
                inputs = list(node.inputs)[1:] if node.type == "MIX_SHADER" else list(node.inputs)
                return lobes(inputs[0], groups) | lobes(inputs[1], groups)
            return {"other"}
        finally:
            visited.remove(key)

    return lobes(socket, groups)


def is_diffuse_only(socket):
    """Preserve Diffuse closures without mistaking a mixed Glossy/Principled branch for diffuse-only."""
    closures = _surface_lobes(socket)
    return "BSDF_DIFFUSE" in closures and closures <= {"BSDF_DIFFUSE", "neutral"}


def is_diffuse_glossy(socket, groups=()):
    closures = _surface_lobes(socket, groups)
    return {"BSDF_DIFFUSE", "BSDF_GLOSSY"} <= closures <= {"BSDF_DIFFUSE", "BSDF_GLOSSY", "neutral"}


def _principled_source(socket, groups=(), visited=None):
    if visited is None:
        visited = set()
    source = _shader_source(socket, groups)
    if source is None:
        return None
    node, groups = source
    key = (node.as_pointer(), tuple(group.as_pointer() for group in groups))
    if key in visited or len(visited) >= 64:
        return None
    visited.add(key)
    if node.type in {"BSDF_PRINCIPLED", "BSDF_DIFFUSE", "BSDF_GLOSSY", "BSDF_GLASS", "BSDF_REFRACTION", "EMISSION"}:
        return node, groups
    if node.type in {"MIX_SHADER", "ADD_SHADER"}:
        inputs = node.inputs[1:] if node.type == "MIX_SHADER" else node.inputs
        for shader_input in inputs:
            result = _principled_source(shader_input, groups, visited)
            if result:
                return result
    return None


def transmission_parameters(surface):
    """Preserve the primary dielectric closure even when other channels need a native texture bake."""
    source = _principled_source(surface)
    if source is None:
        return {"transmission": 0.0, "ior": 1.5}
    node, groups = source
    def scalar(names, default):
        socket = next((node.inputs.get(name) for name in names if node.inputs.get(name) is not None), None)
        value = _constant_input(socket, groups)
        if socket is not None and socket.is_linked and value is None:
            print(f"[BlenderLiveLink] Dielectric approximation: {node.name!r} {socket.name!r} uses socket default")
        return float(value if value is not None else socket.default_value if socket else default)
    transmission = (1.0 if node.type in {"BSDF_GLASS", "BSDF_REFRACTION"}
                    else scalar(("Transmission Weight", "Transmission"), 0.0))
    return {"transmission": max(0.0, min(1.0, transmission)), "ior": max(1.0, scalar(("IOR",), 1.5))}


def _opacity_source(socket, groups=(), visited=None):
    """Return the opacity factor of a transparent/opaque shader mix, with its group context and inversion."""
    if visited is None:
        visited = set()
    source = _shader_source(socket, groups)
    if source is None:
        return None
    node, groups = source
    key = (node.as_pointer(), tuple(group.as_pointer() for group in groups))
    if key in visited or len(visited) >= 64:
        return None
    visited.add(key)
    if node.type == "MIX_SHADER":
        first = _shader_source(node.inputs[1], groups)
        second = _shader_source(node.inputs[2], groups)
        if first and first[0].type == "BSDF_TRANSPARENT":
            return node.inputs[0], groups, False
        if second and second[0].type == "BSDF_TRANSPARENT":
            return node.inputs[0], groups, True
    if node.type in {"MIX_SHADER", "ADD_SHADER"}:
        inputs = node.inputs[1:] if node.type == "MIX_SHADER" else node.inputs
        for shader_input in inputs:
            result = _opacity_source(shader_input, groups, visited)
            if result:
                return result
    return None


def invalidate_image_cache():
    _packed_image_cache.clear()
    _scalar_image_cache.clear()


def _image_path(image, data_map=False):
    if not image:
        return ""
    if image.packed_file or image.source == "GENERATED":
        cache_key = (image.as_pointer(), "packed")
        cached_path = _packed_image_cache.get(cache_key)
        if cached_path and os.path.isfile(cached_path):
            return cached_path
        path = os.path.join(_temp_dir(), image.name_full.replace("/", "_") + ".png")
        original_path, original_format = image.filepath_raw, image.file_format
        try:
            image.filepath_raw = path
            image.file_format = "PNG"
            image.save(save_copy=True)
        finally:
            image.filepath_raw = original_path
            image.file_format = original_format
        _packed_image_cache[cache_key] = path
        return path
    source_path = image.filepath_from_user()
    if not source_path or os.path.splitext(source_path)[1].lower() != ".exr":
        return source_path

    cache_key = (image.as_pointer(), "exr_data_png" if data_map else "exr_color_png")
    cached_path = _packed_image_cache.get(cache_key)
    if cached_path and os.path.isfile(cached_path):
        return cached_path

    width, height = (int(value) for value in image.size)
    if width <= 0 or height <= 0:
        try:
            image.reload()
        except (ReferenceError, RuntimeError):
            pass
        width, height = (int(value) for value in image.size)
    if width <= 0 or height <= 0:
        print(f"[BlenderLiveLink] Skip EXR with invalid dimensions: image={image.name_full!r}, size={tuple(image.size)}")
        return ""

    pixels = np.empty(width * height * 4, dtype=np.float32)
    try:
        image.pixels.foreach_get(pixels)
    except (RuntimeError, ValueError) as error:
        print(f"[BlenderLiveLink] Skip unreadable EXR: image={image.name_full!r}, error={error}")
        return ""

    pixels = pixels.reshape((height, width, 4))
    source_colorspace = image.colorspace_settings.name.lower()
    if not data_map and "linear rec.709" in source_colorspace:
        rgb = np.maximum(pixels[:, :, :3], 0.0)
        pixels[:, :, :3] = np.where(
            rgb <= 0.0031308,
            rgb * 12.92,
            1.055 * np.power(rgb, 1.0 / 2.4) - 0.055,
        )

    conversion_kind = "data" if data_map else "color"
    path = os.path.join(_temp_dir(), image.name_full.replace("/", "_") + f"_exr_{conversion_kind}.png")
    converted = bpy.data.images.new(
        f"{image.name_full}_AiluEXR", width=width, height=height, alpha=True, float_buffer=False
    )
    try:
        converted.colorspace_settings.name = "Non-Color"
        converted.pixels.foreach_set(pixels.ravel())
        converted.filepath_raw = path
        converted.file_format = "PNG"
        converted.save()
    except (RuntimeError, ValueError) as error:
        print(f"[BlenderLiveLink] EXR conversion failed: image={image.name_full!r}, error={error}")
        return ""
    finally:
        bpy.data.images.remove(converted)
    _packed_image_cache[cache_key] = path
    print(
        f"[BlenderLiveLink] EXR prepared for transfer: image={image.name_full!r} "
        f"source_bytes={os.path.getsize(source_path) if os.path.isfile(source_path) else 0} "
        f"png_bytes={os.path.getsize(path) if os.path.isfile(path) else 0}"
    )
    return path


def _image_source(socket, normal_map=False, groups=(), visited=None, approximations=None):
    """Resolve a primary image branch, never a Mix factor or a texture's Vector input.

    Color adjustments and layered materials retain the underlying image as an explicit approximation.
    Group sockets are followed through their actual output and input identifiers, including nested instances.
    """
    if socket is None or not socket.is_linked:
        return None
    if visited is None:
        visited = set()
    key = (socket.as_pointer(), tuple(group.as_pointer() for group in groups))
    if key in visited or len(visited) >= 64:
        return None
    visited.add(key)
    output = socket.links[0].from_socket
    node = output.node

    def follow(input_socket, context=groups, is_normal=normal_map):
        return _image_source(input_socket, is_normal, context, visited, approximations)

    def matching_socket(sockets, target):
        return next((value for value in sockets if value.identifier == target.identifier), None)

    if node.type == "TEX_IMAGE":
        if node.image is None:
            return None
        if node.inputs["Vector"].is_linked and approximations is not None:
            approximations.add("texture coordinates: original mesh UV; use BAKE for native Mapping/object coordinates")
        return node.image, 3 if output.name == "Alpha" else None
    if node.type == "REROUTE":
        return follow(node.inputs[0])
    if node.type == "GROUP" and node.node_tree:
        group_output = next((value for value in node.node_tree.nodes
                             if value.type == "GROUP_OUTPUT" and value.is_active_output), None)
        return follow(matching_socket(group_output.inputs, output), groups + (node,)) if group_output else None
    if node.type == "GROUP_INPUT" and groups:
        return follow(matching_socket(groups[-1].inputs, output), groups[:-1])
    if normal_map:
        if node.type == "NORMAL_MAP":
            return follow(node.inputs.get("Color"), is_normal=False)
        if node.type == "BUMP":
            if approximations is not None:
                approximations.add("BUMP: height omitted")
            return follow(node.inputs.get("Normal"))
    if not normal_map and node.type in {"HUE_SAT", "GAMMA", "BRIGHTCONTRAST", "CURVE_RGB", "INVERT", "VALTORGB"}:
        if approximations is not None:
            approximations.add(node.type + ": adjustment omitted")
        return follow(node.inputs.get("Fac") if node.type == "VALTORGB" else node.inputs.get("Color"))
    if node.type in {"MIX", "MIX_RGB"}:
        if node.type == "MIX_RGB":
            factor, first, second = node.inputs[0], node.inputs[1], node.inputs[2]
        else:
            # ShaderNodeMix contains float, vector and color sockets with duplicate A/B names.
            offset = {"FLOAT": 2, "VECTOR": 4, "RGBA": 6}.get(node.data_type)
            if offset is None:
                return None
            factor, first, second = node.inputs[0], node.inputs[offset], node.inputs[offset + 1]
        if approximations is not None:
            approximations.add(node.type + ": primary layer only")
        if node.blend_type == "MIX" and not factor.is_linked:
            if float(factor.default_value) >= 1.0:
                return follow(second)
            if float(factor.default_value) <= 0.0:
                return follow(first)
        return follow(first) or follow(second)
    if normal_map:
        return None
    if node.type in {"SEPRGB", "SEPARATE_COLOR"}:
        source = follow(node.inputs[0])
        if source is not None and (node.type == "SEPRGB" or node.mode == "RGB"):
            channel = list(node.outputs).index(output)
            return source[0], channel
    return None


def _linked_image(socket, normal_map=False, approximations=None, groups=()):
    source = _image_source(socket, normal_map=normal_map, approximations=approximations, groups=groups)
    return _image_path(source[0], data_map=normal_map) if source else ""


def _scalar_source(socket, approximations=None, groups=()):
    return _image_source(socket, approximations=approximations, groups=groups)


def _constant_input(socket, groups=(), visited=None):
    if socket is None:
        return None
    if not socket.is_linked:
        return getattr(socket, "default_value", None)
    visited = set() if visited is None else visited
    key = (socket.as_pointer(), tuple(group.as_pointer() for group in groups))
    if key in visited or len(visited) >= 64:
        return None
    visited.add(key)
    output = socket.links[0].from_socket
    node = output.node
    if node.type in {"VALUE", "RGB"}:
        return output.default_value
    if node.type == "LIGHT_PATH":
        return 1.0 if output.name == "Is Camera Ray" else 0.0
    if node.type == "AMBIENT_OCCLUSION":
        # Scene occlusion belongs to the engine's lighting path, not a per-instance isolated surface bake.
        return _constant_input(node.inputs.get("Color"), groups, visited) if output.name == "Color" else 1.0
    if node.type == "REROUTE":
        return _constant_input(node.inputs[0], groups, visited)
    if node.type == "GROUP_INPUT" and groups:
        target = next((value for value in groups[-1].inputs if value.identifier == output.identifier), None)
        return _constant_input(target, groups[:-1], visited)
    if node.type == "GROUP" and node.node_tree:
        group_output = next((value for value in node.node_tree.nodes
                             if value.type == "GROUP_OUTPUT" and value.is_active_output), None)
        target = (next((value for value in group_output.inputs if value.identifier == output.identifier), None)
                  if group_output else None)
        return _constant_input(target, groups + (node,), visited) if target else None
    return None


def _can_export_direct(material):
    output = next((node for node in material.node_tree.nodes
                   if node.type == "OUTPUT_MATERIAL" and node.is_active_output), None)
    if not output or not output.inputs["Surface"].is_linked:
        return True
    surface = output.inputs["Surface"]
    active = _shader_source(surface)
    if active and active[0].type == "BSDF_TRANSPARENT":
        return True
    source = _principled_source(surface)
    if not source:
        return False
    node, groups = source
    # Preserve native evaluation of layered opaque shaders; a transparent/Principled image mask is direct PBR.
    if active[0].type in {"MIX_SHADER", "ADD_SHADER"} and _opacity_source(surface) is None:
        return False
    channels = ("Base Color", "Color", "Roughness", "Metallic", "Emission Color", "Emission Strength",
                "Strength", "Alpha")
    for name in channels:
        socket = node.inputs.get(name)
        linked = socket is not None and socket.is_linked
        constant = _constant_input(socket, groups)
        if name in {"Emission Strength", "Strength"} and linked and constant is None:
            return False
        if linked and constant is None and _image_source(socket, groups=groups) is None:
            return False
    normal = node.inputs.get("Normal")
    if normal and normal.is_linked and _image_source(normal, normal_map=True, groups=groups) is None:
        return False
    opacity = _opacity_source(surface)
    if opacity:
        socket, groups, _ = opacity
        if (socket.is_linked and _constant_input(socket, groups) is None
                and _image_source(socket, groups=groups) is None):
            return False
    return True


def _pack_base_alpha(material, base_source, alpha_source, base_color, invert_alpha=False):
    cache_key = (material.as_pointer(), "base_alpha", base_source[0].as_pointer() if base_source else 0,
                 alpha_source[0].as_pointer(), alpha_source[1], tuple(base_color), invert_alpha)
    cached_path = _packed_image_cache.get(cache_key)
    if cached_path and os.path.isfile(cached_path):
        return cached_path

    def read_pixels(image):
        width, height = (int(value) for value in image.size)
        if width <= 0 or height <= 0:
            image.reload()
            width, height = (int(value) for value in image.size)
        if width <= 0 or height <= 0:
            raise ValueError(f"Invalid alpha/base image dimensions: {image.name_full!r}")
        pixels = np.empty(width * height * 4, dtype=np.float32)
        image.pixels.foreach_get(pixels)
        return pixels.reshape(height, width, 4)

    alpha_pixels = read_pixels(alpha_source[0])
    if base_source:
        pixels = read_pixels(base_source[0])
        # Float color images contain linear values; byte color images retain their encoded samples.
        if base_source[0].is_float and "linear rec.709" in base_source[0].colorspace_settings.name.lower():
            rgb = np.maximum(pixels[:, :, :3], 0.0)
            pixels[:, :, :3] = np.where(rgb <= 0.0031308, rgb * 12.92,
                                      1.055 * np.power(rgb, 1.0 / 2.4) - 0.055)
    else:
        pixels = np.empty_like(alpha_pixels)
        rgb = np.maximum(np.array(base_color[:3]), 0.0)
        rgb = np.where(rgb <= 0.0031308, rgb * 12.92, 1.055 * np.power(rgb, 1.0 / 2.4) - 0.055)
        pixels[:, :, :3] = rgb
    height, width = pixels.shape[:2]
    source_height, source_width = alpha_pixels.shape[:2]
    channel = alpha_source[1]
    alpha = (alpha_pixels[:, :, channel] if channel is not None
             else alpha_pixels[:, :, :3] @ np.array((0.2126, 0.7152, 0.0722), dtype=np.float32))
    if invert_alpha:
        alpha = 1.0 - alpha
    y_indices = np.minimum(source_height - 1, np.arange(height) * source_height // height)
    x_indices = np.minimum(source_width - 1, np.arange(width) * source_width // width)
    pixels[:, :, 3] = np.clip(alpha[y_indices[:, None], x_indices[None, :]], 0.0, 1.0)
    path = os.path.join(_temp_dir(), material.name_full.replace("/", "_") + "_base_alpha.png")
    packed = bpy.data.images.new(f"{material.name_full}_AiluAlpha", width=width, height=height, alpha=True)
    try:
        packed.colorspace_settings.name = "Non-Color"
        packed.alpha_mode = "STRAIGHT"
        packed.pixels.foreach_set(pixels.ravel())
        packed.filepath_raw = path
        packed.file_format = "PNG"
        packed.save()
    finally:
        bpy.data.images.remove(packed)
    _packed_image_cache[cache_key] = path
    return path


def _export_scalar_texture(principled, material, input_name, approximations=None, groups=()):
    source = _scalar_source(principled.inputs.get(input_name), approximations, groups)
    if source is None:
        return ""
    image, source_channel = source
    width, height = (int(value) for value in image.size)
    if width <= 0 or height <= 0:
        try:
            image.reload()
        except (ReferenceError, RuntimeError):
            pass
        width, height = (int(value) for value in image.size)
    if width <= 0 or height <= 0:
        print(f"[BlenderLiveLink] Skip scalar image with invalid dimensions: image={image.name_full!r}")
        return ""
    cache_key = (material.as_pointer(), input_name, image.as_pointer(), width, height, source_channel)
    cached_path = _scalar_image_cache.get(cache_key)
    if cached_path and os.path.isfile(cached_path):
        return cached_path
    pixels = np.empty(width * height * 4, dtype=np.float32)
    try:
        image.pixels.foreach_get(pixels)
    except (RuntimeError, ValueError) as error:
        print(f"[BlenderLiveLink] Skip unreadable scalar image: image={image.name_full!r}, error={error}")
        return ""
    pixels = pixels.reshape((height, width, 4))
    scalar = (pixels[:, :, source_channel] if source_channel is not None
              else pixels[:, :, :3] @ np.array((0.2126, 0.7152, 0.0722), dtype=np.float32))
    result = np.ones((height, width, 4), dtype=np.float32)
    result[:, :, :3] = scalar[:, :, None]
    packed = bpy.data.images.new(f"{material.name_full}_Ailu{input_name}", width=width, height=height,
                                 alpha=True, float_buffer=True)
    try:
        packed.colorspace_settings.name = "Non-Color"
        packed.pixels.foreach_set(result.ravel())
        filename = identity.get_guid(material) + "_scalar_" + input_name.lower() + ".png"
        packed.filepath_raw = os.path.join(_temp_dir(), filename)
        packed.file_format = "PNG"
        packed.save()
        path = packed.filepath_raw
    finally:
        bpy.data.images.remove(packed)
    _scalar_image_cache[cache_key] = path
    return path


@contextmanager
def temporary_image_buffers():
    # Image decoding/Cycles may populate source buffers that were not resident before this operation.
    # Release only newly loaded, reloadable clean files; keep user-painted/generated/preloaded images intact.
    started = time.perf_counter()
    unloaded = [image for image in bpy.data.images
                if image.source == "FILE" and not image.has_data and not image.is_dirty]
    scanned = time.perf_counter()
    try:
        yield
    finally:
        cleanup_start = time.perf_counter()
        released = 0
        for image in unloaded:
            if (image.has_data and not image.is_dirty
                    and (image.packed_file or os.path.isfile(bpy.path.abspath(image.filepath, library=image.library)))):
                image.buffers_free()
                released += 1
        if released:
            print(f"[BlenderLiveLink] Source image buffers: released={released} "
                  f"scan={(scanned - started) * 1000:.2f}ms "
                  f"free={(time.perf_counter() - cleanup_start) * 1000:.2f}ms")


def export_material(material, link, obj=None):
    with temporary_image_buffers():
        _export_material(material, link, obj)


def _export_material(material, link, obj=None):
    if not material:
        return
    from . import material_bake
    output = next((node for node in material.node_tree.nodes
                   if node.type == "OUTPUT_MATERIAL" and node.is_active_output), None) if material.use_nodes else None
    if output and not output.inputs["Surface"].is_linked:
        # A volume-only helper must not acquire an opaque checker surface.
        link.send_json(protocol.MATERIAL_UPSERT, {
            "material_guid": material_bake.material_guid(material, obj), "name": material.name_full,
            "base_color": [0.0] * 4, "roughness": 0.5, "metallic": 0.0,
            "emission": [0.0] * 3, "emission_strength": 0.0,
            "base_color_path": "", "normal_path": "", "emission_path": "",
            "roughness_path": "", "metallic_path": "",
            "alpha_test": True, "alpha_blend": False, "double_sided": not material.use_backface_culling,
        })
        if output.inputs["Volume"].is_linked:
            print(f"[BlenderLiveLink] Volume not transferred: material={material.name_full!r}; surface stays invisible")
        return
    if obj is not None and material_bake.requires_bake_for_object(material, obj):
        material_bake.export(material, obj, link)
        return
    if not material.use_nodes:
        link.send_json(protocol.MATERIAL_UPSERT, {
            "material_guid": identity.get_guid(material), "name": material.name_full,
            "base_color": list(material.diffuse_color), "roughness": material.roughness,
            "metallic": material.metallic, "emission": [0.0] * 3, "emission_strength": 0.0,
            "base_color_path": "", "normal_path": "", "emission_path": "",
            "roughness_path": "", "metallic_path": "",
            "alpha_test": False, "alpha_blend": material.diffuse_color[3] < 1.0,
            "double_sided": not material.use_backface_culling,
        })
        return
    export_start = time.perf_counter()
    output = next((node for node in material.node_tree.nodes
                   if node.type == "OUTPUT_MATERIAL" and node.is_active_output), None)
    surface = output.inputs.get("Surface") if output else None
    active_shader = _shader_source(surface)
    if active_shader and active_shader[0].type == "BSDF_TRANSPARENT":
        # Invisible decal/helper planes must not become opaque fallback walls.
        link.send_json(protocol.MATERIAL_UPSERT, {
            "material_guid": identity.get_guid(material), "name": material.name_full,
            "base_color": [1.0, 1.0, 1.0, 0.0], "roughness": 0.5, "metallic": 0.0,
            "emission": [0.0, 0.0, 0.0], "emission_strength": 0.0,
            "base_color_path": "", "normal_path": "", "emission_path": "",
            "roughness_path": "", "metallic_path": "",
            "alpha_test": True, "alpha_cutoff": 0.5, "double_sided": False,
        })
        return
    source = _principled_source(surface)
    principled, groups = source if source else (None, ())
    if principled is None:
        principled = next((node for node in material.node_tree.nodes if node.type == "BSDF_PRINCIPLED"), None)
        if principled is not None:
            print(f"[BlenderLiveLink] Material approximation: material={material.name_full!r}, "
                  "details=active shader is not a direct Principled BSDF; using fallback")
    if not principled:
        print(f"[BlenderLiveLink] Unsupported surface shader: material={material.name_full!r}")
        return
    approximations = set()
    def get_value(key, default):
        socket = principled.inputs.get(key)
        constant = _constant_input(socket, groups)
        return constant if constant is not None else socket.default_value if socket else default
    is_emission = principled.type == "EMISSION"
    base = (0.0, 0.0, 0.0, 1.0) if is_emission else get_value("Base Color", get_value("Color", (0.8, 0.8, 0.8, 1.0)))
    emission = (get_value("Color", (0.0, 0.0, 0.0, 1.0)) if is_emission
                else get_value("Emission Color", get_value("Emission", (0.0, 0.0, 0.0, 1.0))))
    base_socket = principled.inputs.get("Base Color", principled.inputs.get("Color"))
    emission_socket = (principled.inputs.get("Color") if is_emission
                       else principled.inputs.get("Emission Color", principled.inputs.get("Emission")))
    value = {
        "material_guid": identity.get_guid(material),
        "name": material.name_full,
        "base_color": list(base),
        "roughness": float(get_value("Roughness", 0.5)),
        "metallic": float(get_value("Metallic", 1.0 if principled.type == "BSDF_GLOSSY" else 0.0)),
        "emission": list(emission[:3]),
        "emission_strength": float(get_value("Strength", 1.0) if is_emission else get_value("Emission Strength", 0.0)),
        "emission_texture_factor": True,
        "base_color_path": ("" if is_emission else
                            _linked_image(base_socket, approximations=approximations, groups=groups)),
        "normal_path": _linked_image(principled.inputs.get("Normal"), normal_map=True, approximations=approximations,
                                     groups=groups),
        "emission_path": _linked_image(emission_socket, approximations=approximations, groups=groups),
        "roughness_path": _export_scalar_texture(principled, material, "Roughness", approximations, groups),
        "metallic_path": _export_scalar_texture(principled, material, "Metallic", approximations, groups),
    }
    value.update(transmission_parameters(surface))
    for channel in ("roughness", "metallic"):
        if value[channel + "_path"]:
            value[channel] = 1.0
    if is_diffuse_only(surface):
        # Diffuse roughness describes Oren-Nayar scattering, not the engine's GGX reflection width.
        value.update(roughness=1.0, metallic=0.0, roughness_path="", metallic_path="")
    # A linked texture replaces the Blender socket default; Ailu multiplies its texture by this factor.
    if value["base_color_path"]:
        value["base_color"] = [1.0, 1.0, 1.0, 1.0]
    if value["emission_path"]:
        value["emission"] = [1.0] * 3
    alpha_socket = principled.inputs.get("Alpha")
    alpha_groups, invert_alpha = groups, False
    opacity = _opacity_source(surface)
    if opacity is not None:
        alpha_socket, alpha_groups, invert_alpha = opacity
    alpha_source = _image_source(alpha_socket, groups=alpha_groups, approximations=approximations)
    alpha_constant = _constant_input(alpha_socket, alpha_groups)
    alpha_value = float(alpha_constant if alpha_constant is not None
                        else alpha_socket.default_value) if alpha_socket else 1.0
    if invert_alpha:
        alpha_value = 1.0 - alpha_value
    if alpha_source is not None:
        base_source = _image_source(principled.inputs.get("Base Color"), groups=groups)
        value["base_color_path"] = _pack_base_alpha(material, base_source, alpha_source, base, invert_alpha)
        value["base_color"] = [1.0, 1.0, 1.0, 1.0]
    else:
        value["base_color"][3] = alpha_value
    value["alpha_blend"] = alpha_source is None and 0.0 < alpha_value < 1.0
    value["alpha_test"] = (alpha_source is not None or alpha_value < 1.0) and not value["alpha_blend"]
    if alpha_source is not None:
        image = alpha_source[0]
        pixels = np.empty(image.size[0] * image.size[1] * 4, dtype=np.float32)
        image.pixels.foreach_get(pixels)
        pixels = pixels.reshape(-1, 4)
        alpha = (pixels[:, alpha_source[1]] if alpha_source[1] is not None
                 else pixels[:, :3] @ np.array((0.2126, 0.7152, 0.0722)))
        value["alpha_blend"], value["alpha_test"] = material_bake.opacity_mode(
            material, 1.0 - alpha if invert_alpha else alpha)
    value["alpha_cutoff"] = float(np.clip(material.get("ailu_alpha_cutoff", 0.5), 0.0, 1.0))
    value["double_sided"] = not material.use_backface_culling
    if opacity is not None or (active_shader and active_shader[0].type in {"MIX_SHADER", "ADD_SHADER"}):
        approximations.add("shader layers: primary surface closure and alpha coverage; extra reflection lobes omitted")
    if approximations:
        print(f"[BlenderLiveLink] Material approximation: material={material.name_full!r}, "
              f"details={', '.join(sorted(approximations))}")
    for key in ("Base Color", "Roughness", "Metallic", "Normal"):
        socket = principled.inputs.get(key)
        source = _image_source(socket, normal_map=key == "Normal", groups=groups)
        if socket is not None and socket.is_linked and source is None:
            print(f"[BlenderLiveLink] Unsupported texture branch: material={material.name_full!r}, input={key!r}")
    link.send_json(protocol.MATERIAL_UPSERT, value)
    export_ms = (time.perf_counter() - export_start) * 1000.0
    if export_ms >= 50.0:
        texture_paths = sum(bool(value[key]) for key in (
            "base_color_path", "normal_path", "emission_path", "roughness_path", "metallic_path"
        ))
        print(
            f"[BlenderLiveLink] Slow material export: material={material.name_full!r} "
            f"texture_paths={texture_paths} export_ms={export_ms:.2f}"
        )


def send_object_materials(obj, link, sent_materials=None):
    from . import material_bake
    if sent_materials is None:
        sent_materials = set()
    with temporary_image_buffers():
        material_bake.prepare_object_materials(obj, sent_materials)
    for slot in obj.material_slots:
        material = slot.material
        if not material:
            continue
        material_guid = material_bake.material_guid(material, obj)
        if material_guid in sent_materials:
            continue
        export_material(material, link, obj)
        sent_materials.add(material_guid)
