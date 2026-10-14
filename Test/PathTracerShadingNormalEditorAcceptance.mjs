import {Rpc, Write, Capture, Delay} from './TaaEditorAcceptanceClient.mjs';

const settings = await Rpc('render.path_tracer_settings');
Write('original_settings.json', settings);
Write('camera.json', await Rpc('editor.get_scene_camera'));
Write('state.json', await Rpc('editor.get_state'));
try {
  for (const [name, hardware] of [['compute', false], ['dxr', true]]) {
    Write(name + '_settings.json', await Rpc('render.path_tracer_settings', {enabled: true,
      hardware_ray_tracing: hardware, accumulation_mode: 'reference', debug_target: 0}));
    await Delay(15000);
    await Capture(name + '_reference', 'Debug GI', 4,
      {capture_output: false, access_index: 0, camera_ordinal: 0, output_format: 'png'});
    await Capture(name + '_present', 'FinalBlit', 1, {camera_ordinal: 0, output_format: 'png'});
    await Rpc('render.path_tracer_settings', {accumulation_mode: 'disabled'});
    await Delay(1000);
    await Capture(name + '_raw', 'Debug GI', 2,
      {capture_output: false, access_index: 0, camera_ordinal: 0, output_format: 'png'});
  }
} finally {
  Write('restored_settings.json', await Rpc('render.path_tracer_settings', settings));
}
