import {Rpc, Write, Capture, Delay} from './TaaEditorAcceptanceClient.mjs';

const camera = await Rpc('editor.get_scene_camera');
const features = await Rpc('render.list_features');
Write('original_camera.json', camera);
Write('original_features.json', features);
Write('original_state.json', await Rpc('editor.get_state'));
try {
  for (const aa_mode of [0, 2]) {
    await Rpc('editor.set_scene_camera', {aa_mode, position: camera.target_position,
      pitch_degrees: camera.pitch_degrees, yaw_degrees: camera.yaw_degrees, snap: true});
    await Delay(1800);
    Write('camera_aa' + aa_mode + '.json', await Rpc('editor.get_scene_camera'));
    for (const [name, resource_name, access_index] of [
      ['normal', '_GBuffer0', 0], ['basecolor', '_GBuffer1', 1]]) {
      await Capture(name + '_aa' + aa_mode, 'DeferedGeometryPass', 8,
        {resource_name, access_index, camera_ordinal: 0});
    }
    await Capture('lighting_aa' + aa_mode, 'DeferredLightingPass', 8, {camera_ordinal: 0});
    await Capture('tonemapping_aa' + aa_mode, 'ToneMapping', 8,
      {camera_ordinal: 0, output_format: 'png'});
    Write('features_aa' + aa_mode + '.json', await Rpc('render.list_features'));
  }
  Write('completion.json', {aa_modes: [0, 2], frames_per_sequence: 8,
    scope: 'Original SceneView pose, raw GBuffer normal/basecolor and deferred lighting, production features unchanged.'});
} finally {
  Write('restored_camera.json', await Rpc('editor.set_scene_camera', {position: camera.target_position,
    pitch_degrees: camera.pitch_degrees, yaw_degrees: camera.yaw_degrees, fov_degrees: camera.fov_degrees,
    aa_mode: camera.aa_mode, projection_type: camera.projection_type,
    orthographic_size: camera.orthographic_size, snap: true}));
  Write('restored_state.json', await Rpc('editor.get_state'));
}
