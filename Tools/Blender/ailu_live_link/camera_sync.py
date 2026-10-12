import math

from . import identity, protocol


def export_camera(obj, scene, link):
    camera = obj.data
    if camera.type not in {"PERSP", "ORTHO"}:
        return
    render = scene.render
    aspect = (render.resolution_x * render.pixel_aspect_x) / max(1.0, render.resolution_y * render.pixel_aspect_y)
    frame = camera.view_frame(scene=scene)
    if camera.type == "PERSP":
        xs = [corner.x / -corner.z for corner in frame]
        ys = [corner.y / -corner.z for corner in frame]
    else:
        xs = [corner.x for corner in frame]
        ys = [corner.y for corner in frame]
    left, right = min(xs), max(xs)
    bottom, top = min(ys), max(ys)
    width, height = right - left, top - bottom
    if width <= 0.0 or height <= 0.0:
        return
    value = {
        "guid": identity.get_guid(obj),
        "type": camera.type,
        "fov_degrees": math.degrees(camera.angle_x),
        "fov_y_degrees": math.degrees(2.0 * math.atan(height * 0.5)) if camera.type == "PERSP" else 60.0,
        "projection_offset": [-(left + right) / width, -(bottom + top) / height],
        "aspect": aspect,
        "ortho_size": width if camera.type == "ORTHO" else float(camera.ortho_scale),
        "near_clip": float(camera.clip_start),
        "far_clip": float(camera.clip_end),
    }
    link.send_json(protocol.CAMERA_UPSERT, value)
