import fs from 'node:fs';
import net from 'node:net';
import {randomUUID} from 'node:crypto';
import {Rpc, Write, Capture, Delay} from './TaaEditorAcceptanceClient.mjs';

const fixture = JSON.parse(fs.readFileSync('Saved/BlenderTextureValidation/native-graph-fixture/fixture.json'));
const camera = await Rpc('editor.get_scene_camera');
Write('original_camera.json', camera);
const socket = net.createConnection(17777, '127.0.0.1');
await new Promise((resolve, reject) => {socket.once('connect', resolve); socket.once('error', reject);});
let received = Buffer.alloc(0);
socket.on('data', bytes => {received = Buffer.concat([received, bytes]);});
let sequence = 0n;
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
  const center = camera.position.map((value, index) => value + camera.forward[index] * 5);
  Send(10, {session_guid: randomUUID()});
  Send(21, fixture.materials.blue);
  Send(12, {...fixture.object, position: center, rotation: [0, 0, 0, 1], scale: [1, 1, 1]});
  Send(20, Buffer.from(fixture.mesh, 'base64'));
  Send(11, {});
  await Rpc('editor.set_scene_camera', {aa_mode: 0, snap: true});
  await Delay(2500);
  Write('entity.json', await Rpc('scene.inspect_entity', {entity_guid: fixture.object.guid, include_properties: true}));
  const materials = {...fixture.materials, constant: {...fixture.materials.red,
    base_color: [0, 1, 0, 1], emission: [0, 1, 0], base_color_path: '', emission_path: '',
    normal_path: '', roughness_metallic_path: '', alpha_test: false, alpha_blend: false}};
  for (const name of ['blue', 'red', 'blend', 'cutout', 'constant']) {
    Send(21, materials[name]);
    await Delay(1800);
    Write(name + '_material.json', materials[name]);
    await Capture(name + '_gbuffer', 'DeferedGeometryPass', 3,
      {resource_name: '_GBuffer1', access_index: 1, camera_ordinal: 0, output_format: 'png'});
    await Capture(name + '_final', 'TransparentPass', 3, {camera_ordinal: 0, output_format: 'png'});
  }
  Write('completion.json', {scope: 'Blender baked cube, production mesh payload, opaque blue/red and continuous opacity'});
} finally {
  socket.end();
  await Rpc('editor.set_scene_camera', {position: camera.target_position, pitch_degrees: camera.pitch_degrees,
    yaw_degrees: camera.yaw_degrees, fov_degrees: camera.fov_degrees, aa_mode: camera.aa_mode,
    projection_type: camera.projection_type, orthographic_size: camera.orthographic_size, snap: true});
}
