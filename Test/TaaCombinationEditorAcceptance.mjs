import {Rpc, Write, Capture, Delay} from './TaaEditorAcceptanceClient.mjs';
const before = await Rpc('editor.get_scene_camera');
const features = await Rpc('render.list_features');
Write('original_camera.json', before);
Write('original_features.json', features);
Write('original_scene.json', await Rpc('editor.get_state'));
const controlled = ['SSAO', 'VolumetricFog', 'SurfelGI', 'DDGI', 'SpriteRenderFeature', 'RayTraceGI'];
const combinations = [['SSAO'], ['VolumetricFog'], ['SurfelGI'], ['DDGI'],
  ['VolumetricFog', 'SurfelGI', 'DDGI'], ['SpriteRenderFeature'],
  ['SSAO', 'VolumetricFog', 'SurfelGI', 'DDGI', 'SpriteRenderFeature']];
const cases = [];
try {
  await Rpc('editor.set_scene_camera', {aa_mode: 2});
  for (let index = 0; index < combinations.length; ++index) {
    const enabled = combinations[index];
    for (const name of controlled)
      await Rpc('render.set_feature_active', {name, active: enabled.includes(name)});
    await Rpc('editor.set_scene_camera', {position: before.target_position,
      pitch_degrees: before.pitch_degrees, yaw_degrees: before.yaw_degrees, snap: true});
    await Delay(1500);
    cases.push({index, enabled, features: await Rpc('render.list_features')});
    for (const phase of ['static', 'pan', 'rotate', 'cut', 'toggle', 'ortho']) {
      if (phase === 'pan') await Rpc('editor.set_scene_camera', {
        position: before.target_position.map((v, i) => v + (i === 0 ? .5 : 0)), snap: false});
      if (phase === 'rotate') await Rpc('editor.set_scene_camera', {
        yaw_degrees: before.yaw_degrees + 3, snap: false});
      if (phase === 'cut') await Rpc('editor.set_scene_camera', {
        position: before.target_position.map((v, i) => v + (i === 0 ? 5 : 0)), snap: true});
      if (phase === 'toggle') {
        await Rpc('editor.set_scene_camera', {aa_mode: 0});
        await Delay(100);
        await Rpc('editor.set_scene_camera', {aa_mode: 2});
      }
      if (phase === 'ortho') await Rpc('editor.set_scene_camera', {projection_type: 0, orthographic_size: 20});
      cases.push({index, phase, camera: await Rpc('editor.get_scene_camera')});
      Write('cases.json', cases);
      await Capture('matrix_' + index + '_' + phase, 'TemporalAA', 16);
      if (phase === 'ortho') await Rpc('editor.set_scene_camera', {projection_type: before.projection_type});
    }
  }
  const start = Date.now();
  const observations = [];
  while (Date.now() - start < 180000) {
    const seconds = (Date.now() - start) / 1000;
    const idle = seconds < 30;
    await Rpc('editor.set_scene_camera', {position: before.target_position,
      yaw_degrees: before.yaw_degrees + (idle ? 0 : Math.sin(seconds * .03) * 15), snap: false});
    await Rpc('render.set_feature_active', {name: 'SSAO', active: Math.floor(seconds / 15) % 2 === 0});
    observations.push({seconds, camera: await Rpc('editor.get_scene_camera'), state: await Rpc('editor.get_state')});
    Write('longrun.json', observations);
    await Capture('longrun_' + observations.length, 'TemporalAA', 8);
    console.log('longrun elapsed seconds', seconds);
    await Delay(5000);
  }
  Write('completion.json', {matrix_cases: combinations.length, phases: 6, longrun_seconds: (Date.now() - start) / 1000,
    unverified: ['resize', 'editor selection/gizmo', 'separate main game camera', 'scene reload',
      'Cloud + full feature combination', 'RTGI + full feature combination', 'GPU/CPU performance']});
} finally {
  Write('restored_camera.json', await Rpc('editor.set_scene_camera', {position: before.target_position,
    pitch_degrees: before.pitch_degrees, yaw_degrees: before.yaw_degrees, fov_degrees: before.fov_degrees,
    aa_mode: before.aa_mode, projection_type: before.projection_type,
    orthographic_size: before.orthographic_size, snap: true}));
  for (const feature of features.features)
    await Rpc('render.set_feature_active', {name: feature.name, active: feature.active});
  Write('restored_scene.json', await Rpc('editor.get_state'));
}

