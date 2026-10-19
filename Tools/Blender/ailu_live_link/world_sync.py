"""Bake World illumination and camera background separately, in the engine's latlong convention."""
import hashlib
import math
import os
from pathlib import Path

import bpy
import numpy as np
from mathutils import Vector

from . import protocol
from .mesh_sync import _temp_dir

_image_revision = 0


def invalidate_images():
    global _image_revision
    _image_revision += 1


def world_signature(scene):
    from .dirty_tracker import _tree_signature
    world = scene.world
    if world is None:
        return None
    images, settings = [], []
    visited = set()
    animated = False
    def visit(tree):
        nonlocal animated
        if tree is None or tree.as_pointer() in visited:
            return
        visited.add(tree.as_pointer())
        animated |= tree.animation_data is not None
        for node in tree.nodes:
            image = getattr(node, 'image', None)
            if image is not None:
                path = bpy.path.abspath(image.filepath, library=image.library)
                stamp = os.stat(path).st_mtime_ns if os.path.isfile(path) else 0
                images.append((image.as_pointer(), path, stamp, tuple(image.size), image.colorspace_settings.name))
            values = []
            for prop in node.bl_rna.properties:
                if not prop.is_readonly and prop.type in {'BOOLEAN', 'INT', 'FLOAT', 'STRING', 'ENUM'}:
                    if prop.identifier in {'location', 'width', 'height', 'select', 'hide', 'label', 'name'}:
                        continue
                    value = getattr(node, prop.identifier)
                    values.append((prop.identifier, tuple(value) if getattr(prop, "is_array", False) else value))
            settings.append((node.name, tuple(values)))
            if node.type == 'GROUP':
                visit(node.node_tree)
    visit(world.node_tree if world.use_nodes else None)
    animated |= world.animation_data is not None
    return (world.as_pointer(), world.use_nodes, tuple(world.color), _image_revision,
            _tree_signature(world.node_tree, set()) if world.use_nodes else (), tuple(images), tuple(settings),
            scene.frame_current if animated else 0)


def _copy_world(source, camera_ray, groups):
    world = source.copy()
    visited = {}
    def visit(tree):
        for node in list(tree.nodes):
            if node.type == 'GROUP' and node.node_tree is not None:
                pointer = node.node_tree.as_pointer()
                if pointer not in visited:
                    copied = node.node_tree.copy()
                    visited[pointer] = copied
                    groups.append(copied)
                    visit(copied)
                node.node_tree = visited[pointer]
            if node.type == 'LIGHT_PATH':
                for link in list(tree.links):
                    if link.from_node != node:
                        continue
                    if link.from_socket.name != 'Is Camera Ray':
                        raise ValueError('World Light Path supports Is Camera Ray only')
                    socket = link.to_socket
                    tree.links.remove(link)
                    value = tree.nodes.new('ShaderNodeValue')
                    value.outputs[0].default_value = float(camera_ray)
                    tree.links.new(value.outputs[0], socket)
            if node.type == 'OUTPUT_WORLD' and node.is_active_output and node.inputs['Volume'].is_linked:
                raise ValueError('World Volume cannot be represented by an environment texture')
    try:
        if world.use_nodes:
            visit(world.node_tree)
        return world
    except Exception:
        bpy.data.worlds.remove(world)
        raise


def _bake(scene, signature):
    width, height, face_size = 512, 256, 128
    revision = hashlib.sha256(repr(signature).encode()).hexdigest()
    output = Path(_temp_dir()) / ('world_' + revision)
    output.mkdir(exist_ok=True)
    paths = [output / 'lighting.exr', output / 'background.exr']
    if all(path.is_file() for path in paths):
        return revision, paths
    bake = bpy.data.scenes.new('AiluLiveLinkBakeWorld')
    data = bpy.data.cameras.new('AiluWorldCamera')
    camera = bpy.data.objects.new('AiluWorldCamera', data)
    bake.collection.objects.link(camera)
    bake.camera = camera
    data.type, data.angle = 'PERSP', math.pi / 2
    bake.render.engine = 'CYCLES'
    bake.cycles.device, bake.cycles.samples = 'CPU', 1
    bake.cycles.use_denoising = False
    bake.render.threads_mode, bake.render.threads = 'FIXED', 2
    bake.render.resolution_x = bake.render.resolution_y = face_size
    bake.render.resolution_percentage = 100
    bake.render.use_compositing = bake.render.use_sequencer = False
    bake.render.film_transparent = False
    bake.render.dither_intensity = 0
    bake.view_settings.view_transform = 'Raw'
    bake.view_settings.exposure, bake.view_settings.gamma = 0, 1
    bake.render.image_settings.file_format = 'OPEN_EXR'
    bake.render.image_settings.color_depth = '32'
    bake.render.image_settings.exr_codec = 'NONE'
    bake.render.image_settings.color_management = 'FOLLOW_SCENE'
    u, v = np.meshgrid((np.arange(width) + .5) / width, (np.arange(height) + .5) / height)
    longitude, latitude = (u - .5) * (2 * math.pi), (v - .5) * math.pi
    engine_dirs = np.stack([np.sin(longitude) * np.cos(latitude), np.sin(latitude),
                            np.cos(longitude) * np.cos(latitude)], axis=-1)
    # Lighting conversion samples -direction in the existing CubeMapGen shader; camera sky samples +direction.
    worlds, groups, images = [], [], []
    # bpy.ops.render.render always publishes a global "Render Result" image; it is not part of the source scene.
    render_result_existed = bpy.data.images.get("Render Result") is not None
    try:
        for camera_ray, destination in enumerate(paths):
            world = _copy_world(scene.world, camera_ray, groups)
            worlds.append(world)
            bake.world = world
            target = engine_dirs * (1 if camera_ray else -1)
            blender_dirs = np.stack([target[..., 0], -target[..., 2], target[..., 1]], axis=-1)
            major = np.argmax(abs(blender_dirs), axis=-1)
            result = np.ones((height, width, 4), dtype=np.float32)
            for axis in range(3):
                for sign in [-1, 1]:
                    direction = Vector(tuple(sign if i == axis else 0 for i in range(3)))
                    camera.rotation_euler = direction.to_track_quat('-Z', 'Y').to_euler()
                    face_path = output / 'face.exr'
                    bake.render.filepath = str(face_path)
                    bpy.ops.render.render(scene=bake.name, write_still=True)
                    image = bpy.data.images.load(str(face_path), check_existing=False)
                    images.append(image)
                    pixels = np.asarray(image.pixels[:], dtype=np.float32).reshape(face_size, face_size, 4)
                    inverse = np.asarray(camera.rotation_euler.to_matrix().transposed(), dtype=float)
                    local = blender_dirs @ inverse.T
                    mask = (major == axis) & (blender_dirs[..., axis] * sign > 0)
                    view = local[mask]
                    x = np.clip((view[:, 0] / -view[:, 2] * .5 + .5) * face_size, 0, face_size - 1).astype(int)
                    y = np.clip((view[:, 1] / -view[:, 2] * .5 + .5) * face_size, 0, face_size - 1).astype(int)
                    result[mask] = pixels[y, x]
                    bpy.data.images.remove(image)
                    images.remove(image)
            if not np.isfinite(result).all():
                raise ValueError('World bake produced nonfinite radiance')
            image = bpy.data.images.new('AiluWorldLatlong', width=width, height=height, float_buffer=True)
            images.append(image)
            image.colorspace_settings.name = 'Linear Rec.709'
            image.pixels.foreach_set(result[::-1].copy().ravel())
            image.save_render(str(destination), scene=bake)
            bpy.data.images.remove(image)
            images.remove(image)
        return revision, paths
    finally:
        for image in images:
            bpy.data.images.remove(image)
        if not render_result_existed:
            render_result = bpy.data.images.get("Render Result")
            if render_result is not None:
                bpy.data.images.remove(render_result)
        bpy.data.objects.remove(camera, do_unlink=True)
        bpy.data.cameras.remove(data)
        bpy.data.scenes.remove(bake)
        for world in worlds:
            bpy.data.worlds.remove(world)
        for tree in reversed(groups):
            bpy.data.node_groups.remove(tree)


def export_world(context, link):
    from . import dirty_tracker
    signature = world_signature(context.scene)
    if signature is None:
        link.send_json(protocol.WORLD_UPSERT, {'enabled': False})
        return
    try:
        with dirty_tracker.export_scope(context):
            revision, paths = _bake(context.scene, signature)
        link.send_json(protocol.WORLD_UPSERT, {'enabled': True, 'revision': revision,
            'lighting_path': str(paths[0]), 'background_path': str(paths[1])})
    except ValueError as error:
        print('[BlenderLiveLink] World approximation unavailable:', error, flush=True)
        link.send_json(protocol.WORLD_UPSERT, {'enabled': False, 'diagnostic': str(error)})
