import assert from 'node:assert/strict';
import {Rpc, Write, Capture, Delay} from './TaaEditorAcceptanceClient.mjs';

const original_camera = await Rpc('editor.get_scene_camera');
const original_settings = await Rpc('render.path_tracer_settings');
const original_active = (await Rpc('render.list_features')).features.find(x => x.name === 'RayTraceGI').active;
const fields = ['_denoiser', '_denoiser_quality', '_denoiser_use_guides'];
const original_fields = {};
const Property = async (property_path, ...value) => Rpc('render.feature_property', {
  name: 'RayTraceGI', property_path, ...(value.length ? {value: value[0]} : {}),
});
for (const field of fields) original_fields[field] = await Property(field);
Write('original.json', {original_camera, original_settings, original_fields});
try {
  await Rpc('editor.set_scene_camera', {position: [0, 0.5, -4], pitch_degrees: 0, yaw_degrees: 0,
    fov_degrees: 45, aa_mode: 0, snap: true});
  await Rpc('render.set_feature_active', {name: 'RayTraceGI', active: true});
  await Rpc('render.path_tracer_settings', {enabled: true, accumulation_mode: 'reference',
    output_size: [512, 320], debug_target: 0});
  await Property('_denoiser', 'kOidnGpu');
  for (const hardware_ray_tracing of [false, true]) {
    const backend = hardware_ray_tracing ? 'dxr' : 'compute';
    await Rpc('render.path_tracer_settings', {hardware_ray_tracing});
    await Property('_denoiser_use_guides', false);
    await Property('_denoiser_quality', 'kBalanced');
    await Capture(backend + '_color_unpack', 'OIDN Unpack HDR', 1);
    await Delay(500);
    await Capture(backend + '_color', 'Debug GI', 1);
    await Capture(backend + '_color_display', 'ToneMapping', 1, {output_format: 'png'});
    await Property('_denoiser_use_guides', true);
    assert.equal(await Property('_denoiser_use_guides'), true);
    await Property('_denoiser_quality', 'kHigh');
    // These u8 enums neighbor bool fields; reflection must not overwrite adjacent members.
    assert.equal(await Property('_denoiser_use_guides'), true);
    assert.equal(await Property('_denoiser_quality'), 'kHigh');
    await Capture(backend + '_guides_unpack', 'OIDN Unpack HDR', 1);
    await Delay(500);
    await Capture(backend + '_guides', 'Debug GI', 1);
    await Capture(backend + '_guides_display', 'ToneMapping', 1, {output_format: 'png'});
    await Capture(backend + '_albedo', 'OIDN Pack HDR', 1,
      {capture_output: false, access_index: 1, resource_name: 'PathTrace FirstHit Albedo'});
    await Capture(backend + '_normal', 'OIDN Pack HDR', 1,
      {capture_output: false, access_index: 2, resource_name: 'PathTrace FirstHit Normal'});
    await Property('_denoiser', 'kNone');
    await Delay(3000);
    await Capture(backend + '_reference', 'Debug GI', 1);
    await Property('_denoiser', 'kOidnGpu');
  }
  Write('completion.json', {status: 'PASS', scope: 'Editor transport and reflected settings; analyze images separately'});
} finally {
  for (const field of fields) await Property(field, original_fields[field]);
  await Rpc('render.path_tracer_settings', original_settings);
  await Rpc('render.set_feature_active', {name: 'RayTraceGI', active: original_active});
  await Rpc('editor.set_scene_camera', {position: original_camera.target_position,
    pitch_degrees: original_camera.pitch_degrees, yaw_degrees: original_camera.yaw_degrees,
    fov_degrees: original_camera.fov_degrees, aa_mode: original_camera.aa_mode, snap: true});
}
