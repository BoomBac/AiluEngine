import fs from 'node:fs';
import path from 'node:path';
import {execFileSync} from 'node:child_process';
import {Rpc, Write, Capture, Delay} from './TaaEditorAcceptanceClient.mjs';
const [project] = process.argv.slice(2);
const session = JSON.parse(fs.readFileSync(path.join(project, '.ailu/editor_session.json')));
const original = await Rpc('editor.get_state');
const camera = await Rpc('editor.get_scene_camera');
let commands = 0;
function Click(x) {
  execFileSync('powershell.exe', ['-NoProfile', '-File', path.join(process.cwd(), 'Test/TaaM8WindowControl.ps1'),
    '-TargetPid', String(session.pid), '-Action', 'click', '-X', String(x), '-Y', '17'],
    {windowsHide: true, timeout: 20000});
}
Write('original_state.json', original);
try {
  await Rpc('editor.set_scene_camera', {aa_mode: 2});
  await Rpc('scene.create_entity', {name: 'M8TemporaryMainCamera'}); ++commands;
  const query = await Rpc('scene.query_entities', {name: 'M8TemporaryMainCamera', name_match: 'exact'});
  const guid = query.items[0].entity_guid;
  await Rpc('scene.add_component', {entity_guid: guid, component_type: 'Ailu.ECS.CCamera'}); ++commands;
  for (const [component, property, value] of [
    ['Ailu.ECS.CCamera', '_camera._anti_aliasing', 2],
    ['Ailu.ECS.TransformComponent', '_local_transform._position', camera.position]]) {
    await Rpc('scene.set_property', {target: {kind: 'component', scene_guid: original.scene_guid,
      entity_guid: guid, component_type: component}, property_path: property, value}); ++commands;
  }
  Write('main_camera_component.json', await Rpc('scene.inspect_entity', {entity_guid: guid,
    include_components: true, include_properties: true}));
  await Capture('scene_before', 'TemporalAA', 16);
  const states = [];
  for (let index = 0; index < 3; ++index) {
    Click(291); await Delay(1000);
    const playing = await Rpc('editor.get_state'); states.push(playing);
    if (playing.editor_mode !== 'play') throw Error('Play did not enter');
    Write('play_' + index + '_camera.json', await Rpc('scene.inspect_entity', {entity_guid: guid,
      include_components: true, include_properties: true}));
    await Capture('main_' + index, 'TemporalAA', 16);
    Click(427); await Delay(1000);
    states.push(await Rpc('editor.get_state'));
    await Capture('scene_after_' + index, 'TemporalAA', 16);
  }
  Write('states.json', states);
  Write('completion.json', {play_stop_cycles: 3, temporary_main_camera_guid: guid,
    scope: 'Real CCamera with AA TAA, GameView activation, runtime Scene copy creation/destruction ' +
      'and SceneView return.'});
} finally {
  if ((await Rpc('editor.get_state')).editor_mode === 'play') { Click(427); await Delay(1000); }
  while (commands > 0) { await Rpc('editor.undo'); --commands; }
  await Rpc('editor.set_scene_camera', {aa_mode: camera.aa_mode, position: camera.target_position,
    pitch_degrees: camera.pitch_degrees, yaw_degrees: camera.yaw_degrees, snap: true});
  Write('restored_state.json', await Rpc('editor.get_state'));
}
