import fs from 'node:fs';
import {Rpc, Write, Capture, Delay} from './TaaEditorAcceptanceClient.mjs';

const camera = await Rpc('editor.get_scene_camera');
const settings = await Rpc('render.path_tracer_settings');
Write('original_camera.json', camera);
Write('original_settings.json', settings);
let removed = false;
try {
  let synced = false;
  for (let attempt = 0; attempt < 90; ++attempt) {
    if (fs.readFileSync('FileAppender_log.txt', 'utf8').includes('FullSync import:')) {
      synced = true;
      break;
    }
    await Delay(1000);
  }
  if (!synced) throw Error('No completed BMW Full Sync observed');
  await Rpc('editor.set_scene_camera', {position: [-5, 2, 5], pitch_degrees: 12, yaw_degrees: 135,
    aa_mode: 2, snap: true});
  Write('settings.json', await Rpc('render.path_tracer_settings', {enabled: true, hardware_ray_tracing: true,
    debug_target: 0, output_size: [896, 512], accumulation_mode: 'disabled'}));
  await Delay(30000);
  const lights = await Rpc('scene.query_entities', {required_components: ['Ailu.ECS.LightComponent'], limit: 100});
  Write('lights.json', lights);
  if (lights.items.length !== 1) throw Error('Expected one analytic light for the solar control');
  const guid = lights.items[0].entity_guid;
  Write('original_light.json', await Rpc('scene.inspect_entity', {entity_guid: guid, include_properties: true}));
  await Capture('sun_raw', 'Debug GI', 64, {capture_output: false, access_index: 1, camera_ordinal: 0});
  await Rpc('scene.remove_component', {entity_guid: guid, component_type: 'Ailu.ECS.LightComponent'});
  removed = true;
  await Delay(3000);
  await Capture('no_sun_raw', 'Debug GI', 64, {capture_output: false, access_index: 1, camera_ordinal: 0});
  await Rpc('render.path_tracer_settings', {accumulation_mode: 'reference'});
  await Delay(30000);
  await Capture('no_sun_reference', 'Debug GI', 32, {capture_output: false, access_index: 0, camera_ordinal: 0});
  await Capture('no_sun_present', 'FinalBlit', 3, {camera_ordinal: 0, output_format: 'png'});
  Write('completed.json', {scope: 'Same live BMW scene; temporary removal of the sole analytic light'});
} finally {
  if (removed) {
    await Rpc('editor.undo');
    Write('restored_lights.json', await Rpc('scene.query_entities',
      {required_components: ['Ailu.ECS.LightComponent'], limit: 100}));
  }
  await Rpc('render.path_tracer_settings', settings);
  await Rpc('editor.set_scene_camera', {position: camera.target_position, pitch_degrees: camera.pitch_degrees,
    yaw_degrees: camera.yaw_degrees, fov_degrees: camera.fov_degrees, aa_mode: camera.aa_mode,
    projection_type: camera.projection_type, orthographic_size: camera.orthographic_size, snap: true});
}
