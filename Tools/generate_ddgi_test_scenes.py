#!/usr/bin/env python3
"""Generate the DDGITest scene set for an Ailu project.

Each scene isolates one typical DDGI problem (light leaking, firefly driven
temporal noise, probes embedded in geometry, volume boundary, banding on smooth
gradients, one sided occluders, probe count scaling).  Scenes (.almap) and the
materials they need (.alasset) are plain JSON, so they are generated here instead
of being hand edited, and every new asset is registered in the project asset
database (Library/AssetDatabase.json) so the editor can resolve its guid.

Usage:
    python Tools/generate_ddgi_test_scenes.py [--project <project root>]

The default project root is the AiluTestProj next to this repository's parent
directory.  Re-running the script is idempotent: guids are derived from the scene
and entity names, so files are rewritten with identical ids.
"""

from __future__ import annotations

import argparse
import json
import os
import uuid
from pathlib import Path

# --------------------------------------------------------------------------- #
# Engine built-in assets (Engine/Res/assetdb.alasset)
# --------------------------------------------------------------------------- #
MESH_CUBE = "d137d2a7-0d2b-40bc-9c76-6d2708923869"
MESH_SPHERE = "2ff30e48-6661-479b-a90c-863e606734f6"
MESH_PLANE = "c3315e03-1052-4825-98f7-a676beae0615"
SHADER_STANDARD_PBR = "60fb44b6-864f-4c6a-b49b-f4b26829b57d"
SKY_TEXTURE = "cffbfce1-eb20-4319-aa0e-20a68eb19f22"  # engine://Textures/small_cave_1k

# --------------------------------------------------------------------------- #
# Materials that ship with the test project
# --------------------------------------------------------------------------- #
MAT_CORNELL_WHITE = "2ad7428c-3ce8-463d-9cdf-e7e68d0e93d8"
MAT_CORNELL_RED = "8c0964f1-1090-4b40-a48d-e598bf5fb455"
MAT_CORNELL_GREEN = "a98fba29-70cb-4e55-b045-9a984c132739"
MAT_DEFAULT = "ecc6eb41-3b18-40d5-b82a-9286a2819efb"

ASSET_TYPE_MATERIAL = "Ailu::Render::Material"
ASSET_TYPE_SCENE = "Ailu::SceneManagement::Scene"

# The asset database stores project paths as project://<path relative to Assets>.
ASSET_DIR_NAME = "DDGITest"

_DEFAULT_PROJECT = Path(__file__).resolve().parent.parent.parent / "AiluTestProj"
_GUID_NAMESPACE = uuid.UUID("6f1d5a2e-6a2f-4c2b-9f1e-8c3f1b7d5a10")


def guid_for(name: str) -> str:
    """Deterministic guid for a generated asset or entity."""
    return str(uuid.uuid5(_GUID_NAMESPACE, name))


# --------------------------------------------------------------------------- #
# Materials: values are authored in sRGB exactly like the material editor shows
# them; the loader converts colour properties with Color::FromSrgb, so an
# emission of "2.0" is about 5.0 in linear light.
# --------------------------------------------------------------------------- #
def material_document(name: str, albedo, emission, roughness: float = 1.0,
                      metallic: float = 0.0) -> dict:
    return {
        "_header": {
            "_format_version": 1,
            "_guid": guid_for(f"material/{name}"),
            "_asset_type": ASSET_TYPE_MATERIAL,
            "_asset_name": name,
            "_dependencies": [{"_guid": SHADER_STANDARD_PBR, "_type": "kSoft"}],
        },
        "_shader_guid": SHADER_STANDARD_PBR,
        "_keywords": [],
        "_uint_properties": [
            {"_name": "_MaterialID", "_value": 4294967295},
            {"_name": "_cull", "_value": 2},
            {"_name": "_surface", "_value": 4294967295},
        ],
        "_float_properties": [
            {"_name": "_AlphaCulloff", "_value": 0.0},
            {"_name": "_Anisotropy", "_value": 0.0},
            {"_name": "_IOR", "_value": 0.0},
            {"_name": "_MaterialID", "_value": 0.0},
            {"_name": "_MetallicValue", "_value": metallic},
            {"_name": "_RoughnessValue", "_value": roughness},
            {"_name": "_SamplerMask", "_value": 0.0},
            {"_name": "_Transmission", "_value": 0.0},
        ],
        "_vector_properties": [
            {"_name": "_AlbedoValue", "_value": [albedo[0], albedo[1], albedo[2], 1.0]},
            {"_name": "_EmissionValue", "_value": [emission[0], emission[1], emission[2], 0.0]},
            {"_name": "_SpecularValue", "_value": [0.0, 0.0, 0.0, 0.0]},
        ],
        "_int_vector_properties": [],
        "_texture_properties": [
            {"_name": "_AlbedoTex", "_texture_guid": ""},
            {"_name": "_EmissionTex", "_texture_guid": ""},
            {"_name": "_NormalTex", "_texture_guid": ""},
            {"_name": "_RoughnessTex", "_texture_guid": ""},
                {"_name": "_MetallicTex", "_texture_guid": ""},
            {"_name": "_SpecularTex", "_texture_guid": ""},
        ],
        "_color_properties": ["_AlbedoValue", "_EmissionValue", "_SpecularValue"],
    }


MATERIALS = {
    # name: (albedo, emission, note used in the README)
    "MAT_T_Gray": ((0.5, 0.5, 0.5), (0.0, 0.0, 0.0)),
    "MAT_T_Dark": ((0.05, 0.05, 0.05), (0.0, 0.0, 0.0)),
    # Small, very bright emitter: the worst case for a probe ray estimator that
    # has no emitter sampling.  ~0.18 m2 of surface at ~5.0 linear radiance.
    "MAT_T_Emit_Small": ((0.02, 0.02, 0.02), (6.0, 6.0, 6.0)),
    # Large, dim emitter with roughly the same total flux: ~2.25 m2 at ~0.4.
    "MAT_T_Emit_Large": ((0.02, 0.02, 0.02), (1.2, 1.2, 1.2)),
    # Bright emitter for the leak / occlusion scenes.
    "MAT_T_Emit_Strong": ((0.02, 0.02, 0.02), (3.0, 3.0, 3.0)),
    # Warm ceiling light for the Cornell reference scene.
    "MAT_T_Emit_Warm": ((0.02, 0.02, 0.02), (3.0, 1.8, 0.9)),
    # Strip light for the gradient scene.
    "MAT_T_Emit_Strip": ((0.02, 0.02, 0.02), (3.0, 3.0, 3.0)),
}

# Material references used by the scenes below.  Generated materials are named
# here and get their guid from the name; the project / engine materials above
# already hold a real guid.
MAT_T_Gray = "MAT_T_Gray"
MAT_T_Dark = "MAT_T_Dark"
MAT_T_Emit_Small = "MAT_T_Emit_Small"
MAT_T_Emit_Large = "MAT_T_Emit_Large"
MAT_T_Emit_Strong = "MAT_T_Emit_Strong"
MAT_T_Emit_Warm = "MAT_T_Emit_Warm"
MAT_T_Emit_Strip = "MAT_T_Emit_Strip"
MAT_Cornell_white = MAT_CORNELL_WHITE
MAT_Cornell_red = MAT_CORNELL_RED
MAT_Cornell_green = MAT_CORNELL_GREEN
MAT_Default = MAT_DEFAULT


def material_guid(material) -> str:
    """Resolve a material name to its guid; a guid is passed through unchanged."""
    if material in MATERIALS:
        return guid_for(f"material/{material}")
    return material


# --------------------------------------------------------------------------- #
# Scene building blocks
# --------------------------------------------------------------------------- #
_ZERO_ATTACH = "0,0,0,0;0,0,0,0;0,0,0,0;0,0,0,0"


def entity(name, position=(0.0, 0.0, 0.0), rotation=(0.0, 0.0, 0.0, 1.0), scale=(1.0, 1.0, 1.0),
           tag="Untagged", mesh=None, materials=None, light=None, camera=None, ddgi=None,
           environment=None, postprocess=None) -> dict:
    doc = {
        "_entity_guid": guid_for(f"entity/{name}"),
        "_tag_component": {"_name": name, "_tag": tag, "_layer_mask": 1},
        "_disabled_components": [],
        "_transform_component": {
            "_position": [float(v) for v in position],
            "_rotation": [float(v) for v in rotation],
            "_scale": [float(v) for v in scale],
        },
    }
    if mesh is not None:
        doc["_static_mesh_component"] = {
            "_mesh_guid": mesh,
            "_material_guids": [material_guid(item) for item in (materials or [])],
        }
    if light is not None:
        doc["_light_component"] = light
    if camera is not None:
        doc["_camera_component"] = camera
    if ddgi is not None:
        doc["_ddgi_volume_component"] = ddgi
    if environment is not None:
        doc["_environment_component"] = environment
    if postprocess is not None:
        doc["_postprocess_volume_component"] = postprocess
    doc["_hierarchy_component"] = {
        "_parent_guid": "",
        "_sibling_index": 0,
        "_inv_matrix_attach": _ZERO_ATTACH,
        "_enabled": True,
    }
    return doc


def box(name, position, scale, material):
    return entity(name, position, scale=scale, mesh=MESH_CUBE, materials=[material])


def ball(name, position, diameter, material):
    d = float(diameter)
    return entity(name, position, scale=(d, d, d), mesh=MESH_SPHERE, materials=[material])


def quad(name, position, scale, material, rotation=(0.0, 0.0, 0.0, 1.0)):
    """The plane primitive lies in XZ; rotate it to stand up."""
    return entity(name, position, rotation=rotation, scale=scale, mesh=MESH_PLANE, materials=[material])


def directional_light(name, position, rotation, color=(1.0, 1.0, 1.0, 1.0), angular_radius=0.1,
                      cast_shadow=True):
    return entity(
        name, position, rotation=rotation,
        light={
            "_type": "kDirectional",
            "_light": {"_light_color": list(color), "_light_param": [0.0, 0.0, 0.0, angular_radius],
                       "_is_two_side": False},
            "_shadow": {"_is_cast_shadow": cast_shadow, "_constant_bias": 0.1993, "_slope_bias": 0.7476},
        },
    )


def main_camera(name, position, rotation, fov=60.0, aspect=1.7777778, near=0.1, far=200.0):
    return entity(name, position, rotation=rotation, tag="MainCamera",
                  camera={"_type": "kPerspective", "_aspect": aspect, "_far_clip": far,
                          "_near_clip": near, "_fov_h": fov, "_size": 10.0})


def ddgi_volume(name, position, probe_count, probe_spacing, max_ray_distance=30.0,
                draw_probes=True, debug_mode="kIrradiance", draw_probe_rays=False,
                freeze_ray_rotation=False, debug_probe_radius=0.1, debug_ray_length=4.0,
                use_probe_relocation=False, relocation_backface_threshold=0.25,
                random_ray_backface_threshold=0.1, relocation_min_frontface_distance_ratio=0.1,
                draw_relocation_debug=False):
    return entity(
        name, position,
        ddgi={
            "_probe_count": [int(v) for v in probe_count],
            "_probe_spacing": [float(v) for v in probe_spacing],
            "_draw_probes": draw_probes,
            "_max_ray_distance": float(max_ray_distance),
            "_draw_probe_rays": draw_probe_rays,
            "_use_probe_relocation": bool(use_probe_relocation),
            "_relocation_backface_threshold": float(relocation_backface_threshold),
            "_random_ray_backface_threshold": float(random_ray_backface_threshold),
            "_relocation_min_frontface_distance_ratio": float(relocation_min_frontface_distance_ratio),
            "_draw_relocation_debug": bool(draw_relocation_debug),
            "_freeze_ray_rotation": freeze_ray_rotation,
            "_debug_probe_radius": float(debug_probe_radius),
            "_debug_ray_length": float(debug_ray_length),
            "_debug_mode": debug_mode,
        },
    )


def environment(name="Environment", intensity=0.1, render_sky=False, enable_ibl=True,
                sky_color=(0.46, 0.60, 0.79, 1.0), ground_color=(0.31, 0.27, 0.22, 1.0)):
    return entity(
        name,
        environment={
            "_source": "kProceduralSky",
            "_texture_guid": SKY_TEXTURE,
            "_sky_color": list(sky_color),
            "_ground_color": list(ground_color),
            "_intensity": float(intensity),
            "_rotation": 0.0,
            "_render_sky": render_sky,
            "_enable_ibl": enable_ibl,
        },
    )


def postprocess_volume(name="PostProcess", exposure=1.0, bloom_intensity=0.35):
    return entity(
        name,
        postprocess={
            "_is_global": True,
            "_priority": 0,
            "_blend_weight": 1.0,
            "_blend_distance": 0.0,
            "_settings": {
                "_bloom": {"_enabled": True, "_intensity": float(bloom_intensity), "_radius": 0.005},
                "_tone_mapping": {"_mode": "kAces", "_exposure": float(exposure)},
            },
        },
    )


def room(prefix, min_x, max_x, min_z, max_z, height, floor_material, wall_material,
         ceiling_material=None, thickness=0.2):
    """A closed box of interior size (max-min) x height x (max-min), built from cubes.

    The interior surfaces sit exactly on the given bounds; slabs extend outwards.
    """
    ceiling_material = ceiling_material or floor_material
    half = thickness * 0.5
    width = max_x - min_x
    depth = max_z - min_z
    center_x = (min_x + max_x) * 0.5
    center_z = (min_z + max_z) * 0.5
    return [
        box(f"{prefix}_floor", (center_x, -half, center_z),
            (width + 2 * thickness, thickness, depth + 2 * thickness), floor_material),
        box(f"{prefix}_ceiling", (center_x, height + half, center_z),
            (width + 2 * thickness, thickness, depth + 2 * thickness), ceiling_material),
        box(f"{prefix}_wall_xmin", (min_x - half, height * 0.5, center_z),
            (thickness, height + 2 * thickness, depth + 2 * thickness), wall_material),
        box(f"{prefix}_wall_xmax", (max_x + half, height * 0.5, center_z),
            (thickness, height + 2 * thickness, depth + 2 * thickness), wall_material),
        box(f"{prefix}_wall_zmin", (center_x, height * 0.5, min_z - half),
            (width + 2 * thickness, height + 2 * thickness, thickness), wall_material),
        box(f"{prefix}_wall_zmax", (center_x, height * 0.5, max_z + half),
            (width + 2 * thickness, height + 2 * thickness, thickness), wall_material),
    ]


def coloured_room(prefix, min_x, max_x, min_z, max_z, height, floor_material, ceiling_material,
                  wall_xmin_material, wall_xmax_material, wall_z_material, thickness=0.2):
    """Same as room() but with the two side walls carrying their own material."""
    entities = room(prefix, min_x, max_x, min_z, max_z, height, floor_material, wall_z_material,
                    ceiling_material, thickness)
    half = thickness * 0.5
    height_span = height + 2 * thickness
    depth_span = (max_z - min_z) + 2 * thickness
    center_z = (min_z + max_z) * 0.5
    entities[2] = box(f"{prefix}_wall_xmin", (min_x - half, height * 0.5, center_z),
                      (thickness, height_span, depth_span), wall_xmin_material)
    entities[3] = box(f"{prefix}_wall_xmax", (max_x + half, height * 0.5, center_z),
                      (thickness, height_span, depth_span), wall_xmax_material)
    return entities


# --------------------------------------------------------------------------- #
# Scenes: one per typical DDGI problem
# --------------------------------------------------------------------------- #
def scene_cornell_bleed():
    """Baseline: colour bleeding from the red / green walls, no leak, smooth field."""
    entities = []
    entities += coloured_room("box", -2.0, 2.0, -2.0, 2.0, 3.0,
                              MAT_CORNELL_WHITE, MAT_CORNELL_WHITE,
                              MAT_CORNELL_RED, MAT_CORNELL_GREEN, MAT_CORNELL_WHITE)
    entities.append(box("ceiling_light", (0.0, 2.87, 1.2), (1.2, 0.06, 1.2), MAT_T_Emit_Warm))
    entities.append(box("white_box", (-0.55, 0.4, 0.75), (0.8, 0.8, 0.8), MAT_CORNELL_WHITE))
    entities.append(ball("white_ball", (0.65, 0.3, 0.35), 0.6, MAT_CORNELL_WHITE))
    # Probe lattice: the OUTERMOST plane on each axis must land exactly on the room's inner
    # surface, and every other plane must be inside the room.  Measured with the editor's image
    # capture (Saved/DDGIArtifact/, see run_capture.ps1):
    #
    #   6x6x6 @ (0.75, 0.6, 0.75)  x/z planes +-0.375..1.875   -> walls BLACK (0.000)
    #   6x7x6 @ (0.75, 0.5, 0.75)  padded past the room         -> walls 0.10
    #   6x6x6 @ (0.8, 0.6, 0.8)    x/z planes +-2.0 exactly     -> walls 0.445  <- this one
    #   5x5x5 @ (1, 0.75, 1)       on-surface reference         -> walls 0.445
    #
    # The reason is relocation: a probe that starts *on* a surface sits inside that surface's
    # slab (DXR: t_near < 0 < t_far = origin inside), and ddgi_relocation.hlsl's near-front-face
    # branch then pushes it off the surface by min(spacing) * 0.1.  That small offset is what
    # makes the probe an ordinary in-room probe.  Move the plane 0.125 off the wall instead and
    # the "closest front face" is no longer nearer than that threshold, so the probe is never
    # pushed and the wall ends up sampled from a location that reports no illumination - the
    # whole room goes black except the floor.
    #
    # Spacing 0.8 on x/z gives exactly 5 cells over the 4 m room; 0.6 on y gives exactly 5 cells
    # over the 3 m height, so 6 planes per axis end on the surfaces on every axis:
    #   x/z: -2.0 -1.2 -0.4 0.4 1.2 2.0     y: 0.0 0.6 1.2 1.8 2.4 3.0
    # The y = 3.0 plane sits right under the emissive panel, which is what lights the ceiling.
    entities.append(ddgi_volume("DDGI", (0.0, 1.5, 0.0), (6, 6, 6), (0.8, 0.6, 0.8),
                                max_ray_distance=30.0, use_probe_relocation=True))
    entities.append(main_camera("MainCamera", (0.0, 1.25, -1.5), (-0.1908, 0.0, 0.0, 0.9816)))
    entities.append(environment(intensity=1.0))
    entities.append(postprocess_volume())
    return entities


def scene_leak_thin_wall():
    """Light leaking through a thin wall between a lit and a dark room."""
    entities = []
    entities += room("rooms", -4.0, 4.0, -2.0, 2.0, 3.0, MAT_T_Gray, MAT_T_Gray)
    # 5 cm divider: the leak test.  Thicken it to 0.3 m to see the leak change.
    entities.append(box("divider_5cm", (0.0, 1.5, 0.0), (0.05, 3.0, 4.0), MAT_T_Gray))
    entities.append(box("lit_room_emitter", (-2.2, 2.85, 0.0), (2.4, 0.06, 2.4), MAT_T_Emit_Strong))
    entities.append(ball("dark_room_probe_ball", (2.2, 0.3, 0.0), 0.6, MAT_CORNELL_WHITE))
    entities.append(box("lit_room_ball", (-1.2, 0.35, 1.0), (0.7, 0.7, 0.7), MAT_CORNELL_WHITE))
    # Very dark mat on the unlit room floor: leaked light shows up far more clearly on it.
    entities.append(box("dark_room_mat", (2.0, 0.004, 0.0), (3.6, 0.008, 3.6), MAT_T_Dark))
    entities.append(ddgi_volume("DDGI", (0.0, 1.5, 0.0), (9, 5, 5), (1.0, 0.75, 1.0), max_ray_distance=30.0))
    # Look at the divider from the dark room.
    entities.append(main_camera("MainCamera", (2.7, 1.3, -1.5), (0.0, -0.7071068, 0.0, 0.7071068)))
    entities.append(environment(intensity=1.0))
    entities.append(postprocess_volume())
    return entities


def _small_room_5x3x5(prefix, floor_material=MAT_T_Gray):
    return room(prefix, -2.5, 2.5, -2.5, 2.5, 3.0, floor_material, floor_material)


def scene_firefly_small_emitter():
    """A tiny, very bright emitter: rare hits drive per frame variance (flicker)."""
    entities = []
    entities += _small_room_5x3x5("room")
    entities.append(ball("tiny_emitter_d0.24", (0.0, 1.5, 0.0), 0.24, MAT_T_Emit_Small))
    entities.append(ddgi_volume("DDGI", (0.0, 1.5, 0.0), (5, 5, 5), (1.0, 0.75, 1.0), max_ray_distance=30.0))
    entities.append(main_camera("MainCamera", (0.0, 1.4, -2.1), (0.0, 0.0, 0.0, 1.0)))
    entities.append(environment(intensity=1.0))
    entities.append(postprocess_volume())
    return entities


def scene_area_emitter_same_flux():
    """Same room and roughly the same total flux as the tiny emitter, spread over a panel."""
    entities = []
    entities += _small_room_5x3x5("room")
    entities.append(box("area_emitter_1.5m", (0.0, 1.5, 0.0), (1.5, 1.5, 0.06), MAT_T_Emit_Large))
    entities.append(ddgi_volume("DDGI", (0.0, 1.5, 0.0), (5, 5, 5), (1.0, 0.75, 1.0), max_ray_distance=30.0))
    entities.append(main_camera("MainCamera", (0.0, 1.4, -2.1), (0.0, 0.0, 0.0, 1.0)))
    entities.append(environment(intensity=1.0))
    entities.append(postprocess_volume())
    return entities


def scene_probe_in_geometry():
    """A full row of probes sits inside a 1.2 m wall: back face rays, no relocation."""
    entities = []
    entities += room("room", -3.0, 3.0, -3.0, 3.0, 3.0, MAT_T_Gray, MAT_T_Gray)
    entities.append(box("solid_wall_1.2m", (0.0, 1.5, 0.0), (1.2, 3.0, 4.0), MAT_T_Gray))
    entities.append(box("left_emitter", (-2.0, 2.85, 0.0), (1.8, 0.06, 1.8), MAT_T_Emit_Strong))
    entities.append(ball("back_room_ball", (2.2, 0.35, 0.0), 0.7, MAT_CORNELL_WHITE))
    # probe_count.x = 7 with 1 m spacing puts the x = 0 row inside the wall.
    entities.append(ddgi_volume("DDGI", (0.0, 1.5, 0.0), (7, 5, 7), (1.0, 0.75, 1.0), max_ray_distance=30.0))
    entities.append(main_camera("MainCamera", (2.5, 1.4, 0.0), (-0.0494, -0.7054, -0.0494, 0.7054)))
    entities.append(environment(intensity=1.0))
    entities.append(postprocess_volume())
    return entities


def scene_open_volume_boundary():
    """Open scene: the volume only covers the middle, so its boundary is visible."""
    entities = []
    entities.append(quad("ground", (0.0, 0.0, 0.0), (14.0, 1.0, 14.0), MAT_DEFAULT))
    for i, (x, z) in enumerate(((-3.0, -3.0), (3.0, -3.0), (-3.0, 3.0), (3.0, 3.0))):
        entities.append(box(f"pillar_{i}", (x, 1.5, z), (0.6, 3.0, 0.6), MAT_T_Gray))
    entities.append(box("low_emitter", (0.0, 0.35, -1.6), (1.8, 0.06, 1.0), MAT_T_Emit_Strong))
    # Deliberately small: x, z in [-2.5, 2.5] while the ground reaches +-7.
    entities.append(ddgi_volume("DDGI", (0.0, 1.0, 0.0), (6, 3, 6), (1.0, 1.0, 1.0),
                                max_ray_distance=40.0, debug_probe_radius=0.15))
    entities.append(main_camera("MainCamera", (0.0, 3.4, -7.5), (0.1564, 0.0, 0.0, 0.9877)))
    entities.append(environment(intensity=1.0, render_sky=True))
    entities.append(postprocess_volume())
    return entities


def scene_gradient_banding():
    """A long flat wall with a smooth gradient: probe cell banding and dark end steps."""
    entities = []
    entities += room("hall", -6.0, 6.0, -3.0, 3.0, 3.0, MAT_T_Gray, MAT_T_Gray)
    entities.append(box("ceiling_strip", (0.0, 2.9, 0.8), (9.0, 0.06, 0.4), MAT_T_Emit_Strip))
    entities.append(ball("corner_ball", (-4.6, 0.4, 2.0), 0.8, MAT_CORNELL_WHITE))
    entities.append(ddgi_volume("DDGI", (0.0, 1.5, 0.0), (9, 5, 5), (1.5, 0.75, 1.5), max_ray_distance=40.0))
    # Grazing view along the +Z wall so banding is easy to see.
    entities.append(main_camera("MainCamera", (-5.0, 1.5, -1.8), (0.0, 0.5255, 0.0, 0.8508)))
    entities.append(environment(intensity=1.0))
    entities.append(postprocess_volume())
    return entities


def scene_thin_plane_occluder():
    """A one sided plane divides the room: back face hits must not leak light."""
    entities = []
    entities += room("room", -4.0, 4.0, -3.0, 3.0, 3.0, MAT_T_Gray, MAT_T_Gray)
    # rotX(90 deg) stands the plane up: 8.4 m wide, 3 m tall, facing +Z.
    entities.append(quad("divider_plane", (0.0, 1.5, 0.0), (8.4, 1.0, 3.0),
                         MAT_T_Gray, rotation=(0.7071068, 0.0, 0.0, 0.7071068)))
    entities.append(box("emitter_behind_plane", (0.0, 2.8, -2.0), (3.0, 0.06, 1.6), MAT_T_Emit_Strong))
    entities.append(ball("dark_side_ball", (1.8, 0.35, 1.2), 0.7, MAT_CORNELL_WHITE))
    entities.append(ddgi_volume("DDGI", (0.0, 1.5, 0.0), (9, 5, 7), (1.0, 0.75, 1.0), max_ray_distance=30.0))
    entities.append(main_camera("MainCamera", (0.0, 1.4, 2.3), (0.0, 1.0, 0.0, 0.0)))
    entities.append(environment(intensity=1.0))
    entities.append(postprocess_volume())
    return entities


def scene_cornell_bleed_diag_a():
    """01 with the ORIGINAL probe lattice: 5x5x5 @ (1, 0.75, 1) centred on the room, with
    relocation on.  Probe rows land exactly on the wall / floor / ceiling planes."""
    entities = scene_cornell_bleed()
    for e in entities:
        if "_ddgi_volume_component" in e:
            e["_ddgi_volume_component"]["_probe_count"] = [5, 5, 5]
            e["_ddgi_volume_component"]["_probe_spacing"] = [1.0, 0.75, 1.0]
    return entities


def scene_cornell_bleed_diag_b():
    """01 with the half-cell lattice but relocation OFF, to tell "lattice" apart from
    "relocation" when the walls go dark."""
    entities = scene_cornell_bleed()
    for e in entities:
        if "_ddgi_volume_component" in e:
            e["_ddgi_volume_component"]["_use_probe_relocation"] = False
    return entities


def scene_probe_stress():
    """Large volume: probe count / atlas layout / update cost scaling."""
    entities = []
    entities += room("hall", -20.0, 20.0, -20.0, 20.0, 6.0, MAT_T_Gray, MAT_T_Gray, thickness=0.4)
    for i, (x, z) in enumerate(((-10.0, -10.0), (10.0, -10.0), (-10.0, 10.0), (10.0, 10.0))):
        entities.append(box(f"column_{i}", (x, 3.0, z), (1.5, 6.0, 1.5), MAT_T_Gray))
    entities.append(box("strip_a", (0.0, 5.6, -6.0), (16.0, 0.1, 0.6), MAT_T_Emit_Strip))
    entities.append(box("strip_b", (0.0, 5.6, 6.0), (16.0, 0.1, 0.6), MAT_T_Emit_Strip))
    # 40 x 7 x 40 = 11200 probes, atlas 320 x 2240 (7 rows cover floor y=0 .. ceiling y=6).
    entities.append(ddgi_volume("DDGI", (0.0, 3.0, 0.0), (40, 7, 40), (1.0, 1.0, 1.0),
                                max_ray_distance=60.0, draw_probes=False, debug_probe_radius=0.2))
    entities.append(main_camera("MainCamera", (0.0, 3.4, -17.0), (0.0872, 0.0, 0.0, 0.9962)))
    entities.append(environment(intensity=1.0))
    entities.append(postprocess_volume())
    return entities


SCENES = [
    ("01_cornell_bleed", "基线：颜色渗透方向、无漏光、场是否平滑", scene_cornell_bleed),
    ("02_leak_thin_wall", "薄墙漏光（DDGI 经典失效）", scene_leak_thin_wall),
    ("03_firefly_small_emitter", "小而极亮的发光体：逐帧方差 → 脉冲/闪烁", scene_firefly_small_emitter),
    ("04_area_emitter_same_flux", "同通量的大面积发光体：噪声应显著变小", scene_area_emitter_same_flux),
    ("05_probe_in_geometry", "探针埋在几何体内部：背面命中 / 无 relocation", scene_probe_in_geometry),
    ("06_open_volume_boundary", "开阔场景 + 体素边界：边界硬切换、射线全部 miss", scene_open_volume_boundary),
    ("07_gradient_banding", "大面积平滑渐变的色阶/波浪分层", scene_gradient_banding),
    ("08_thin_plane_occluder", "单面薄片遮挡：背面命中被丢弃后的漏光", scene_thin_plane_occluder),
    ("09_probe_stress", "探针数量/图集布局/更新开销的规模测试", scene_probe_stress),
    # A/B scenes for the "walls went dark after the lattice change" investigation.  Same room
    # as 01, only the DDGI volume differs, so both can be compared in one session.
    ("01a_cornell_bleed_lattice_old",
     "对照 A：探针回到 5x5x5 @ (1,0.75,1) 贴面 + relocation 开", scene_cornell_bleed_diag_a),
    ("01b_cornell_bleed_no_relocation",
     "对照 B：半格偏移点阵 + relocation 关", scene_cornell_bleed_diag_b),
]

# Per scene documentation used to build the README: layout, what to watch, knobs.
SCENE_NOTES = {
    "01_cornell_bleed": {
        "layout": "4x3x4m 白盒，-X 墙红 / +X 墙绿，天花 1.2m 暖光板（emission 3.0），盒内白方块 + 白球；探针 6x6x6 @ (0.8,0.6,0.8)：平面 x/z = ±2.0/±1.2/±0.4、y = 0/0.6/1.2/1.8/2.4/3.0，最外层正好压在房间内表面上（relocation 会把它们推成有效探针）；相机 (0,1.25,-1.5) 上仰 22° 把光源纳入画面",
        "watch": "白方块/白球的受光面与地板应出现红、绿染色且方向正确；墙面应平滑（无探针格子）；盒外不应有光",
        "knobs": "probe spacing、`_hysteresis`、`_max_ray_distance`",
    },
    "02_leak_thin_wall": {
        "layout": "8x3x4m 双房，中间 5cm 薄墙（x=0）；亮房天花 2.4m 强发光板（emission 3.0）；暗房地板铺 0.05 反照率暗垫；探针 9x5x5（y 间距 0.75，贴地/贴天花）；相机在暗房朝 -X 看薄墙",
        "watch": "暗房应基本全黑。若薄墙附近（尤其暗垫上）出现亮晕/亮边即为漏光；把 divider_5cm 的 scale.x 改成 0.3 再对比，若漏光消失说明与薄墙/背面命中相关",
        "knobs": "薄墙厚度、`_use_visibility`、`_max_ray_distance`",
    },
    "03_firefly_small_emitter": {
        "layout": "5x3x5m 中灰房，中心 Ø0.24m 强发光球（emission 6.0 ≈ 线性 33，约 0.18m²）；探针 5x5x5（y 间距 0.75）；相机 (0,1.4,-2.1) 正对发光球",
        "watch": "墙面出现明显的逐帧闪烁/脉冲：小亮源是罕见命中，单帧方差极大。这是 04 的对照组",
        "knobs": "`_hysteresis` 0.97→0.99、`_history_step_ratio` 0.25→0.1、`_use_temporal` 关掉对照、`kRaysPerProbe` 128→256、勾选 `_freeze_ray_rotation`",
    },
    "04_area_emitter_same_flux": {
        "layout": "与 03 完全相同的房间，把球换成 1.5x1.5m 弱发光板（emission 1.2 ≈ 线性 3.0，约 2.25m²，总通量与 03 大致相同）",
        "watch": "整体亮度应与 03 接近，但静止画面的噪声应显著更小 —— 用来验证“stage-1 下把发光体做大做暗”这一结论",
        "knobs": "与 03 相同的调项，重点比较两场景的墙面噪声",
    },
    "05_probe_in_geometry": {
        "layout": "6x3x6m 中灰房，中间 1.2m 厚实心墙（x=0，4m 长），左侧天花强发光板（emission 3.0）；探针 7x5x7（y 间距 0.75）→ x=0 整排埋在墙体内；相机在 +X 侧朝 -X 略上仰",
        "watch": "埋入探针的辐照度（debug 球）与墙两侧的明暗：当前没有 relocation/classification，预期能看到偏黑或异常；debug mode 切 `kHitType` 可看到背面射线",
        "knobs": "debug mode（kHitType/kDistance）、probe spacing、墙厚",
    },
    "06_open_volume_boundary": {
        "layout": "开放场景：14x14m 地面 + 四根 0.6m 方柱（±3,±3）+ 低处发光板（emission 3.0）；探针体**只覆盖中间 5x5m**（6x3x6 @1m，底排正好落在地面 y=0，所以地面在网格内）；相机在体素外上方俯视",
        "watch": "离开体素范围后 GI 直接消失（`SampleDDGIIrradiance` 在体外返回 false），地面上会出现矩形硬边界；边界探针有大量 miss（0.1 环境），注意是否把远处冲淡",
        "knobs": "体素 probe_count/spacing、`_max_ray_distance`、相机位置",
    },
    "07_gradient_banding": {
        "layout": "12x3x6m 长厅，天花一条 9m 长条光源偏 +Z（emission 3.0），形成大面积平滑渐变；探针 9x5x5（x/z 间距 1.5m、y 间距 0.75m）；相机在 -X 端擦着 +Z 墙看",
        "watch": "探针格子尺度的分层/波浪（八面体 6x6 + 三线性插值的结构），以及最暗角落的色阶断层（8bit 线性量化已修，这里用来验证）",
        "knobs": "probe spacing、`_hysteresis`、后期曝光",
    },
    "08_thin_plane_occluder": {
        "layout": "8x3x6m 灰房，中间**单面 plane**（rotX 90°，8.4x3m，法线 +Z）当隔墙，光源在 -Z 侧天花（emission 3.0）；探针 9x5x7（y 间距 0.75）；相机在 +Z 侧朝 -Z 看隔板",
        "watch": "单面几何的背面命中会被丢弃（radiance 里 `_distance<=0` 直接 continue），所以亮侧的光会穿过来 → 预期漏光，用来验证背面/单面几何策略",
        "knobs": "隔板朝向与法线（rotation 取反即可翻面）、材质 `_cull`、`_use_visibility`",
    },
    "09_probe_stress": {
        "layout": "40x6x40m 大厅 + 四根方柱 + 两条 16m 灯带；探针 **40x7x40 = 11200**（1m 间距，y 覆盖 0..6），图集 320x2240，关掉 probe debug 绘制；相机在厅内",
        "watch": "DDGI 更新耗时、图集布局/显存、是否出现 probe_count 或 dispatch 相关告警；顺带看大体积的收敛速度",
        "knobs": "probe_count、`_max_ray_distance`、`_hysteresis`",
    },
}


def scene_document(name: str, title: str, entities) -> dict:
    entity_names = [doc["_tag_component"]["_name"] for doc in entities]
    if len(set(entity_names)) != len(entity_names):
        raise ValueError(f"scene {name}: duplicate entity names")
    for index, doc in enumerate(entities):
        doc["_hierarchy_component"]["_sibling_index"] = index
        # Entity guids carry the scene name so identical helper names in different
        # scenes cannot collide, while staying stable when a scene is regenerated.
        doc["_entity_guid"] = guid_for(f"entity/{name}/{doc['_tag_component']['_name']}")
    return {
        "_header": {
            "_format_version": 1,
            "_guid": guid_for(f"scene/{name}"),
            "_asset_type": ASSET_TYPE_SCENE,
            "_asset_name": name,
            "_dependencies": [{"_guid": SKY_TEXTURE, "_type": "kSoft"}],
        },
        "_scene_format_version": 4,
        "_entities": entities,
        "_prefab_instances": [],
    }


# --------------------------------------------------------------------------- #
# Asset database
# --------------------------------------------------------------------------- #
def register_asset(database: dict, guid: str, project_path: str, type_string: str) -> None:
    assets = database.setdefault("assets", [])
    entry = {"guid": guid, "path": project_path, "type": type_string}
    for existing in assets:
        if existing.get("path") == project_path:
            existing.update(entry)
            return
    assets.append(entry)


def readme_text(project_path: str) -> str:
    lines = [
        "# DDGI test scenes",
        "",
        "每个场景只针对一个典型 DDGI 问题，几何尽量少、光照尽量单一，方便快速判断画面表现。",
        "场景与材质都由 `Tools/generate_ddgi_test_scenes.py` 生成（guid 由名字派生，可重复生成），",
        "生成时会同时登记进 `Library/AssetDatabase.json`。",
        "",
        "## 打开方式",
        "",
        f"- 资产浏览器里打开 `{project_path}/` 下的 `.almap`；或把 `Editor/EditorConfig.json` 的",
        '  `_scene_path` 改成 `project://' + project_path + '/01_cornell_bleed.almap`。',
        "- 若列表里看不到新文件，先做一次资产刷新/重扫再打开。",
        "",
        "## 前提：光照与 GI（很重要）",
        "",
        "- stage-1 命中辐亮度 = `DDGIReadMaterialEmission()` + `DDGIEvaluateDirectLighting()`（`ddgi.hlsl`）：",
        "  命中点用 `SampleUnifiedLight` 采样**一盏统一光（方向光/点光/聚光都在内）**，再打一条阴影射线",
        "  `DDGIIsDirectLightOccluded`；`_unified_light_count == 0` 时这一项直接返回 0。",
        "  只有**打空**的射线才用硬编码的环境项 `_environment_radiance_intensity = 0.1`（`DDGI.cpp` 里写死）。",
        "- 本组场景**不放任何光源**（`_unified_light_count == 0`），所以 `DDGIEvaluateDirectLighting()` 恒为 0，",
        "  探针命中的只可能是自发光材质 —— 这样测到的 GI 里没有直接光和阴影的成分。",
        "- 材质颜色按 sRGB 存储、加载时经 `Color::FromSrgb` 转线性：emitter 的 `_EmissionValue`",
        "  写 6.0 ≈ 线性 33、写 3.0 ≈ 线性 12；emitter 的尺寸/亮度就是按这个换算配的。",
        "- 场景**没有方向光**：GI 的 stage-1 里 `DDGIEvaluateDirectLighting()` 在 `_unified_light_count == 0` 时直接返回 0，",
        "  所以探针命中的只可能是自发光材质，打空的射线用硬编码的 0.1 环境项 —— 测到的是纯 DDGI，不含直接光/阴影。",
        "- 可见性与环境基线来自天空 IBL：`sky_atmosphere.hlsl` 现在用 `lum *= PI * _EnvironmentIntensity`",
        "  （原来乘 `_MainlightColor`：无方向光时天空全黑，还会让体积光里的 `normalize(0)` 出 NaN 把整屏染黑）。",
        "  **天空不进入 GI**（探针打空用的是硬编码 0.1），它只影响探针网格之外那些表面的环境光。",
        "- 需要方向光做对照时，用生成器里的 `directional_light()`（位置/朝向可抄 `scene/NewScene.almap` 的 `main_light`）；",
        "  加了之后 `_unified_light_count` 不再为 0，GI 会多出“命中点直接光 + 阴影射线”这一项。",
        "- **`Environment._intensity` 会一起缩放 GI**：`lighting.hlsli` 的间接项整体乘 `g_EnvironmentIntensity`，"
        "  而 `deferred_lighting.hlsl` 在体素内把 `diffuse_irradiance` 换成了 DDGI 的辐照度；"
        "  所以这个值既是天空环境光强度，也是 DDGI 的亮度倍率（场景统一用 1.0，与 NewScene 一致）。",
        "  体素外：`PI × _intensity² × SH`（天空项不再乘主光颜色）；体素内：`自发光驱动的 DDGI 辐照度 × _intensity`。",
        "- 场景不含 light probe group / 静态漫反射探针数据，所以体素范围之外没有 GI（见 06）。",
        "- 顺带覆盖“无主光场景”：无主光时 EnvironmentSystem 曾每帧重烘环境 cubemap，已在 `Environment.cpp` 修掉。",
        "- **GI 只在探针网格范围内生效**：`SampleDDGIIrradiance()` 对网格外的着色点直接返回 false，"
        "  没被网格覆盖的地面/天花只拿 IBL。要让哪块表面有 GI，就把探针网格盖到那里（06 专门测这个边界）。",
        "- **探针点阵：每个轴的最外层探针平面必须正好落在房间内表面上**，其余平面全部在房间内部。",
        "  这是实测结论（编辑器 Image Capture 抓 `DeferredLightingPass -> _CameraColorAttachmentA`，"
        "  脚本 `Saved/DDGIArtifact/run_capture.ps1`，同一相机同一帧对比）：",
        "",
        "  | 点阵 | x/z 最外层平面 | 墙面亮度 |",
        "  | --- | --- | --- |",
        "  | 6x6x6 @ (0.75, 0.6, 0.75) | ±1.875（离墙 0.125） | **0.000 全黑** |",
        "  | 7x7x7 @ 0.75（向外加垫） | ±2.25（在墙外） | 0.10 |",
        "  | 6x6x6 @ (0.8, 0.6, 0.8) | **±2.0（正好在墙上）** | **0.445** |",
        "  | 5x5x5 @ (1, 0.75, 1)（旧基线） | ±2.0（正好在墙上） | 0.445 |",
        "",
        "  机制在 `ddgi_relocation.hlsl`：探针正好落在表面上时它在该板的**内部**"
        "（DXR slab 判定 t_near < 0 < t_far = 起点在盒内），"
        "  重定位的“近正面”分支会把它沿法线推开 `min(spacing) * _relocation_min_frontface_distance_ratio`，"
        "  这个偏移量才让探针变成一颗普通的室内探针。把平面挪到离墙 0.125 而不是 0 时，"
        "  最近正面距离大于该阈值，重定位永远不触发，这颗探针就一直停在离墙 0.125 的位置上报不到光 ——"
        "  结果是除地面外整个房间发黑。**所以不要为了“让探针不与表面共面”去缩间距**。",
        "",
        "  间距取法：房间跨度 / 间距 = 格数，格数 + 1 = 探针层数，这样最外层正好压在表面上。"
        "  01 的 4m x 3m x 4m → x/z 用 0.8（5 格）、y 用 0.6（5 格）→ 6x6x6，"
        "  平面 x/z = ±2.0/±1.2/±0.4，y = 0/0.6/1.2/1.8/2.4/3.0（y=3.0 正好贴着发光板，天花靠它点亮）。",
        "",
        "  顺带：把平面挪到墙外（加垫）只能恢复约 1/4 的墙面亮度，因为从表面出发的三线性插值会"
        "  在“表面外那颗探针”上分到权重，而它永远是黑的；只有让探针真的贴在表面上，重定位才会把它推成有效探针。",
        "",
        "## 每个场景",
        "",
    ]
    for name, purpose, _ in SCENES:
        note = SCENE_NOTES.get(name)
        lines += [
            f"### {name}.almap",
            "",
            f"- 针对：{purpose}",
        ]
        if note is not None:
            lines += [
                f"- 布局：{note['layout']}",
                f"- 看什么：{note['watch']}",
                f"- 可调：{note['knobs']}",
            ]
        lines.append("")
    lines += [
        "## 生成的材质",
        "",
        "| 材质 | 反照率 | 自发光 (sRGB 值) | 说明 |",
        "| --- | --- | --- | --- |",
        "| MAT_T_Gray | 0.5 | 0 | 通用中灰墙/地 |",
        "| MAT_T_Dark | 0.05 | 0 | 暗房暗垫，用来放大漏光 |",
        "| MAT_T_Emit_Small | 0.02 | 6.0 | 小面积强发光体（罕见命中，约 0.18m²） |",
        "| MAT_T_Emit_Large | 0.02 | 1.2 | 大面积弱发光体，与上者总通量接近 |",
        "| MAT_T_Emit_Strong | 0.02 | 3.0 | 漏光/遮挡场景的光源 |",
        "| MAT_T_Emit_Warm | 0.02 | 3.0/1.8/0.9 | Cornell 参考场景的暖光顶灯 |",
        "| MAT_T_Emit_Strip | 0.02 | 3.0 | 渐变场景的长条灯 |",
        "",
        "## 通用提示",
        "",
        "- `DDGIVolumeComponent._debug_mode`：`kIrradiance` 看色度（默认），`kHitType`/`kDistance` 看命中类型与距离，",
        "  `kRayDirection` 看射线分布；probe debug 只在 `_draw_probes` 为 true 时绘制（09 关掉了）。",
        "- `_freeze_ray_rotation = true` 冻结射线旋转，用来判断“闪烁是否来自随机旋转”。",
        "- 全部场景都是静态几何 + 静态相机，帧间变化只可能来自 GI / 后处理，不来自几何运动。",
        "- 重新生成：`python Tools/generate_ddgi_test_scenes.py [--project <项目根目录>]`，重复执行结果一致。",
        "",
    ]
    return "\n".join(lines)


# --------------------------------------------------------------------------- #
# Validation
# --------------------------------------------------------------------------- #
def collect_known_guids(project_root: Path) -> dict:
    known = {}
    engine_db = project_root.parent / "AiluEngine" / "Engine" / "Res" / "assetdb.alasset"
    if engine_db.is_file():
        for item in json.loads(engine_db.read_text(encoding="utf-8")).get("assets", []):
            known[item["guid"]] = item["path"]
    project_db = project_root / "Library" / "AssetDatabase.json"
    if project_db.is_file():
        for item in json.loads(project_db.read_text(encoding="utf-8")).get("assets", []):
            known[item["guid"]] = item["path"]
    return known


def validate(project_root: Path, asset_dir: Path) -> bool:
    known = collect_known_guids(project_root)
    ok = True
    total_entities = 0
    seen_entity_guids: set = set()
    for name, _, _ in SCENES:
        path = asset_dir / f"{name}.almap"
        doc = json.loads(path.read_text(encoding="utf-8"))
        entities = doc["_entities"]
        total_entities += len(entities)
        guids = [e["_entity_guid"] for e in entities]
        if len(set(guids)) != len(guids):
            print(f"  ERROR {name}: duplicate entity guid")
            ok = False
        if any(not g for g in guids):
            print(f"  ERROR {name}: empty entity guid")
            ok = False
        overlap = seen_entity_guids.intersection(guids)
        if overlap:
            print(f"  ERROR {name}: entity guid shared with another scene ({len(overlap)} collisions)")
            ok = False
        seen_entity_guids.update(guids)
        if doc["_header"]["_guid"] not in known:
            print(f"  ERROR {name}: scene guid is missing from the asset database")
            ok = False
        has_ddgi = False
        has_camera = False
        for index, entity_doc in enumerate(entities):
            if entity_doc["_hierarchy_component"]["_sibling_index"] != index:
                print(f"  ERROR {name}: sibling index mismatch on entity {index}")
                ok = False
            has_ddgi = has_ddgi or "_ddgi_volume_component" in entity_doc
            has_camera = has_camera or entity_doc["_tag_component"]["_tag"] == "MainCamera"
            mesh = entity_doc.get("_static_mesh_component")
            if mesh is not None and mesh["_mesh_guid"] not in known:
                print(f"  ERROR {name}: unknown mesh guid {mesh['_mesh_guid']}")
                ok = False
            for material_guid in (mesh or {}).get("_material_guids", []):
                if material_guid not in known:
                    print(f"  ERROR {name}: unknown material guid {material_guid}")
                    ok = False
            ddgi = entity_doc.get("_ddgi_volume_component")
            if ddgi is not None:
                probes = ddgi["_probe_count"][0] * ddgi["_probe_count"][1] * ddgi["_probe_count"][2]
                if probes == 0:
                    print(f"  ERROR {name}: empty probe grid")
                    ok = False
        if not has_ddgi:
            print(f"  ERROR {name}: no DDGI volume")
            ok = False
        if not has_camera:
            print(f"  ERROR {name}: no MainCamera")
            ok = False
        print(f"  {name}: {len(entities)} entities, guid {doc['_header']['_guid']}")
    print(f"validated {len(SCENES)} scenes, {total_entities} entities, ok={ok}")
    return ok


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate the DDGITest scene set.")
    parser.add_argument("--project", type=Path, default=_DEFAULT_PROJECT,
                        help="project root (contains Assets/ and Library/)")
    args = parser.parse_args()

    project_root = args.project.resolve()
    assets_root = project_root / "Assets"
    if not assets_root.is_dir():
        print(f"project Assets folder not found: {assets_root}")
        return 1
    asset_dir = assets_root / ASSET_DIR_NAME
    asset_dir.mkdir(parents=True, exist_ok=True)

    database_path = project_root / "Library" / "AssetDatabase.json"
    database = json.loads(database_path.read_text(encoding="utf-8")) if database_path.is_file() else {"assets": []}

    for name, _material in MATERIALS.items():
        path = asset_dir / f"{name}.alasset"
        albedo, emission = _material
        document = material_document(name, albedo, emission)
        path.write_text(json.dumps(document, indent=4, ensure_ascii=False) + "\n", encoding="utf-8")
        register_asset(database, document["_header"]["_guid"],
                       f"project://{ASSET_DIR_NAME}/{name}.alasset", ASSET_TYPE_MATERIAL)
        print(f"material {path.name}")

    for name, purpose, builder in SCENES:
        document = scene_document(name, purpose, builder())
        path = asset_dir / f"{name}.almap"
        path.write_text(json.dumps(document, indent=4, ensure_ascii=False) + "\n", encoding="utf-8")
        register_asset(database, document["_header"]["_guid"],
                       f"project://{ASSET_DIR_NAME}/{name}.almap", ASSET_TYPE_SCENE)
        print(f"scene    {path.name}  ({purpose})")

    database_path.parent.mkdir(parents=True, exist_ok=True)
    database_path.write_text(json.dumps(database, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"asset database updated: {database_path} ({len(database['assets'])} entries)")

    (asset_dir / "README.md").write_text(readme_text(ASSET_DIR_NAME), encoding="utf-8")
    print(f"readme   {(asset_dir / 'README.md')}")

    print("validating...")
    return 0 if validate(project_root, asset_dir) else 1


if __name__ == "__main__":
    raise SystemExit(main())
