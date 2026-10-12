import fs from 'node:fs';
import {Rpc, Write, Capture, Delay} from './TaaEditorAcceptanceClient.mjs';

const camera = await Rpc('editor.get_scene_camera');
const original_settings = await Rpc('render.path_tracer_settings');
Write('original_camera.json', camera);
Write('original_settings.json', original_settings);
try {
  // The running Blender BMW session reconnects and performs Full Sync.
  // Wait for the completed batch instead of starting a competing Live Link client.
  let synced = false;
  for (let attempt = 0; attempt < 90; ++attempt) {
    if (fs.readFileSync('FileAppender_log.txt', 'utf8').includes('FullSync import:')) {
      synced = true;
      break;
    }
    await Delay(1000);
  }
  if (!synced) throw Error('No completed Blender Full Sync observed');
  await Delay(3000);
  Write('cars.json', await Rpc('scene.query_entities', {name: 'carShell', name_match: 'prefix', limit: 100}));
  Write('scene.json', await Rpc('scene.query_entities', {limit: 500}));
  Write('state.json', await Rpc('editor.get_state'));
  await Rpc('editor.set_scene_camera', {position: [-5, 2, 5], pitch_degrees: 12, yaw_degrees: 135,
    aa_mode: 2, snap: true});
  Write('camera.json', await Rpc('editor.get_scene_camera'));
  Write('settings.json', await Rpc('render.path_tracer_settings', {enabled: true, hardware_ray_tracing: true,
    debug_target: 0, output_size: [896, 512], accumulation_mode: 'disabled'}));
  // Full Sync completion precedes GPU mesh/TLAS readiness. Warm up before collecting the first mode.
  await Delay(30000);
  for (const mode of ['disabled', 'realtime', 'reference']) {
    Write(mode + '_settings.json', await Rpc('render.path_tracer_settings', {accumulation_mode: mode}));
    await Delay(1000);
    await Capture(mode + '_early', 'Debug GI', 16,
      {capture_output: false, access_index: 0, camera_ordinal: 0});
    await Capture(mode + '_raw', 'Debug GI', 16,
      {capture_output: false, access_index: 1, camera_ordinal: 0});
    await Delay(30000);
    await Capture(mode + '_late', 'Debug GI', 32,
      {capture_output: false, access_index: 0, camera_ordinal: 0});
    await Capture(mode + '_present', 'FinalBlit', 3, {camera_ordinal: 0, output_format: 'png'});
    if (mode !== 'disabled') await Capture(mode + '_length', 'Denoise', 8,
      {capture_output: true, access_index: 2, camera_ordinal: 0});
    Write(mode + '_observed.json', await Rpc('render.path_tracer_settings'));
  }
  Write('completed.json', {scope: 'Live Blender BMW scene, DXR, three accumulation modes'});
} finally {
  await Rpc('render.path_tracer_settings', original_settings);
  await Rpc('editor.set_scene_camera', {position: camera.target_position, pitch_degrees: camera.pitch_degrees,
    yaw_degrees: camera.yaw_degrees, fov_degrees: camera.fov_degrees, aa_mode: camera.aa_mode,
    projection_type: camera.projection_type, orthographic_size: camera.orthographic_size, snap: true});
}
