import math

from . import coordinate, identity, protocol


def export_camera(obj, scene, link):
    camera = obj.data
    if camera.type not in {"PERSP", "ORTHO"}:
        return
    render = scene.render
    aspect = (render.resolution_x * render.pixel_aspect_x) / max(1.0, render.resolution_y * render.pixel_aspect_y)
    value = {
        "guid": identity.get_guid(obj),
        "type": camera.type,
        "fov_degrees": math.degrees(camera.angle_x),
        "aspect": aspect,
        "ortho_size": float(camera.ortho_scale),
        "near_clip": float(camera.clip_start),
        "far_clip": float(camera.clip_end),
    }
    link.send_json(protocol.CAMERA_UPSERT, value)
