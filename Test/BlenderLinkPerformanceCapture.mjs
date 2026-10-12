import fs from 'node:fs';
import path from 'node:path';
import {Rpc, Write, Capture, Delay} from './TaaEditorAcceptanceClient.mjs';

const output = process.argv[3];
const camera = await Rpc('editor.get_scene_camera');
Write('original_camera.json', camera);
try {
  const pose = JSON.parse(fs.readFileSync(path.join(output, 'camera.json'), 'utf8'));
  await Rpc('editor.set_scene_camera', pose);
  await Delay(2500);
  Write('test_camera.json', await Rpc('editor.get_scene_camera'));
  await Capture('geometry', 'DeferedGeometryPass', 3,
    {resource_name: '_GBuffer1', access_index: 1, camera_ordinal: 0, output_format: 'png'});
  await Capture('final', 'TransparentPass', 3, {camera_ordinal: 0, output_format: 'png'});
} finally {
  await Rpc('editor.set_scene_camera', {position: camera.target_position, pitch_degrees: camera.pitch_degrees,
    yaw_degrees: camera.yaw_degrees, fov_degrees: camera.fov_degrees, aa_mode: camera.aa_mode, snap: true});
}
