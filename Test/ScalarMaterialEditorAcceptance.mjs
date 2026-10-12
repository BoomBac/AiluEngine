import fs from 'node:fs';
import net from 'node:net';
import path from 'node:path';
import {randomUUID} from 'node:crypto';
import {Rpc, Write, Capture, Delay} from './TaaEditorAcceptanceClient.mjs';

const fixture = JSON.parse(fs.readFileSync(process.env.AILU_SCALAR_FIXTURE));
const deadline = Date.now() + 360000;
let socket;
while (!socket && Date.now() < deadline) {
  const candidate = net.createConnection(17777, '127.0.0.1');
  try {
    await new Promise((resolve, reject) => {candidate.once('connect', resolve); candidate.once('error', reject);});
    socket = candidate;
  } catch {candidate.destroy(); await Delay(25);}
}
if (!socket) throw Error('Live Link server startup timeout');
let received = Buffer.alloc(0), sequence = 0n;
socket.on('data', bytes => { received = Buffer.concat([received, bytes]); });
let camera;
function Send(type, value) {
  const payload = Buffer.isBuffer(value) ? value : Buffer.from(JSON.stringify(value));
  const header = Buffer.alloc(20);
  header.writeUInt32LE(0x4B4C4C41);
  header.writeUInt16LE(1, 4);
  header.writeUInt16LE(type, 6);
  header.writeUInt32LE(payload.length, 8);
  header.writeBigUInt64LE(++sequence, 12);
  socket.write(Buffer.concat([header, payload]));
}
try {
  Send(1, {});
  for (let attempt = 0; received.length < 20 && attempt < 50; ++attempt) await Delay(100);
  if (received.length < 20 || received.readUInt16LE(6) !== 2) throw Error('Handshake failed');
  if (process.env.AILU_EXPECTED_EDITOR_PID) {
    const session_path = path.join(process.argv[2], '.ailu/editor_session.json');
    let ready = false;
    while (!ready && Date.now() < deadline) {
      try {ready = JSON.parse(fs.readFileSync(session_path)).pid === Number(process.env.AILU_EXPECTED_EDITOR_PID);}
      catch {}
      if (!ready) await Delay(100);
    }
    if (!ready) throw Error('Editor automation startup timeout');
  }
  camera = await Rpc('editor.get_scene_camera');
  Write('original_camera.json', camera);
  const center = camera.position.map((value, index) => value + camera.forward[index] * 5);
  Send(10, {session_guid: randomUUID()});
  Send(21, fixture.material);
  Send(12, {...fixture.object, position: center, rotation: [0, 0, 0, 1], scale: [1, 1, 1]});
  Send(20, Buffer.from(fixture.mesh, 'base64'));
  Send(11, {});
  await Rpc('editor.set_scene_camera', {aa_mode: 0, snap: true});
  await Delay(2500);
  Write('entity.json', await Rpc('scene.inspect_entity', {entity_guid: fixture.object.guid, include_properties: true}));
  const results = [];
  for (const [name, roughness_map, metallic_map] of [
    ['both', true, true], ['roughness_only', true, false], ['metallic_only', false, true], ['neither', false, false]
  ]) {
    const material = {...fixture.material, roughness: 0.5, metallic: 0.4,
      base_color: [1, 1, 1, 1], alpha_test: false, alpha_blend: false,
      roughness_path: roughness_map ? fixture.material.roughness_path : '',
      metallic_path: metallic_map ? fixture.material.metallic_path : ''};
    Send(21, material);
    await Delay(1200);
    Write(name + '_material.json', material);
    for (const [channel, resource_name, access_index] of [['roughness', '_GBuffer1', 1], ['metallic', '_GBuffer2', 2]])
      await Capture(name + '_' + channel, 'DeferedGeometryPass', 3,
        {resource_name, access_index, camera_ordinal: 0});
    results.push({name, roughness: roughness_map ? 0.125 : 0.5, metallic: metallic_map ? 0.3 : 0.4});
  }
  Write('expected.json', results);
} finally {
  socket.end();
  if (camera) await Rpc('editor.set_scene_camera', {position: camera.target_position, pitch_degrees: camera.pitch_degrees,
    yaw_degrees: camera.yaw_degrees, fov_degrees: camera.fov_degrees, aa_mode: camera.aa_mode,
    projection_type: camera.projection_type, orthographic_size: camera.orthographic_size, snap: true});
}
