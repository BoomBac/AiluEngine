"""Render the staged Blender scene with Eevee for inclusion in a material validation report."""
import argparse
import json
import sys
from pathlib import Path

import bpy


def main():
    args_list = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--disable-dof", action="store_true", help="Disable camera DOF for texture comparisons")
    parser.add_argument("--engine-camera", type=Path, help="Match the captured perspective projection/dimensions")
    args = parser.parse_args(args_list)

    scene = bpy.context.scene
    if scene.camera is None:
        raise RuntimeError("The staged Blender scene has no active camera")

    render_engines = bpy.types.RenderSettings.bl_rna.properties["engine"].enum_items.keys()
    if "BLENDER_EEVEE_NEXT" in render_engines:
        scene.render.engine = "BLENDER_EEVEE_NEXT"
    elif "BLENDER_EEVEE" in render_engines:
        scene.render.engine = "BLENDER_EEVEE"
    else:
        raise RuntimeError("This Blender installation has no Eevee render engine")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    scene.render.resolution_x = 1280
    scene.render.resolution_y = 720
    scene.render.resolution_percentage = 100
    if args.engine_camera:
        camera = json.loads(args.engine_camera.read_text(encoding="utf-8"))["test_camera"]
        if camera["projection_type"] != 1:
            raise RuntimeError("Matched material preview requires a perspective camera")
        scene.camera.data.sensor_fit = "HORIZONTAL"
        scene.camera.data.lens = scene.camera.data.sensor_width * abs(camera["projection_stable"][0]) * 0.5
        scene.render.resolution_x = camera["render_width"]
        scene.render.resolution_y = camera["render_height"]
    if args.disable_dof:
        scene.camera.data.dof.use_dof = False
    scene.render.use_compositing = False
    scene.render.use_sequencer = False
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.render.filepath = str(args.output.resolve())
    bpy.ops.render.render(write_still=True)
    print(f"BLENDER_EEVEE_PREVIEW_WRITTEN {args.output.resolve()}", flush=True)


if __name__ == "__main__":
    main()
