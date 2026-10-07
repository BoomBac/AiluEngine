import os
import array
import bpy

from . import identity, protocol
from .mesh_sync import _temp_dir


def _image_path(image):
    if not image:
        return ""
    if image.packed_file or image.source == "GENERATED":
        path = os.path.join(_temp_dir(), image.name_full.replace("/", "_") + ".png")
        image.filepath_raw = path
        image.file_format = "PNG"
        image.save()
        return path
    return image.filepath_from_user()


def _linked_image(socket, normal_map=False):
    if not socket or not socket.is_linked:
        return ""
    node = socket.links[0].from_node
    if normal_map and node.type == "NORMAL_MAP":
        color = node.inputs.get("Color")
        if not color or not color.is_linked:
            return ""
        node = color.links[0].from_node
    return _image_path(getattr(node, "image", None)) if node.type == "TEX_IMAGE" else ""


def _scalar_source(socket):
    if not socket or not socket.is_linked:
        return None
    node = socket.links[0].from_node
    return getattr(node, "image", None) if node.type == "TEX_IMAGE" else None


def _pack_roughness_metallic(principled, material):
    rough_socket = principled.inputs.get("Roughness")
    metal_socket = principled.inputs.get("Metallic")
    rough_image = _scalar_source(rough_socket)
    metal_image = _scalar_source(metal_socket)
    if not rough_image and not metal_image:
        return ""
    width = max(rough_image.size[0] if rough_image else 1, metal_image.size[0] if metal_image else 1)
    height = max(rough_image.size[1] if rough_image else 1, metal_image.size[1] if metal_image else 1)
    rough_pixels = list(rough_image.pixels[:]) if rough_image else None
    metal_pixels = list(metal_image.pixels[:]) if metal_image else None
    rough_width, rough_height = rough_image.size if rough_image else (1, 1)
    metal_width, metal_height = metal_image.size if metal_image else (1, 1)
    result = array.array("f", [0.0]) * (width * height * 4)
    for y in range(height):
        for x in range(width):
            output = (y * width + x) * 4
            if rough_pixels:
                source = ((min(rough_height - 1, y * rough_height // height) * rough_width
                    + min(rough_width - 1, x * rough_width // width)) * 4)
                result[output] = rough_pixels[source]
            else:
                result[output] = float(rough_socket.default_value)
            if metal_pixels:
                source = ((min(metal_height - 1, y * metal_height // height) * metal_width
                    + min(metal_width - 1, x * metal_width // width)) * 4)
                result[output + 1] = metal_pixels[source]
            else:
                result[output + 1] = float(metal_socket.default_value)
            result[output + 2] = 0.0
            result[output + 3] = 1.0
    packed = bpy.data.images.new(f"{material.name_full}_AiluRM", width=width, height=height, alpha=True,
        float_buffer=True)
    packed.pixels.foreach_set(result)
    packed.filepath_raw = os.path.join(_temp_dir(), material.name_full.replace("/", "_") + "_rm.png")
    packed.file_format = "PNG"
    packed.save()
    return packed.filepath_raw


def export_material(material, link):
    if not material or not material.use_nodes:
        return
    principled = next((node for node in material.node_tree.nodes if node.type == "BSDF_PRINCIPLED"), None)
    if not principled:
        return
    get_value = lambda key, default: principled.inputs[key].default_value if key in principled.inputs else default
    base = get_value("Base Color", (0.8, 0.8, 0.8, 1.0))
    emission = get_value("Emission Color", get_value("Emission", (0.0, 0.0, 0.0, 1.0)))
    value = {
        "material_guid": identity.get_guid(material),
        "name": material.name_full,
        "base_color": list(base),
        "roughness": float(get_value("Roughness", 0.5)),
        "metallic": float(get_value("Metallic", 0.0)),
        "emission": list(emission[:3]),
        "emission_strength": float(get_value("Emission Strength", 0.0)),
        "base_color_path": _linked_image(principled.inputs.get("Base Color")),
        "normal_path": _linked_image(principled.inputs.get("Normal"), normal_map=True),
        "emission_path": _linked_image(principled.inputs.get("Emission Color", principled.inputs.get("Emission"))),
        "roughness_metallic_path": _pack_roughness_metallic(principled, material),
    }
    link.send_json(protocol.MATERIAL_UPSERT, value)


def send_object_materials(obj, link):
    for slot in obj.material_slots:
        export_material(slot.material, link)
