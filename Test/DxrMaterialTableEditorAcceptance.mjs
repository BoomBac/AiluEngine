import fs from 'node:fs';
import {Rpc, Write, Capture, Delay} from './TaaEditorAcceptanceClient.mjs';

if (process.env.AILU_WAIT_FOR_FULL_SYNC === '1') {
  let synced = false;
  for (let attempt = 0; attempt < 90; ++attempt) {
    const log = fs.readFileSync('FileAppender_log.txt', 'utf8');
    if (log.includes('FullSync import:')) {
      const state = await Rpc('editor.get_state');
      if (state.tlas_node_count > 0) { synced = true; break; }
    }
    await Delay(2000);
  }
  if (!synced) throw Error('Blender Full Sync did not produce traceable scene geometry.');
}
const settings = await Rpc('render.path_tracer_settings');
const camera = await Rpc('editor.get_scene_camera');
Write('original_settings.json', settings);
Write('original_camera.json', camera);
Write('state.json', await Rpc('editor.get_state'));
const meshes = await Rpc('scene.query_entities', {required_components: ['Ailu.ECS.StaticMeshComponent'], limit: 1});
if (!meshes.items.length) throw Error('Material validation requires scene mesh geometry; empty captures are invalid.');
try {
  if (process.env.AILU_REFERENCE_CAMERA) {
    const reference = JSON.parse(fs.readFileSync(process.env.AILU_REFERENCE_CAMERA, 'utf8'));
    await Rpc('editor.set_scene_camera', {position: reference.position, pitch_degrees: reference.pitch_degrees,
      yaw_degrees: reference.yaw_degrees, fov_degrees: reference.fov_degrees, aa_mode: reference.aa_mode, snap: true});
  }
  Write('camera.json', await Rpc('editor.get_scene_camera'));
  for (const [name, hardware] of [['dxr_initial', true], ['compute', false], ['dxr_return', true]]) {
    // Disable accumulation between controls to start each Reference run with fresh history.
    await Rpc('render.path_tracer_settings', {accumulation_mode: 'disabled'});
    Write(name + '_settings.json', await Rpc('render.path_tracer_settings', {enabled: true,
      hardware_ray_tracing: hardware, accumulation_mode: 'reference', debug_target: 0}));
    await Delay(15000);
    await Capture(name + '_reference', 'Debug GI', 4,
      {capture_output: false, access_index: 0, camera_ordinal: 0, output_format: 'png'});
    await Capture(name + '_present', 'FinalBlit', 1, {camera_ordinal: 0, output_format: 'png'});
  }
} finally {
  Write('restored_settings.json', await Rpc('render.path_tracer_settings', settings));
  if (process.env.AILU_REFERENCE_CAMERA) {
    await Rpc('editor.set_scene_camera', {position: camera.target_position, pitch_degrees: camera.pitch_degrees,
      yaw_degrees: camera.yaw_degrees, fov_degrees: camera.fov_degrees, aa_mode: camera.aa_mode,
      projection_type: camera.projection_type, orthographic_size: camera.orthographic_size, snap: true});
  }
}
