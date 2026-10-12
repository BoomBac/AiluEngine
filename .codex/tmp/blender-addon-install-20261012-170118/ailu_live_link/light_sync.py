from mathutils import Vector

from . import coordinate, identity, protocol


def export_light(obj, link):
    light = obj.data
    if light.type not in {"SUN", "POINT", "SPOT", "AREA"}:
        return
    direction = coordinate.blender_to_ailu_direction(
        obj.matrix_world.to_3x3() @ Vector((0.0, 0.0, -1.0)))
    value = {
        "guid": identity.get_guid(obj),
        "type": light.type,
        "color": list(light.color),
        "energy": float(light.energy),
        "range": float(getattr(light, "cutoff_distance", 1000.0)),
        "radius": float(getattr(light, "shadow_soft_size", 0.05)),
        "inner_angle_degrees": float(getattr(light, "spot_size", 0.785398) * 57.2957795 *
                                     (1.0 - getattr(light, "spot_blend", 0.0))),
        "outer_angle_degrees": float(getattr(light, "spot_size", 0.785398) * 57.2957795),
        "width": float(getattr(light, "size", 1.0)),
        "height": float(getattr(light, "size_y", getattr(light, "size", 1.0))),
        "direction": list(direction),
    }
    link.send_json(protocol.LIGHT_UPSERT, value)
