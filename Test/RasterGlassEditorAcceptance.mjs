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
  '--python', path.resolve('Test/ExportBmwGlassLive.py'), '--', output],
  {windowsHide: true, stdio: ['ignore', log, log]});
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
  Write('settings.json', await Rpc('render.path_tracer_settings', {enabled: false, debug_target: 0}));
  await Rpc('editor.set_scene_camera', {position: [-5, 2, 5], pitch_degrees: 12, yaw_degrees: 135,
    aa_mode: 0, snap: true});
  await Delay(45000);
  Write('features.json', await Rpc('render.list_features'));
  Write('timings.json', await Rpc('render.gpu_timings'));
  if (process.env.AILU_RASTER_WHEEL_DIAG === '1') {
    await Rpc('editor.set_scene_camera', {position: [-4, 1.7, 4], pitch_degrees: 12, yaw_degrees: 135,
      aa_mode: 0, snap: true});
    await Delay(3000);
    await Capture('wheel_fixed_gbuffer', 'DeferedGeometryPass', 8, {capture_output: true, access_index: 1});
    await Capture('wheel_fixed_present', 'FinalBlit', 4, {camera_ordinal: 0, output_format: 'png'});
    fs.writeFileSync(path.join(output, 'wheel_control_request'), 'request');
    for (let attempt = 0; attempt < 100 && !fs.existsSync(path.join(output, 'wheel_control_ready')); ++attempt)
      await Delay(100);
    if (!fs.existsSync(path.join(output, 'wheel_control_ready'))) throw Error('Wheel control timeout');
    await Delay(3000);
    await Capture('wheel_old_gbuffer', 'DeferedGeometryPass', 8, {capture_output: true, access_index: 1});
    await Capture('wheel_old_present', 'FinalBlit', 4, {camera_ordinal: 0, output_format: 'png'});
    Write('completed.json', {scope: 'BMW TireRubber roughness 1 / 0, unchanged geometry, camera and wheel rim'});
  } else if (process.env.AILU_RASTER_LAMP_DIAG === '1') {
    for (const [name, position, pitch, yaw] of [
      ['original', [-5, 2, 5], 12, 135],
      ['closer', [-4, 1.7, 4], 12, 135],
      ['frontal', [-2.5, 1.7, 5], 12, 150],
      ['side', [-5, 1.7, 3.4], 12, 120]]) {
      await Rpc('editor.set_scene_camera', {position, pitch_degrees: pitch, yaw_degrees: yaw, aa_mode: 0, snap: true});
      await Delay(3000);
      Write(name + '_camera.json', await Rpc('editor.get_scene_camera'));
      await Capture(name + '_snapshot', 'ThinGlassSceneColor', 4, {camera_ordinal: 0, capture_output: true});
      await Capture(name + '_present', 'FinalBlit', 4, {camera_ordinal: 0, output_format: 'png'});
    }
    Write('completed.json', {scope: 'BMW lamp: upper/lower transmission at four camera angles'});
  } else if (process.env.AILU_RASTER_GLASS_CONTROL === 'opaque') {
    await Capture('opaque_noaa', 'CopyColor', 16, {camera_ordinal: 0, capture_output: true, access_index: 0});
    await Capture('opaque_copycolor', 'CopyColor', 8, {camera_ordinal: 0, capture_output: true, access_index: 0});
    await Capture('opaque_gizmo_input', 'GizmoPass', 8, {camera_ordinal: 0, capture_output: false, access_index: 0});
    await Capture('opaque_gizmo_output', 'GizmoPass', 8, {camera_ordinal: 0, capture_output: true, access_index: 0});
    await Capture('opaque_final_input', 'FinalBlit', 8, {camera_ordinal: 0, capture_output: false, access_index: 0});
    await Capture('opaque_final_native', 'FinalBlit', 8, {camera_ordinal: 0, capture_output: true, access_index: 0});
    await Capture('opaque_noaa_present', 'FinalBlit', 3, {camera_ordinal: 0, output_format: 'png'});
    Write('completed.json', {scope: 'Fresh BMW raster opaque control; geometry/camera/light unchanged'});
  } else {
  await Capture('glass_snapshot', 'ThinGlassSceneColor', 8, {camera_ordinal: 0, capture_output: true});
  await Capture('glass_noaa', 'CopyColor', 16, {camera_ordinal: 0, capture_output: true, access_index: 0});
  await Capture('glass_noaa_present', 'FinalBlit', 3, {camera_ordinal: 0, output_format: 'png'});
  fs.writeFileSync(path.join(output, 'opaque_baseline_request'), 'request');
  for (let attempt = 0; attempt < 100 && !fs.existsSync(path.join(output, 'opaque_baseline_ready')); ++attempt)
    await Delay(100);
  if (!fs.existsSync(path.join(output, 'opaque_baseline_ready'))) throw Error('Opaque control timeout');
  await Delay(15000);
  await Capture('opaque_noaa', 'CopyColor', 16, {camera_ordinal: 0, capture_output: true, access_index: 0});
  await Capture('opaque_noaa_present', 'FinalBlit', 3, {camera_ordinal: 0, output_format: 'png'});
  fs.writeFileSync(path.join(output, 'glass_restore_request'), 'request');
  for (let attempt = 0; attempt < 100 && !fs.existsSync(path.join(output, 'glass_restore_ready')); ++attempt)
    await Delay(100);
  if (!fs.existsSync(path.join(output, 'glass_restore_ready'))) throw Error('Glass restore timeout');
  await Rpc('editor.set_scene_camera', {aa_mode: 2, snap: true});
  await Delay(15000);
  await Capture('glass_taa', 'CopyColor', 16, {camera_ordinal: 0, capture_output: true, access_index: 0});
  await Capture('glass_taa_present', 'FinalBlit', 3, {camera_ordinal: 0, output_format: 'png'});
  Write('completed.json', {scope: 'Actual BMW raster thin glass, AA off/TAA, same-scene opaque control'});
  }
} finally {
  fs.writeFileSync(path.join(output, 'stop_blender'), 'stop');
  await Rpc('render.path_tracer_settings', original_settings);
  await Rpc('editor.set_scene_camera', {position: camera.target_position, pitch_degrees: camera.pitch_degrees,
    yaw_degrees: camera.yaw_degrees, fov_degrees: camera.fov_degrees, aa_mode: camera.aa_mode,
    projection_type: camera.projection_type, orthographic_size: camera.orthographic_size, snap: true});
  Write('restored_camera.json', await Rpc('editor.get_scene_camera'));
  Write('restored_settings.json', await Rpc('render.path_tracer_settings'));
  for (let attempt = 0; attempt < 30 && blender.exitCode === null; ++attempt) await Delay(200);
  if (blender.exitCode === null) blender.kill();
  fs.closeSync(log);
}
