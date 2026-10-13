import fs from 'node:fs';
import {Rpc, Write, Capture, Delay} from './TaaEditorAcceptanceClient.mjs';

const mode = process.argv[4] ?? 'ndf';
const session = process.argv[2] + '/.ailu/editor_session.json';
let ready = false;
for (let attempt = 0; !ready && attempt < 180; ++attempt) {
  try {
    if (fs.existsSync(session)) { await Rpc('editor.get_state'); ready = true; }
  } catch (error) {
    if (error.message.startsWith('{')) throw error;
  }
  if (!ready) await Delay(1000);
}
if (!ready) throw Error('Editor automation startup timeout');
Write(mode + '_state.json', await Rpc('editor.get_state'));
Write(mode + '_camera.json', await Rpc('editor.set_scene_camera', {position: [0, 1.7, -6],
  pitch_degrees: 6, yaw_degrees: 0, fov_degrees: 55, aa_mode: 0, fsr_enabled: false,
  dlss_enabled: false, snap: true}));
Write(mode + '_settings.json', await Rpc('render.path_tracer_settings', {enabled: true,
  hardware_ray_tracing: !mode.includes('compute'), debug_target: 0,
  output_size: [640, 360], accumulation_mode: 'disabled'}));
await Delay(4000);
// Offline averaging of raw samples gives exact equal-spp comparisons without depending on capture start latency.
await Capture(mode + '_raw', 'Debug GI', mode.includes('compute') ? 8 : 256,
  {capture_output: false, access_index: 1, camera_ordinal: 0});
await Capture(mode + '_present', 'FinalBlit', 1, {output_format: 'png', camera_ordinal: 0});
Write(mode + '_completed.json', await Rpc('render.path_tracer_settings'));
