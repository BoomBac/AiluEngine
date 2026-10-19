"""Source display settings; shading and material data remain scene-linear."""
from . import protocol


def scene_settings_signature(scene):
    view = scene.view_settings
    return (view.view_transform, scene.display_settings.display_device, view.look,
            float(view.exposure), float(view.gamma))


def export_scene_settings(scene, link):
    view, display, look, exposure, gamma = scene_settings_signature(scene)
    link.send_json(protocol.SCENE_SETTINGS_UPSERT, {
        "view_transform": view, "display_device": display, "look": look,
        "exposure_ev": exposure, "gamma": gamma,
    })
