import fs from 'node:fs';
import net from 'node:net';
import path from 'node:path';

const [projectPath, outputPath, expectedPidText, appliedSentinel] = process.argv.slice(2);
if (!projectPath || !outputPath || !expectedPidText || !appliedSentinel)
  throw Error('Usage: capture_m2_render.mjs PROJECT OUTPUT EDITOR_PID APPLIED_SENTINEL');
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
const project = path.resolve(projectPath);
const output = path.resolve(outputPath);
const expectedPid = Number(expectedPidText);
const sessionPath = path.join(project, '.ailu/editor_session.json');
const sentinelPath = path.resolve(appliedSentinel);
fs.mkdirSync(output, {recursive: true});
let requestId = 0;

async function rpc(method, arguments_ = {}) {
  const session = JSON.parse(fs.readFileSync(sessionPath, 'utf8'));
  return await new Promise((resolve, reject) => {
    const pipe = net.createConnection('\\\\.\\pipe\\' + session.session_id);
    let buffer = Buffer.alloc(0);
    const timer = setTimeout(() => pipe.destroy(Error('RPC timeout: ' + method)), 20000);
    const finish = (error, value) => {
      clearTimeout(timer);
      pipe.destroy();
      if (error) reject(error); else resolve(value);
    };
    pipe.once('error', error => finish(error));
    pipe.once('connect', () => {
      const body = Buffer.from(JSON.stringify({request_id: ++requestId, method, arguments: arguments_,
        context: {caller: 'Blender Link M2 render oracle', allow_write: true,
          allow_destructive: true, interactive: false}}));
      const header = Buffer.alloc(4);
      header.writeUInt32LE(body.length);
      pipe.write(Buffer.concat([header, body]));
    });
    pipe.on('data', bytes => {
      buffer = Buffer.concat([buffer, bytes]);
      if (buffer.length < 4 || buffer.length < buffer.readUInt32LE(0) + 4) return;
      const response = JSON.parse(buffer.subarray(4, 4 + buffer.readUInt32LE(0)).toString('utf8'));
      if (!response.success) finish(Error(JSON.stringify(response)));
      else finish(null, response.data);
    });
  });
}

async function waitFor(predicate, timeout, label) {
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) {
    if (predicate()) return;
    await delay(50);
  }
  throw Error('Timed out waiting for ' + label);
}

async function capture(name, pass, extra = {}) {
  const directory = path.join(output, 'captures', name);
  await rpc('render.capture_pass', {pass_name: pass, frame_count: 3, access_index: 0,
    camera_ordinal: 0, output_format: 'dds_native', output_directory: directory,
    file_prefix: name, ...extra});
  let status;
  for (let attempt = 0; attempt < 240; ++attempt) {
    status = await rpc('render.capture_status');
    if (status.state === 'failed' || status.state === 'cancelled')
      throw Error('Capture failed: ' + JSON.stringify(status));
    if (status.state === 'completed') break;
    await delay(250);
  }
  if (status?.state !== 'completed' || status.completed_frames !== 3 || status.files?.length !== 3)
    throw Error('Capture did not complete three frames: ' + JSON.stringify(status));
  return status;
}

let originalCamera;
let testCamera;
let restoredCamera;
const captures = {};
let failure = null;
try {
  await waitFor(() => {
    try { return JSON.parse(fs.readFileSync(sessionPath, 'utf8')).pid === expectedPid; }
    catch { return false; }
  }, 30000, 'owned Editor automation session');
  await waitFor(() => fs.existsSync(sentinelPath), 180000, 'applied Full Sync End sentinel');
  originalCamera = await rpc('editor.get_scene_camera');
  await rpc('editor.set_scene_camera', {position: [0.0, 0.0, -8.0], pitch_degrees: 0.0,
    yaw_degrees: 0.0, fov_degrees: 60.0, snap: true});
  await delay(1800);
  testCamera = await rpc('editor.get_scene_camera');
  captures.gbuffer_normal = await capture('gbuffer_normal', 'DeferedGeometryPass',
    {resource_name: '_GBuffer0', access_index: 0});
  captures.gbuffer_albedo = await capture('gbuffer_albedo', 'DeferedGeometryPass',
    {resource_name: '_GBuffer1', access_index: 1});
  captures.final_color = await capture('final_color', 'TransparentPass', {access_index: 0});
} catch (error) {
  failure = String(error);
} finally {
  if (originalCamera) {
    try {
      await rpc('editor.set_scene_camera', {position: originalCamera.target_position,
        pitch_degrees: originalCamera.pitch_degrees, yaw_degrees: originalCamera.yaw_degrees,
        fov_degrees: originalCamera.fov_degrees, snap: true});
      restoredCamera = await rpc('editor.get_scene_camera');
    } catch (error) {
      failure = failure || ('Camera restoration failed: ' + String(error));
    }
  }
}

const record = {status: failure ? 'FAIL' : 'PASS', failure, editor_pid: expectedPid,
  original_camera: originalCamera, test_camera: testCamera, restored_camera: restoredCamera, captures};
fs.writeFileSync(path.join(output, 'm2_capture.json'), JSON.stringify(record, null, 2));
console.log(JSON.stringify({status: record.status, failure, captures: Object.keys(captures)}));
if (failure) process.exitCode = 1;
