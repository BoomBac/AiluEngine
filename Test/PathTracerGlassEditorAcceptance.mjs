import fs from 'node:fs';
import path from 'node:path';
import {spawn} from 'node:child_process';
import {Rpc, Write, Capture, Delay} from './TaaEditorAcceptanceClient.mjs';
const output = process.argv[3];
const camera = await Rpc('editor.get_scene_camera');
const original_settings = await Rpc('render.path_tracer_settings');
Write('original_camera.json', camera);
Write('original_settings.json', original_settings);
const log = fs.openSync(path.join(output, 'blender_export.log'), 'w');
const blender = spawn('D:/blender5.0/blender.exe', ['--background', 'I:/Downloads/BMW27.blend/BMW27.blend',
  '--python', path.resolve('Test/ExportBmwGlassLive.py'), '--', output], {windowsHide: true, stdio: ['ignore', log, log]});
try {
  let synced = false;
  for (let attempt = 0; attempt < 600; ++attempt) {
    if (fs.existsSync(path.join(output, 'export_completed.json')) &&
        fs.readFileSync('FileAppender_log.txt', 'utf8').includes('FullSync import:')) { synced = true; break; }
    if (blender.exitCode !== null) throw Error('Blender exporter exited ' + blender.exitCode);
    await Delay(1000);
  }
  if (!synced) throw Error('BMW Full Sync timeout');
  Write('scene.json', await Rpc('scene.query_entities', {limit: 500}));
  await Rpc('editor.set_scene_camera', {position: [-5, 2, 5], pitch_degrees: 12, yaw_degrees: 135,
    aa_mode: 2, snap: true});
  Write('settings.json', await Rpc('render.path_tracer_settings', {enabled: true, hardware_ray_tracing: true,
    debug_target: 0, output_size: [896, 512], accumulation_mode: 'reference'}));
  await Delay(60000);
  await Capture('reference_late', 'Debug GI', 32, {capture_output: false, access_index: 0, camera_ordinal: 0});
  await Capture('reference_present', 'FinalBlit', 3, {camera_ordinal: 0, output_format: 'png'});
  await Capture('reference_length', 'Denoise', 8, {capture_output: true, access_index: 2, camera_ordinal: 0});
  // Controlled old material semantics, in the same geometry/camera/light setup.
  fs.writeFileSync(path.join(output, 'opaque_baseline_request'), 'request');
  for (let attempt = 0; attempt < 100 && !fs.existsSync(path.join(output, 'opaque_baseline_ready')); ++attempt)
    await Delay(100);
  if (!fs.existsSync(path.join(output, 'opaque_baseline_ready'))) throw Error('Opaque material control timeout');
  await Rpc('render.path_tracer_settings', {accumulation_mode: 'disabled'});
  await Delay(1000);
  await Rpc('render.path_tracer_settings', {accumulation_mode: 'reference'});
  await Delay(60000);
  await Capture('opaque_late', 'Debug GI', 32, {capture_output: false, access_index: 0, camera_ordinal: 0});
  await Capture('opaque_present', 'FinalBlit', 3, {camera_ordinal: 0, output_format: 'png'});
  Write('completed.json', {scope: 'Actual BMW exported from current checkout, DXR Reference, primary glass transport'});
} finally {
  fs.writeFileSync(path.join(output, 'stop_blender'), 'stop');
  await Rpc('render.path_tracer_settings', original_settings);
  await Rpc('editor.set_scene_camera', {position: camera.target_position, pitch_degrees: camera.pitch_degrees,
    yaw_degrees: camera.yaw_degrees, fov_degrees: camera.fov_degrees, aa_mode: camera.aa_mode,
    projection_type: camera.projection_type, orthographic_size: camera.orthographic_size, snap: true});
  for (let attempt = 0; attempt < 30 && blender.exitCode === null; ++attempt) await Delay(200);
  if (blender.exitCode === null) blender.kill();
  fs.closeSync(log);
}
