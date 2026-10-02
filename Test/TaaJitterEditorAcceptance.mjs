import path from 'node:path';
import {Rpc, Write, Capture, Delay} from './TaaEditorAcceptanceClient.mjs';

const camera = await Rpc('editor.get_scene_camera');
Write('original_camera.json', camera);
Write('original_features.json', await Rpc('render.list_features'));
Write('original_state.json', await Rpc('editor.get_state'));
try {
  for (const aa_mode of [0, 2]) {
    await Rpc('editor.set_scene_camera', {aa_mode, position: camera.target_position,
      pitch_degrees: camera.pitch_degrees, yaw_degrees: camera.yaw_degrees, snap: true});
    await Delay(2500);
    Write('camera_aa' + aa_mode + '.json', await Rpc('editor.get_scene_camera'));
    await Capture('tone_aa' + aa_mode, 'ToneMapping', 64, {camera_ordinal: 0, output_format: 'png'});
    if (aa_mode === 2) {
      await Capture('taa_output', 'TemporalAA', 64, {camera_ordinal: 0});
      await Capture('taa_stable', 'TemporalAAStabilize', 64, {camera_ordinal: 0});
      await Capture('taa_current', 'TemporalAA', 16, {camera_ordinal: 0, capture_output: false, access_index: 0});
      await Capture('taa_motion', 'TemporalAA', 16, {camera_ordinal: 0, capture_output: false, access_index: 2});
      await Capture('taa_reactive', 'TemporalAA', 16, {camera_ordinal: 0, access_index: 1});
    }
  }
  const output = process.argv[3];
  await Rpc('render.capture_pass', {pass_name: 'ToneMapping', frame_count: 128, camera_ordinal: 0,
    output_format: 'png', output_directory: path.join(output, 'camera_motion'), file_prefix: 'camera_motion'});
  const observations = [];
  for (let step = 0; step <= 20; ++step) {
    const offset = .25 * (1 - Math.cos(2 * Math.PI * step / 20));
    observations.push(await Rpc('editor.set_scene_camera', {position: camera.target_position.map((v, i) =>
      v + (i === 0 ? offset : 0)), snap: false}));
    await Delay(100);
  }
  await Rpc('editor.set_scene_camera', {position: camera.target_position, snap: true});
  Write('motion_camera_observations.json', observations);
  let completed = false;
  for (let attempt = 0; attempt < 120; ++attempt) {
    const status = await Rpc('render.capture_status');
    if (status.state === 'failed') throw Error(JSON.stringify(status));
    if (status.state === 'completed') {
      Write('camera_motion_status.json', status);
      completed = true;
      break;
    }
    await Delay(200);
  }
  if (!completed) throw Error('Camera-motion capture timeout');
  await Delay(1500);
  await Capture('tone_return', 'ToneMapping', 32, {camera_ordinal: 0, output_format: 'png'});
  Write('completion.json', {scope: 'Original SceneView, AA off/on, smooth lateral camera sweep and settling.'});
} finally {
  Write('restored_camera.json', await Rpc('editor.set_scene_camera', {position: camera.target_position,
    pitch_degrees: camera.pitch_degrees, yaw_degrees: camera.yaw_degrees, fov_degrees: camera.fov_degrees,
    aa_mode: camera.aa_mode, projection_type: camera.projection_type,
    orthographic_size: camera.orthographic_size, snap: true}));
  Write('restored_state.json', await Rpc('editor.get_state'));
}
