import fs from 'node:fs';
import net from 'node:net';
import path from 'node:path';

const [project, output] = process.argv.slice(2);
if (!project || !output) throw Error('Usage: node TaaRenderGraphEditorAcceptance.mjs PROJECT OUTPUT');
fs.mkdirSync(output, {recursive: true});
const Write = (name, value) => fs.writeFileSync(path.join(output, name), JSON.stringify(value, null, 2));
const Delay = ms => new Promise(resolve => setTimeout(resolve, ms));
let request_id = 0;
async function Attempt(method, arguments_ = {}) {
  const session = JSON.parse(fs.readFileSync(path.join(project, '.ailu/editor_session.json')));
  return await new Promise((resolve, reject) => {
    const socket = net.createConnection('\\\\.\\pipe\\' + session.session_id);
    let received = Buffer.alloc(0);
    socket.setTimeout(15000, () => socket.destroy(Error('RPC timeout: ' + method)));
    socket.on('error', reject);
    socket.on('connect', () => {
      const body = Buffer.from(JSON.stringify({request_id: ++request_id, method, arguments: arguments_,
        context: {caller: 'M4 acceptance', allow_write: true, allow_destructive: true, interactive: false}}));
      const header = Buffer.alloc(4);
      header.writeUInt32LE(body.length);
      socket.write(Buffer.concat([header, body]));
    });
    socket.on('data', data => {
      received = Buffer.concat([received, data]);
      if (received.length < 4 || received.length < 4 + received.readUInt32LE(0)) return;
      const result = JSON.parse(received.subarray(4, 4 + received.readUInt32LE(0)));
      socket.end();
      if (!result.success) reject(Error(JSON.stringify(result))); else resolve(result.data);
    });
  });
}
async function Rpc(method, arguments_ = {}) {
  for (let retry = 0; retry < 8; ++retry) {
    try { return await Attempt(method, arguments_); }
    catch (error) {
      if (retry === 7 || error.message.startsWith('{')) throw error;
      await Delay(100);
    }
  }
}
async function Capture(name, pass, count = 8, extra = {}) {
  const start = Date.now();
  await Rpc('render.capture_pass', {pass_name: pass, frame_count: count,
    output_directory: path.join(output, name), file_prefix: name, output_format: 'dds_native', ...extra});
  for (let attempt = 0; attempt < 120; ++attempt) {
    const status = await Rpc('render.capture_status');
    if (status.state === 'failed') throw Error(JSON.stringify(status));
    if (status.state === 'completed') {
      status.elapsed_ms = Date.now() - start;
      Write(name + '_status.json', status);
      console.log(name, status.completed_frames, status.elapsed_ms);
      return;
    }
    await Delay(200);
  }
  throw Error('Capture timeout: ' + name);
}

const before = await Rpc('editor.get_scene_camera');
const features = await Rpc('render.list_features');
Write('original_camera.json', before);
Write('original_features.json', features);
Write('original_scene.json', await Rpc('editor.get_state'));
try {
  for (const feature of features.features)
    if (['VolumetricFog', 'DDGI', 'SurfelGI', 'SSAO', 'SpriteRenderFeature'].includes(feature.name))
      await Rpc('render.set_feature_active', {name: feature.name, active: false});
  await Rpc('editor.set_scene_camera', {aa_mode: 2});
  await Delay(500);
  Write('taa_camera.json', await Rpc('editor.get_scene_camera'));
  await Capture('taa_output', 'TemporalAA', 64);
  for (const [name, index] of [['current', 0], ['depth', 1], ['motion', 2], ['history', 3]])
    await Capture('taa_' + name, 'TemporalAA', 8, {capture_output: false, access_index: index});
  await Capture('copycolor', 'CopyColor', 8);
  await Capture('bloom_input', 'BloomDownsample_0', 8, {capture_output: false});
  await Capture('tonemapping', 'ToneMapping', 8, {output_format: 'png'});
  await Rpc('editor.set_scene_camera', {aa_mode: 0});
  await Capture('off_copycolor', 'CopyColor', 8);
  await Rpc('editor.set_scene_camera', {aa_mode: 2});
  await Capture('toggle_output', 'TemporalAA', 16);
  await Rpc('editor.set_scene_camera', {position: before.target_position.map((value, index) => value + (index === 0 ? 5 : 0)), snap: true});
  await Capture('cut_output', 'TemporalAA', 16);
  await Rpc('editor.set_scene_camera', {position: before.target_position, snap: true});
  await Capture('return_output', 'TemporalAA', 16);
} finally {
  Write('restored_camera.json', await Rpc('editor.set_scene_camera', {position: before.target_position,
    pitch_degrees: before.pitch_degrees, yaw_degrees: before.yaw_degrees, fov_degrees: before.fov_degrees,
    aa_mode: before.aa_mode, projection_type: before.projection_type, orthographic_size: before.orthographic_size, snap: true}));
  for (const feature of features.features)
    await Rpc('render.set_feature_active', {name: feature.name, active: feature.active});
  Write('restored_scene.json', await Rpc('editor.get_state'));
}
