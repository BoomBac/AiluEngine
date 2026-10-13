import path from 'node:path';
import {Rpc, Write, Capture, Delay} from './TaaEditorAcceptanceClient.mjs';
const output = process.argv[3];
const camera = await Rpc('editor.get_scene_camera');
Write('original_camera.json', camera);
Write('original_state.json', await Rpc('editor.get_state'));
const start = {position: [5, 4, -7], pitch_degrees: 23, yaw_degrees: -35, fov_degrees: 60, aa_mode: 0, snap: true};
const observations = [];
async function WaitCapture(name) {
  for (let i = 0; i < 180; ++i) {
    const status = await Rpc('render.capture_status');
    if (status.state === 'failed') throw Error(JSON.stringify(status));
    if (status.state === 'completed') { Write(name + '_status.json', status); return; }
    await Delay(200);
  }
  throw Error('Timed out: ' + name);
}
async function Motion(name, extra = {}) {
  await Rpc('editor.set_scene_camera', start);
  await Delay(800);
  await Rpc('render.capture_pass', {pass_name: 'FinalBlit', frame_count: 128, output_format: 'png',
    output_directory: path.join(output, name), file_prefix: name, ...extra});
  for (let i = 0; i < 24; ++i) {
    const phase = i / 23 * Math.PI * 2;
    const result = await Rpc('editor.set_scene_camera', {position: [5 + Math.sin(phase) * 1.5, 4, -7],
      pitch_degrees: 23, yaw_degrees: -35 + Math.sin(phase) * 8, snap: false});
    observations.push({name, step: i, time: Date.now(), camera: result});
    await Delay(65);
  }
  await WaitCapture(name);
  Write('motion_observations.json', observations);
}
try {
  Write('scene_entities.json', await Rpc('scene.query_entities', {limit: 100}));
  await Rpc('editor.set_scene_camera', {...start, fsr_enabled: false});
  await Delay(2500);
  await Capture('off_final', 'FinalBlit', 3, {output_format: 'png'});
  await Capture('off_depth', 'CopyDepthPass', 3);
  await Motion('off_motion');
  await Rpc('editor.set_scene_camera', {...start, fsr_enabled: true});
  await Delay(2500);
  const active = await Rpc('editor.get_scene_camera');
  Write('fsr_active_camera.json', active);
  if (!active.fsr_active) throw Error('FSR was not activated');
  Write('fsr_timings.json', await Rpc('render.gpu_timings'));
  Write('fsr_features.json', await Rpc('render.list_features'));
  await Capture('fsr_final', 'FinalBlit', 3, {output_format: 'png'});
  await Capture('fsr_depth', 'CopyDepthPass', 3);
  await Capture('fsr_color_before_overlay', 'SuperResolutionEvaluate', 3);
  await Motion('fsr_motion');
  await Rpc('editor.set_scene_camera', {...start, fsr_enabled: false, aa_mode: 2});
  await Delay(1500);
  await Capture('taa_final', 'FinalBlit', 3, {output_format: 'png'});
  Write('taa_camera.json', await Rpc('editor.get_scene_camera'));
  Write('completed.json', {scope: 'FSR on/off camera sweeps, native input depth/HDR output and TAA restoration'});
} catch (error) {
  Write('failure.json', {error: String(error), capture_status: await Rpc('render.capture_status')});
  console.log('FAILED', String(error));
  process.exitCode = 1;
} finally {
  await Rpc('editor.set_scene_camera', {position: camera.target_position, pitch_degrees: camera.pitch_degrees,
    yaw_degrees: camera.yaw_degrees, fov_degrees: camera.fov_degrees, aa_mode: camera.aa_mode,
    fsr_enabled: camera.fsr_enabled, snap: true});
  Write('restored_camera.json', await Rpc('editor.get_scene_camera'));
}
